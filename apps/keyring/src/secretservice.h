#pragma once

#include "crypto.h"
#include "passwordlist.h"

#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusObjectPath>
#include <QDBusVirtualObject>
#include <QMap>

#include <functional>
#include <memory>
#include <optional>

class QDBusServiceWatcher;

// the (oayays) Secret struct
struct SecretStruct {
    QDBusObjectPath session;
    QByteArray parameters;
    QByteArray value;
    QString contentType;
};
Q_DECLARE_METATYPE(SecretStruct)
using StringMap = QMap<QString, QString>;
using SecretMap = QMap<QDBusObjectPath, SecretStruct>;
Q_DECLARE_METATYPE(SecretMap)

// The freedesktop Secret Service (org.freedesktop.secrets, spec 0.2): what
// libsecret, and through it Chromium, Electron programs (Claude, VS Code),
// Email and the Web Browser, keep their passwords in. Everything under
// /org/freedesktop/secrets is answered here:
//   /org/freedesktop/secrets                   the service
//   .../collection/NAME, .../aliases/ALIAS     collections ("login" is the
//                                              password list's; "session"
//                                              is kept in memory only)
//   .../collection/NAME/N                      items
//   .../session/N, .../prompt/N                sessions and prompts
//
// Plus org.wayland95.Keyring on /org/freedesktop/secrets, for the login
// (pam_wayland95_keyring): LoginPassword(s), ChangePassword(s, s) -> b,
// Lock(), and the State property.
//
// A prompt (unlocking the list, or making it when there's none and no login
// password came) is shown by the desktop: win95shell's
// org.wayland95.KeyringPrompter.
class SecretService : public QDBusVirtualObject
{
    Q_OBJECT

public:
    SecretService(PasswordList *list, const QDBusConnection &bus, QObject *parent = nullptr);
    ~SecretService() override;

    bool handleMessage(const QDBusMessage &message, const QDBusConnection &connection) override;
    QString introspect(const QString &path) const override;

    // the login's password: unlocks the list (or will make it)
    void loginPassword(const QByteArray &password);

private:
    struct Target {
        enum Kind { None, Service, Collection, Item, Session, Prompt, Node } kind = None;
        QString collection; // its name
        quint32 item = 0;
        QString id; // session's or prompt's
    };
    struct Session {
        QString owner;
        std::shared_ptr<Crypto::DhSession> dh; // none: "plain"
    };
    enum class Need { Unlock, Create };
    struct PendingPrompt {
        QString owner;
        Need need = Need::Unlock;
        // after the list is unlocked (or made): the prompt's result,
        // nothing if it couldn't be done after all (then it's dismissed)
        std::function<std::optional<QVariant>()> finish;
        bool started = false;
        // what Completed carries when dismissed: an empty one of the same
        // type (libsecret insists)
        QVariant dismissedResult;
    };

    Target resolve(const QString &path) const;

    // the calls, by interface
    void serviceCall(const QDBusMessage &m);
    void collectionCall(const QDBusMessage &m, const Target &t);
    void itemCall(const QDBusMessage &m, const Target &t);
    void sessionCall(const QDBusMessage &m, const Target &t);
    void promptCall(const QDBusMessage &m, const Target &t);
    void keyringCall(const QDBusMessage &m);
    void propertiesCall(const QDBusMessage &m, const Target &t);
    void introspectCall(const QDBusMessage &m, const Target &t);

    void reply(const QDBusMessage &m, const QVariantList &args = {});
    void error(const QDBusMessage &m, const QString &name, const QString &text);

    // the model
    KeyringCollection *collection(const QString &name);
    const KeyringCollection *collection(const QString &name) const;
    bool isLocked(const QString &collectionName) const;
    QStringList collectionNames() const;
    QString aliasTarget(const QString &alias) const;
    static QString collectionPath(const QString &name);
    static QString itemPath(const QString &collectionName, quint32 id);
    QList<QDBusObjectPath> itemPaths(const KeyringCollection &c) const;
    bool matches(const KeyringItem &item, const StringMap &query, bool locked) const;
    void changed(const QString &collectionName); // saves (unless "session")

    QVariantMap properties(const Target &t, const QString &iface) const;
    void emitSignal(const QString &path, const QString &iface, const QString &name, const QVariantList &args);
    void emitPropertiesChanged(const QString &path, const QString &iface, const QVariantMap &changed);
    void emitLockedChanged();

    // secrets in and out of a session
    std::optional<SecretStruct> encode(const QString &sessionPath, const QString &caller, const KeyringItem &item);
    std::optional<QByteArray> decode(const SecretStruct &secret, const QString &caller);

    // creating an item (CreateItem, now or after a prompt): its path
    std::optional<QString> createItem(const QString &collectionName, const QVariantMap &props, const QByteArray &secret,
                                      const QString &contentType, bool replace);
    // a new collection (CreateCollection): its name
    std::optional<QString> createCollection(const QString &label, const QString &alias);

    // prompts
    QDBusObjectPath newPrompt(const QString &owner, Need need, std::function<std::optional<QVariant>()> finish,
                              const QVariant &dismissedResult = QVariant::fromValue(QDBusObjectPath(QStringLiteral("/"))));
    void nextPrompt();
    void askShell(bool retry);
    void completePrompt(const QString &id, bool dismissed);
    void unlocked(const QByteArray &password);
    void created(const QByteArray &password);

    void ownerGone(const QString &owner);

    PasswordList *m_list;
    QDBusConnection m_bus;
    KeyringCollection m_session; // the "session" collection
    QMap<QString, Session> m_sessions;
    QMap<QString, PendingPrompt> m_prompts;
    QStringList m_promptQueue;
    QString m_asking; // the prompt the desktop is showing
    int m_askGeneration = 0;
    int m_askTries = 0;
    quint64 m_nextSession = 1;
    quint64 m_nextPrompt = 1;
    QByteArray m_loginPassword;
    QDBusServiceWatcher *m_watcher;
};
