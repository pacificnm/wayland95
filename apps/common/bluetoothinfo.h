#pragma once

#include <QDBusContext>
#include <QDBusMessage>
#include <QDBusObjectPath>
#include <QObject>
#include <QTimer>
#include <QVariantList>
#include <QVariantMap>
#include <QtQmlIntegration/qqmlintegration.h>

#include <functional>

class BluetoothInfo;

// The pairing agent BlueZ calls back when a device needs the user: to
// confirm a passkey, type a PIN, or allow a device to pair. Questions are
// answered asynchronously (delayed D-Bus replies) once the user has
// responded in the applet. Only used for pairing started from the applet:
// we don't make ourselves the system's default agent.
class BluetoothAgent : public QObject, protected QDBusContext
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.bluez.Agent1")

public:
    explicit BluetoothAgent(BluetoothInfo *owner);

public slots:
    void Release();
    QString RequestPinCode(const QDBusObjectPath &device);
    void DisplayPinCode(const QDBusObjectPath &device, const QString &pincode);
    uint RequestPasskey(const QDBusObjectPath &device);
    void DisplayPasskey(const QDBusObjectPath &device, uint passkey, ushort entered);
    void RequestConfirmation(const QDBusObjectPath &device, uint passkey);
    void RequestAuthorization(const QDBusObjectPath &device);
    void AuthorizeService(const QDBusObjectPath &device, const QString &uuid);
    void Cancel();

private:
    void ask(const QString &kind, const QDBusObjectPath &device, const QString &code);

    BluetoothInfo *m_owner;
};

// The Bluetooth applet's view of BlueZ, over D-Bus: the adapter, known and
// nearby devices, and pairing. Changes made elsewhere (a device connecting
// on its own, another Bluetooth tool) show up live.
//
// Exposed to QML as the singleton `Bluetooth`.
class BluetoothInfo : public QObject
{
    Q_OBJECT
    QML_NAMED_ELEMENT(Bluetooth)
    QML_SINGLETON

    // BlueZ is running and there's an adapter
    Q_PROPERTY(bool available READ available NOTIFY adapterChanged)
    Q_PROPERTY(bool powered READ powered NOTIFY adapterChanged)
    // switched off with rfkill (airplane mode or a hardware switch)
    Q_PROPERTY(bool blocked READ blocked NOTIFY adapterChanged)
    Q_PROPERTY(bool discoverable READ discoverable NOTIFY adapterChanged)
    Q_PROPERTY(int discoverableTimeout READ discoverableTimeout NOTIFY adapterChanged)
    Q_PROPERTY(bool discovering READ discovering NOTIFY adapterChanged)
    Q_PROPERTY(QString adapterName READ adapterName NOTIFY adapterChanged)
    Q_PROPERTY(QString adapterAddress READ adapterAddress NOTIFY adapterChanged)
    // [{ path, address, name, icon, kind, paired, trusted, connected, battery }]
    // connected first, then paired, then nearby devices; by name
    Q_PROPERTY(QVariantList devices READ devices NOTIFY devicesChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
    Q_PROPERTY(QString busyText READ busyText NOTIFY busyChanged)

public:
    explicit BluetoothInfo(QObject *parent = nullptr);

    bool available() const { return !m_adapterPath.isEmpty(); }
    bool powered() const { return m_adapter.value(QStringLiteral("Powered")).toBool(); }
    bool blocked() const;
    bool discoverable() const { return m_adapter.value(QStringLiteral("Discoverable")).toBool(); }
    int discoverableTimeout() const { return m_adapter.value(QStringLiteral("DiscoverableTimeout")).toInt(); }
    bool discovering() const { return m_adapter.value(QStringLiteral("Discovering")).toBool(); }
    QString adapterName() const { return m_adapter.value(QStringLiteral("Alias")).toString(); }
    QString adapterAddress() const { return m_adapter.value(QStringLiteral("Address")).toString(); }
    QVariantList devices() const { return m_devices; }
    bool busy() const { return !m_busyText.isEmpty(); }
    QString busyText() const { return m_busyText; }

    Q_INVOKABLE void setPowered(bool on);
    Q_INVOKABLE void setDiscoverable(bool on);
    Q_INVOKABLE void setAdapterName(const QString &name);
    // look for nearby devices for a minute (or until stopped)
    Q_INVOKABLE void startDiscovery();
    Q_INVOKABLE void stopDiscovery();
    // pair, trust, then connect
    Q_INVOKABLE void pair(const QString &path);
    Q_INVOKABLE void connectDevice(const QString &path);
    Q_INVOKABLE void disconnectDevice(const QString &path);
    // forget a device (unpairs it)
    Q_INVOKABLE void removeDevice(const QString &path);
    // the user's answer to pairingRequest(); `value` is the PIN or passkey
    Q_INVOKABLE void answerPairing(bool accept, const QString &value = {});

signals:
    void adapterChanged();
    void devicesChanged();
    void busyChanged();
    // an operation finished; `message` is for the user (empty on success)
    void operationFinished(bool ok, const QString &message);
    // kind: "confirm" (does `code` match the device?), "display" (type
    // `code` on the device), "pin" / "passkey" (ask the user for one),
    // "authorize" (may this device pair?), "cancel" (the request is gone)
    void pairingRequest(const QString &kind, const QString &deviceName, const QString &code);

private slots:
    void scheduleReload();
    void reload();

private:
    friend class BluetoothAgent;

    QDBusMessage adapterCall(const QString &method) const;
    void setAdapterProperty(const QString &name, const QVariant &value, const QString &failure);
    void deviceCall(const QString &path, const QString &method, const QString &busyText,
                    const QString &failure, int timeoutMs, std::function<void()> onSuccess = {});
    QString deviceName(const QString &path) const;
    QString friendlyError(const QString &errorName, const QString &errorMessage) const;
    void setBusy(const QString &text);
    void finish(bool ok, const QString &message);
    // from the agent
    void agentRequest(const QString &kind, const QDBusMessage &reply, const QString &devicePath,
                      const QString &code);
    void agentCancelled();

    BluetoothAgent *m_agent = nullptr;
    bool m_agentRegistered = false;
    QString m_adapterPath;
    QVariantMap m_adapter;
    QVariantList m_devices;
    QString m_busyText;
    QTimer m_reloadTimer;
    QTimer m_discoveryTimer;

    // an agent question waiting for the user
    QDBusMessage m_pendingAgentMessage;
    QString m_pendingAgentKind;
};
