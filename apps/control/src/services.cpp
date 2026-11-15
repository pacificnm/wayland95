#include "services.h"
#include "software.h" // PkTransaction

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHostInfo>
#include <QJSEngine>
#include <QNetworkInterface>
#include <QProcess>
#include <QRegularExpression>
#include <QSaveFile>
#include <QStandardPaths>
#include <QSysInfo>

#include <memory>
#include <unistd.h>
#include <pwd.h>

namespace {

// PackageKit's enums (pk-enum.h)
constexpr quint64 FilterInstalled = 1 << 2;
constexpr quint64 FilterNotInstalled = 1 << 3;
constexpr quint64 FlagOnlyTrusted = 1 << 1;

QString idName(const QString &id) { return id.section(QLatin1Char(';'), 0, 0); }
QString idArch(const QString &id) { return id.section(QLatin1Char(';'), 2, 2); }

bool nativeArch(const QString &arch)
{
    static const QStringList native = [] {
        const QString cpu = QSysInfo::currentCpuArchitecture();
        if (cpu == QLatin1String("x86_64"))
            return QStringList{ QStringLiteral("amd64"), QStringLiteral("x86_64") };
        if (cpu == QLatin1String("arm64"))
            return QStringList{ QStringLiteral("arm64"), QStringLiteral("aarch64") };
        return QStringList{ cpu };
    }();
    return arch.isEmpty() || native.contains(arch) || arch == QLatin1String("all") || arch == QLatin1String("noarch");
}

QString pkStatusText(uint status)
{
    switch (status) {
    case 1: return QStringLiteral("Waiting...");
    case 4: case 5: case 17: return QStringLiteral("Getting information...");
    case 6: return QStringLiteral("Removing...");
    case 8: return QStringLiteral("Downloading...");
    case 9: return QStringLiteral("Installing...");
    case 11: return QStringLiteral("Cleaning up...");
    case 13: return QStringLiteral("Finding what else is needed...");
    case 14: return QStringLiteral("Checking signatures...");
    case 16: return QStringLiteral("Making changes...");
    case 30: return QStringLiteral("Waiting for another program that's installing software...");
    case 31: return QStringLiteral("Waiting for permission...");
    case 36: return QStringLiteral("Running setup scripts...");
    default: return QString();
    }
}

// which of `names` are installed, by dpkg's database or PackageKit
void queryInstalled(QObject *context, const QStringList &names, std::function<void(QSet<QString>)> done)
{
    const QString dpkg = QStandardPaths::findExecutable(QStringLiteral("dpkg-query"));
    if (!dpkg.isEmpty()) {
        auto *p = new QProcess(context);
        QObject::connect(p, &QProcess::finished, context, [p, done] {
            QSet<QString> found;
            const QStringList lines = QString::fromUtf8(p->readAllStandardOutput()).split(QLatin1Char('\n'));
            for (const QString &line : lines) {
                const QString name = line.section(QLatin1Char('\t'), 0, 0).section(QLatin1Char(':'), 0, 0);
                if (line.section(QLatin1Char('\t'), 1).startsWith(QLatin1String("ii")))
                    found.insert(name);
            }
            p->deleteLater();
            done(found);
        });
        // exits 1 when some names aren't known at all; the rest still print
        p->start(dpkg, QStringList{ QStringLiteral("-W"), QStringLiteral("-f=${Package}\t${db:Status-Abbrev}\n") } + names);
        return;
    }
    auto found = std::make_shared<QSet<QString>>();
    auto *t = new PkTransaction(context);
    QObject::connect(t, &PkTransaction::package, context, [found](uint, const QString &id, const QString &) {
        found->insert(idName(id));
    });
    QObject::connect(t, &PkTransaction::finished, context, [found, done](bool, const QString &) { done(*found); });
    t->start(QStringLiteral("Resolve"), { QVariant::fromValue(FilterInstalled), names });
}

// "key value" or "key = value" config lines (sshd, PostgreSQL)
QString unquote(QString v)
{
    v = v.trimmed();
    const int hash = v.indexOf(QLatin1Char('#'));
    if (hash >= 0 && !v.startsWith(QLatin1Char('\'')))
        v = v.left(hash).trimmed();
    if (v.size() >= 2 && v.startsWith(QLatin1Char('\''))) {
        const int end = v.indexOf(QLatin1Char('\''), 1);
        return v.mid(1, end > 0 ? end - 1 : -1);
    }
    if (v.size() >= 2 && v.startsWith(QLatin1Char('"')) && v.endsWith(QLatin1Char('"')))
        return v.mid(1, v.size() - 2);
    return v;
}

QStringList globFiles(const QString &pattern)
{
    const QFileInfo fi(pattern);
    const QStringList names = QDir(fi.path()).entryList({ fi.fileName() }, QDir::Files, QDir::Name);
    QStringList out;
    for (const QString &n : names)
        out << fi.path() + QLatin1Char('/') + n;
    return out;
}

// sshd: the first value of each keyword wins, Include is read where it
// stands, and Match blocks (for some users only) are left out
void readSshd(const QString &path, QVariantMap &out, int depth = 0)
{
    QFile f(path);
    if (depth > 4 || !f.open(QIODevice::ReadOnly | QIODevice::Text))
        return;
    while (!f.atEnd()) {
        const QString line = QString::fromUtf8(f.readLine()).trimmed();
        if (line.isEmpty() || line.startsWith(QLatin1Char('#')))
            continue;
        static const QRegularExpression split(QStringLiteral("^(\\S+)\\s*(?:=\\s*|\\s+)(.*)$"));
        const auto m = split.match(line);
        if (!m.hasMatch())
            continue;
        const QString key = m.captured(1).toLower();
        const QString value = unquote(m.captured(2));
        if (key == QLatin1String("match"))
            return;
        if (key == QLatin1String("include")) {
            for (QString pattern : value.split(QLatin1Char(' '), Qt::SkipEmptyParts)) {
                if (!pattern.startsWith(QLatin1Char('/')))
                    pattern = QStringLiteral("/etc/ssh/") + pattern;
                for (const QString &file : globFiles(pattern))
                    readSshd(file, out, depth + 1);
            }
            continue;
        }
        if (!out.contains(key))
            out.insert(key, value);
    }
}

QString terminalProgram()
{
    const QString local = QCoreApplication::applicationDirPath() + QStringLiteral("/win95terminal");
    return QFileInfo::exists(local) ? local : QStringLiteral("win95terminal");
}

} // namespace

ServiceManager::ServiceManager(QObject *parent)
    : QObject(parent)
{
    m_defs = {
        { QStringLiteral("ssh"), QStringLiteral("SSH Server"), QStringLiteral("server"),
          QStringLiteral("Lets people log on to this computer from other computers over the network, and copy "
                         "files to and from it (ssh, scp, sftp)."),
          QStringLiteral("ssh"), QStringLiteral("SshSettings.qml"),
          { QStringLiteral("openssh-server") }, {}, {} },
        { QStringLiteral("postgresql"), QStringLiteral("PostgreSQL"), QStringLiteral("server"),
          QStringLiteral("A database server. Programs on this computer, or on others if you allow it, keep "
                         "their data in its databases."),
          QStringLiteral("postgresql"), QStringLiteral("PostgresSettings.qml"),
          { QStringLiteral("postgresql") }, {}, {} },
        { QStringLiteral("python"), QStringLiteral("Python"), QStringLiteral("language"),
          QStringLiteral("The Python programming language, with pip (its package installer) and virtual "
                         "environments for each project's packages."),
          QString(), QStringLiteral("PythonSettings.qml"),
          { QStringLiteral("python3"), QStringLiteral("python3-pip"), QStringLiteral("python3-venv") },
          { QStringLiteral("python3-pip"), QStringLiteral("python3-venv") },
          { QStringLiteral("/usr/bin/python3"), QStringLiteral("--version") } },
        { QStringLiteral("nodejs"), QStringLiteral("Node.js"), QStringLiteral("language"),
          QStringLiteral("Runs JavaScript programs outside a web browser, with npm to install their packages."),
          QString(), QStringLiteral("NodeSettings.qml"),
          { QStringLiteral("nodejs") },
          { QStringLiteral("nodejs"), QStringLiteral("npm") },
          { QStringLiteral("/usr/bin/node"), QStringLiteral("--version") } },
    };
    refresh();
}

QString ServiceManager::userName() const
{
    const passwd *pw = getpwuid(getuid());
    return pw ? QString::fromLocal8Bit(pw->pw_name) : qEnvironmentVariable("USER");
}

QString ServiceManager::homePath() const { return QDir::homePath(); }

const ServiceManager::Def *ServiceManager::def(const QString &id) const
{
    for (const Def &d : m_defs)
        if (d.id == id)
            return &d;
    return nullptr;
}

QString ServiceManager::helperPath() const
{
#ifdef WAYLAND95_SERVICES_HELPER
    if (QFileInfo::exists(QStringLiteral(WAYLAND95_SERVICES_HELPER)))
        return QStringLiteral(WAYLAND95_SERVICES_HELPER);
#endif
#ifdef WAYLAND95_SERVICES_HELPER_SRC
    // not installed yet: the copy in the source tree
    return QStringLiteral(WAYLAND95_SERVICES_HELPER_SRC);
#else
    return QString();
#endif
}

// ---------------------------------------------------------------------------
// What's installed and running

void ServiceManager::refresh()
{
    QStringList packages;
    QStringList units;
    for (const Def &d : m_defs) {
        packages << d.packages << d.removable;
        if (d.id == QLatin1String("ssh"))
            units << QStringLiteral("ssh.service") << QStringLiteral("ssh.socket");
        else if (!d.unit.isEmpty())
            units << d.unit + QStringLiteral(".service");
    }
    packages.removeDuplicates();

    m_loading += 2;
    emit servicesChanged();

    queryInstalled(this, packages, [this](QSet<QString> found) {
        m_installed = found;
        --m_loading;
        publish();
        // versions of the languages that are there
        for (const Def &d : std::as_const(m_defs)) {
            if (d.version.isEmpty() || !QFileInfo::exists(d.version.first())) {
                m_versions.remove(d.id);
                continue;
            }
            ++m_loading;
            auto *p = new QProcess(this);
            const QString id = d.id;
            connect(p, &QProcess::finished, this, [this, p, id] {
                QString v = QString::fromUtf8(p->readAllStandardOutput() + p->readAllStandardError()).trimmed();
                v = v.section(QLatin1Char('\n'), 0, 0);
                if (v.startsWith(QLatin1Char('v')))
                    v = v.mid(1);
                v = v.section(QLatin1Char(' '), -1); // "Python 3.13.5" -> "3.13.5"
                m_versions.insert(id, v);
                p->deleteLater();
                --m_loading;
                publish();
            });
            connect(p, &QProcess::errorOccurred, this, [this, p](QProcess::ProcessError e) {
                if (e == QProcess::FailedToStart) {
                    p->deleteLater();
                    --m_loading;
                    publish();
                }
            });
            p->start(d.version.first(), d.version.mid(1));
        }
    });

    auto *p = new QProcess(this);
    connect(p, &QProcess::finished, this, [this, p] {
        m_unitState.clear();
        const QStringList blocks = QString::fromUtf8(p->readAllStandardOutput()).split(QStringLiteral("\n\n"));
        for (const QString &block : blocks) {
            QVariantMap m;
            for (const QString &line : block.split(QLatin1Char('\n'), Qt::SkipEmptyParts))
                m.insert(line.section(QLatin1Char('='), 0, 0), line.section(QLatin1Char('='), 1));
            if (m.contains(QStringLiteral("Id")))
                m_unitState.insert(m.value(QStringLiteral("Id")).toString(), m);
        }
        p->deleteLater();
        --m_loading;
        publish();
    });
    p->start(QStringLiteral("systemctl"), QStringList{ QStringLiteral("show"), QStringLiteral("--no-pager"),
                                                       QStringLiteral("-p"), QStringLiteral("Id,LoadState,ActiveState,UnitFileState") } + units);
}

void ServiceManager::publish()
{
    QVariantList list;
    for (const Def &d : std::as_const(m_defs)) {
        bool installed = !m_installed.isEmpty();
        for (const QString &p : d.packages)
            installed = installed && m_installed.contains(p);

        bool enabled = installed, active = false;
        QString status;
        if (d.kind == QLatin1String("server")) {
            const QStringList units = d.id == QLatin1String("ssh")
                    ? QStringList{ QStringLiteral("ssh.service"), QStringLiteral("ssh.socket") }
                    : QStringList{ d.unit + QStringLiteral(".service") };
            enabled = false;
            for (const QString &u : units) {
                const QVariantMap s = m_unitState.value(u);
                if (s.value(QStringLiteral("LoadState")) != QLatin1String("loaded"))
                    continue;
                enabled = enabled || s.value(QStringLiteral("UnitFileState")).toString().startsWith(QLatin1String("enabled"));
                active = active || s.value(QStringLiteral("ActiveState")) == QLatin1String("active");
            }
            enabled = installed && enabled;
            active = installed && active;
            status = !installed ? QStringLiteral("Not installed")
                   : active ? QStringLiteral("Running")
                   : enabled ? QStringLiteral("Stopped")
                   : QStringLiteral("Off");
        } else {
            bool some = false;
            for (const QString &p : d.packages)
                some = some || m_installed.contains(p);
            status = installed ? QStringLiteral("Installed") : some ? QStringLiteral("Partly installed") : QStringLiteral("Not installed");
        }
        const bool available = d.version.isEmpty() ? installed : QFileInfo::exists(d.version.first());
        const QString version = available ? m_versions.value(d.id) : QString();
        if (installed && !version.isEmpty())
            status = QStringLiteral("Version ") + version;

        list << QVariantMap{
            { QStringLiteral("id"), d.id },
            { QStringLiteral("name"), d.name },
            { QStringLiteral("kind"), d.kind },
            { QStringLiteral("description"), d.description },
            { QStringLiteral("packages"), d.packages },
            { QStringLiteral("installed"), installed },
            // its settings can be shown (a language that's partly there)
            { QStringLiteral("available"), available },
            { QStringLiteral("enabled"), enabled },
            { QStringLiteral("active"), active },
            { QStringLiteral("version"), version },
            { QStringLiteral("status"), status },
            { QStringLiteral("settings"), d.settings },
        };
    }
    m_services = list;
    emit servicesChanged();
}

QVariantMap ServiceManager::service(const QString &id) const
{
    for (const QVariant &v : m_services)
        if (v.toMap().value(QStringLiteral("id")) == id)
            return v.toMap();
    return {};
}

void ServiceManager::installedPackages(const QStringList &names, const QJSValue &done)
{
    queryInstalled(this, names, [this, done](QSet<QString> found) {
        QStringList list(found.begin(), found.end());
        QJSValue cb = done;
        if (QJSEngine *e = qjsEngine(this))
            cb.call({ e->toScriptValue(list) });
    });
}

// ---------------------------------------------------------------------------
// Turning things on and off

void ServiceManager::turnOn(const QString &id)
{
    const Def *d = def(id);
    if (!d || m_busy)
        return;
    QList<Step> steps{ installStep(d->packages) };
    if (id == QLatin1String("nodejs")) {
        // Debian's nodejs leaves npm to its own package; NodeSource's
        // includes it (and conflicts with Debian's)
        steps << [this] {
            if (QFileInfo::exists(QStringLiteral("/usr/bin/npm")))
                nextStep();
            else
                installStep({ QStringLiteral("npm") })();
        };
    }
    QString done;
    if (d->kind == QLatin1String("server")) {
        steps << adminStep({ QStringLiteral("unit"), QStringLiteral("enable"), d->unit }, QStringLiteral("Starting ") + d->name + QStringLiteral("..."));
        done = d->name + QStringLiteral(" is on. It starts whenever the computer starts.");
    } else {
        done = d->name + QStringLiteral(" is installed.");
    }
    runTask(QStringLiteral("Turning on ") + d->name, steps, done);
}

void ServiceManager::turnOff(const QString &id)
{
    const Def *d = def(id);
    if (!d || m_busy)
        return;
    if (d->kind == QLatin1String("server")) {
        runTask(QStringLiteral("Turning off ") + d->name,
                { adminStep({ QStringLiteral("unit"), QStringLiteral("disable"), d->unit }, QStringLiteral("Stopping ") + d->name + QStringLiteral("...")) },
                d->name + QStringLiteral(" is off. It's still installed; turn it on again any time."));
    } else {
        QStringList remove;
        for (const QString &p : d->removable)
            if (m_installed.contains(p))
                remove << p;
        runTask(QStringLiteral("Removing ") + d->name, { removeStep(remove) }, d->name + QStringLiteral(" has been removed."));
    }
}

void ServiceManager::control(const QString &id, const QString &action)
{
    const Def *d = def(id);
    if (!d || m_busy || d->unit.isEmpty())
        return;
    const QString verb = action == QLatin1String("stop") ? QStringLiteral("Stopping ")
                       : action == QLatin1String("restart") ? QStringLiteral("Restarting ")
                       : QStringLiteral("Starting ");
    runTask(verb + d->name, { adminStep({ QStringLiteral("unit"), action, d->unit }, verb + d->name + QStringLiteral("...")) }, QString());
}

void ServiceManager::changePackages(const QString &title, const QStringList &install, const QStringList &remove)
{
    if (m_busy)
        return;
    QList<Step> steps;
    if (!install.isEmpty())
        steps << installStep(install);
    if (!remove.isEmpty())
        steps << removeStep(remove);
    runTask(title, steps, QString());
}

void ServiceManager::runTask(const QString &title, const QList<Step> &steps, const QString &doneMessage)
{
    m_busy = true;
    m_task = title;
    m_status = QStringLiteral("Starting...");
    m_percent = -1;
    m_steps = steps;
    m_doneMessage = doneMessage;
    emit taskChanged();
    nextStep();
}

void ServiceManager::nextStep()
{
    if (!m_busy)
        return;
    if (m_steps.isEmpty()) {
        m_busy = false;
        m_current = nullptr;
        emit taskChanged();
        refresh();
        emit taskFinished(true, m_doneMessage);
        return;
    }
    const Step step = m_steps.takeFirst();
    step();
}

void ServiceManager::taskFailed(const QString &error)
{
    m_steps.clear();
    m_busy = false;
    m_current = nullptr;
    emit taskChanged();
    refresh();
    emit taskFinished(false, error.isEmpty() ? QStringLiteral("Something went wrong.") : error);
}

void ServiceManager::setStatus(const QString &status, int percent)
{
    if (!status.isEmpty())
        m_status = status;
    m_percent = percent;
    emit taskChanged();
}

PkTransaction *ServiceManager::pk(const QString &method, const QVariantList &args, bool interactive)
{
    auto *t = new PkTransaction(this);
    connect(t, &PkTransaction::progress, this, [this](int percent, uint status) {
        setStatus(pkStatusText(status), percent);
    });
    m_current = t;
    t->start(method, args, interactive);
    return t;
}

ServiceManager::Step ServiceManager::installStep(const QStringList &packages)
{
    return [this, packages] {
        setStatus(QStringLiteral("Looking for ") + packages.join(QStringLiteral(", ")) + QStringLiteral("..."), -1);
        // which packages exactly (this computer's kind), of those not there yet
        auto ids = std::make_shared<QHash<QString, QString>>();
        auto *resolve = pk(QStringLiteral("Resolve"), { QVariant::fromValue(FilterNotInstalled), packages }, false);
        connect(resolve, &PkTransaction::package, this, [ids](uint, const QString &id, const QString &) {
            if (nativeArch(idArch(id)) || !ids->contains(idName(id)))
                ids->insert(idName(id), id);
        });
        connect(resolve, &PkTransaction::finished, this, [this, ids, packages](bool ok, const QString &error) {
            if (!ok) {
                taskFailed(error);
                return;
            }
            if (ids->isEmpty()) {
                // nothing to do, or it can't be found
                queryInstalled(this, packages, [this, packages](QSet<QString> found) {
                    QStringList missing;
                    for (const QString &p : packages)
                        if (!found.contains(p))
                            missing << p;
                    if (missing.isEmpty())
                        nextStep();
                    else
                        taskFailed(missing.join(QStringLiteral(", "))
                                   + QStringLiteral(" isn't available from this computer's software sources. "
                                                    "Try Add/Remove Programs > Updates > Check Now, then try again."));
                });
                return;
            }
            setStatus(QStringLiteral("Installing ") + QStringList(ids->keys()).join(QStringLiteral(", ")) + QStringLiteral("..."), -1);
            auto *t = pk(QStringLiteral("InstallPackages"), { QVariant::fromValue(FlagOnlyTrusted), QStringList(ids->values()) }, true);
            connect(t, &PkTransaction::finished, this, [this](bool ok, const QString &error) { ok ? nextStep() : taskFailed(error); });
        });
    };
}

ServiceManager::Step ServiceManager::removeStep(const QStringList &packages)
{
    return [this, packages] {
        if (packages.isEmpty()) {
            nextStep();
            return;
        }
        auto ids = std::make_shared<QStringList>();
        auto *resolve = pk(QStringLiteral("Resolve"), { QVariant::fromValue(FilterInstalled), packages }, false);
        connect(resolve, &PkTransaction::package, this, [ids](uint, const QString &id, const QString &) { *ids << id; });
        connect(resolve, &PkTransaction::finished, this, [this, ids](bool ok, const QString &error) {
            if (!ok) {
                taskFailed(error);
                return;
            }
            if (ids->isEmpty()) {
                nextStep();
                return;
            }
            setStatus(QStringLiteral("Removing..."), -1);
            // allow_deps false: if other programs need them, PackageKit
            // refuses rather than removing those too; autoremove: the
            // packages that came along only for these go as well
            auto *t = pk(QStringLiteral("RemovePackages"), { QVariant::fromValue(quint64(0)), *ids, false, true }, true);
            connect(t, &PkTransaction::finished, this, [this](bool ok, const QString &error) {
                ok ? nextStep() : taskFailed(error.contains(QLatin1String("depend"), Qt::CaseInsensitive)
                                             ? QStringLiteral("Other installed programs need it, so it was left installed.\n\n") + error
                                             : error);
            });
        });
    };
}

ServiceManager::Step ServiceManager::adminStep(const QStringList &args, const QString &status)
{
    return [this, args, status] {
        setStatus(status, -1);
        const QString pkexec = QStandardPaths::findExecutable(QStringLiteral("pkexec"));
        const QString helper = helperPath();
        if (pkexec.isEmpty()) {
            taskFailed(QStringLiteral("pkexec isn't installed, so Services can't change system settings (sudo apt install pkexec)."));
            return;
        }
        auto *p = new QProcess(this);
        p->setProcessChannelMode(QProcess::MergedChannels);
        connect(p, &QProcess::finished, this, [this, p](int code, QProcess::ExitStatus st) {
            const QString out = QString::fromUtf8(p->readAll()).trimmed();
            p->deleteLater();
            if (st == QProcess::NormalExit && code == 0)
                nextStep();
            else if (code == 126 || code == 127)
                taskFailed(QStringLiteral("You need an administrator's password to change this."));
            else
                taskFailed(out);
        });
        p->start(pkexec, QStringList{ helper } + args);
        p->closeWriteChannel();
    };
}

void ServiceManager::admin(const QStringList &args, const QString &input, const QJSValue &done)
{
    const QString pkexec = QStandardPaths::findExecutable(QStringLiteral("pkexec"));
    QJSValue cb = done;
    if (pkexec.isEmpty()) {
        cb.call({ false, QStringLiteral("pkexec isn't installed, so Services can't change system settings (sudo apt install pkexec).") });
        return;
    }
    auto *p = new QProcess(this);
    p->setProcessChannelMode(QProcess::MergedChannels);
    connect(p, &QProcess::finished, this, [p, cb](int code, QProcess::ExitStatus st) mutable {
        QString out = QString::fromUtf8(p->readAll()).trimmed();
        p->deleteLater();
        const bool ok = st == QProcess::NormalExit && code == 0;
        if (code == 126 || code == 127)
            out = QStringLiteral("You need an administrator's password to change this.");
        cb.call({ ok, out });
    });
    p->start(pkexec, QStringList{ helperPath() } + args);
    QByteArray data = input.toUtf8();
    if (!data.isEmpty() && !data.endsWith('\n'))
        data += '\n';
    p->write(data);
    p->closeWriteChannel();
}

void ServiceManager::run(const QString &program, const QStringList &args, const QString &input,
                         const QJSValue &done, const QString &workdir)
{
    auto *p = new QProcess(this);
    if (!workdir.isEmpty())
        p->setWorkingDirectory(workdir);
    if (program.startsWith(QLatin1String("/usr/bin/"))) {
        // the system's programs with the system's helpers: /usr/bin/npm
        // starts "env node", which would otherwise find a Node.js the user
        // installed for themselves (nvm), with its own folders
        QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
        env.insert(QStringLiteral("PATH"), QStringLiteral("/usr/bin:/bin:") + env.value(QStringLiteral("PATH")));
        for (const QString &k : env.keys())
            if (k.startsWith(QLatin1String("NVM_")) || k.startsWith(QLatin1String("npm_config_"), Qt::CaseInsensitive))
                env.remove(k);
        p->setProcessEnvironment(env);
    }
    QJSValue cb = done;
    auto finished = std::make_shared<bool>(false);
    connect(p, &QProcess::finished, this, [p, cb, finished](int code, QProcess::ExitStatus st) mutable {
        *finished = true;
        const QString out = QString::fromUtf8(p->readAllStandardOutput());
        const QString err = QString::fromUtf8(p->readAllStandardError());
        p->deleteLater();
        if (cb.isCallable())
            cb.call({ st == QProcess::NormalExit ? code : -1, out, err });
    });
    connect(p, &QProcess::errorOccurred, this, [p, cb, finished, program](QProcess::ProcessError e) mutable {
        if (e != QProcess::FailedToStart || *finished)
            return;
        *finished = true;
        p->deleteLater();
        if (cb.isCallable())
            cb.call({ -1, QString(), program + QStringLiteral(" couldn't be started.") });
    });
    p->start(program, args);
    if (!input.isEmpty())
        p->write(input.toUtf8());
    p->closeWriteChannel();
}

void ServiceManager::openTerminal(const QString &workdir, const QStringList &command)
{
    QStringList args;
    if (!workdir.isEmpty())
        args << QStringLiteral("--workdir") << workdir;
    if (!command.isEmpty())
        args << QStringLiteral("-e") << command;
    QProcess::startDetached(terminalProgram(), args);
}

// ---------------------------------------------------------------------------
// Files and settings

static QString expandHome(const QString &path)
{
    if (path == QLatin1String("~"))
        return QDir::homePath();
    if (path.startsWith(QLatin1String("~/")))
        return QDir::homePath() + path.mid(1);
    return path;
}

QString ServiceManager::readFile(const QString &path) const
{
    QFile f(expandHome(path));
    if (!f.open(QIODevice::ReadOnly))
        return QString();
    return QString::fromUtf8(f.readAll());
}

bool ServiceManager::writeFile(const QString &path, const QString &text, bool privateFile)
{
    const QString full = expandHome(path);
    const QString dir = QFileInfo(full).absolutePath();
    if (!QDir().exists(dir)) {
        if (!QDir().mkpath(dir))
            return false;
        if (privateFile)
            QFile::setPermissions(dir, QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner);
    }
    QSaveFile f(full);
    if (!f.open(QIODevice::WriteOnly))
        return false;
    f.write(text.toUtf8());
    if (privateFile)
        f.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner);
    return f.commit();
}

bool ServiceManager::exists(const QString &path) const { return QFileInfo::exists(expandHome(path)); }

QStringList ServiceManager::listDirs(const QString &path) const
{
    return QDir(expandHome(path)).entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name | QDir::IgnoreCase);
}

bool ServiceManager::removeDir(const QString &path)
{
    // only folders of the user's own, and never the home folder itself
    const QString full = QFileInfo(expandHome(path)).canonicalFilePath();
    const QString home = QFileInfo(QDir::homePath()).canonicalFilePath();
    if (full.isEmpty() || !full.startsWith(home + QLatin1Char('/')))
        return false;
    return QDir(full).removeRecursively();
}

QStringList ServiceManager::addresses() const
{
    // real network cards only: not loopback, nor the bridges containers
    // and virtual machines make (docker0, virbr0, veth...)
    QStringList out;
    const auto all = QNetworkInterface::allInterfaces();
    for (const QNetworkInterface &i : all) {
        const auto f = i.flags();
        if (!(f & QNetworkInterface::IsUp) || !(f & QNetworkInterface::IsRunning) || (f & QNetworkInterface::IsLoopBack))
            continue;
        static const QRegularExpression virt(QStringLiteral("^(docker|br-|veth|virbr|vnet|lxc|lxd|podman|cni|flannel)"));
        if (virt.match(i.name()).hasMatch())
            continue;
        for (const QNetworkAddressEntry &e : i.addressEntries()) {
            const QHostAddress a = e.ip();
            if (!a.isLoopback() && !a.isLinkLocal())
                out << a.toString();
        }
    }
    return out;
}

QString ServiceManager::hostName() const { return QHostInfo::localHostName(); }

QVariantMap ServiceManager::sshConfig() const
{
    QVariantMap raw;
    readSshd(QStringLiteral("/etc/ssh/sshd_config"), raw);
    auto get = [&raw](const char *key, const char *def) {
        const QString v = raw.value(QString::fromLatin1(key).toLower()).toString();
        return v.isEmpty() ? QString::fromLatin1(def) : v;
    };
    QString root = get("PermitRootLogin", "prohibit-password");
    if (root == QLatin1String("without-password"))
        root = QStringLiteral("prohibit-password");
    return {
        { QStringLiteral("Port"), get("Port", "22").toInt() },
        { QStringLiteral("PasswordAuthentication"), get("PasswordAuthentication", "yes") == QLatin1String("yes") },
        { QStringLiteral("PubkeyAuthentication"), get("PubkeyAuthentication", "yes") == QLatin1String("yes") },
        { QStringLiteral("X11Forwarding"), get("X11Forwarding", "no") == QLatin1String("yes") },
        { QStringLiteral("PermitRootLogin"), root },
        { QStringLiteral("AllowUsers"), get("AllowUsers", "") },
    };
}

QVariantMap ServiceManager::postgresConfig() const
{
    QProcess p;
    p.start(QStringLiteral("pg_lsclusters"), { QStringLiteral("-h") });
    if (!p.waitForFinished(5000))
        return {};
    // "17 main 5432 online postgres /var/lib/postgresql/17/main /var/log/..."
    QStringList lines = QString::fromUtf8(p.readAllStandardOutput()).split(QLatin1Char('\n'), Qt::SkipEmptyParts);
    if (lines.isEmpty())
        return {};
    std::sort(lines.begin(), lines.end(), [](const QString &a, const QString &b) {
        return a.section(QLatin1Char(' '), 0, 0).toDouble() < b.section(QLatin1Char(' '), 0, 0).toDouble();
    });
    const QStringList f = lines.last().simplified().split(QLatin1Char(' '));
    const QString dir = QStringLiteral("/etc/postgresql/%1/%2").arg(f.value(0), f.value(1));

    QString listen = QStringLiteral("localhost");
    int maxConnections = 100;
    bool remote = false;
    QStringList files{ dir + QStringLiteral("/postgresql.conf") };
    files << globFiles(dir + QStringLiteral("/conf.d/*.conf"));
    for (const QString &file : std::as_const(files)) {
        QFile in(file);
        if (!in.open(QIODevice::ReadOnly | QIODevice::Text))
            continue;
        while (!in.atEnd()) {
            const QString line = QString::fromUtf8(in.readLine()).trimmed();
            if (line == QLatin1String("# wayland95: remote=yes"))
                remote = true;
            if (line.startsWith(QLatin1Char('#')) || !line.contains(QLatin1Char('=')))
                continue;
            const QString key = line.section(QLatin1Char('='), 0, 0).trimmed();
            const QString value = unquote(line.section(QLatin1Char('='), 1));
            if (key == QLatin1String("listen_addresses"))
                listen = value;
            else if (key == QLatin1String("max_connections"))
                maxConnections = value.toInt();
        }
    }
    const bool local = listen.isEmpty() || listen == QLatin1String("localhost")
            || listen == QLatin1String("127.0.0.1") || listen == QLatin1String("::1");
    return {
        { QStringLiteral("version"), f.value(0) },
        { QStringLiteral("cluster"), f.value(1) },
        { QStringLiteral("port"), f.value(2).toInt() },
        { QStringLiteral("online"), f.value(3) == QLatin1String("online") },
        { QStringLiteral("listen"), local ? QStringLiteral("localhost") : QStringLiteral("all") },
        { QStringLiteral("max_connections"), maxConnections },
        { QStringLiteral("remote"), remote && !local },
    };
}
