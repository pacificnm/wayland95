#include "systemlog.h"

#include <QClipboard>
#include <QDateTime>
#include <QFileInfo>
#include <QGuiApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QProcess>
#include <QSaveFile>
#include <QStandardPaths>

#include <grp.h>
#include <unistd.h>

#include <algorithm>
#include <memory>

namespace {

const int kMaxEntries = 2000;

// in the groups that may read the whole journal (or root)
bool mayReadJournal()
{
    if (geteuid() == 0)
        return true;
    const int n = getgroups(0, nullptr);
    if (n <= 0)
        return false;
    std::vector<gid_t> groups(n);
    if (getgroups(n, groups.data()) < 0)
        return false;
    for (const char *name : {"systemd-journal", "adm", "wheel"}) {
        if (const group *g = getgrnam(name)) {
            if (std::find(groups.begin(), groups.end(), g->gr_gid) != groups.end())
                return true;
        }
    }
    return false;
}

// journalctl's JSON: a string, null, or (not valid UTF-8, or binary) an
// array of byte values
QString fieldText(const QJsonValue &v)
{
    if (v.isString())
        return v.toString();
    if (v.isArray()) {
        const QJsonArray a = v.toArray();
        // a field given more than once: an array of the above
        if (!a.isEmpty() && !a.first().isDouble()) {
            QStringList parts;
            for (const QJsonValue &p : a)
                parts << fieldText(p);
            return parts.join(QLatin1Char('\n'));
        }
        QByteArray bytes;
        bytes.reserve(a.size());
        for (const QJsonValue &b : a)
            bytes.append(char(b.toInt()));
        return QString::fromUtf8(bytes);
    }
    if (v.isNull())
        return QString();
    return v.toVariant().toString();
}

} // namespace

SystemLog::SystemLog(QObject *parent)
    : QObject(parent)
    , m_canReadAll(mayReadJournal())
{
}

QString SystemLog::helperPath() const
{
#ifdef WAYLAND95_SERVICES_HELPER
    if (QFileInfo::exists(QStringLiteral(WAYLAND95_SERVICES_HELPER)))
        return QStringLiteral(WAYLAND95_SERVICES_HELPER);
#endif
#ifdef WAYLAND95_SERVICES_HELPER_SRC
    return QStringLiteral(WAYLAND95_SERVICES_HELPER_SRC);
#else
    return QString();
#endif
}

bool SystemLog::useHelper(const QString &log) const
{
    return m_admin && !m_canReadAll && log != QLatin1String("user");
}

void SystemLog::setAdmin(bool on)
{
    if (m_admin == on)
        return;
    m_admin = on;
    emit adminChanged();
}

void SystemLog::run(const QString &program, const QStringList &args, const Done &done)
{
    auto *p = new QProcess(this);
    connect(p, &QProcess::finished, this, [p, done](int code, QProcess::ExitStatus st) {
        const QByteArray out = p->readAllStandardOutput();
        const QString err = QString::fromUtf8(p->readAllStandardError()).trimmed();
        p->deleteLater();
        done(st == QProcess::NormalExit ? code : -1, out, err);
    });
    connect(p, &QProcess::errorOccurred, this, [p, done, program](QProcess::ProcessError e) {
        if (e != QProcess::FailedToStart)
            return;
        p->deleteLater();
        done(-1, QByteArray(), program + QStringLiteral(" isn't installed."));
    });
    p->start(program, args);
    p->closeWriteChannel();
}

void SystemLog::load(const QString &log, int boot, int priority)
{
    priority = std::clamp(priority, 0, 7);
    boot = std::clamp(boot, -999, 0);
    const quint64 generation = ++m_generation;
    if (m_loading++ == 0)
        emit loadingChanged();

    auto done = [this, generation, log](int code, const QByteArray &out, const QString &err) {
        if (--m_loading == 0)
            emit loadingChanged();
        if (generation != m_generation)
            return;
        QString problem;
        if (useHelper(log) && (code == 126 || code == 127)) {
            // the password box was cancelled: back to your own messages
            setAdmin(false);
            problem = QStringLiteral("You need an administrator's password to see all of the computer's messages.");
        } else if (code != 0 && out.isEmpty()) {
            problem = err.isEmpty() ? QStringLiteral("journalctl stopped with code %1.").arg(code) : err;
        }
        finish(generation, out, problem, !m_canReadAll && !useHelper(log) && log != QLatin1String("user"));
    };

    if (useHelper(log)) {
        const QString pkexec = QStandardPaths::findExecutable(QStringLiteral("pkexec"));
        if (pkexec.isEmpty()) {
            setAdmin(false);
            done(1, QByteArray(), QStringLiteral("pkexec isn't installed, so only your own messages can be shown (sudo apt install pkexec)."));
            return;
        }
        run(pkexec, {helperPath(), QStringLiteral("journal"), QString::number(boot), QString::number(priority),
                     log == QLatin1String("kernel") ? QStringLiteral("kernel") : QStringLiteral("all")},
            done);
        return;
    }

    QStringList args{QStringLiteral("--no-pager"), QStringLiteral("-q"), QStringLiteral("-o"), QStringLiteral("json"),
                     QStringLiteral("-r"), QStringLiteral("-n"), QString::number(kMaxEntries),
                     QStringLiteral("-b"), QString::number(boot), QStringLiteral("-p"), QStringLiteral("0..%1").arg(priority)};
    if (log == QLatin1String("user"))
        args.prepend(QStringLiteral("--user"));
    else if (log == QLatin1String("kernel"))
        args << QStringLiteral("-k");
    run(QStringLiteral("journalctl"), args, done);
}

void SystemLog::finish(quint64 generation, const QByteArray &json, const QString &err, bool partial)
{
    Q_UNUSED(generation);
    QVariantList entries;
    QHash<QString, QJsonObject> raw;
    entries.reserve(kMaxEntries);
    int start = 0;
    while (start < json.size()) {
        int end = json.indexOf('\n', start);
        if (end < 0)
            end = json.size();
        const QByteArray lineData = json.mid(start, end - start);
        start = end + 1;
        if (lineData.trimmed().isEmpty())
            continue;
        const QJsonObject o = QJsonDocument::fromJson(lineData).object();
        if (o.isEmpty())
            continue;
        const QString key = o.value(QStringLiteral("__CURSOR")).toString();
        if (key.isEmpty() || raw.contains(key))
            continue;
        QString source = fieldText(o.value(QStringLiteral("SYSLOG_IDENTIFIER")));
        if (source.isEmpty())
            source = fieldText(o.value(QStringLiteral("_COMM")));
        if (source.isEmpty() && fieldText(o.value(QStringLiteral("_TRANSPORT"))) == QLatin1String("kernel"))
            source = QStringLiteral("kernel");
        QString unit = fieldText(o.value(QStringLiteral("_SYSTEMD_USER_UNIT")));
        if (unit.isEmpty())
            unit = fieldText(o.value(QStringLiteral("_SYSTEMD_UNIT")));
        const QString prio = fieldText(o.value(QStringLiteral("PRIORITY")));
        QVariantMap e;
        e.insert(QStringLiteral("key"), key);
        e.insert(QStringLiteral("time"),
                 double(fieldText(o.value(QStringLiteral("__REALTIME_TIMESTAMP"))).toLongLong() / 1000));
        e.insert(QStringLiteral("priority"), prio.isEmpty() ? 6 : prio.toInt());
        e.insert(QStringLiteral("source"), source);
        e.insert(QStringLiteral("unit"), unit);
        e.insert(QStringLiteral("pid"), fieldText(o.value(QStringLiteral("_PID"))));
        e.insert(QStringLiteral("message"), fieldText(o.value(QStringLiteral("MESSAGE"))));
        entries << e;
        raw.insert(key, o);
    }
    m_entries = entries;
    m_raw = raw;
    m_error = err;
    m_partial = partial;
    emit entriesChanged();
}

void SystemLog::loadBoots(const QString &log)
{
    const quint64 generation = ++m_bootGeneration;
    auto done = [this, generation](int code, const QByteArray &out, const QString &) {
        if (generation != m_bootGeneration || code != 0)
            return;
        QVariantList boots;
        const QJsonArray a = QJsonDocument::fromJson(out).array();
        for (const QJsonValue &v : a) {
            const QJsonObject o = v.toObject();
            QVariantMap b;
            b.insert(QStringLiteral("index"), o.value(QStringLiteral("index")).toInt());
            b.insert(QStringLiteral("first"), o.value(QStringLiteral("first_entry")).toDouble() / 1000);
            b.insert(QStringLiteral("last"), o.value(QStringLiteral("last_entry")).toDouble() / 1000);
            boots.prepend(b);
        }
        m_boots = boots;
        emit bootsChanged();
    };
    if (useHelper(log)) {
        const QString pkexec = QStandardPaths::findExecutable(QStringLiteral("pkexec"));
        if (!pkexec.isEmpty())
            run(pkexec, {helperPath(), QStringLiteral("journal-boots")}, done);
        return;
    }
    QStringList args{QStringLiteral("--no-pager"), QStringLiteral("-q"), QStringLiteral("--list-boots"),
                     QStringLiteral("-o"), QStringLiteral("json")};
    if (log == QLatin1String("user"))
        args.prepend(QStringLiteral("--user"));
    run(QStringLiteral("journalctl"), args, done);
}

QString SystemLog::fields(const QString &key) const
{
    const auto it = m_raw.constFind(key);
    if (it == m_raw.constEnd())
        return QString();
    QStringList names = it->keys();
    std::sort(names.begin(), names.end(), [](const QString &a, const QString &b) {
        // the message's own fields first, then journald's (_X), then its bookkeeping (__X)
        auto rank = [](const QString &s) { return s.startsWith(QLatin1String("__")) ? 2 : s.startsWith(QLatin1Char('_')) ? 1 : 0; };
        return rank(a) != rank(b) ? rank(a) < rank(b) : a < b;
    });
    QStringList lines;
    for (const QString &n : names) {
        if (n == QLatin1String("MESSAGE"))
            continue;
        lines << n + QLatin1Char('=') + fieldText(it->value(n)).trimmed();
    }
    return lines.join(QLatin1Char('\n'));
}

QString SystemLog::line(const QString &key) const
{
    const auto it = m_raw.constFind(key);
    if (it == m_raw.constEnd())
        return QString();
    const qint64 us = fieldText(it->value(QStringLiteral("__REALTIME_TIMESTAMP"))).toLongLong();
    QString source = fieldText(it->value(QStringLiteral("SYSLOG_IDENTIFIER")));
    if (source.isEmpty())
        source = fieldText(it->value(QStringLiteral("_COMM")));
    if (source.isEmpty())
        source = QStringLiteral("kernel");
    const QString pid = fieldText(it->value(QStringLiteral("_PID")));
    const QString prio = fieldText(it->value(QStringLiteral("PRIORITY")));
    return QDateTime::fromMSecsSinceEpoch(us / 1000).toString(QStringLiteral("yyyy-MM-dd HH:mm:ss"))
         + QLatin1Char(' ') + priorityName(prio.isEmpty() ? 6 : prio.toInt())
         + QLatin1Char(' ') + source + (pid.isEmpty() ? QString() : QLatin1Char('[') + pid + QLatin1Char(']'))
         + QStringLiteral(": ") + fieldText(it->value(QStringLiteral("MESSAGE")));
}

void SystemLog::copy(const QString &text) const
{
    if (QGuiApplication::clipboard())
        QGuiApplication::clipboard()->setText(text);
}

QString SystemLog::save(const QString &path, const QStringList &keys) const
{
    QSaveFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Text))
        return f.errorString();
    // oldest first, the way a log file reads
    for (auto it = keys.crbegin(); it != keys.crend(); ++it)
        f.write(line(*it).toUtf8() + '\n');
    if (!f.commit())
        return f.errorString();
    return QString();
}

QString SystemLog::priorityName(int priority)
{
    switch (priority) {
    case 0: return QStringLiteral("Emergency");
    case 1: return QStringLiteral("Alert");
    case 2: return QStringLiteral("Critical");
    case 3: return QStringLiteral("Error");
    case 4: return QStringLiteral("Warning");
    case 5: return QStringLiteral("Notice");
    case 6: return QStringLiteral("Information");
    default: return QStringLiteral("Debug");
    }
}
