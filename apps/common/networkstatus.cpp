#include "networkstatus.h"

#include <QStandardPaths>

#include <functional>

namespace {

// nmcli -t separates fields with ':' and escapes ':' and '\' inside them
QStringList splitTerse(const QString &line)
{
    QStringList fields;
    QString current;
    for (qsizetype i = 0; i < line.size(); ++i) {
        const QChar c = line.at(i);
        if (c == QLatin1Char('\\') && i + 1 < line.size()) {
            current += line.at(++i);
        } else if (c == QLatin1Char(':')) {
            fields << current;
            current.clear();
        } else {
            current += c;
        }
    }
    fields << current;
    return fields;
}

} // namespace

NetworkStatus::NetworkStatus(QObject *parent)
    : QObject(parent)
{
    m_refreshTimer.setSingleShot(true);
    m_refreshTimer.setInterval(300);
    connect(&m_refreshTimer, &QTimer::timeout, this, &NetworkStatus::refresh);

    m_signalTimer.setInterval(30 * 1000);
    connect(&m_signalTimer, &QTimer::timeout, this, &NetworkStatus::refresh);
    m_signalTimer.start();

    m_restartTimer.setSingleShot(true);
    m_restartTimer.setInterval(10 * 1000);
    connect(&m_restartTimer, &QTimer::timeout, this, &NetworkStatus::startMonitor);

    if (QStandardPaths::findExecutable(QStringLiteral("nmcli")).isEmpty())
        return; // no NetworkManager: stays unavailable
    startMonitor();
    refresh();
}

NetworkStatus::~NetworkStatus()
{
    if (m_monitor) {
        m_monitor->disconnect(this);
        m_monitor->kill();
        m_monitor->waitForFinished(500);
    }
}

void NetworkStatus::startMonitor()
{
    if (m_monitor)
        m_monitor->deleteLater();
    m_monitor = new QProcess(this);
    m_monitor->setProgram(QStringLiteral("nmcli"));
    m_monitor->setArguments({ QStringLiteral("monitor") });
    m_monitor->setStandardInputFile(QProcess::nullDevice());
    connect(m_monitor, &QProcess::readyReadStandardOutput, this, [this] {
        m_monitor->readAllStandardOutput(); // any line means "something changed"
        m_refreshTimer.start();
    });
    connect(m_monitor, &QProcess::finished, this, [this] {
        // NetworkManager stopped or restarted
        m_refreshTimer.start();
        m_restartTimer.start();
    });
    m_monitor->start();
}

void NetworkStatus::runQuery(const QStringList &args, std::function<void(bool, const QString &)> done)
{
    auto *p = new QProcess(this);
    p->setProgram(QStringLiteral("nmcli"));
    p->setArguments(args);
    p->setStandardInputFile(QProcess::nullDevice());
    connect(p, &QProcess::finished, this, [p, done](int code, QProcess::ExitStatus status) {
        p->deleteLater();
        done(status == QProcess::NormalExit && code == 0, QString::fromUtf8(p->readAllStandardOutput()));
    });
    connect(p, &QProcess::errorOccurred, this, [p, done](QProcess::ProcessError e) {
        if (e == QProcess::FailedToStart) {
            p->deleteLater();
            done(false, {});
        }
    });
    p->start();
}

void NetworkStatus::refresh()
{
    if (m_refreshing) {
        m_refreshAgain = true;
        return;
    }
    m_refreshing = true;
    runQuery({ QStringLiteral("-t"), QStringLiteral("-f"), QStringLiteral("STATE,CONNECTIVITY,WIFI"),
               QStringLiteral("general") },
             [this](bool ok, const QString &out) { readGeneral(ok, out); });
}

void NetworkStatus::readGeneral(bool ok, const QString &out)
{
    m_nextAvailable = ok;
    if (!ok) {
        m_nextState.clear();
        m_nextType.clear();
        m_nextName.clear();
        m_nextSignal = 0;
        m_nextWifiEnabled = m_nextHasWifi = false;
        publish();
        return;
    }
    const QStringList f = splitTerse(out.trimmed());
    const QString state = f.value(0), connectivity = f.value(1);
    m_nextWifiEnabled = f.value(2) == QLatin1String("enabled");
    if (state.startsWith(QLatin1String("connected"))) {
        m_nextState = (connectivity == QLatin1String("full") || connectivity == QLatin1String("unknown"))
                          ? QStringLiteral("connected") : QStringLiteral("limited");
    } else if (state == QLatin1String("connecting")) {
        m_nextState = QStringLiteral("connecting");
    } else if (state == QLatin1String("asleep")) {
        m_nextState = QStringLiteral("off");
    } else {
        m_nextState = QStringLiteral("disconnected");
    }
    runQuery({ QStringLiteral("-t"), QStringLiteral("-f"), QStringLiteral("TYPE,STATE,CONNECTION"),
               QStringLiteral("device") },
             [this](bool ok, const QString &out) { readDevices(ok, out); });
}

void NetworkStatus::readDevices(bool ok, const QString &out)
{
    m_nextType.clear();
    m_nextName.clear();
    m_nextHasWifi = false;
    QString otherName;
    if (ok) {
        // the main connection: a cable wins over Wi-Fi, as NetworkManager's
        // default route usually does; anything else only if there's no
        // cable or Wi-Fi (docker bridges, loopback and VPNs don't count)
        for (const QString &line : out.split(QLatin1Char('\n'), Qt::SkipEmptyParts)) {
            const QStringList f = splitTerse(line);
            const QString type = f.value(0), state = f.value(1), name = f.value(2);
            if (type == QLatin1String("wifi"))
                m_nextHasWifi = true;
            if (!state.startsWith(QLatin1String("connected")) || state.contains(QLatin1String("externally")))
                continue;
            if (type == QLatin1String("ethernet")) {
                if (m_nextType != QLatin1String("ethernet")) {
                    m_nextType = QStringLiteral("ethernet");
                    m_nextName = name;
                }
            } else if (type == QLatin1String("wifi")) {
                if (m_nextType.isEmpty()) {
                    m_nextType = QStringLiteral("wifi");
                    m_nextName = name;
                }
            } else if (type != QLatin1String("loopback") && type != QLatin1String("bridge")
                       && type != QLatin1String("wireguard") && type != QLatin1String("tun")
                       && otherName.isEmpty()) {
                otherName = name;
            }
        }
    }
    if (m_nextType.isEmpty() && !otherName.isEmpty()) {
        m_nextType = QStringLiteral("other");
        m_nextName = otherName;
    }
    m_nextSignal = 0;
    if (m_nextType == QLatin1String("wifi")) {
        runQuery({ QStringLiteral("-t"), QStringLiteral("-f"), QStringLiteral("IN-USE,SIGNAL"), QStringLiteral("device"),
                   QStringLiteral("wifi"), QStringLiteral("list"), QStringLiteral("--rescan"), QStringLiteral("no") },
                 [this](bool ok, const QString &out) { readWifi(ok, out); });
        return;
    }
    publish();
}

void NetworkStatus::readWifi(bool ok, const QString &out)
{
    if (ok) {
        for (const QString &line : out.split(QLatin1Char('\n'), Qt::SkipEmptyParts)) {
            const QStringList f = splitTerse(line);
            if (f.value(0) == QLatin1String("*")) {
                m_nextSignal = f.value(1).toInt();
                break;
            }
        }
    }
    publish();
}

void NetworkStatus::publish()
{
    m_refreshing = false;
    const bool differs = m_nextAvailable != m_available || m_nextState != m_state || m_nextType != m_type
                         || m_nextName != m_connectionName || m_nextSignal != m_signal
                         || m_nextWifiEnabled != m_wifiEnabled || m_nextHasWifi != m_hasWifi;
    m_available = m_nextAvailable;
    m_state = m_nextState;
    m_type = m_nextType;
    m_connectionName = m_nextName;
    m_signal = m_nextSignal;
    m_wifiEnabled = m_nextWifiEnabled;
    m_hasWifi = m_nextHasWifi;
    if (differs)
        emit changed();
    if (m_refreshAgain) {
        m_refreshAgain = false;
        refresh();
    }
}

void NetworkStatus::setWifiEnabled(bool on)
{
    runQuery({ QStringLiteral("radio"), QStringLiteral("wifi"), on ? QStringLiteral("on") : QStringLiteral("off") },
             [this](bool, const QString &) { refresh(); });
}
