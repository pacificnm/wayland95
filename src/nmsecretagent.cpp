#include "nmsecretagent.h"

#include <QDBusMetaType>
#include <QDBusObjectPath>
#include <QDBusPendingCallWatcher>
#include <QGuiApplication>
#include <QProcess>
#include <QTimer>
#include <QtDebug>

namespace {
const QString AgentPath = QStringLiteral("/org/freedesktop/NetworkManager/SecretAgent");
const QString NoSecrets = QStringLiteral("org.freedesktop.NetworkManager.SecretAgent.NoSecrets");
const QString UserCanceled = QStringLiteral("org.freedesktop.NetworkManager.SecretAgent.UserCanceled");
// GetSecrets flags
constexpr uint AllowInteraction = 0x1;
constexpr uint RequestNew = 0x2;
}

// ---------------------------------------------------------------------------

NetworkSecretAgentAdaptor::NetworkSecretAgentAdaptor(NetworkSecretAgent *agent)
    : QObject(agent)
    , m_agent(agent)
{
}

NMSettings NetworkSecretAgentAdaptor::GetSecrets(const NMSettings &connection, const QDBusObjectPath &, const QString &setting,
                                                 const QStringList &, uint flags)
{
    // answered once the user has typed it (or right away, with nothing)
    setDelayedReply(true);
    m_agent->request(message(), connection, setting, flags);
    return {};
}

void NetworkSecretAgentAdaptor::CancelGetSecrets(const QDBusObjectPath &path, const QString &setting)
{
    m_agent->cancelRequest(path.path(), setting);
}

// We keep no secrets of our own: saved passwords live in NetworkManager's
// profile (see savePermanently), so there's nothing to save or delete here.
void NetworkSecretAgentAdaptor::SaveSecrets(const NMSettings &, const QDBusObjectPath &) {}
void NetworkSecretAgentAdaptor::DeleteSecrets(const NMSettings &, const QDBusObjectPath &) {}

// ---------------------------------------------------------------------------

NetworkSecretAgent::NetworkSecretAgent(QObject *parent)
    : QObject(parent)
    , m_bus(QDBusConnection::systemBus())
{
    // only as the session on the screen itself (see Launcher's isSession)
    const QString platform = QGuiApplication::platformName();
    setup(qEnvironmentVariableIsSet("WAYLAND95_SESSION")
          && (platform == QLatin1String("eglfs") || platform == QLatin1String("linuxfb")));
}

NetworkSecretAgent::NetworkSecretAgent(const QDBusConnection &bus, bool registerWithNetworkManager, QObject *parent)
    : QObject(parent)
    , m_bus(bus)
{
    setup(registerWithNetworkManager);
}

void NetworkSecretAgent::setup(bool registerWithNetworkManager)
{
    qDBusRegisterMetaType<NMSettings>();
    if (!m_bus.isConnected())
        return;
    if (!m_bus.registerObject(AgentPath, new NetworkSecretAgentAdaptor(this), QDBusConnection::ExportAllSlots))
        return;
    if (!registerWithNetworkManager)
        return;
    // ask NetworkManager to use us (it forgets us when we exit)
    QDBusMessage call = QDBusMessage::createMethodCall(QStringLiteral("org.freedesktop.NetworkManager"),
                                                       QStringLiteral("/org/freedesktop/NetworkManager/AgentManager"),
                                                       QStringLiteral("org.freedesktop.NetworkManager.AgentManager"),
                                                       QStringLiteral("Register"));
    call << QStringLiteral("org.wayland95.networkmanager-agent");
    auto *watcher = new QDBusPendingCallWatcher(m_bus.asyncCall(call), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [watcher] {
        watcher->deleteLater();
        if (watcher->isError())
            qInfo().noquote() << "Network passwords: NetworkManager didn't take Wayland 95 as its agent ("
                                     + watcher->error().message() + ")";
        else
            qInfo() << "Network passwords: Wayland 95 answers NetworkManager's password requests";
    });
}

void NetworkSecretAgent::request(const QDBusMessage &call, const NMSettings &connection, const QString &setting, uint flags)
{
    const QVariantMap conn = connection.value(QStringLiteral("connection"));

    // which secret: a Wi-Fi key, or an enterprise login
    QString kind, key;
    if (setting == QLatin1String("802-11-wireless-security")) {
        const QString mgmt = connection.value(setting).value(QStringLiteral("key-mgmt")).toString();
        if (mgmt == QLatin1String("wpa-psk") || mgmt == QLatin1String("sae")) {
            kind = QStringLiteral("wifi");
            key = QStringLiteral("psk");
        } else if (mgmt == QLatin1String("none")) {
            kind = QStringLiteral("wifi");
            key = QStringLiteral("wep-key0");
        }
    } else if (setting == QLatin1String("802-1x")) {
        kind = QStringLiteral("enterprise");
        key = QStringLiteral("password");
    }

    // nothing we can ask for, or not allowed to ask (NetworkManager then
    // tries without, or gives up)
    if (kind.isEmpty() || !(flags & AllowInteraction) || m_pending) {
        m_bus.send(call.createErrorReply(NoSecrets, QStringLiteral("No secrets available")));
        return;
    }

    m_pending = true;
    m_call = call;
    m_setting = setting;
    m_secretKey = key;
    m_kind = kind;
    m_uuid = conn.value(QStringLiteral("uuid")).toString();
    m_retry = flags & RequestNew;
    // the network's name (the SSID), else the connection's
    const QByteArray ssid = connection.value(QStringLiteral("802-11-wireless")).value(QStringLiteral("ssid")).toByteArray();
    m_network = !ssid.isEmpty() ? QString::fromUtf8(ssid) : conn.value(QStringLiteral("id")).toString();
    m_user = connection.value(QStringLiteral("802-1x")).value(QStringLiteral("identity")).toString();
    m_path.clear();
    emit requestChanged();
    emit requested();
}

void NetworkSecretAgent::provide(const QString &userName, const QString &password, bool save)
{
    if (!m_pending)
        return;
    QVariantMap secrets{ { m_secretKey, password } };
    if (m_kind == QLatin1String("enterprise") && !userName.isEmpty())
        secrets.insert(QStringLiteral("identity"), userName);
    const NMSettings reply{ { m_setting, secrets } };
    m_bus.send(m_call.createReply(QVariant::fromValue(reply)));
    if (save)
        savePermanently(m_uuid, m_setting, m_secretKey, password,
                        m_kind == QLatin1String("enterprise") ? QStringLiteral("identity") : QString(), userName);
    finish();
}

void NetworkSecretAgent::cancel()
{
    if (!m_pending)
        return;
    m_bus.send(m_call.createErrorReply(UserCanceled, QStringLiteral("Canceled by the user")));
    finish();
}

void NetworkSecretAgent::cancelRequest(const QString &, const QString &setting)
{
    // NetworkManager gave up waiting (the connection was dropped, say)
    if (m_pending && setting == m_setting)
        finish();
}

void NetworkSecretAgent::finish()
{
    m_pending = false;
    m_call = QDBusMessage();
    emit requestChanged();
}

void NetworkSecretAgent::savePermanently(const QString &uuid, const QString &setting, const QString &key, const QString &value,
                                         const QString &userKey, const QString &userValue)
{
    if (uuid.isEmpty())
        return;
    // After the connection has had a moment to come up: the profile keeps the
    // password itself (owned by NetworkManager, "flags 0"), so any desktop
    // has it. The password goes in through nmcli's stdin, never on a
    // command line.
    QTimer::singleShot(4000, this, [uuid, setting, key, value, userKey, userValue] {
        auto *modify = new QProcess;
        QObject::connect(modify, &QProcess::finished, modify, [modify, uuid, setting, key, value, userKey, userValue] {
            modify->deleteLater();
            auto *edit = new QProcess;
            QObject::connect(edit, &QProcess::finished, edit, &QObject::deleteLater);
            edit->start(QStringLiteral("nmcli"), { QStringLiteral("connection"), QStringLiteral("edit"), QStringLiteral("uuid"), uuid });
            QByteArray input = "set " + setting.toUtf8() + '.' + key.toUtf8() + ' ' + value.toUtf8() + '\n';
            if (!userKey.isEmpty() && !userValue.isEmpty())
                input += "set " + setting.toUtf8() + '.' + userKey.toUtf8() + ' ' + userValue.toUtf8() + '\n';
            input += "save\nquit\n";
            edit->write(input);
            edit->closeWriteChannel();
        });
        modify->start(QStringLiteral("nmcli"), { QStringLiteral("connection"), QStringLiteral("modify"), QStringLiteral("uuid"), uuid,
                                                 setting + QLatin1Char('.') + key + QStringLiteral("-flags"), QStringLiteral("0") });
    });
}
