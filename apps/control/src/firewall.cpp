#include "firewall.h"

#include <QCoreApplication>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QRegularExpression>
#include <QStandardPaths>

namespace {

const char *kConfig = "/etc/wayland95/firewall.conf";

QVariantMap exception(const QString &key, const QString &name, const QString &tcp, const QString &udp,
                      const QString &scope, bool on)
{
    return {
        {QStringLiteral("key"), key},
        {QStringLiteral("name"), name},
        {QStringLiteral("tcp"), tcp},
        {QStringLiteral("udp"), udp},
        {QStringLiteral("scope"), scope},
        {QStringLiteral("on"), on},
        {QStringLiteral("builtIn"), false},
    };
}

bool yes(const QString &v) { return v == QLatin1String("yes"); }

} // namespace

Firewall::Firewall(QObject *parent)
    : QObject(parent)
{
    m_available = !QStandardPaths::findExecutable(QStringLiteral("nft"), {QStringLiteral("/usr/sbin"), QStringLiteral("/sbin")}).isEmpty()
               || !QStandardPaths::findExecutable(QStringLiteral("nft")).isEmpty();
    refresh();
}

// the exceptions every firewall lists, off until wanted (but printer and
// device discovery, which the local network needs)
QVariantList Firewall::builtIns() const
{
    QVariantList list{
        exception(QStringLiteral("mdns"), QStringLiteral("Printer and Device Discovery (mDNS)"), QString(), QStringLiteral("5353"), QStringLiteral("local"), true),
        exception(QStringLiteral("ssh"), QStringLiteral("SSH Server"), QStringLiteral("22"), QString(), QStringLiteral("any"), false),
        exception(QStringLiteral("samba"), QStringLiteral("File and Printer Sharing (Samba)"), QStringLiteral("139,445"), QStringLiteral("137-138"), QStringLiteral("local"), false),
        exception(QStringLiteral("web"), QStringLiteral("Web Server (HTTP and HTTPS)"), QStringLiteral("80,443"), QString(), QStringLiteral("any"), false),
        exception(QStringLiteral("postgresql"), QStringLiteral("PostgreSQL"), QStringLiteral("5432"), QString(), QStringLiteral("local"), false),
        exception(QStringLiteral("mysql"), QStringLiteral("MySQL (MariaDB)"), QStringLiteral("3306"), QString(), QStringLiteral("local"), false),
        exception(QStringLiteral("wireguard"), QStringLiteral("WireGuard VPN"), QString(), QStringLiteral("51820"), QStringLiteral("any"), false),
    };
    for (QVariant &v : list) {
        QVariantMap m = v.toMap();
        m.insert(QStringLiteral("builtIn"), true);
        v = m;
    }
    return list;
}

QVariantMap Firewall::defaults() const
{
    return {
        {QStringLiteral("enabled"), false},
        {QStringLiteral("exceptions"), true},
        {QStringLiteral("ping"), false},
        {QStringLiteral("log"), false},
        {QStringLiteral("saved"), false},
        {QStringLiteral("allow"), builtIns()},
    };
}

void Firewall::refresh()
{
    QVariantMap config = defaults();
    QFile f(QString::fromLatin1(kConfig));
    if (f.open(QIODevice::ReadOnly | QIODevice::Text)) {
        config.insert(QStringLiteral("saved"), true);
        QVariantList allow;
        const QVariantList builtIn = builtIns();
        QStringList seen;
        while (!f.atEnd()) {
            const QString line = QString::fromUtf8(f.readLine()).trimmed();
            if (line.isEmpty() || line.startsWith(QLatin1Char('#')))
                continue;
            const int eq = line.indexOf(QLatin1Char('='));
            if (eq < 0)
                continue;
            const QString key = line.left(eq);
            const QString value = line.mid(eq + 1);
            if (key == QLatin1String("allow")) {
                const QStringList p = value.split(QLatin1Char('\t'));
                if (p.size() != 6 || seen.contains(p[0]))
                    continue;
                QVariantMap e = exception(p[0], p[1], p[2] == QLatin1String("-") ? QString() : p[2],
                                          p[3] == QLatin1String("-") ? QString() : p[3], p[4], p[5] == QLatin1String("on"));
                for (const QVariant &b : builtIn)
                    if (b.toMap().value(QStringLiteral("key")) == p[0])
                        e.insert(QStringLiteral("builtIn"), true);
                seen << p[0];
                allow << e;
            } else if (key == QLatin1String("enabled") || key == QLatin1String("exceptions") || key == QLatin1String("ping")
                       || key == QLatin1String("log")) {
                config.insert(key, yes(value));
            }
        }
        // built-in exceptions added since the file was written
        for (const QVariant &b : builtIn)
            if (!seen.contains(b.toMap().value(QStringLiteral("key")).toString()))
                allow << b;
        config.insert(QStringLiteral("allow"), allow);
    }
    m_config = config;
    emit configChanged();

    // what's running: ours, and anyone else's
    const quint64 generation = ++m_generation;
    auto *p = new QProcess(this);
    connect(p, &QProcess::finished, this, [this, p, generation](int, QProcess::ExitStatus) {
        const QStringList states = QString::fromUtf8(p->readAllStandardOutput()).split(QLatin1Char('\n'));
        p->deleteLater();
        if (generation != m_generation)
            return;
        auto on = [&states](int i) { return states.value(i).trimmed() == QLatin1String("active"); };
        m_active = on(0);
        m_others.clear();
        if (on(1))
            m_others << QStringLiteral("ufw");
        if (on(2))
            m_others << QStringLiteral("firewalld");
        emit statusChanged();
    });
    p->start(QStringLiteral("systemctl"), {QStringLiteral("is-active"), QStringLiteral("wayland95-firewall.service"),
                                           QStringLiteral("ufw.service"), QStringLiteral("firewalld.service")});
}

QString Firewall::checkPorts(const QString &ports) const
{
    const QString s = QString(ports).remove(QLatin1Char(' '));
    if (s.isEmpty())
        return QStringLiteral("Type a port number.");
    static const QRegularExpression re(QStringLiteral("^\\d+(-\\d+)?(,\\d+(-\\d+)?)*$"));
    if (!re.match(s).hasMatch())
        return QStringLiteral("Ports are numbers, or ranges like 8000-8010, separated by commas.");
    for (const QString &part : s.split(QLatin1Char(','))) {
        const QStringList ends = part.split(QLatin1Char('-'));
        for (const QString &e : ends) {
            const int n = e.toInt();
            if (n < 1 || n > 65535)
                return QStringLiteral("%1 isn't a port: they go from 1 to 65535.").arg(e);
        }
        if (ends.size() == 2 && ends[0].toInt() >= ends[1].toInt())
            return QStringLiteral("%1 goes backwards.").arg(part);
    }
    return QString();
}

QString Firewall::toLines(const QVariantMap &config) const
{
    auto flag = [&config](const char *k) {
        return QString::fromLatin1(k) + (config.value(QString::fromLatin1(k)).toBool() ? QStringLiteral("=yes") : QStringLiteral("=no"));
    };
    QStringList lines{flag("enabled"), flag("exceptions"), flag("ping"), flag("log")};
    for (const QVariant &v : config.value(QStringLiteral("allow")).toList()) {
        const QVariantMap e = v.toMap();
        auto ports = [&e](const char *k) {
            const QString p = e.value(QString::fromLatin1(k)).toString().remove(QLatin1Char(' '));
            return p.isEmpty() ? QStringLiteral("-") : p;
        };
        QString name = e.value(QStringLiteral("name")).toString().simplified();
        name.remove(QLatin1Char('\t'));
        lines << QStringLiteral("allow=") + QStringList{e.value(QStringLiteral("key")).toString(), name, ports("tcp"), ports("udp"),
                                                        e.value(QStringLiteral("scope")).toString(),
                                                        e.value(QStringLiteral("on")).toBool() ? QStringLiteral("on") : QStringLiteral("off")}
                                                .join(QLatin1Char('\t'));
    }
    return lines.join(QLatin1Char('\n'));
}

void Firewall::showBlocked() const
{
    QProcess::startDetached(QCoreApplication::applicationFilePath(),
                            {QStringLiteral("--applet"), QStringLiteral("systemlog"), QStringLiteral("--log"), QStringLiteral("kernel"),
                             QStringLiteral("--find"), QStringLiteral("wayland95-firewall")});
}
