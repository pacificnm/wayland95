#pragma once

#include <QDBusArgument>
#include <QDBusContext>
#include <QDBusMessage>
#include <QObject>
#include <QPointer>
#include <QProcess>
#include <QVariantMap>
#include <QtQmlIntegration/qqmlintegration.h>

// polkit's (sa{sv}) identities and subjects: a kind ("unix-user",
// "unix-session"...) and its details
struct PolkitIdentity
{
    QString kind;
    QVariantMap details;
};
Q_DECLARE_METATYPE(PolkitIdentity)
QDBusArgument &operator<<(QDBusArgument &arg, const PolkitIdentity &id);
const QDBusArgument &operator>>(const QDBusArgument &arg, PolkitIdentity &id);

class PolkitAgent;

// org.freedesktop.PolicyKit1.AuthenticationAgent, which polkitd calls
class PolkitAgentAdaptor : public QObject, protected QDBusContext
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.freedesktop.PolicyKit1.AuthenticationAgent")

public:
    explicit PolkitAgentAdaptor(PolkitAgent *agent);

public slots:
    void BeginAuthentication(const QString &actionId, const QString &message, const QString &iconName,
                             const QMap<QString, QString> &details, const QString &cookie,
                             const QList<PolkitIdentity> &identities);
    void CancelAuthentication(const QString &cookie);

private:
    PolkitAgent *m_agent;
};

// The desktop's polkit authentication agent: when a program asks to do
// something that needs a password (installing software, changing system
// settings), polkit asks us, and we show the Enter Password dialog.
// The password goes to polkit's own setuid helper, which checks it and
// tells polkit the answer; we never see whether it was right except
// through the helper.
//
// Only one agent serves a login session: nested inside another desktop,
// that desktop's agent is already there and keeps doing the job.
//
// Exposed to QML as the singleton `PolkitAgent`.
class PolkitAgent : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

    // we're the session's agent
    Q_PROPERTY(bool registered READ registered NOTIFY registeredChanged)
    // a request is waiting for a password
    Q_PROPERTY(bool pending READ pending NOTIFY requestChanged)
    Q_PROPERTY(QString message READ message NOTIFY requestChanged)
    Q_PROPERTY(QString actionId READ actionId NOTIFY requestChanged)
    // who may answer: [{ name, uid }]; usually just you, or the admins
    Q_PROPERTY(QVariantList users READ users NOTIFY requestChanged)
    Q_PROPERTY(int defaultUser READ defaultUser NOTIFY requestChanged)
    // checking the password right now
    Q_PROPERTY(bool checking READ checking NOTIFY stateChanged)
    // why the last try failed ("" if it didn't)
    Q_PROPERTY(QString error READ error NOTIFY stateChanged)

public:
    explicit PolkitAgent(QObject *parent = nullptr);

    bool registered() const { return m_registered; }
    bool pending() const { return !m_requests.isEmpty(); }
    QString message() const;
    QString actionId() const;
    QVariantList users() const;
    int defaultUser() const;
    bool checking() const { return m_helper != nullptr; }
    QString error() const { return m_error; }

    Q_INVOKABLE void authenticate(int userIndex, const QString &password);
    Q_INVOKABLE void cancel();

    // from the adaptor
    void begin(const QDBusMessage &call, const QString &actionId, const QString &message,
               const QString &cookie, const QList<PolkitIdentity> &identities);
    void cancelCookie(const QString &cookie);

signals:
    void registeredChanged();
    void requestChanged();
    void stateChanged();
    // the dialog should appear (a new request is in front)
    void requested();

private:
    struct Request {
        QDBusMessage call;
        QString actionId, message, cookie;
        QVariantList users; // [{ name, uid }]
    };
    void registerAgent();
    void finish(bool cancelled);
    void stopHelper();

    bool m_registered = false;
    QList<Request> m_requests;
    QPointer<QProcess> m_helper;
    QString m_password;      // for the helper's prompt; cleared right after
    QString m_helperMessages;
    QString m_error;
};
