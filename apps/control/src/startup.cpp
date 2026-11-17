#include "startup.h"

#include "desktopentry.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJSEngine>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QRegularExpression>
#include <QSaveFile>
#include <QStandardPaths>
#include <QtQml/qqml.h>

namespace {

// a unit name systemctl is given: letters, digits and :-_.\@ only
bool usableUnit(const QString &unit)
{
    static const QRegularExpression re(QStringLiteral("^[A-Za-z0-9:_.\\\\@-]+\\.(service|socket|timer|target|path|mount)$"));
    return re.match(unit).hasMatch();
}

QString unitTitle(const QString &unit)
{
    QString n = unit;
    if (n.endsWith(QLatin1String(".service")))
        n.chop(8);
    // systemd's escapes in names: \x2d is "-"
    static const QRegularExpression hex(QStringLiteral("\\\\x([0-9a-fA-F]{2})"));
    QRegularExpressionMatch m;
    while ((m = hex.match(n)).hasMatch())
        n.replace(m.capturedStart(), m.capturedLength(), QChar(m.captured(1).toInt(nullptr, 16)));
    return n;
}

// what systemd makes of the XDG autostart files for desktops it starts
// (app-NAME@autostart.service): the Programs tab has them already
bool autostartCopy(const QString &unit)
{
    return unit.startsWith(QLatin1String("app-")) && unit.contains(QLatin1String("@autostart."));
}

void call(const QJSValue &fn, const QJSValueList &args)
{
    if (fn.isCallable())
        QJSValue(fn).call(args);
}

// Exec's rules: % is a field code, so a typed % has to be %%
QString execEscape(const QString &command)
{
    QString out = command;
    out.replace(QLatin1Char('%'), QStringLiteral("%%"));
    return out;
}

QString execUnescape(const QString &exec)
{
    QString out = exec;
    static const QRegularExpression codes(QStringLiteral("%[fFuUdDnNickvm]"));
    out.remove(codes);
    out.replace(QStringLiteral("%%"), QStringLiteral("%"));
    return out.trimmed();
}

} // namespace

StartupManager::StartupManager(QObject *parent)
    : QObject(parent)
{
    refreshPrograms();
}

// ============================================================== programs

QString StartupManager::autostartDir() const
{
    return qEnvironmentVariable("XDG_CONFIG_HOME", QDir::homePath() + QStringLiteral("/.config")) + QStringLiteral("/autostart");
}

QStringList StartupManager::systemAutostartDirs() const
{
    QStringList dirs;
    for (const QString &d : qEnvironmentVariable("XDG_CONFIG_DIRS", QStringLiteral("/etc/xdg")).split(QLatin1Char(':'), Qt::SkipEmptyParts))
        dirs << d + QStringLiteral("/autostart");
    return dirs;
}

void StartupManager::refreshPrograms()
{
    // the same rules as the session's own autostart (src/launcher.cpp)
    QVariantList list;
    QSet<QString> seen;
    QStringList systemFiles;
    for (const QString &dir : systemAutostartDirs())
        for (const QFileInfo &f : QDir(dir).entryInfoList({QStringLiteral("*.desktop")}, QDir::Files))
            systemFiles << f.fileName();
    QStringList dirs{autostartDir()};
    dirs << systemAutostartDirs();
    for (const QString &dir : dirs) {
        const bool own = dir == autostartDir();
        for (const QFileInfo &f : QDir(dir).entryInfoList({QStringLiteral("*.desktop")}, QDir::Files)) {
            if (seen.contains(f.fileName()))
                continue;
            seen.insert(f.fileName());
            const DesktopEntry e = DesktopEntry::read(f.absoluteFilePath());
            if (!e.application)
                continue;
            bool here = true;
            if (!e.onlyShowIn.isEmpty() && !e.onlyShowIn.contains(QLatin1String("Wayland95")))
                here = false;
            if (e.notShowIn.contains(QLatin1String("Wayland95")))
                here = false;
            if (!e.tryExec.isEmpty() && QStandardPaths::findExecutable(e.tryExec).isEmpty() && !QFileInfo(e.tryExec).isExecutable())
                here = false;
            if (f.fileName() == QLatin1String("org.kde.xwaylandvideobridge.desktop"))
                here = false; // (the session leaves it out: it only works in KDE)
            QVariantMap m;
            m[QStringLiteral("key")] = f.fileName();
            m[QStringLiteral("name")] = e.name.isEmpty() ? f.completeBaseName() : e.name;
            m[QStringLiteral("comment")] = e.comment;
            m[QStringLiteral("command")] = execUnescape(e.exec);
            m[QStringLiteral("icon")] = appIconUrl(e.icon);
            m[QStringLiteral("enabled")] = !e.hidden && !e.autostartOff;
            m[QStringLiteral("system")] = systemFiles.contains(f.fileName());
            m[QStringLiteral("own")] = own;
            m[QStringLiteral("here")] = here;
            m[QStringLiteral("path")] = f.absoluteFilePath();
            list << m;
        }
    }
    std::sort(list.begin(), list.end(), [](const QVariant &a, const QVariant &b) {
        return a.toMap().value(QStringLiteral("name")).toString().compare(b.toMap().value(QStringLiteral("name")).toString(), Qt::CaseInsensitive) < 0;
    });
    m_programs = list;
    emit programsChanged();
}

// writes ~/.config/autostart/FILE: the file there (or copyFrom) with the
// keys in `set` set and those in `remove` gone, in [Desktop Entry]
QString StartupManager::writeEntry(const QString &file, const QList<QPair<QString, QString>> &set,
                                   const QStringList &remove, const QString &copyFrom)
{
    const QString target = autostartDir() + QLatin1Char('/') + file;
    QString source = QFileInfo::exists(target) ? target : copyFrom;
    QStringList lines;
    if (!source.isEmpty()) {
        QFile in(source);
        if (!in.open(QIODevice::ReadOnly | QIODevice::Text))
            return QStringLiteral("Cannot read %1.").arg(source);
        lines = QString::fromUtf8(in.readAll()).split(QLatin1Char('\n'));
        while (!lines.isEmpty() && lines.last().isEmpty())
            lines.removeLast();
    }
    if (lines.isEmpty())
        lines << QStringLiteral("[Desktop Entry]");
    // the [Desktop Entry] group: from its header to the next group
    int start = lines.indexOf(QStringLiteral("[Desktop Entry]"));
    if (start < 0) {
        lines.prepend(QStringLiteral("[Desktop Entry]"));
        start = 0;
    }
    int end = start + 1;
    while (end < lines.size() && !lines.at(end).startsWith(QLatin1Char('[')))
        ++end;
    QStringList keys;
    for (const auto &kv : set)
        keys << kv.first;
    keys << remove;
    for (int i = end - 1; i > start; --i) {
        const QString key = lines.at(i).section(QLatin1Char('='), 0, 0).trimmed();
        // Name[de]= goes with Name: a new name replaces them all
        const QString base = key.section(QLatin1Char('['), 0, 0);
        if (keys.contains(key) || (base != key && keys.contains(base) && base == QLatin1String("Name"))) {
            lines.removeAt(i);
            --end;
        }
    }
    int at = end;
    while (at > start + 1 && lines.at(at - 1).trimmed().isEmpty())
        --at;
    for (const auto &kv : set)
        lines.insert(at++, kv.first + QLatin1Char('=') + kv.second);

    if (!QDir().mkpath(autostartDir()))
        return QStringLiteral("Cannot make the folder %1.").arg(autostartDir());
    QSaveFile out(target);
    if (!out.open(QIODevice::WriteOnly | QIODevice::Text))
        return QStringLiteral("Cannot write %1.").arg(target);
    out.write((lines.join(QLatin1Char('\n')) + QLatin1Char('\n')).toUtf8());
    if (!out.commit())
        return QStringLiteral("Cannot write %1.").arg(target);
    return {};
}

QString StartupManager::setProgramEnabled(const QString &key, bool on)
{
    QVariantMap p;
    for (const QVariant &v : std::as_const(m_programs))
        if (v.toMap().value(QStringLiteral("key")) == key)
            p = v.toMap();
    if (p.isEmpty())
        return QStringLiteral("There's no such program.");
    QString error;
    if (on) {
        // Hidden=true also hides the system's own; ours says false outright
        error = writeEntry(key, {{QStringLiteral("Hidden"), QStringLiteral("false")}},
                           {QStringLiteral("X-GNOME-Autostart-enabled")}, p.value(QStringLiteral("path")).toString());
    } else {
        error = writeEntry(key, {{QStringLiteral("Hidden"), QStringLiteral("true")}}, {},
                           p.value(QStringLiteral("path")).toString());
    }
    refreshPrograms();
    return error;
}

QString StartupManager::addProgram(const QString &name, const QString &command, const QString &icon)
{
    const QString n = name.trimmed(), c = command.trimmed();
    if (n.isEmpty())
        return QStringLiteral("Type a name for it.");
    if (c.isEmpty())
        return QStringLiteral("Type the command that starts it.");
    // a file name from the name: "My Backup" -> my-backup.desktop (unused)
    QString base = n.toLower();
    base.replace(QRegularExpression(QStringLiteral("[^a-z0-9]+")), QStringLiteral("-"));
    base = base.remove(QRegularExpression(QStringLiteral("^-+|-+$")));
    if (base.isEmpty())
        base = QStringLiteral("program");
    QString file = base + QStringLiteral(".desktop");
    for (int i = 2; QFileInfo::exists(autostartDir() + QLatin1Char('/') + file); ++i)
        file = base + QLatin1Char('-') + QString::number(i) + QStringLiteral(".desktop");
    QList<QPair<QString, QString>> set{
        {QStringLiteral("Type"), QStringLiteral("Application")},
        {QStringLiteral("Name"), n},
        {QStringLiteral("Exec"), execEscape(c)},
    };
    if (!icon.isEmpty())
        set << qMakePair(QStringLiteral("Icon"), icon);
    const QString error = writeEntry(file, set, {});
    refreshPrograms();
    return error;
}

QString StartupManager::addInstalled(const QString &desktopId)
{
    for (const DesktopEntry &e : DesktopEntry::all()) {
        if (e.id != desktopId)
            continue;
        QString file = desktopId;
        for (int i = 2; QFileInfo::exists(autostartDir() + QLatin1Char('/') + file); ++i)
            file = QFileInfo(desktopId).completeBaseName() + QLatin1Char('-') + QString::number(i) + QStringLiteral(".desktop");
        // its own file, as it starts from the menu (its Exec, icon, Terminal...)
        const QString error = writeEntry(file, {{QStringLiteral("Hidden"), QStringLiteral("false")}},
                                         {QStringLiteral("X-GNOME-Autostart-enabled"), QStringLiteral("NoDisplay"),
                                          QStringLiteral("OnlyShowIn"), QStringLiteral("NotShowIn")},
                                         e.path);
        refreshPrograms();
        return error;
    }
    return QStringLiteral("That program isn't installed any more.");
}

QString StartupManager::editProgram(const QString &key, const QString &name, const QString &command)
{
    if (name.trimmed().isEmpty())
        return QStringLiteral("Type a name for it.");
    if (command.trimmed().isEmpty())
        return QStringLiteral("Type the command that starts it.");
    QString path;
    for (const QVariant &v : std::as_const(m_programs))
        if (v.toMap().value(QStringLiteral("key")) == key)
            path = v.toMap().value(QStringLiteral("path")).toString();
    if (path.isEmpty())
        return QStringLiteral("There's no such program.");
    const QString error = writeEntry(key, {{QStringLiteral("Name"), name.trimmed()}, {QStringLiteral("Exec"), execEscape(command.trimmed())}},
                                     {QStringLiteral("TryExec")}, path);
    refreshPrograms();
    return error;
}

QString StartupManager::removeProgram(const QString &key)
{
    if (key.contains(QLatin1Char('/')) || !key.endsWith(QLatin1String(".desktop")))
        return QStringLiteral("There's no such program.");
    const QString file = autostartDir() + QLatin1Char('/') + key;
    if (!QFileInfo::exists(file))
        return QStringLiteral("That's one of the system's: it can only be turned off.");
    if (!QFile::remove(file))
        return QStringLiteral("Cannot remove %1.").arg(file);
    refreshPrograms();
    return {};
}

QVariantList StartupManager::installedPrograms() const
{
    QVariantList list;
    for (const DesktopEntry &e : DesktopEntry::all()) {
        QVariantMap m;
        m[QStringLiteral("key")] = e.id;
        m[QStringLiteral("name")] = e.name;
        m[QStringLiteral("icon")] = appIconUrl(e.icon);
        m[QStringLiteral("command")] = execUnescape(e.exec);
        list << m;
    }
    std::sort(list.begin(), list.end(), [](const QVariant &a, const QVariant &b) {
        return a.toMap().value(QStringLiteral("name")).toString().compare(b.toMap().value(QStringLiteral("name")).toString(), Qt::CaseInsensitive) < 0;
    });
    return list;
}

// ============================================================== services

void StartupManager::run(const QString &program, const QStringList &args, const Done &done)
{
    auto *p = new QProcess(this);
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    env.insert(QStringLiteral("LC_ALL"), QStringLiteral("C.UTF-8"));
    env.insert(QStringLiteral("SYSTEMD_PAGER"), QString());
    env.insert(QStringLiteral("SYSTEMD_COLORS"), QStringLiteral("0"));
    env.insert(QStringLiteral("SYSTEMD_URLIFY"), QStringLiteral("0"));
    p->setProcessEnvironment(env);
    connect(p, &QProcess::finished, this, [p, done](int code, QProcess::ExitStatus status) {
        done(status == QProcess::NormalExit ? code : -1, QString::fromUtf8(p->readAllStandardOutput()),
             QString::fromUtf8(p->readAllStandardError()));
        p->deleteLater();
    });
    connect(p, &QProcess::errorOccurred, this, [p, done, program](QProcess::ProcessError e) {
        if (e != QProcess::FailedToStart)
            return;
        done(-2, QString(), QStringLiteral("%1 isn't installed.").arg(program));
        p->deleteLater();
    });
    p->start(program, args);
    p->closeWriteChannel();
}

void StartupManager::refreshUnits(bool user)
{
    const int u = user ? 1 : 0;
    const quint64 gen = ++m_generation[u];
    ++m_loading[u];
    emit unitsChanged();
    QStringList base;
    if (user)
        base << QStringLiteral("--user");
    base << QStringLiteral("--no-pager") << QStringLiteral("-o") << QStringLiteral("json");

    auto files = std::make_shared<QJsonArray>();
    auto finish = [this, u, gen, files, base](const QJsonArray &units) {
        // every service there's a file for, plus running ones without
        // (instances of templates: getty@tty1)
        QHash<QString, QVariantMap> byName;
        for (const QJsonValue &v : *files) {
            const QJsonObject o = v.toObject();
            const QString unit = o.value(QStringLiteral("unit_file")).toString();
            const QString name = QFileInfo(unit).fileName();
            if (name.contains(QLatin1String("@.")))
                continue; // a template: its instances are what run
            QVariantMap m;
            m[QStringLiteral("unit")] = name;
            m[QStringLiteral("file")] = o.value(QStringLiteral("state")).toString();
            m[QStringLiteral("preset")] = o.value(QStringLiteral("preset")).toString();
            byName.insert(name, m);
        }
        for (const QJsonValue &v : units) {
            const QJsonObject o = v.toObject();
            const QString name = o.value(QStringLiteral("unit")).toString();
            if (o.value(QStringLiteral("load")).toString() == QLatin1String("not-found") && !byName.contains(name))
                continue;
            QVariantMap m = byName.value(name);
            m[QStringLiteral("unit")] = name;
            m[QStringLiteral("description")] = o.value(QStringLiteral("description")).toString();
            m[QStringLiteral("active")] = o.value(QStringLiteral("active")).toString();
            m[QStringLiteral("sub")] = o.value(QStringLiteral("sub")).toString();
            if (!m.contains(QStringLiteral("file")))
                m[QStringLiteral("file")] = name.contains(QLatin1Char('@')) ? QStringLiteral("indirect") : QStringLiteral("static");
            byName.insert(name, m);
        }
        // descriptions of the ones not loaded (not running, not wanted)
        QStringList missing;
        for (auto it = byName.cbegin(); it != byName.cend(); ++it)
            if (!it->contains(QStringLiteral("description")))
                missing << it.key();
        auto publish = [this, u, gen](QHash<QString, QVariantMap> all) {
            if (gen != m_generation[u])
                return;
            QStringList names = all.keys();
            std::sort(names.begin(), names.end(), [](const QString &a, const QString &b) { return a.compare(b, Qt::CaseInsensitive) < 0; });
            QVariantList list;
            for (const QString &n : std::as_const(names)) {
                if (autostartCopy(n))
                    continue;
                QVariantMap m = all.value(n);
                const QString file = m.value(QStringLiteral("file")).toString();
                m[QStringLiteral("key")] = n;
                m[QStringLiteral("name")] = unitTitle(n);
                if (!m.contains(QStringLiteral("active")))
                    m[QStringLiteral("active")] = QStringLiteral("inactive"), m[QStringLiteral("sub")] = QStringLiteral("dead");
                m[QStringLiteral("enabled")] = file.startsWith(QLatin1String("enabled")) || file == QLatin1String("alias");
                m[QStringLiteral("canEnable")] = file == QLatin1String("enabled") || file == QLatin1String("disabled")
                    || file == QLatin1String("enabled-runtime") || file == QLatin1String("linked");
                m[QStringLiteral("important")] = isImportant(n);
                list << m;
            }
            m_units[u] = list;
            m_error[u].clear();
            --m_loading[u];
            emit unitsChanged();
        };
        if (missing.isEmpty()) {
            publish(byName);
            return;
        }
        QStringList args = base.mid(0, base.indexOf(QStringLiteral("-o")));
        args << QStringLiteral("show") << QStringLiteral("-p") << QStringLiteral("Id,Description") << QStringLiteral("--") << missing;
        run(QStringLiteral("systemctl"), args, [publish, byName](int, const QString &out, const QString &) mutable {
            QString id;
            for (const QString &line : out.split(QLatin1Char('\n'))) {
                if (line.startsWith(QLatin1String("Id=")))
                    id = line.mid(3);
                else if (line.startsWith(QLatin1String("Description=")) && byName.contains(id))
                    byName[id][QStringLiteral("description")] = line.mid(12);
            }
            publish(byName);
        });
    };
    auto failed = [this, u, gen](const QString &err) {
        if (gen != m_generation[u])
            return;
        m_units[u].clear();
        m_error[u] = err.trimmed().isEmpty() ? QStringLiteral("systemctl didn't answer.") : err.trimmed();
        --m_loading[u];
        emit unitsChanged();
    };
    run(QStringLiteral("systemctl"), base + QStringList{QStringLiteral("list-unit-files"), QStringLiteral("--type=service")},
        [this, base, files, finish, failed](int code, const QString &out, const QString &err) {
        if (code != 0) {
            failed(err);
            return;
        }
        *files = QJsonDocument::fromJson(out.toUtf8()).array();
        run(QStringLiteral("systemctl"), base + QStringList{QStringLiteral("list-units"), QStringLiteral("--type=service"), QStringLiteral("--all")},
            [finish, failed](int code, const QString &out, const QString &err) {
            if (code != 0) {
                failed(err);
                return;
            }
            finish(QJsonDocument::fromJson(out.toUtf8()).array());
        });
    });
}

QString StartupManager::failure(const QString &err, int code)
{
    const QString e = err.trimmed();
    if (e.contains(QLatin1String("Access denied")) || e.contains(QLatin1String("authentication"), Qt::CaseInsensitive)
        || e.contains(QLatin1String("Permission denied")))
        return QStringLiteral("You need an administrator's password to change this.");
    if (!e.isEmpty())
        return e;
    return QStringLiteral("systemctl stopped with code %1.").arg(code);
}

void StartupManager::unitAction(bool user, const QString &unit, const QString &action, const QJSValue &done)
{
    static const QStringList actions{QStringLiteral("start"), QStringLiteral("stop"), QStringLiteral("restart"), QStringLiteral("enable"),
                                     QStringLiteral("disable"), QStringLiteral("mask"), QStringLiteral("unmask")};
    if (!actions.contains(action) || !usableUnit(unit)) {
        call(done, {false, QStringLiteral("Not something systemctl can do.")});
        return;
    }
    QStringList args;
    if (user)
        args << QStringLiteral("--user");
    args << QStringLiteral("--no-pager") << action << QStringLiteral("--") << unit;
    ++m_busy;
    emit busyChanged();
    QJSValue cb = done;
    run(QStringLiteral("systemctl"), args, [this, user, cb](int code, const QString &, const QString &err) {
        --m_busy;
        emit busyChanged();
        call(cb, {code == 0, code == 0 ? QString() : failure(err, code)});
        refreshUnits(user);
    });
}

void StartupManager::unitStatus(bool user, const QString &unit, const QJSValue &done)
{
    if (!usableUnit(unit)) {
        call(done, {QString()});
        return;
    }
    QStringList args;
    if (user)
        args << QStringLiteral("--user");
    args << QStringLiteral("--no-pager") << QStringLiteral("--full") << QStringLiteral("--lines=30") << QStringLiteral("status")
         << QStringLiteral("--") << unit;
    QJSValue cb = done;
    run(QStringLiteral("systemctl"), args, [cb](int, const QString &out, const QString &err) {
        call(cb, {out.isEmpty() ? err : out}); // (status says 3 for a stopped one)
    });
}

bool StartupManager::isImportant(const QString &unit)
{
    static const QStringList prefixes{
        QStringLiteral("systemd-"), QStringLiteral("dbus"), QStringLiteral("user@"), QStringLiteral("getty@"),
        QStringLiteral("polkit"), QStringLiteral("NetworkManager"), QStringLiteral("wpa_supplicant"),
        QStringLiteral("sddm"), QStringLiteral("gdm"), QStringLiteral("lightdm"), QStringLiteral("greetd"),
        QStringLiteral("display-manager"), QStringLiteral("udisks2"), QStringLiteral("accounts-daemon"),
        QStringLiteral("pipewire"), QStringLiteral("wireplumber"), QStringLiteral("xdg-desktop-portal"),
        QStringLiteral("wayland95"), QStringLiteral("console-setup"), QStringLiteral("keyboard-setup"),
        QStringLiteral("apparmor"), QStringLiteral("networking"), QStringLiteral("ssh"),
    };
    for (const QString &p : prefixes)
        if (unit.startsWith(p))
            return true;
    return false;
}

// ================================================================== boot

void StartupManager::bootInfo(const QJSValue &done)
{
    QJSValue cb = done;
    auto info = std::make_shared<QVariantMap>();
    run(QStringLiteral("systemd-analyze"), {QStringLiteral("--no-pager")}, [this, cb, info](int, const QString &out, const QString &err) {
        QString summary = out.section(QLatin1Char('\n'), 0, 0).trimmed();
        if (summary.isEmpty())
            summary = err.trimmed().section(QLatin1Char('\n'), 0, 0);
        (*info)[QStringLiteral("summary")] = summary;
        (*info)[QStringLiteral("detail")] = out.section(QLatin1Char('\n'), 1).trimmed();
        run(QStringLiteral("systemd-analyze"), {QStringLiteral("--no-pager"), QStringLiteral("blame")},
            [this, cb, info](int, const QString &out, const QString &) {
            QVariantList blame;
            static const QRegularExpression line(QStringLiteral("^\\s*(.+?)\\s+(\\S+\\.(?:service|mount|socket|device|target|timer|swap|path|slice|scope))\\s*$"));
            for (const QString &l : out.split(QLatin1Char('\n'))) {
                const QRegularExpressionMatch m = line.match(l);
                if (!m.hasMatch())
                    continue;
                blame << QVariantMap{{QStringLiteral("time"), m.captured(1)}, {QStringLiteral("unit"), m.captured(2)}};
                if (blame.size() >= 15)
                    break;
            }
            (*info)[QStringLiteral("blame")] = blame;
            run(QStringLiteral("systemctl"), {QStringLiteral("get-default")}, [this, cb, info](int, const QString &out, const QString &) {
                (*info)[QStringLiteral("target")] = out.trimmed();
                QJSEngine *engine = qjsEngine(this);
                if (cb.isCallable() && engine)
                    QJSValue(cb).call({engine->toScriptValue(*info)});
            });
        });
    });
}

void StartupManager::setDefaultTarget(const QString &target, const QJSValue &done)
{
    if (target != QLatin1String("graphical.target") && target != QLatin1String("multi-user.target")) {
        call(done, {false, QStringLiteral("Not a way the computer can start.")});
        return;
    }
    QJSValue cb = done;
    ++m_busy;
    emit busyChanged();
    run(QStringLiteral("systemctl"), {QStringLiteral("set-default"), QStringLiteral("--"), target},
        [this, cb](int code, const QString &, const QString &err) {
        --m_busy;
        emit busyChanged();
        call(cb, {code == 0, code == 0 ? QString() : failure(err, code)});
    });
}
