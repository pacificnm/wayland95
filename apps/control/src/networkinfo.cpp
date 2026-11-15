#include "networkinfo.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QStandardPaths>
#include <QSysInfo>
#include <QTemporaryFile>

namespace {

QProcessEnvironment toolEnvironment()
{
    // stable, untranslated output to parse; UTF-8 so SSIDs survive
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    env.insert(QStringLiteral("LC_ALL"), QStringLiteral("C.UTF-8"));
    return env;
}

// nmcli -t separates fields with ':' and escapes ':' and '\' inside values
QStringList splitTerse(const QString &line)
{
    QStringList fields;
    QString current;
    for (int i = 0; i < line.size(); ++i) {
        const QChar ch = line.at(i);
        if (ch == QLatin1Char('\\') && i + 1 < line.size()) {
            current += line.at(++i);
        } else if (ch == QLatin1Char(':')) {
            fields << current;
            current.clear();
        } else {
            current += ch;
        }
    }
    fields << current;
    return fields;
}

QString typeLabel(const QString &type)
{
    if (type == QLatin1String("wifi"))
        return QStringLiteral("Wi-Fi adapter");
    if (type == QLatin1String("ethernet"))
        return QStringLiteral("Ethernet adapter");
    if (type == QLatin1String("bridge"))
        return QStringLiteral("Network bridge");
    if (type == QLatin1String("wireguard"))
        return QStringLiteral("WireGuard VPN");
    if (type == QLatin1String("tun") || type == QLatin1String("vpn"))
        return QStringLiteral("VPN tunnel");
    if (type == QLatin1String("bond"))
        return QStringLiteral("Bonded adapter");
    if (type == QLatin1String("gsm") || type == QLatin1String("cdma"))
        return QStringLiteral("Mobile broadband");
    if (type == QLatin1String("bt"))
        return QStringLiteral("Bluetooth network");
    return QStringLiteral("Network adapter (%1)").arg(type);
}

} // namespace

NetworkInfo::NetworkInfo(QObject *parent)
    : QObject(parent)
{
    bool ok = false;
    if (!QStandardPaths::findExecutable(QStringLiteral("nmcli")).isEmpty()) {
        const QString state = runSync({QStringLiteral("-t"), QStringLiteral("-f"), QStringLiteral("RUNNING"),
                                       QStringLiteral("general")}, &ok);
        m_available = ok && state.trimmed() == QLatin1String("running");
    }
    if (m_available)
        readWifiState();
}

NetworkInfo::~NetworkInfo()
{
    if (m_process) {
        m_process->disconnect(this);
        m_process->kill();
        m_process->waitForFinished(1000);
    }
    delete m_passwordFile;
}

QString NetworkInfo::runSync(const QStringList &args, bool *ok, int timeoutMs) const
{
    QProcess p;
    p.setProcessEnvironment(toolEnvironment());
    p.start(QStringLiteral("nmcli"), args);
    const bool finished = p.waitForFinished(timeoutMs);
    if (ok)
        *ok = finished && p.exitStatus() == QProcess::NormalExit && p.exitCode() == 0;
    return QString::fromUtf8(p.readAllStandardOutput());
}

void NetworkInfo::readWifiState()
{
    bool ok = false;
    const bool on = runSync({QStringLiteral("-t"), QStringLiteral("radio"), QStringLiteral("wifi")}, &ok).trimmed()
        == QLatin1String("enabled");
    if (ok && on != m_wifiEnabled) {
        m_wifiEnabled = on;
        emit wifiEnabledChanged();
    }
}

QString NetworkInfo::defaultRouteDevice() const
{
    QProcess p;
    p.start(QStringLiteral("ip"), {QStringLiteral("-j"), QStringLiteral("route"), QStringLiteral("show"),
                                   QStringLiteral("default")});
    if (!p.waitForFinished(3000))
        return {};
    const QJsonArray routes = QJsonDocument::fromJson(p.readAllStandardOutput()).array();
    return routes.isEmpty() ? QString() : routes.first().toObject().value(QStringLiteral("dev")).toString();
}

// ---------------------------------------------------------------------------
// reads

QVariantList NetworkInfo::devices() const
{
    QVariantList list;
    if (!m_available)
        return list;
    const QString defaultDev = defaultRouteDevice();
    const QString out = runSync({QStringLiteral("-t"), QStringLiteral("-f"),
                                 QStringLiteral("DEVICE,TYPE,STATE,CONNECTION"), QStringLiteral("device")});
    for (const QString &line : out.split(QLatin1Char('\n'), Qt::SkipEmptyParts)) {
        const QStringList f = splitTerse(line);
        if (f.size() < 4)
            continue;
        const QString name = f.at(0), type = f.at(1), state = f.at(2);
        // plumbing nobody configures by hand
        if (type == QLatin1String("loopback") || type == QLatin1String("wifi-p2p")
            || name.startsWith(QLatin1String("veth")))
            continue;
        const bool managed = state != QLatin1String("unmanaged");
        const bool external = state.contains(QLatin1String("externally"));
        list << QVariantMap{
            {QStringLiteral("name"), name},
            {QStringLiteral("type"), type},
            {QStringLiteral("typeLabel"), typeLabel(type)},
            {QStringLiteral("state"), state},
            {QStringLiteral("connection"), f.at(3)},
            {QStringLiteral("connected"), state.startsWith(QLatin1String("connected"))},
            // NetworkManager only controls what it set up itself
            {QStringLiteral("controllable"), managed && !external && state != QLatin1String("unavailable")},
            {QStringLiteral("isDefault"), name == defaultDev},
        };
    }
    return list;
}

QVariantMap NetworkInfo::deviceDetails(const QString &device) const
{
    QVariantMap d;
    QStringList ipv4, ipv6, dns;
    bool ok = false;
    const QString out = runSync({QStringLiteral("-t"), QStringLiteral("device"), QStringLiteral("show"), device}, &ok);
    for (const QString &line : out.split(QLatin1Char('\n'), Qt::SkipEmptyParts)) {
        const int colon = line.indexOf(QLatin1Char(':'));
        if (colon < 0)
            continue;
        const QString key = line.left(colon);
        const QString value = splitTerse(line.mid(colon + 1)).join(QLatin1Char(':')).trimmed();
        if (value.isEmpty() || value == QLatin1String("--"))
            continue;
        if (key == QLatin1String("GENERAL.HWADDR"))
            d[QStringLiteral("hwaddr")] = value;
        else if (key == QLatin1String("GENERAL.STATE"))
            d[QStringLiteral("state")] = value.section(QLatin1Char('('), 1).chopped(value.endsWith(QLatin1Char(')')) ? 1 : 0);
        else if (key == QLatin1String("GENERAL.CONNECTION"))
            d[QStringLiteral("connection")] = value;
        else if (key == QLatin1String("GENERAL.MTU"))
            d[QStringLiteral("mtu")] = value;
        else if (key.startsWith(QLatin1String("IP4.ADDRESS")))
            ipv4 << value;
        else if (key == QLatin1String("IP4.GATEWAY"))
            d[QStringLiteral("gateway4")] = value;
        else if (key.startsWith(QLatin1String("IP4.DNS")) || key.startsWith(QLatin1String("IP6.DNS")))
            dns << value;
        else if (key.startsWith(QLatin1String("IP6.ADDRESS")))
            ipv6 << value;
        else if (key == QLatin1String("IP6.GATEWAY"))
            d[QStringLiteral("gateway6")] = value;
    }

    // unmanaged / external devices: ask the kernel directly
    if (ipv4.isEmpty() && ipv6.isEmpty()) {
        QProcess p;
        p.start(QStringLiteral("ip"), {QStringLiteral("-j"), QStringLiteral("addr"), QStringLiteral("show"),
                                       QStringLiteral("dev"), device});
        if (p.waitForFinished(3000)) {
            const QJsonArray links = QJsonDocument::fromJson(p.readAllStandardOutput()).array();
            for (const QJsonValue &link : links) {
                const QJsonObject l = link.toObject();
                if (!d.contains(QStringLiteral("hwaddr")) && l.contains(QStringLiteral("address")))
                    d[QStringLiteral("hwaddr")] = l.value(QStringLiteral("address")).toString().toUpper();
                for (const QJsonValue &a : l.value(QStringLiteral("addr_info")).toArray()) {
                    const QJsonObject ai = a.toObject();
                    const QString addr = QStringLiteral("%1/%2").arg(ai.value(QStringLiteral("local")).toString())
                                             .arg(ai.value(QStringLiteral("prefixlen")).toInt());
                    (ai.value(QStringLiteral("family")).toString() == QLatin1String("inet6") ? ipv6 : ipv4) << addr;
                }
            }
        }
    }

    d[QStringLiteral("ipv4")] = ipv4;
    d[QStringLiteral("ipv6")] = ipv6;
    d[QStringLiteral("dns")] = dns;
    return d;
}

QVariantMap NetworkInfo::identification() const
{
    QVariantMap id;
    id[QStringLiteral("hostname")] = QSysInfo::machineHostName();

    QProcess p;
    p.start(QStringLiteral("hostnamectl"), {QStringLiteral("--pretty")});
    id[QStringLiteral("prettyHostname")] = p.waitForFinished(3000)
        ? QString::fromUtf8(p.readAllStandardOutput()).trimmed() : QString();

    // the DNS search domain, from resolv.conf
    QString domain;
    QFile resolv(QStringLiteral("/etc/resolv.conf"));
    if (resolv.open(QIODevice::ReadOnly | QIODevice::Text)) {
        for (const QByteArray &line : resolv.readAll().split('\n')) {
            const QList<QByteArray> parts = line.simplified().split(' ');
            if (parts.size() >= 2 && (parts.first() == "domain" || parts.first() == "search")) {
                domain = QString::fromUtf8(parts.at(1));
                break;
            }
        }
    }
    id[QStringLiteral("domain")] = domain;
    return id;
}

bool NetworkInfo::isKnownWifi(const QString &ssid) const
{
    for (const QVariant &v : m_wifiNetworks) {
        const QVariantMap n = v.toMap();
        if (n.value(QStringLiteral("ssid")).toString() == ssid)
            return n.value(QStringLiteral("known")).toBool();
    }
    return false;
}

// ---------------------------------------------------------------------------
// background operations

void NetworkInfo::setBusy(bool busy, const QString &text)
{
    if (busy == m_busy && text == m_busyText)
        return;
    m_busy = busy;
    m_busyText = text;
    emit busyChanged();
}

void NetworkInfo::finish(bool ok, const QString &message)
{
    delete m_passwordFile;
    m_passwordFile = nullptr;
    m_password.clear();
    m_steps.clear();
    setBusy(false);
    readWifiState();
    emit devicesChanged();
    emit operationFinished(ok, message);
}

void NetworkInfo::runSteps(const QString &busyText, const QList<Step> &steps, const QString &ssid,
                           const QString &password)
{
    if (m_busy || !m_available)
        return;
    m_steps = steps;
    m_ssid = ssid;
    m_password = password;
    setBusy(true, busyText);
    runNextStep();
}

void NetworkInfo::runNextStep()
{
    if (m_steps.isEmpty()) {
        finish(true, {});
        return;
    }
    const Step step = m_steps.first();
    QStringList args = step.args;

    if (step.passwordFile) {
        // nmcli reads secrets from a file, so the key never appears on a
        // command line other local users could see in `ps`
        delete m_passwordFile;
        m_passwordFile = new QTemporaryFile(
            QStandardPaths::writableLocation(QStandardPaths::RuntimeLocation) + QStringLiteral("/w95-netkey-XXXXXX"));
        if (!m_passwordFile->open()) {
            finish(false, QStringLiteral("Could not hand the network key to NetworkManager."));
            return;
        }
        m_passwordFile->write(m_password.toUtf8());
        m_passwordFile->flush();
        for (QString &a : args)
            a.replace(QLatin1String("%PASSWORD_FILE%"), m_passwordFile->fileName());
    }

    auto *process = new QProcess(this);
    m_process = process;
    process->setProcessEnvironment(toolEnvironment());
    connect(process, &QProcess::finished, this, [this, process, step](int code, QProcess::ExitStatus status) {
        process->deleteLater();
        QString err = QString::fromUtf8(process->readAllStandardError()).trimmed();
        const QString out = QString::fromUtf8(process->readAllStandardOutput());
        if (!step.expect.isEmpty() && !out.contains(step.expect)) {
            code = code ? code : 1;
            if (err.isEmpty())
                err = out.split(QLatin1Char('\n'), Qt::SkipEmptyParts).filter(QStringLiteral("rror")).join(QLatin1Char(' '));
        }
        if (status != QProcess::NormalExit || code != 0) {
            if (!step.undoArgs.isEmpty())
                runSync(step.undoArgs); // e.g. remove a half-made Wi-Fi profile
            QString message = err;
            message.remove(QStringLiteral("Error: "));
            finish(false, message.isEmpty() ? QStringLiteral("The network operation failed.") : message);
            return;
        }
        m_steps.removeFirst();
        runNextStep();
    });
    process->start(QStringLiteral("nmcli"), args);
    if (!step.input.isEmpty()) {
        process->write(step.input);
        process->closeWriteChannel();
    }
}

void NetworkInfo::connectDevice(const QString &device)
{
    runSteps(QStringLiteral("Connecting %1...").arg(device),
             {Step{{QStringLiteral("-w"), QStringLiteral("45"), QStringLiteral("device"), QStringLiteral("connect"), device}}});
}

void NetworkInfo::disconnectDevice(const QString &device)
{
    runSteps(QStringLiteral("Disconnecting %1...").arg(device),
             {Step{{QStringLiteral("device"), QStringLiteral("disconnect"), device}}});
}

void NetworkInfo::setWifiEnabled(bool on)
{
    runSteps(on ? QStringLiteral("Turning wireless on...") : QStringLiteral("Turning wireless off..."),
             {Step{{QStringLiteral("radio"), QStringLiteral("wifi"), on ? QStringLiteral("on") : QStringLiteral("off")}}});
}

void NetworkInfo::connectWifi(const QString &ssid, const QString &password)
{
    if (password.isEmpty()) {
        // an open network, or one NetworkManager already has a profile for
        runSteps(QStringLiteral("Connecting to %1...").arg(ssid),
                 {Step{{QStringLiteral("-w"), QStringLiteral("45"), QStringLiteral("device"), QStringLiteral("wifi"),
                        QStringLiteral("connect"), ssid}}},
                 ssid);
        return;
    }

    QString security;
    for (const QVariant &v : m_wifiNetworks)
        if (v.toMap().value(QStringLiteral("ssid")).toString() == ssid)
            security = v.toMap().value(QStringLiteral("security")).toString();

    if (security.contains(QLatin1String("802.1X"))) {
        emit operationFinished(false, QStringLiteral("%1 uses an enterprise (802.1X) login, which the Network "
                                                     "applet can't set up yet.").arg(ssid));
        return;
    }

    QString keyMgmt = QStringLiteral("wpa-psk");
    QString secretName = QStringLiteral("802-11-wireless-security.psk");
    if (security.contains(QLatin1String("WPA3")) && !security.contains(QLatin1String("WPA2"))) {
        keyMgmt = QStringLiteral("sae");
    } else if (security.contains(QLatin1String("WEP"))) {
        keyMgmt = QStringLiteral("none");
        secretName = QStringLiteral("802-11-wireless-security.wep-key0");
    }

    // The key is saved in the profile itself, kept by NetworkManager (not by
    // a desktop's keyring: another desktop's, KDE's KWallet say, isn't
    // there when you log into this one), so it connects by itself next time
    // in any desktop. A new network gets a profile first.
    const QString profile = ssid;
    const bool fresh = !isKnownWifi(ssid);
    const QStringList undo = fresh ? QStringList{QStringLiteral("connection"), QStringLiteral("delete"), QStringLiteral("id"), profile}
                                   : QStringList{};
    QList<Step> steps;
    if (fresh) {
        steps << Step{{QStringLiteral("connection"), QStringLiteral("add"), QStringLiteral("type"), QStringLiteral("wifi"),
                       QStringLiteral("con-name"), profile, QStringLiteral("ssid"), ssid,
                       QStringLiteral("wifi-sec.key-mgmt"), keyMgmt}};
    }
    Step owner{{QStringLiteral("connection"), QStringLiteral("modify"), QStringLiteral("id"), profile,
                QStringLiteral("802-11-wireless-security.key-mgmt"), keyMgmt,
                secretName + QStringLiteral("-flags"), QStringLiteral("0")}};
    owner.undoArgs = undo;
    steps << owner;
    // the key goes in through the editor's stdin, never on a command line
    Step store{{QStringLiteral("connection"), QStringLiteral("edit"), QStringLiteral("id"), profile}};
    store.input = "set " + secretName.toUtf8() + ' ' + password.toUtf8() + "\nsave\nquit\n";
    store.expect = QStringLiteral("successfully updated");
    store.undoArgs = undo;
    steps << store;
    Step up{{QStringLiteral("-w"), QStringLiteral("45"), QStringLiteral("connection"), QStringLiteral("up"),
             QStringLiteral("id"), profile}};
    up.undoArgs = undo;
    steps << up;

    runSteps(QStringLiteral("Connecting to %1...").arg(ssid), steps, ssid, secretName + QLatin1Char(':') + password + QLatin1Char('\n'));
}

void NetworkInfo::refreshWifi(bool rescan)
{
    if (!m_available || m_busy)
        return;
    readWifiState();

    // which SSIDs already have saved profiles (named after the SSID by default)
    QStringList known;
    const QString conns = runSync({QStringLiteral("-t"), QStringLiteral("-f"), QStringLiteral("NAME,TYPE"),
                                   QStringLiteral("connection"), QStringLiteral("show")});
    for (const QString &line : conns.split(QLatin1Char('\n'), Qt::SkipEmptyParts)) {
        const QStringList f = splitTerse(line);
        if (f.size() >= 2 && f.at(1) == QLatin1String("802-11-wireless"))
            known << f.at(0);
    }

    setBusy(true, rescan ? QStringLiteral("Searching for wireless networks...") : QString());
    auto *process = new QProcess(this);
    m_process = process;
    process->setProcessEnvironment(toolEnvironment());
    connect(process, &QProcess::finished, this, [this, process, known]() {
        process->deleteLater();
        QHash<QString, QVariantMap> bySsid; // strongest access point per network
        const QString out = QString::fromUtf8(process->readAllStandardOutput());
        for (const QString &line : out.split(QLatin1Char('\n'), Qt::SkipEmptyParts)) {
            const QStringList f = splitTerse(line);
            if (f.size() < 4 || f.at(1).isEmpty()) // hidden networks have no name
                continue;
            const QString ssid = f.at(1);
            const int signal = f.at(2).toInt();
            const bool inUse = f.at(0).trimmed() == QLatin1String("*");
            QVariantMap existing = bySsid.value(ssid);
            if (!existing.isEmpty() && existing.value(QStringLiteral("signal")).toInt() >= signal && !inUse) {
                continue;
            }
            const bool wasInUse = existing.value(QStringLiteral("inUse")).toBool();
            bySsid.insert(ssid, QVariantMap{
                {QStringLiteral("ssid"), ssid},
                {QStringLiteral("signal"), signal},
                {QStringLiteral("security"), f.at(3).trimmed() == QLatin1String("--") ? QString() : f.at(3).trimmed()},
                {QStringLiteral("inUse"), inUse || wasInUse},
                {QStringLiteral("known"), known.contains(ssid)},
            });
        }
        QVariantList list;
        for (const QVariantMap &n : std::as_const(bySsid))
            list << n;
        std::sort(list.begin(), list.end(), [](const QVariant &a, const QVariant &b) {
            const QVariantMap x = a.toMap(), y = b.toMap();
            if (x.value(QStringLiteral("inUse")).toBool() != y.value(QStringLiteral("inUse")).toBool())
                return x.value(QStringLiteral("inUse")).toBool();
            return x.value(QStringLiteral("signal")).toInt() > y.value(QStringLiteral("signal")).toInt();
        });
        m_wifiNetworks = list;
        emit wifiNetworksChanged();
        setBusy(false);
    });
    process->start(QStringLiteral("nmcli"), {QStringLiteral("-t"), QStringLiteral("-f"),
                                             QStringLiteral("IN-USE,SSID,SIGNAL,SECURITY"), QStringLiteral("device"),
                                             QStringLiteral("wifi"), QStringLiteral("list"), QStringLiteral("--rescan"),
                                             rescan ? QStringLiteral("yes") : QStringLiteral("no")});
}
