#pragma once

#include <QDBusConnection>
#include <QDBusContext>
#include <QDBusMessage>
#include <QDBusObjectPath>
#include <QObject>
#include <QVariantMap>
#include <QtQmlIntegration/qqmlintegration.h>

// NetworkManager's a{sa{sv}}: setting name -> its properties
using NMSettings = QMap<QString, QVariantMap>;
Q_DECLARE_METATYPE(NMSettings)

class NetworkSecretAgent;

// org.freedesktop.NetworkManager.SecretAgent, which NetworkManager calls
// when a connection needs a password it doesn't have
class NetworkSecretAgentAdaptor : public QObject, protected QDBusContext
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.freedesktop.NetworkManager.SecretAgent")

public:
    explicit NetworkSecretAgentAdaptor(NetworkSecretAgent *agent);

public slots:
    NMSettings GetSecrets(const NMSettings &connection, const QDBusObjectPath &path, const QString &setting,
                          const QStringList &hints, uint flags);
    void CancelGetSecrets(const QDBusObjectPath &path, const QString &setting);
    void SaveSecrets(const NMSettings &connection, const QDBusObjectPath &path);
    void DeleteSecrets(const NMSettings &connection, const QDBusObjectPath &path);

private:
    NetworkSecretAgent *m_agent;
};

// The desktop's NetworkManager secret agent: when a network needs a
// password NetworkManager doesn't have (a new network it's joining by
// itself, a changed Wi-Fi password, an enterprise login, or one another
// desktop kept in its own keyring), it asks here, and Wayland 95 shows
// Enter Network Password. With "Save this password" the password is then
// kept by NetworkManager itself, so every desktop has it from then on.
//
// Only as the session (WAYLAND95_SESSION): nested inside another desktop,
// that desktop's agent answers.
//
// Exposed to QML as the singleton `NetworkSecrets`.
class NetworkSecretAgent : public QObject
{
    Q_OBJECT
    QML_NAMED_ELEMENT(NetworkSecrets)
    QML_SINGLETON

    Q_PROPERTY(bool pending READ pending NOTIFY requestChanged)
    // "wifi" (a key), "enterprise" (user name and password)
    Q_PROPERTY(QString kind READ kind NOTIFY requestChanged)
    // the network's name, and the connection's
    Q_PROPERTY(QString network READ network NOTIFY requestChanged)
    Q_PROPERTY(QString userName READ userName NOTIFY requestChanged)
    // asked again: the last password didn't work
    Q_PROPERTY(bool retry READ retry NOTIFY requestChanged)

public:
    explicit NetworkSecretAgent(QObject *parent = nullptr);
    // for tests: answer on `bus` without registering with NetworkManager
    NetworkSecretAgent(const QDBusConnection &bus, bool registerWithNetworkManager, QObject *parent = nullptr);

    bool pending() const { return m_pending; }
    QString kind() const { return m_kind; }
    QString network() const { return m_network; }
    QString userName() const { return m_user; }
    bool retry() const { return m_retry; }

    Q_INVOKABLE void provide(const QString &userName, const QString &password, bool save);
    Q_INVOKABLE void cancel();

    // from the adaptor
    void request(const QDBusMessage &call, const NMSettings &connection, const QString &setting, uint flags);
    void cancelRequest(const QString &path, const QString &setting);

signals:
    void requestChanged();
    // the dialog should appear
    void requested();

private:
    void setup(bool registerWithNetworkManager);
    void finish();
    void savePermanently(const QString &uuid, const QString &setting, const QString &key, const QString &value,
                         const QString &userKey, const QString &userValue);

    QDBusConnection m_bus;
    bool m_pending = false;
    QDBusMessage m_call;
    QString m_path, m_setting, m_uuid, m_secretKey, m_kind, m_network, m_user;
    bool m_retry = false;
};
