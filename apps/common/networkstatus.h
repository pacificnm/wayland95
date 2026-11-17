#pragma once

#include <QObject>
#include <QProcess>
#include <QTimer>
#include <QStringList>
#include <QVariantList>
#include <QtQmlIntegration/qqmlintegration.h>

#include <functional>

// A live summary of the network connection for the taskbar: connected or
// not, by cable or Wi-Fi, which network, and Wi-Fi signal strength.
//
// `nmcli monitor` reports every change NetworkManager makes; each one
// triggers a quick asynchronous re-read. Nothing here blocks, so it's safe
// to use in the compositor. (The Network applet has the full controls.)
//
// Exposed to QML as the singleton `NetworkStatus`.
class NetworkStatus : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

    // NetworkManager is running
    Q_PROPERTY(bool available READ available NOTIFY changed)
    // "connected", "limited" (no internet), "connecting", "disconnected", "off"
    Q_PROPERTY(QString state READ state NOTIFY changed)
    // of the main connection: "ethernet", "wifi", "other" (empty if none)
    Q_PROPERTY(QString type READ type NOTIFY changed)
    Q_PROPERTY(QString connectionName READ connectionName NOTIFY changed)
    // Wi-Fi signal strength, 0-100 (when connected by Wi-Fi)
    Q_PROPERTY(int signal READ signal NOTIFY changed)
    Q_PROPERTY(bool wifiEnabled READ wifiEnabled NOTIFY changed)
    // there is a Wi-Fi adapter
    Q_PROPERTY(bool hasWifi READ hasWifi NOTIFY changed)
    // the main connection's adapter and addresses: "wlp0s20f3", its
    // hardware address, IPv4 addresses with their prefix ("192.168.1.20/24"),
    // the gateway, DNS servers, and IPv6 addresses (not the link-local ones)
    Q_PROPERTY(QString device READ device NOTIFY changed)
    Q_PROPERTY(QString hardwareAddress READ hardwareAddress NOTIFY changed)
    Q_PROPERTY(QStringList ipv4 READ ipv4 NOTIFY changed)
    Q_PROPERTY(QString gateway READ gateway NOTIFY changed)
    Q_PROPERTY(QStringList dns READ dns NOTIFY changed)
    Q_PROPERTY(QStringList ipv6 READ ipv6 NOTIFY changed)
    // WireGuard tunnels that are up: [{ name, addresses: [...] }]
    Q_PROPERTY(QVariantList vpns READ vpns NOTIFY changed)
    Q_PROPERTY(bool vpnConnected READ vpnConnected NOTIFY changed)

public:
    explicit NetworkStatus(QObject *parent = nullptr);
    ~NetworkStatus() override;

    bool available() const { return m_available; }
    QString state() const { return m_state; }
    QString type() const { return m_type; }
    QString connectionName() const { return m_connectionName; }
    int signal() const { return m_signal; }
    bool wifiEnabled() const { return m_wifiEnabled; }
    bool hasWifi() const { return m_hasWifi; }
    QString device() const { return m_details.device; }
    QString hardwareAddress() const { return m_details.hardwareAddress; }
    QStringList ipv4() const { return m_details.ipv4; }
    QString gateway() const { return m_details.gateway; }
    QStringList dns() const { return m_details.dns; }
    QStringList ipv6() const { return m_details.ipv6; }
    QVariantList vpns() const { return m_vpns; }
    bool vpnConnected() const { return !m_vpns.isEmpty(); }

    Q_INVOKABLE void setWifiEnabled(bool on);
    // re-read now (e.g. when a pop-up opens)
    Q_INVOKABLE void refresh();

signals:
    void changed();

private:
    void startMonitor();
    void runQuery(const QStringList &args, std::function<void(bool, const QString &)> done);
    void readGeneral(bool ok, const QString &out);
    void readDevices(bool ok, const QString &out);
    void readWifi(bool ok, const QString &out);
    void readDetails(); // the main device's, then each tunnel's
    void publish();

    struct Details {
        QString device, hardwareAddress, gateway;
        QStringList ipv4, dns, ipv6;
        bool operator==(const Details &o) const
        {
            return device == o.device && hardwareAddress == o.hardwareAddress && gateway == o.gateway
                   && ipv4 == o.ipv4 && dns == o.dns && ipv6 == o.ipv6;
        }
    };
    static Details parseDetails(const QString &out);

    QProcess *m_monitor = nullptr;
    QTimer m_refreshTimer;  // debounce bursts of monitor lines
    QTimer m_signalTimer;   // signal strength drifts without events
    QTimer m_restartTimer;
    bool m_refreshing = false;
    bool m_refreshAgain = false;

    bool m_available = false;
    QString m_state;
    QString m_type;
    QString m_connectionName;
    int m_signal = 0;
    bool m_wifiEnabled = false;
    bool m_hasWifi = false;
    Details m_details;
    QVariantList m_vpns;

    // being gathered by the current refresh
    Details m_nextDetails;
    QString m_nextDevice;
    QStringList m_nextVpnDevices, m_pendingVpns;
    QVariantList m_nextVpns;
    QString m_nextState, m_nextType, m_nextName;
    int m_nextSignal = 0;
    bool m_nextAvailable = false, m_nextWifiEnabled = false, m_nextHasWifi = false;
};
