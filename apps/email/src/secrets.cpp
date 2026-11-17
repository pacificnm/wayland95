#include "secrets.h"

#include <QDBusArgument>
#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusMessage>
#include <QDBusMetaType>
#include <QDBusPendingCallWatcher>
#include <QDBusVariant>

// the (oayays) Secret struct
namespace SecretsDetail {
struct Secret {
    QDBusObjectPath session;
    QByteArray parameters;
    QByteArray value;
    QString contentType;
};

QDBusArgument &operator<<(QDBusArgument &a, const Secret &s)
{
    a.beginStructure();
    a << s.session << s.parameters << s.value << s.contentType;
    a.endStructure();
    return a;
}
const QDBusArgument &operator>>(const QDBusArgument &a, Secret &s)
{
    a.beginStructure();
    a >> s.session >> s.parameters >> s.value >> s.contentType;
    a.endStructure();
    return a;
}
} // namespace SecretsDetail
Q_DECLARE_METATYPE(SecretsDetail::Secret)

namespace {

using SecretsDetail::Secret;
const QString Service = QStringLiteral("org.freedesktop.secrets");
const QString ServicePath = QStringLiteral("/org/freedesktop/secrets");
const QString ServiceIface = QStringLiteral("org.freedesktop.Secret.Service");
const QString DefaultCollection = QStringLiteral("/org/freedesktop/secrets/aliases/default");

using StringMap = QMap<QString, QString>;

QDBusConnection bus() { return QDBusConnection::sessionBus(); }

StringMap attributes(const QString &account)
{
    return {{QStringLiteral("application"), QStringLiteral("wayland95-email")}, {QStringLiteral("account"), account}};
}

void registerTypes()
{
    static bool done = false;
    if (done)
        return;
    done = true;
    qDBusRegisterMetaType<Secret>();
    qDBusRegisterMetaType<StringMap>();
}

void call(const QString &path, const QString &iface, const QString &method, const QVariantList &args,
          const std::function<void(const QDBusMessage &reply)> &done)
{
    QDBusMessage m = QDBusMessage::createMethodCall(Service, path, iface, method);
    m.setArguments(args);
    auto *w = new QDBusPendingCallWatcher(bus().asyncCall(m, 120000));
    QObject::connect(w, &QDBusPendingCallWatcher::finished, w, [done](QDBusPendingCallWatcher *w) {
        w->deleteLater();
        done(w->reply());
    });
}

// a prompt (unlocking the keyring): shown, then Completed(dismissed, result)
class PromptWaiter : public QObject
{
    Q_OBJECT
public:
    std::function<void(bool dismissed, const QVariant &result)> done;
public slots:
    void completed(bool dismissed, const QDBusVariant &result)
    {
        if (done)
            done(dismissed, result.variant());
        deleteLater();
    }
};

void prompt(const QDBusObjectPath &path, const std::function<void(bool dismissed, const QVariant &result)> &done)
{
    if (path.path() == QLatin1String("/")) {
        done(false, QVariant());
        return;
    }
    auto *w = new PromptWaiter;
    w->done = done;
    bus().connect(Service, path.path(), QStringLiteral("org.freedesktop.Secret.Prompt"), QStringLiteral("Completed"), w,
                  SLOT(completed(bool, QDBusVariant)));
    call(path.path(), QStringLiteral("org.freedesktop.Secret.Prompt"), QStringLiteral("Prompt"), {QString()},
         [w](const QDBusMessage &reply) {
        if (reply.type() == QDBusMessage::ErrorMessage) {
            if (w->done)
                w->done(true, QVariant());
            w->deleteLater();
        }
    });
}

void openSession(const std::function<void(bool ok, const QDBusObjectPath &session)> &done)
{
    registerTypes();
    call(ServicePath, ServiceIface, QStringLiteral("OpenSession"),
         {QStringLiteral("plain"), QVariant::fromValue(QDBusVariant(QString()))}, [done](const QDBusMessage &reply) {
        if (reply.type() != QDBusMessage::ReplyMessage || reply.arguments().size() < 2) {
            done(false, QDBusObjectPath());
            return;
        }
        done(true, reply.arguments().at(1).value<QDBusObjectPath>());
    });
}

QList<QDBusObjectPath> paths(const QVariant &v)
{
    QList<QDBusObjectPath> out;
    if (v.canConvert<QDBusArgument>()) {
        const QDBusArgument a = v.value<QDBusArgument>();
        a >> out;
    } else {
        out = qdbus_cast<QList<QDBusObjectPath>>(v);
    }
    return out;
}

// the items for an account, unlocked (the keyring asked for if need be)
void find(const QString &account, const std::function<void(const QList<QDBusObjectPath> &items)> &done)
{
    call(ServicePath, ServiceIface, QStringLiteral("SearchItems"), {QVariant::fromValue(attributes(account))},
         [done](const QDBusMessage &reply) {
        if (reply.type() != QDBusMessage::ReplyMessage || reply.arguments().size() < 2) {
            done({});
            return;
        }
        const QList<QDBusObjectPath> unlocked = paths(reply.arguments().at(0));
        const QList<QDBusObjectPath> locked = paths(reply.arguments().at(1));
        if (locked.isEmpty()) {
            done(unlocked);
            return;
        }
        call(ServicePath, ServiceIface, QStringLiteral("Unlock"), {QVariant::fromValue(locked)},
             [done, unlocked, locked](const QDBusMessage &r) {
            if (r.type() != QDBusMessage::ReplyMessage || r.arguments().size() < 2) {
                done(unlocked);
                return;
            }
            QList<QDBusObjectPath> now = unlocked + paths(r.arguments().at(0));
            prompt(r.arguments().at(1).value<QDBusObjectPath>(), [done, now, locked](bool dismissed, const QVariant &) {
                done(dismissed ? now : now + locked);
            });
        });
    });
}

} // namespace

namespace Secrets {

bool available()
{
    QDBusConnectionInterface *i = bus().interface();
    return i && (i->isServiceRegistered(Service) || i->activatableServiceNames().value().contains(Service));
}

void store(const QString &account, const QString &label, const QString &password,
           const std::function<void(bool ok, const QString &error)> &done)
{
    openSession([=](bool ok, const QDBusObjectPath &session) {
        if (!ok) {
            done(false, QStringLiteral("The keyring isn't there, so the password can't be kept."));
            return;
        }
        QVariantMap props;
        props.insert(QStringLiteral("org.freedesktop.Secret.Item.Label"), label);
        props.insert(QStringLiteral("org.freedesktop.Secret.Item.Attributes"), QVariant::fromValue(attributes(account)));
        Secret s{session, QByteArray(), password.toUtf8(), QStringLiteral("text/plain; charset=utf8")};
        call(DefaultCollection, QStringLiteral("org.freedesktop.Secret.Collection"), QStringLiteral("CreateItem"),
             {QVariant::fromValue(props), QVariant::fromValue(s), true}, [=](const QDBusMessage &reply) {
            if (reply.type() != QDBusMessage::ReplyMessage || reply.arguments().size() < 2) {
                done(false, QStringLiteral("The keyring didn't keep the password: %1").arg(reply.errorMessage()));
                return;
            }
            const QDBusObjectPath item = reply.arguments().at(0).value<QDBusObjectPath>();
            if (item.path() != QLatin1String("/")) {
                done(true, QString());
                return;
            }
            // the keyring was locked: unlocked (its prompt), then made
            prompt(reply.arguments().at(1).value<QDBusObjectPath>(), [=](bool dismissed, const QVariant &) {
                done(!dismissed, dismissed ? QStringLiteral("The keyring stayed locked, so the password wasn't kept.") : QString());
            });
        });
    });
}

void lookup(const QString &account, const std::function<void(bool found, const QString &password)> &done)
{
    if (!available()) {
        done(false, QString());
        return;
    }
    openSession([=](bool ok, const QDBusObjectPath &session) {
        if (!ok) {
            done(false, QString());
            return;
        }
        find(account, [=](const QList<QDBusObjectPath> &items) {
            if (items.isEmpty()) {
                done(false, QString());
                return;
            }
            call(ServicePath, ServiceIface, QStringLiteral("GetSecrets"), {QVariant::fromValue(items), QVariant::fromValue(session)},
                 [=](const QDBusMessage &reply) {
                if (reply.type() != QDBusMessage::ReplyMessage || reply.arguments().isEmpty()) {
                    done(false, QString());
                    return;
                }
                // a{o(oayays)}: the first one's value
                const QDBusArgument a = reply.arguments().at(0).value<QDBusArgument>();
                QString password;
                bool found = false;
                a.beginMap();
                while (!a.atEnd()) {
                    QDBusObjectPath p;
                    Secret s;
                    a.beginMapEntry();
                    a >> p >> s;
                    a.endMapEntry();
                    if (!found) {
                        password = QString::fromUtf8(s.value);
                        found = true;
                    }
                }
                a.endMap();
                done(found, password);
            });
        });
    });
}

void remove(const QString &account)
{
    if (!available())
        return;
    find(account, [](const QList<QDBusObjectPath> &items) {
        for (const QDBusObjectPath &p : items)
            call(p.path(), QStringLiteral("org.freedesktop.Secret.Item"), QStringLiteral("Delete"), {}, [](const QDBusMessage &) {});
    });
}

} // namespace Secrets

#include "secrets.moc"
