#include "bluetoothinfo.h"

#include <QDBusArgument>
#include <QDBusConnection>
#include <QDBusMetaType>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QDBusVariant>
#include <QMap>

#include <algorithm>

namespace {

const QString Service = QStringLiteral("org.bluez");
const QString AdapterInterface = QStringLiteral("org.bluez.Adapter1");
const QString DeviceInterface = QStringLiteral("org.bluez.Device1");
const QString BatteryInterface = QStringLiteral("org.bluez.Battery1");
const QString PropertiesInterface = QStringLiteral("org.freedesktop.DBus.Properties");
const QString ObjectManagerInterface = QStringLiteral("org.freedesktop.DBus.ObjectManager");
const QString AgentPath = QStringLiteral("/org/wayland95/bluetooth/agent");

using InterfaceMap = QMap<QString, QVariantMap>;
using ObjectMap = QMap<QDBusObjectPath, InterfaceMap>;

QDBusConnection bus() { return QDBusConnection::systemBus(); }

// BlueZ's icon names, reduced to the handful of pictures the applet draws
QString kindForIcon(const QString &icon)
{
    if (icon.startsWith(QLatin1String("audio")))
        return QStringLiteral("audio");
    if (icon == QLatin1String("input-keyboard"))
        return QStringLiteral("keyboard");
    if (icon == QLatin1String("input-mouse") || icon == QLatin1String("input-tablet"))
        return QStringLiteral("mouse");
    if (icon == QLatin1String("input-gaming"))
        return QStringLiteral("gamepad");
    if (icon == QLatin1String("phone"))
        return QStringLiteral("phone");
    if (icon == QLatin1String("computer"))
        return QStringLiteral("computer");
    return QStringLiteral("other");
}

QString passkeyText(uint passkey)
{
    return QStringLiteral("%1").arg(passkey, 6, 10, QLatin1Char('0'));
}

} // namespace

// ---------------------------------------------------------------------------
// BluetoothAgent

BluetoothAgent::BluetoothAgent(BluetoothInfo *owner)
    : QObject(owner)
    , m_owner(owner)
{
}

void BluetoothAgent::ask(const QString &kind, const QDBusObjectPath &device, const QString &code)
{
    // answered later, from BluetoothInfo::answerPairing()
    setDelayedReply(true);
    m_owner->agentRequest(kind, message(), device.path(), code);
}

void BluetoothAgent::Release()
{
    m_owner->m_agentRegistered = false;
}

QString BluetoothAgent::RequestPinCode(const QDBusObjectPath &device)
{
    ask(QStringLiteral("pin"), device, {});
    return {};
}

void BluetoothAgent::DisplayPinCode(const QDBusObjectPath &device, const QString &pincode)
{
    emit m_owner->pairingRequest(QStringLiteral("display"), m_owner->deviceName(device.path()), pincode);
}

uint BluetoothAgent::RequestPasskey(const QDBusObjectPath &device)
{
    ask(QStringLiteral("passkey"), device, {});
    return 0;
}

void BluetoothAgent::DisplayPasskey(const QDBusObjectPath &device, uint passkey, ushort entered)
{
    Q_UNUSED(entered);
    emit m_owner->pairingRequest(QStringLiteral("display"), m_owner->deviceName(device.path()),
                                 passkeyText(passkey));
}

void BluetoothAgent::RequestConfirmation(const QDBusObjectPath &device, uint passkey)
{
    ask(QStringLiteral("confirm"), device, passkeyText(passkey));
}

void BluetoothAgent::RequestAuthorization(const QDBusObjectPath &device)
{
    ask(QStringLiteral("authorize"), device, {});
}

void BluetoothAgent::AuthorizeService(const QDBusObjectPath &device, const QString &uuid)
{
    Q_UNUSED(uuid);
    // Services of devices the user paired are fine; anything else is refused.
    for (const QVariant &v : m_owner->m_devices) {
        const QVariantMap d = v.toMap();
        if (d.value(QStringLiteral("path")).toString() == device.path() && d.value(QStringLiteral("paired")).toBool())
            return;
    }
    sendErrorReply(QStringLiteral("org.bluez.Error.Rejected"), QStringLiteral("Device is not paired"));
}

void BluetoothAgent::Cancel()
{
    m_owner->agentCancelled();
}

// ---------------------------------------------------------------------------
// BluetoothInfo

BluetoothInfo::BluetoothInfo(QObject *parent)
    : QObject(parent)
{
    qDBusRegisterMetaType<InterfaceMap>();
    qDBusRegisterMetaType<ObjectMap>();

    // Changes come in bursts (a device connecting changes several
    // properties at once), so reload once they've settled.
    m_reloadTimer.setSingleShot(true);
    m_reloadTimer.setInterval(100);
    connect(&m_reloadTimer, &QTimer::timeout, this, &BluetoothInfo::reload);

    m_discoveryTimer.setSingleShot(true);
    m_discoveryTimer.setInterval(60 * 1000);
    connect(&m_discoveryTimer, &QTimer::timeout, this, &BluetoothInfo::stopDiscovery);

    QDBusConnection b = bus();
    b.connect(Service, QString(), PropertiesInterface, QStringLiteral("PropertiesChanged"), this,
              SLOT(scheduleReload()));
    b.connect(Service, QStringLiteral("/"), ObjectManagerInterface, QStringLiteral("InterfacesAdded"), this,
              SLOT(scheduleReload()));
    b.connect(Service, QStringLiteral("/"), ObjectManagerInterface, QStringLiteral("InterfacesRemoved"), this,
              SLOT(scheduleReload()));
    // bluetoothd starting or stopping
    b.connect(QStringLiteral("org.freedesktop.DBus"), QStringLiteral("/org/freedesktop/DBus"),
              QStringLiteral("org.freedesktop.DBus"), QStringLiteral("NameOwnerChanged"),
              { Service }, QString(), this, SLOT(scheduleReload()));

    m_agent = new BluetoothAgent(this);
    b.registerObject(AgentPath, m_agent, QDBusConnection::ExportAllSlots);

    reload();
}

bool BluetoothInfo::blocked() const
{
    return m_adapter.value(QStringLiteral("PowerState")).toString() == QLatin1String("off-blocked");
}

void BluetoothInfo::scheduleReload()
{
    m_reloadTimer.start();
}

void BluetoothInfo::reload()
{
    const QDBusMessage call = QDBusMessage::createMethodCall(Service, QStringLiteral("/"), ObjectManagerInterface,
                                                             QStringLiteral("GetManagedObjects"));
    const QDBusMessage reply = bus().call(call, QDBus::Block, 3000);

    ObjectMap objects;
    if (reply.type() == QDBusMessage::ReplyMessage && !reply.arguments().isEmpty())
        objects = qdbus_cast<ObjectMap>(reply.arguments().constFirst());

    // the first adapter (hci0 on nearly every machine)
    QString adapterPath;
    QVariantMap adapter;
    for (auto it = objects.cbegin(); it != objects.cend(); ++it) {
        if (it.value().contains(AdapterInterface)) {
            adapterPath = it.key().path();
            adapter = it.value().value(AdapterInterface);
            break;
        }
    }

    QVariantList devices;
    for (auto it = objects.cbegin(); it != objects.cend(); ++it) {
        if (!it.value().contains(DeviceInterface))
            continue;
        const QVariantMap d = it.value().value(DeviceInterface);
        if (d.value(QStringLiteral("Adapter")).value<QDBusObjectPath>().path() != adapterPath)
            continue;
        const bool paired = d.value(QStringLiteral("Paired")).toBool();
        const bool connected = d.value(QStringLiteral("Connected")).toBool();
        const QString name = d.value(QStringLiteral("Name")).toString();
        // Nearby devices that don't announce a name are mostly beacons and
        // other gadgets nobody wants to pair with.
        if (!paired && !connected && name.isEmpty())
            continue;
        const QString icon = d.value(QStringLiteral("Icon")).toString();
        int battery = -1;
        if (it.value().contains(BatteryInterface))
            battery = it.value().value(BatteryInterface).value(QStringLiteral("Percentage")).toInt();
        devices.append(QVariantMap{
            { QStringLiteral("path"), it.key().path() },
            { QStringLiteral("address"), d.value(QStringLiteral("Address")).toString() },
            { QStringLiteral("name"), name.isEmpty() ? d.value(QStringLiteral("Alias")).toString() : name },
            { QStringLiteral("icon"), icon },
            { QStringLiteral("kind"), kindForIcon(icon) },
            { QStringLiteral("paired"), paired },
            { QStringLiteral("trusted"), d.value(QStringLiteral("Trusted")).toBool() },
            { QStringLiteral("connected"), connected },
            { QStringLiteral("battery"), battery },
        });
    }
    std::stable_sort(devices.begin(), devices.end(), [](const QVariant &a, const QVariant &b) {
        const QVariantMap x = a.toMap(), y = b.toMap();
        auto rank = [](const QVariantMap &d) {
            return d.value(QStringLiteral("connected")).toBool() ? 0 : d.value(QStringLiteral("paired")).toBool() ? 1 : 2;
        };
        if (rank(x) != rank(y))
            return rank(x) < rank(y);
        return x.value(QStringLiteral("name")).toString().compare(y.value(QStringLiteral("name")).toString(),
                                                                  Qt::CaseInsensitive) < 0;
    });

    if (adapterPath != m_adapterPath || adapter != m_adapter) {
        const bool appeared = m_adapterPath.isEmpty() && !adapterPath.isEmpty();
        m_adapterPath = adapterPath;
        m_adapter = adapter;
        if (appeared || adapterPath.isEmpty())
            m_agentRegistered = false;
        emit adapterChanged();
    }
    if (devices != m_devices) {
        m_devices = devices;
        emit devicesChanged();
    }
}

QString BluetoothInfo::deviceName(const QString &path) const
{
    for (const QVariant &v : m_devices) {
        const QVariantMap d = v.toMap();
        if (d.value(QStringLiteral("path")).toString() == path)
            return d.value(QStringLiteral("name")).toString();
    }
    // not listed yet (e.g. pairing initiated by the device): ask BlueZ
    const QDBusMessage call = QDBusMessage::createMethodCall(Service, path, PropertiesInterface, QStringLiteral("Get"));
    QDBusMessage withArgs = call;
    withArgs << DeviceInterface << QStringLiteral("Alias");
    const QDBusMessage reply = bus().call(withArgs, QDBus::Block, 1000);
    if (reply.type() == QDBusMessage::ReplyMessage && !reply.arguments().isEmpty())
        return reply.arguments().constFirst().value<QDBusVariant>().variant().toString();
    return QStringLiteral("the device");
}

QString BluetoothInfo::friendlyError(const QString &errorName, const QString &errorMessage) const
{
    const QString e = errorName.section(QLatin1Char('.'), -1);
    if (e == QLatin1String("AuthenticationFailed") || e == QLatin1String("AuthenticationRejected"))
        return QStringLiteral("The device refused to pair, or the codes didn't match.");
    if (e == QLatin1String("AuthenticationCanceled"))
        return QStringLiteral("Pairing was cancelled.");
    if (e == QLatin1String("AuthenticationTimeout") || e == QLatin1String("NoReply"))
        return QStringLiteral("The device didn't answer in time.");
    if (e == QLatin1String("ConnectionAttemptFailed") || errorMessage.contains(QLatin1String("page-timeout"))
        || errorMessage.contains(QLatin1String("Host is down")))
        return QStringLiteral("The device couldn't be reached. Make sure it's switched on, nearby, and not "
                              "connected to another computer or phone.");
    if (e == QLatin1String("NotReady"))
        return QStringLiteral("Bluetooth is turned off.");
    if (e == QLatin1String("Blocked") || errorMessage.contains(QLatin1String("rfkill"), Qt::CaseInsensitive))
        return QStringLiteral("Bluetooth is switched off by airplane mode or a hardware switch.");
    if (e == QLatin1String("InProgress"))
        return QStringLiteral("The device is busy with another request. Try again in a moment.");
    if (e == QLatin1String("AccessDenied") || e == QLatin1String("NotAuthorized"))
        return QStringLiteral("You don't have permission to change Bluetooth settings.");
    if (e == QLatin1String("ServiceUnknown"))
        return QStringLiteral("The Bluetooth service (bluetoothd) isn't running.");
    if (e == QLatin1String("NotAvailable") || e == QLatin1String("NotSupported")
        || errorMessage.contains(QLatin1String("Protocol not available")))
        return QStringLiteral("This device has nothing this computer can connect to (no supported "
                              "Bluetooth profile).");
    return errorMessage.isEmpty() ? errorName : errorMessage;
}

void BluetoothInfo::setBusy(const QString &text)
{
    if (m_busyText == text)
        return;
    m_busyText = text;
    emit busyChanged();
}

void BluetoothInfo::finish(bool ok, const QString &message)
{
    setBusy({});
    emit operationFinished(ok, message);
    scheduleReload();
}

void BluetoothInfo::setAdapterProperty(const QString &name, const QVariant &value, const QString &failure)
{
    if (m_adapterPath.isEmpty())
        return;
    QDBusMessage call = QDBusMessage::createMethodCall(Service, m_adapterPath, PropertiesInterface, QStringLiteral("Set"));
    call << AdapterInterface << name << QVariant::fromValue(QDBusVariant(value));
    auto *watcher = new QDBusPendingCallWatcher(bus().asyncCall(call, 10000), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this, failure](QDBusPendingCallWatcher *w) {
        w->deleteLater();
        if (w->isError())
            emit operationFinished(false, failure + QLatin1Char(' ')
                                              + friendlyError(w->error().name(), w->error().message()));
        scheduleReload();
    });
}

void BluetoothInfo::setPowered(bool on)
{
    setAdapterProperty(QStringLiteral("Powered"), on,
                       on ? QStringLiteral("Couldn't turn Bluetooth on.") : QStringLiteral("Couldn't turn Bluetooth off."));
}

void BluetoothInfo::setDiscoverable(bool on)
{
    setAdapterProperty(QStringLiteral("Discoverable"), on, QStringLiteral("Couldn't change whether this computer can be found."));
}

void BluetoothInfo::setAdapterName(const QString &name)
{
    const QString trimmed = name.trimmed();
    if (!trimmed.isEmpty() && trimmed != adapterName())
        setAdapterProperty(QStringLiteral("Alias"), trimmed, QStringLiteral("Couldn't rename this computer."));
}

QDBusMessage BluetoothInfo::adapterCall(const QString &method) const
{
    return QDBusMessage::createMethodCall(Service, m_adapterPath, AdapterInterface, method);
}

void BluetoothInfo::startDiscovery()
{
    if (m_adapterPath.isEmpty() || !powered())
        return;
    auto *watcher = new QDBusPendingCallWatcher(bus().asyncCall(adapterCall(QStringLiteral("StartDiscovery"))), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this](QDBusPendingCallWatcher *w) {
        w->deleteLater();
        if (w->isError() && !w->error().name().endsWith(QLatin1String("InProgress"))) {
            emit operationFinished(false, QStringLiteral("Couldn't search for devices. ")
                                              + friendlyError(w->error().name(), w->error().message()));
            return;
        }
        m_discoveryTimer.start();
        scheduleReload();
    });
}

void BluetoothInfo::stopDiscovery()
{
    m_discoveryTimer.stop();
    if (m_adapterPath.isEmpty())
        return;
    // BlueZ also stops it by itself if we quit; errors here don't matter
    bus().asyncCall(adapterCall(QStringLiteral("StopDiscovery")));
}

void BluetoothInfo::deviceCall(const QString &path, const QString &method, const QString &busyText,
                               const QString &failure, int timeoutMs, std::function<void()> onSuccess)
{
    if (busy())
        return;
    setBusy(busyText);
    const QDBusMessage call = QDBusMessage::createMethodCall(Service, path, DeviceInterface, method);
    auto *watcher = new QDBusPendingCallWatcher(bus().asyncCall(call, timeoutMs), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this,
            [this, failure, onSuccess](QDBusPendingCallWatcher *w) {
        w->deleteLater();
        const QString error = w->isError() ? w->error().name() : QString();
        // "already done" isn't a failure
        if (!w->isError() || error.endsWith(QLatin1String("AlreadyExists"))
            || error.endsWith(QLatin1String("AlreadyConnected"))) {
            if (onSuccess)
                onSuccess();
            else
                finish(true, {});
            return;
        }
        finish(false, failure + QLatin1String("\n\n") + friendlyError(error, w->error().message()));
    });
}

void BluetoothInfo::pair(const QString &path)
{
    if (!m_agentRegistered) {
        // Our agent answers the passkey/PIN questions for pairings we start.
        QDBusMessage call = QDBusMessage::createMethodCall(Service, QStringLiteral("/org/bluez"),
                                                           QStringLiteral("org.bluez.AgentManager1"),
                                                           QStringLiteral("RegisterAgent"));
        call << QVariant::fromValue(QDBusObjectPath(AgentPath)) << QStringLiteral("KeyboardDisplay");
        const QDBusMessage reply = bus().call(call, QDBus::Block, 3000);
        m_agentRegistered = reply.type() == QDBusMessage::ReplyMessage
                            || reply.errorName().endsWith(QLatin1String("AlreadyExists"));
    }
    stopDiscovery(); // searching slows pairing down
    const QString name = deviceName(path);
    deviceCall(path, QStringLiteral("Pair"), QStringLiteral("Pairing with %1...").arg(name),
               QStringLiteral("Couldn't pair with %1.").arg(name), 90 * 1000, [this, path, name] {
        // remember it, so it may reconnect by itself later
        QDBusMessage trust = QDBusMessage::createMethodCall(Service, path, PropertiesInterface, QStringLiteral("Set"));
        trust << DeviceInterface << QStringLiteral("Trusted") << QVariant::fromValue(QDBusVariant(true));
        bus().asyncCall(trust);
        setBusy({});
        emit pairingRequest(QStringLiteral("cancel"), name, {}); // close any passkey display
        connectDevice(path);
    });
}

void BluetoothInfo::connectDevice(const QString &path)
{
    const QString name = deviceName(path);
    deviceCall(path, QStringLiteral("Connect"), QStringLiteral("Connecting to %1...").arg(name),
               QStringLiteral("Couldn't connect to %1.").arg(name), 30 * 1000);
}

void BluetoothInfo::disconnectDevice(const QString &path)
{
    const QString name = deviceName(path);
    deviceCall(path, QStringLiteral("Disconnect"), QStringLiteral("Disconnecting %1...").arg(name),
               QStringLiteral("Couldn't disconnect %1.").arg(name), 15 * 1000);
}

void BluetoothInfo::removeDevice(const QString &path)
{
    if (m_adapterPath.isEmpty() || busy())
        return;
    const QString name = deviceName(path);
    setBusy(QStringLiteral("Removing %1...").arg(name));
    QDBusMessage call = adapterCall(QStringLiteral("RemoveDevice"));
    call << QVariant::fromValue(QDBusObjectPath(path));
    auto *watcher = new QDBusPendingCallWatcher(bus().asyncCall(call, 10000), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this, name](QDBusPendingCallWatcher *w) {
        w->deleteLater();
        if (w->isError())
            finish(false, QStringLiteral("Couldn't remove %1.\n\n").arg(name)
                              + friendlyError(w->error().name(), w->error().message()));
        else
            finish(true, {});
    });
}

void BluetoothInfo::agentRequest(const QString &kind, const QDBusMessage &message, const QString &devicePath,
                                 const QString &code)
{
    // a new question replaces an unanswered one
    if (m_pendingAgentMessage.type() == QDBusMessage::MethodCallMessage)
        bus().send(m_pendingAgentMessage.createErrorReply(QStringLiteral("org.bluez.Error.Canceled"),
                                                          QStringLiteral("Superseded")));
    m_pendingAgentMessage = message;
    m_pendingAgentKind = kind;
    emit pairingRequest(kind, deviceName(devicePath), code);
}

void BluetoothInfo::agentCancelled()
{
    m_pendingAgentMessage = QDBusMessage();
    m_pendingAgentKind.clear();
    emit pairingRequest(QStringLiteral("cancel"), {}, {});
}

void BluetoothInfo::answerPairing(bool accept, const QString &value)
{
    if (m_pendingAgentMessage.type() != QDBusMessage::MethodCallMessage)
        return;
    QDBusMessage reply;
    if (!accept)
        reply = m_pendingAgentMessage.createErrorReply(QStringLiteral("org.bluez.Error.Rejected"),
                                                       QStringLiteral("Rejected by the user"));
    else if (m_pendingAgentKind == QLatin1String("pin"))
        reply = m_pendingAgentMessage.createReply(value);
    else if (m_pendingAgentKind == QLatin1String("passkey"))
        reply = m_pendingAgentMessage.createReply(QVariant::fromValue(value.toUInt()));
    else
        reply = m_pendingAgentMessage.createReply();
    bus().send(reply);
    m_pendingAgentMessage = QDBusMessage();
    m_pendingAgentKind.clear();
}
