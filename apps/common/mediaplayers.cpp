#include "mediaplayers.h"

#include <QDBusArgument>
#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusMessage>
#include <QDBusMetaType>
#include <QDBusObjectPath>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QDBusReply>
#include <QDBusVariant>

namespace {
const QString kPrefix = QStringLiteral("org.mpris.MediaPlayer2.");
const QString kPath = QStringLiteral("/org/mpris/MediaPlayer2");
const QString kPlayer = QStringLiteral("org.mpris.MediaPlayer2.Player");
const QString kProps = QStringLiteral("org.freedesktop.DBus.Properties");

// a value as it came over the bus: a{sv} and the like may still be QDBusArguments
QVariant plain(const QVariant &v)
{
    if (v.canConvert<QDBusVariant>() && v.metaType() == QMetaType::fromType<QDBusVariant>())
        return plain(v.value<QDBusVariant>().variant());
    if (v.metaType() != QMetaType::fromType<QDBusArgument>())
        return v;
    const QDBusArgument arg = v.value<QDBusArgument>();
    switch (arg.currentType()) {
    case QDBusArgument::MapType: {
        QVariantMap out;
        arg.beginMap();
        while (!arg.atEnd()) {
            QString key;
            QDBusVariant value;
            arg.beginMapEntry();
            arg >> key >> value;
            arg.endMapEntry();
            out.insert(key, plain(value.variant()));
        }
        arg.endMap();
        return out;
    }
    case QDBusArgument::ArrayType: {
        QStringList out;
        arg.beginArray();
        while (!arg.atEnd()) {
            QString s;
            arg >> s;
            out << s;
        }
        arg.endArray();
        return out;
    }
    default:
        return arg.asVariant();
    }
}
} // namespace

MediaPlayers::MediaPlayers(QObject *parent)
    : QObject(parent)
{
    QDBusConnection bus = QDBusConnection::sessionBus();
    if (!bus.isConnected())
        return;
    bus.connect(QString(), kPath, kProps, QStringLiteral("PropertiesChanged"), this,
                SLOT(propertiesChanged(QString,QVariantMap,QStringList)));
    connect(bus.interface(), &QDBusConnectionInterface::serviceOwnerChanged, this, &MediaPlayers::ownerChanged);
    const QStringList names = bus.interface()->registeredServiceNames().value();
    for (const QString &n : names) {
        if (n.startsWith(kPrefix))
            add(n);
    }
}

void MediaPlayers::ownerChanged(const QString &name, const QString &oldOwner, const QString &newOwner)
{
    if (!name.startsWith(kPrefix))
        return;
    if (!oldOwner.isEmpty())
        remove(name);
    if (!newOwner.isEmpty())
        add(name);
}

void MediaPlayers::add(const QString &name)
{
    const QDBusReply<QString> owner = QDBusConnection::sessionBus().interface()->serviceOwner(name);
    if (owner.isValid())
        m_owners.insert(owner.value(), name);
    m_props.insert(name, {});
    if (!m_order.contains(name))
        m_order.append(name);
    read(name);
}

void MediaPlayers::remove(const QString &name)
{
    m_props.remove(name);
    m_order.removeAll(name);
    for (auto it = m_owners.begin(); it != m_owners.end();) {
        if (it.value() == name)
            it = m_owners.erase(it);
        else
            ++it;
    }
    choose();
}

void MediaPlayers::read(const QString &name)
{
    for (const QString &iface : { QStringLiteral("org.mpris.MediaPlayer2"), kPlayer }) {
        QDBusMessage get = QDBusMessage::createMethodCall(name, kPath, kProps, QStringLiteral("GetAll"));
        get << iface;
        auto *watcher = new QDBusPendingCallWatcher(QDBusConnection::sessionBus().asyncCall(get), this);
        connect(watcher, &QDBusPendingCallWatcher::finished, this, [this, name, iface](QDBusPendingCallWatcher *w) {
            w->deleteLater();
            QDBusPendingReply<QVariantMap> reply = *w;
            if (reply.isError() || !m_props.contains(name))
                return;
            QVariantMap &props = m_props[name];
            const QVariantMap got = reply.value();
            for (auto it = got.begin(); it != got.end(); ++it)
                props.insert(it.key(), plain(it.value()));
            if (iface == kPlayer && props.value(QStringLiteral("PlaybackStatus")).toString() == QLatin1String("Playing")) {
                m_order.removeAll(name);
                m_order.prepend(name);
            }
            choose();
        });
    }
}

void MediaPlayers::propertiesChanged(const QString &interface, const QVariantMap &changedProps, const QStringList &)
{
    if (!message().service().isEmpty() && interface.startsWith(QLatin1String("org.mpris.MediaPlayer2"))) {
        const QString name = m_owners.value(message().service());
        if (name.isEmpty() || !m_props.contains(name))
            return;
        QVariantMap &props = m_props[name];
        for (auto it = changedProps.begin(); it != changedProps.end(); ++it)
            props.insert(it.key(), plain(it.value()));
        // (the one that starts playing is the one the keys go to)
        if (changedProps.value(QStringLiteral("PlaybackStatus")).toString() == QLatin1String("Playing")) {
            m_order.removeAll(name);
            m_order.prepend(name);
        }
        choose();
    }
}

void MediaPlayers::choose()
{
    QString pick;
    for (const QString &n : std::as_const(m_order)) {
        if (m_props.value(n).value(QStringLiteral("PlaybackStatus")).toString() == QLatin1String("Playing")) {
            pick = n;
            break;
        }
    }
    if (pick.isEmpty() && !m_order.isEmpty())
        pick = m_order.first();
    m_current = pick;
    emit changed();
}

QVariant MediaPlayers::value(const QString &key) const
{
    return m_props.value(m_current).value(key);
}

QVariantMap MediaPlayers::metadata() const
{
    return value(QStringLiteral("Metadata")).toMap();
}

QString MediaPlayers::title() const { return metadata().value(QStringLiteral("xesam:title")).toString(); }
QString MediaPlayers::album() const { return metadata().value(QStringLiteral("xesam:album")).toString(); }
QString MediaPlayers::artUrl() const { return metadata().value(QStringLiteral("mpris:artUrl")).toString(); }
QString MediaPlayers::artist() const
{
    const QVariant a = metadata().value(QStringLiteral("xesam:artist"));
    return a.metaType() == QMetaType::fromType<QStringList>() ? a.toStringList().join(QStringLiteral(", ")) : a.toString();
}

void MediaPlayers::call(const QString &method)
{
    if (m_current.isEmpty())
        return;
    QDBusConnection::sessionBus().asyncCall(QDBusMessage::createMethodCall(m_current, kPath, kPlayer, method));
}

void MediaPlayers::raise()
{
    if (m_current.isEmpty())
        return;
    QDBusConnection::sessionBus().asyncCall(QDBusMessage::createMethodCall(m_current, kPath,
                                                                           QStringLiteral("org.mpris.MediaPlayer2"), QStringLiteral("Raise")));
}
