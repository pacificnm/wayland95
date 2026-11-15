#pragma once

#include <QObject>
#include <QPointer>
#include <QVariantList>
#include <QVariantMap>
#include <QtQmlIntegration/qqmlintegration.h>

class QProcess;
class QTemporaryFile;

// The Network applet's view of the machine, through NetworkManager's
// command-line client (nmcli) and iproute2. Reads are quick and
// synchronous; anything that changes the network runs in the background
// and reports through operationFinished().
//
// Exposed to QML as the singleton `Network`.
class NetworkInfo : public QObject
{
    Q_OBJECT
    QML_NAMED_ELEMENT(Network)
    QML_SINGLETON

    // NetworkManager is installed and running
    Q_PROPERTY(bool available READ available CONSTANT)
    // an operation (connect, disconnect, scan...) is in progress
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
    Q_PROPERTY(QString busyText READ busyText NOTIFY busyChanged)
    Q_PROPERTY(bool wifiEnabled READ wifiEnabled NOTIFY wifiEnabledChanged)
    // last scan result: [{ ssid, signal, security, inUse, known }]
    Q_PROPERTY(QVariantList wifiNetworks READ wifiNetworks NOTIFY wifiNetworksChanged)

public:
    explicit NetworkInfo(QObject *parent = nullptr);
    ~NetworkInfo() override;

    bool available() const { return m_available; }
    bool busy() const { return m_busy; }
    QString busyText() const { return m_busyText; }
    bool wifiEnabled() const { return m_wifiEnabled; }
    QVariantList wifiNetworks() const { return m_wifiNetworks; }

    // [{ name, type, typeLabel, state, connection, managed, connected, isDefault }]
    Q_INVOKABLE QVariantList devices() const;
    // { hwaddr, state, connection, mtu, ipv4: [], gateway4, dns: [], ipv6: [], gateway6 }
    Q_INVOKABLE QVariantMap deviceDetails(const QString &device) const;
    // { hostname, prettyHostname, domain }
    Q_INVOKABLE QVariantMap identification() const;
    Q_INVOKABLE bool isKnownWifi(const QString &ssid) const;

    Q_INVOKABLE void connectDevice(const QString &device);
    Q_INVOKABLE void disconnectDevice(const QString &device);
    Q_INVOKABLE void setWifiEnabled(bool on);
    Q_INVOKABLE void refreshWifi(bool rescan);
    // `password` may be empty for open or already-saved networks
    Q_INVOKABLE void connectWifi(const QString &ssid, const QString &password);

signals:
    void busyChanged();
    void wifiEnabledChanged();
    void wifiNetworksChanged();
    void devicesChanged();
    // an operation finished; `message` is for the user (empty on success)
    void operationFinished(bool ok, const QString &message);

private:
    struct Step {
        QStringList args;
        bool passwordFile = false; // feed the key via a private 0600 file
        QStringList undoArgs;      // run if this step fails (e.g. delete a new profile)
        QByteArray input;          // written to nmcli's stdin (keeps the key off the command line)
        QString expect;            // must appear in its output, or the step failed
    };

    QString runSync(const QStringList &args, bool *ok = nullptr, int timeoutMs = 5000) const;
    void runSteps(const QString &busyText, const QList<Step> &steps, const QString &ssid = {},
                  const QString &password = {});
    void runNextStep();
    void finish(bool ok, const QString &message);
    void setBusy(bool busy, const QString &text = {});
    void readWifiState();
    QString defaultRouteDevice() const;

    bool m_available = false;
    bool m_busy = false;
    QString m_busyText;
    bool m_wifiEnabled = false;
    QVariantList m_wifiNetworks;

    // the running multi-step operation
    QList<Step> m_steps;
    QString m_ssid;
    QString m_password;
    QPointer<QProcess> m_process;
    QTemporaryFile *m_passwordFile = nullptr;
};
