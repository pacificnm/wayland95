#include "secretservice.h"

#include <QDBusArgument>
#include <QDBusConnectionInterface>
#include <QDBusMetaType>
#include <QDBusPendingCallWatcher>
#include <QDBusServiceWatcher>
#include <QDBusVariant>
#include <QDateTime>
#include <QRegularExpression>
#include <QTimer>
#include <QtDebug>

QDBusArgument &operator<<(QDBusArgument &a, const SecretStruct &s)
{
    a.beginStructure();
    a << s.session << s.parameters << s.value << s.contentType;
    a.endStructure();
    return a;
}
const QDBusArgument &operator>>(const QDBusArgument &a, SecretStruct &s)
{
    a.beginStructure();
    a >> s.session >> s.parameters >> s.value >> s.contentType;
    a.endStructure();
    return a;
}

namespace {
const QString Base = QStringLiteral("/org/freedesktop/secrets");
const QString ServiceIface = QStringLiteral("org.freedesktop.Secret.Service");
const QString CollectionIface = QStringLiteral("org.freedesktop.Secret.Collection");
const QString ItemIface = QStringLiteral("org.freedesktop.Secret.Item");
const QString SessionIface = QStringLiteral("org.freedesktop.Secret.Session");
const QString PromptIface = QStringLiteral("org.freedesktop.Secret.Prompt");
const QString KeyringIface = QStringLiteral("org.wayland95.Keyring");
const QString PropertiesIface = QStringLiteral("org.freedesktop.DBus.Properties");
const QString IntrospectIface = QStringLiteral("org.freedesktop.DBus.Introspectable");

const QString ErrNoSuchObject = QStringLiteral("org.freedesktop.Secret.Error.NoSuchObject");
const QString ErrNoSession = QStringLiteral("org.freedesktop.Secret.Error.NoSession");
const QString ErrIsLocked = QStringLiteral("org.freedesktop.Secret.Error.IsLocked");
const QString ErrNotSupported = QStringLiteral("org.freedesktop.DBus.Error.NotSupported");
const QString ErrInvalidArgs = QStringLiteral("org.freedesktop.DBus.Error.InvalidArgs");
const QString ErrUnknownMethod = QStringLiteral("org.freedesktop.DBus.Error.UnknownMethod");
const QString ErrUnknownObject = QStringLiteral("org.freedesktop.DBus.Error.UnknownObject");
const QString ErrAccessDenied = QStringLiteral("org.freedesktop.DBus.Error.AccessDenied");

const QString PrompterService = QStringLiteral("org.wayland95.KeyringPrompter");
const QString PrompterPath = QStringLiteral("/org/wayland95/KeyringPrompter");
const QString PrompterIface = QStringLiteral("org.wayland95.KeyringPrompter");

const QString SessionCollection = QStringLiteral("session");
const QString PropLabel = QStringLiteral("org.freedesktop.Secret.Item.Label");
const QString PropAttributes = QStringLiteral("org.freedesktop.Secret.Item.Attributes");
const QString PropType = QStringLiteral("org.freedesktop.Secret.Item.Type");
const QString PropCollectionLabel = QStringLiteral("org.freedesktop.Secret.Collection.Label");

const QDBusObjectPath NoPrompt(QStringLiteral("/"));

const char ServiceXml[] = R"XML(<interface name="org.freedesktop.Secret.Service">
  <method name="OpenSession"><arg name="algorithm" type="s" direction="in"/><arg name="input" type="v" direction="in"/><arg name="output" type="v" direction="out"/><arg name="result" type="o" direction="out"/></method>
  <method name="CreateCollection"><arg name="properties" type="a{sv}" direction="in"/><arg name="alias" type="s" direction="in"/><arg name="collection" type="o" direction="out"/><arg name="prompt" type="o" direction="out"/></method>
  <method name="SearchItems"><arg name="attributes" type="a{ss}" direction="in"/><arg name="unlocked" type="ao" direction="out"/><arg name="locked" type="ao" direction="out"/></method>
  <method name="Unlock"><arg name="objects" type="ao" direction="in"/><arg name="unlocked" type="ao" direction="out"/><arg name="prompt" type="o" direction="out"/></method>
  <method name="Lock"><arg name="objects" type="ao" direction="in"/><arg name="locked" type="ao" direction="out"/><arg name="Prompt" type="o" direction="out"/></method>
  <method name="GetSecrets"><arg name="items" type="ao" direction="in"/><arg name="session" type="o" direction="in"/><arg name="secrets" type="a{o(oayays)}" direction="out"/></method>
  <method name="ReadAlias"><arg name="name" type="s" direction="in"/><arg name="collection" type="o" direction="out"/></method>
  <method name="SetAlias"><arg name="name" type="s" direction="in"/><arg name="collection" type="o" direction="in"/></method>
  <signal name="CollectionCreated"><arg name="collection" type="o"/></signal>
  <signal name="CollectionDeleted"><arg name="collection" type="o"/></signal>
  <signal name="CollectionChanged"><arg name="collection" type="o"/></signal>
  <property name="Collections" type="ao" access="read"/>
</interface>
<interface name="org.wayland95.Keyring">
  <method name="LoginPassword"><arg name="password" type="s" direction="in"/></method>
  <method name="ChangePassword"><arg name="old" type="s" direction="in"/><arg name="new" type="s" direction="in"/><arg name="ok" type="b" direction="out"/></method>
  <method name="Lock"/>
  <property name="State" type="s" access="read"/>
</interface>
)XML";

const char CollectionXml[] = R"XML(<interface name="org.freedesktop.Secret.Collection">
  <method name="Delete"><arg name="prompt" type="o" direction="out"/></method>
  <method name="SearchItems"><arg name="attributes" type="a{ss}" direction="in"/><arg name="results" type="ao" direction="out"/></method>
  <method name="CreateItem"><arg name="properties" type="a{sv}" direction="in"/><arg name="secret" type="(oayays)" direction="in"/><arg name="replace" type="b" direction="in"/><arg name="item" type="o" direction="out"/><arg name="prompt" type="o" direction="out"/></method>
  <signal name="ItemCreated"><arg name="item" type="o"/></signal>
  <signal name="ItemDeleted"><arg name="item" type="o"/></signal>
  <signal name="ItemChanged"><arg name="item" type="o"/></signal>
  <property name="Items" type="ao" access="read"/>
  <property name="Label" type="s" access="readwrite"/>
  <property name="Locked" type="b" access="read"/>
  <property name="Created" type="t" access="read"/>
  <property name="Modified" type="t" access="read"/>
</interface>
)XML";

const char ItemXml[] = R"XML(<interface name="org.freedesktop.Secret.Item">
  <method name="Delete"><arg name="Prompt" type="o" direction="out"/></method>
  <method name="GetSecret"><arg name="session" type="o" direction="in"/><arg name="secret" type="(oayays)" direction="out"/></method>
  <method name="SetSecret"><arg name="secret" type="(oayays)" direction="in"/></method>
  <property name="Locked" type="b" access="read"/>
  <property name="Attributes" type="a{ss}" access="readwrite"/>
  <property name="Label" type="s" access="readwrite"/>
  <property name="Type" type="s" access="readwrite"/>
  <property name="Created" type="t" access="read"/>
  <property name="Modified" type="t" access="read"/>
</interface>
)XML";

const char SessionXml[] = R"XML(<interface name="org.freedesktop.Secret.Session">
  <method name="Close"/>
</interface>
)XML";

const char PromptXml[] = R"XML(<interface name="org.freedesktop.Secret.Prompt">
  <method name="Prompt"><arg name="window-id" type="s" direction="in"/></method>
  <method name="Dismiss"/>
  <signal name="Completed"><arg name="dismissed" type="b"/><arg name="result" type="v"/></signal>
</interface>
)XML";

const char StandardXml[] = R"XML(<interface name="org.freedesktop.DBus.Introspectable">
  <method name="Introspect"><arg name="xml_data" type="s" direction="out"/></method>
</interface>
<interface name="org.freedesktop.DBus.Properties">
  <method name="Get"><arg name="interface_name" type="s" direction="in"/><arg name="property_name" type="s" direction="in"/><arg name="value" type="v" direction="out"/></method>
  <method name="GetAll"><arg name="interface_name" type="s" direction="in"/><arg name="properties" type="a{sv}" direction="out"/></method>
  <method name="Set"><arg name="interface_name" type="s" direction="in"/><arg name="property_name" type="s" direction="in"/><arg name="value" type="v" direction="in"/></method>
  <signal name="PropertiesChanged"><arg name="interface_name" type="s"/><arg name="changed_properties" type="a{sv}"/><arg name="invalidated_properties" type="as"/></signal>
</interface>
)XML";

template<typename T>
T fromDBus(const QVariant &v)
{
    if (v.canConvert<QDBusArgument>())
        return qdbus_cast<T>(v.value<QDBusArgument>());
    return v.value<T>();
}

QVariant unwrap(const QVariant &v)
{
    return v.canConvert<QDBusVariant>() ? v.value<QDBusVariant>().variant() : v;
}

qint64 now() { return QDateTime::currentSecsSinceEpoch(); }
} // namespace

SecretService::SecretService(PasswordList *list, const QDBusConnection &bus, QObject *parent)
    : QDBusVirtualObject(parent)
    , m_list(list)
    , m_bus(bus)
    , m_watcher(new QDBusServiceWatcher(this))
{
    qDBusRegisterMetaType<SecretStruct>();
    qDBusRegisterMetaType<StringMap>();
    qDBusRegisterMetaType<QList<QDBusObjectPath>>();
    qDBusRegisterMetaType<SecretMap>();

    m_session.name = SessionCollection;
    m_session.label = QStringLiteral("Session");
    m_session.created = m_session.modified = now();

    m_watcher->setConnection(m_bus);
    m_watcher->setWatchMode(QDBusServiceWatcher::WatchForUnregistration);
    connect(m_watcher, &QDBusServiceWatcher::serviceUnregistered, this, &SecretService::ownerGone);
}

SecretService::~SecretService()
{
    Crypto::wipe(m_loginPassword);
    for (KeyringItem &i : m_session.items)
        Crypto::wipe(i.secret);
}

// --- paths -------------------------------------------------------------------

QString SecretService::collectionPath(const QString &name)
{
    return Base + QStringLiteral("/collection/") + name;
}

QString SecretService::itemPath(const QString &collectionName, quint32 id)
{
    return collectionPath(collectionName) + QLatin1Char('/') + QString::number(id);
}

QString SecretService::aliasTarget(const QString &alias) const
{
    if (alias == SessionCollection)
        return SessionCollection;
    const QString name = m_list->aliases.value(alias);
    return collection(name) ? name : QString();
}

SecretService::Target SecretService::resolve(const QString &path) const
{
    Target t;
    if (path == Base) {
        t.kind = Target::Service;
        return t;
    }
    if (!path.startsWith(Base + QLatin1Char('/'))) {
        if (path == QLatin1String("/") || path == QLatin1String("/org") || path == QLatin1String("/org/freedesktop"))
            t.kind = Target::Node;
        return t;
    }
    const QStringList parts = path.mid(Base.size() + 1).split(QLatin1Char('/'));
    const QString kind = parts.at(0);
    if (parts.size() == 1) {
        if (kind == QLatin1String("collection") || kind == QLatin1String("aliases")
            || kind == QLatin1String("session") || kind == QLatin1String("prompt"))
            t.kind = Target::Node;
        return t;
    }
    if (kind == QLatin1String("session") && parts.size() == 2) {
        if (m_sessions.contains(parts.at(1))) {
            t.kind = Target::Session;
            t.id = parts.at(1);
        }
        return t;
    }
    if (kind == QLatin1String("prompt") && parts.size() == 2) {
        if (m_prompts.contains(parts.at(1))) {
            t.kind = Target::Prompt;
            t.id = parts.at(1);
        }
        return t;
    }
    QString name;
    if (kind == QLatin1String("collection"))
        name = collection(parts.at(1)) ? parts.at(1) : QString();
    else if (kind == QLatin1String("aliases"))
        name = aliasTarget(parts.at(1));
    if (name.isEmpty() || parts.size() > 3)
        return t;
    t.collection = name;
    if (parts.size() == 2) {
        t.kind = Target::Collection;
        return t;
    }
    bool ok = false;
    const quint32 id = parts.at(2).toUInt(&ok);
    if (ok && collection(name)->items.contains(id)) {
        t.kind = Target::Item;
        t.item = id;
    }
    return t;
}

// --- the model ---------------------------------------------------------------

KeyringCollection *SecretService::collection(const QString &name)
{
    if (name == SessionCollection)
        return &m_session;
    auto it = m_list->collections.find(name);
    return it == m_list->collections.end() ? nullptr : &*it;
}

const KeyringCollection *SecretService::collection(const QString &name) const
{
    if (name == SessionCollection)
        return &m_session;
    auto it = m_list->collections.constFind(name);
    return it == m_list->collections.cend() ? nullptr : &*it;
}

bool SecretService::isLocked(const QString &collectionName) const
{
    return collectionName != SessionCollection && m_list->state() != PasswordList::Unlocked;
}

QStringList SecretService::collectionNames() const
{
    return m_list->collections.keys() << SessionCollection;
}

QList<QDBusObjectPath> SecretService::itemPaths(const KeyringCollection &c) const
{
    QList<QDBusObjectPath> out;
    for (const KeyringItem &i : c.items)
        out << QDBusObjectPath(itemPath(c.name, i.id));
    return out;
}

bool SecretService::matches(const KeyringItem &item, const StringMap &query, bool locked) const
{
    if (!locked) {
        for (auto it = query.cbegin(); it != query.cend(); ++it) {
            auto found = item.attributes.constFind(it.key());
            if (found == item.attributes.cend() || *found != it.value())
                return false;
        }
        return true;
    }
    for (const QByteArray &h : m_list->hashes(query)) {
        if (!item.hashes.contains(h))
            return false;
    }
    return true;
}

void SecretService::changed(const QString &collectionName)
{
    if (KeyringCollection *c = collection(collectionName))
        c->modified = now();
    if (collectionName != SessionCollection && !m_list->save())
        qWarning().noquote() << "win95keyring: couldn't save" << m_list->path() << ":" << m_list->lastError();
}

// --- D-Bus plumbing ----------------------------------------------------------

void SecretService::reply(const QDBusMessage &m, const QVariantList &args)
{
    if (m.isReplyRequired())
        m_bus.send(m.createReply(args));
}

void SecretService::error(const QDBusMessage &m, const QString &name, const QString &text)
{
    m_bus.send(m.createErrorReply(name, text));
}

void SecretService::emitSignal(const QString &path, const QString &iface, const QString &name, const QVariantList &args)
{
    QDBusMessage s = QDBusMessage::createSignal(path, iface, name);
    s.setArguments(args);
    m_bus.send(s);
}

void SecretService::emitPropertiesChanged(const QString &path, const QString &iface, const QVariantMap &changed)
{
    emitSignal(path, PropertiesIface, QStringLiteral("PropertiesChanged"),
               { iface, QVariant::fromValue(changed), QVariant::fromValue(QStringList()) });
}

void SecretService::emitLockedChanged()
{
    const bool locked = m_list->state() != PasswordList::Unlocked;
    for (const QString &name : m_list->collections.keys()) {
        const KeyringCollection *c = collection(name);
        emitPropertiesChanged(collectionPath(name), CollectionIface,
                              { { QStringLiteral("Locked"), locked },
                                { QStringLiteral("Items"), QVariant::fromValue(itemPaths(*c)) } });
        emitSignal(Base, ServiceIface, QStringLiteral("CollectionChanged"),
                   { QVariant::fromValue(QDBusObjectPath(collectionPath(name))) });
    }
    emitPropertiesChanged(Base, KeyringIface,
                          { { QStringLiteral("State"), properties({ Target::Service }, KeyringIface).value(QStringLiteral("State")) } });
}

bool SecretService::handleMessage(const QDBusMessage &m, const QDBusConnection &)
{
    if (m.type() != QDBusMessage::MethodCallMessage)
        return false;
    const Target t = resolve(m.path());
    const QString iface = m.interface();

    if (iface == IntrospectIface || (iface.isEmpty() && m.member() == QLatin1String("Introspect"))) {
        introspectCall(m, t);
        return true;
    }
    if (t.kind == Target::None) {
        error(m, ErrNoSuchObject, QStringLiteral("No such object: ") + m.path());
        return true;
    }
    if (iface == PropertiesIface) {
        propertiesCall(m, t);
        return true;
    }
    switch (t.kind) {
    case Target::Service:
        if (iface == KeyringIface)
            keyringCall(m);
        else
            serviceCall(m);
        break;
    case Target::Collection:
        collectionCall(m, t);
        break;
    case Target::Item:
        itemCall(m, t);
        break;
    case Target::Session:
        sessionCall(m, t);
        break;
    case Target::Prompt:
        promptCall(m, t);
        break;
    default:
        error(m, ErrUnknownMethod, QStringLiteral("No such method: ") + m.member());
    }
    return true;
}

QString SecretService::introspect(const QString &) const
{
    return {}; // handleMessage answers Introspect itself
}

void SecretService::introspectCall(const QDBusMessage &m, const Target &t)
{
    QString xml = QStringLiteral("<!DOCTYPE node PUBLIC \"-//freedesktop//DTD D-BUS Object Introspection 1.0//EN\" "
                                 "\"http://www.freedesktop.org/standards/dbus/1.0/introspect.dtd\">\n<node>\n");
    QStringList children;
    switch (t.kind) {
    case Target::Service:
        xml += QLatin1String(ServiceXml) + QLatin1String(StandardXml);
        children = { QStringLiteral("collection"), QStringLiteral("aliases") };
        if (!m_sessions.isEmpty())
            children << QStringLiteral("session");
        if (!m_prompts.isEmpty())
            children << QStringLiteral("prompt");
        break;
    case Target::Collection:
        xml += QLatin1String(CollectionXml) + QLatin1String(StandardXml);
        for (quint32 id : collection(t.collection)->items.keys())
            children << QString::number(id);
        break;
    case Target::Item:
        xml += QLatin1String(ItemXml) + QLatin1String(StandardXml);
        break;
    case Target::Session:
        xml += QLatin1String(SessionXml) + QLatin1String(StandardXml);
        break;
    case Target::Prompt:
        xml += QLatin1String(PromptXml) + QLatin1String(StandardXml);
        break;
    case Target::Node: {
        const QString p = m.path();
        if (p == QLatin1String("/"))
            children << QStringLiteral("org");
        else if (p == QLatin1String("/org"))
            children << QStringLiteral("freedesktop");
        else if (p == QLatin1String("/org/freedesktop"))
            children << QStringLiteral("secrets");
        else if (p.endsWith(QLatin1String("/collection")))
            children = collectionNames();
        else if (p.endsWith(QLatin1String("/aliases")))
            children = QStringList(m_list->aliases.keys()) << SessionCollection;
        else if (p.endsWith(QLatin1String("/session")))
            children = m_sessions.keys();
        else if (p.endsWith(QLatin1String("/prompt")))
            children = m_prompts.keys();
        break;
    }
    case Target::None:
        break;
    }
    for (const QString &c : children)
        xml += QStringLiteral("<node name=\"%1\"/>\n").arg(c);
    xml += QLatin1String("</node>\n");
    reply(m, { xml });
}

// --- properties --------------------------------------------------------------

QVariantMap SecretService::properties(const Target &t, const QString &iface) const
{
    QVariantMap p;
    if (t.kind == Target::Service && iface == ServiceIface) {
        QList<QDBusObjectPath> paths;
        for (const QString &name : collectionNames())
            paths << QDBusObjectPath(collectionPath(name));
        p.insert(QStringLiteral("Collections"), QVariant::fromValue(paths));
    } else if (t.kind == Target::Service && iface == KeyringIface) {
        const char *state = m_list->state() == PasswordList::Unlocked ? "unlocked"
                            : m_list->state() == PasswordList::Locked ? "locked"
                                                                      : "none";
        p.insert(QStringLiteral("State"), QString::fromLatin1(state));
    } else if (t.kind == Target::Collection && iface == CollectionIface) {
        const KeyringCollection *c = collection(t.collection);
        p.insert(QStringLiteral("Items"), QVariant::fromValue(itemPaths(*c)));
        p.insert(QStringLiteral("Label"), c->label);
        p.insert(QStringLiteral("Locked"), isLocked(t.collection));
        p.insert(QStringLiteral("Created"), qulonglong(c->created));
        p.insert(QStringLiteral("Modified"), qulonglong(c->modified));
    } else if (t.kind == Target::Item && iface == ItemIface) {
        const KeyringItem &i = collection(t.collection)->items[t.item];
        const bool locked = isLocked(t.collection);
        p.insert(QStringLiteral("Locked"), locked);
        p.insert(QStringLiteral("Attributes"), QVariant::fromValue(locked ? StringMap() : i.attributes));
        p.insert(QStringLiteral("Label"), locked ? QString() : i.label);
        p.insert(QStringLiteral("Type"), locked ? QString() : i.contentType);
        p.insert(QStringLiteral("Created"), qulonglong(i.created));
        p.insert(QStringLiteral("Modified"), qulonglong(i.modified));
    }
    return p;
}

void SecretService::propertiesCall(const QDBusMessage &m, const Target &t)
{
    const QVariantList a = m.arguments();
    const QString member = m.member();
    if (member == QLatin1String("GetAll") && m.signature() == QLatin1String("s")) {
        reply(m, { QVariant::fromValue(properties(t, a.at(0).toString())) });
    } else if (member == QLatin1String("Get") && m.signature() == QLatin1String("ss")) {
        const QVariantMap p = properties(t, a.at(0).toString());
        if (!p.contains(a.at(1).toString()))
            return error(m, ErrInvalidArgs, QStringLiteral("No such property: ") + a.at(1).toString());
        reply(m, { QVariant::fromValue(QDBusVariant(p.value(a.at(1).toString()))) });
    } else if (member == QLatin1String("Set") && m.signature() == QLatin1String("ssv")) {
        const QString iface = a.at(0).toString();
        const QString name = a.at(1).toString();
        const QVariant value = unwrap(a.at(2));
        if (t.kind == Target::Collection && iface == CollectionIface && name == QLatin1String("Label")) {
            if (isLocked(t.collection))
                return error(m, ErrIsLocked, QStringLiteral("The password list is locked"));
            collection(t.collection)->label = value.toString();
            changed(t.collection);
            emitPropertiesChanged(m.path(), iface, { { name, value.toString() } });
            reply(m);
        } else if (t.kind == Target::Item && iface == ItemIface
                   && (name == QLatin1String("Label") || name == QLatin1String("Attributes") || name == QLatin1String("Type"))) {
            if (isLocked(t.collection))
                return error(m, ErrIsLocked, QStringLiteral("The password list is locked"));
            KeyringItem &i = collection(t.collection)->items[t.item];
            if (name == QLatin1String("Label"))
                i.label = value.toString();
            else if (name == QLatin1String("Type"))
                i.contentType = value.toString();
            else {
                i.attributes = fromDBus<StringMap>(value);
                m_list->rehash(i);
            }
            i.modified = now();
            changed(t.collection);
            const QString path = itemPath(t.collection, t.item);
            emitPropertiesChanged(path, iface, { { name, properties(t, iface).value(name) } });
            emitSignal(collectionPath(t.collection), CollectionIface, QStringLiteral("ItemChanged"),
                       { QVariant::fromValue(QDBusObjectPath(path)) });
            reply(m);
        } else {
            error(m, ErrAccessDenied, QStringLiteral("Property can't be set: ") + name);
        }
    } else {
        error(m, ErrUnknownMethod, QStringLiteral("No such method: ") + member);
    }
}

// --- sessions and secrets ----------------------------------------------------

std::optional<SecretStruct> SecretService::encode(const QString &sessionPath, const QString &caller, const KeyringItem &item)
{
    const QString id = sessionPath.section(QLatin1Char('/'), -1);
    auto it = m_sessions.find(id);
    if (!sessionPath.startsWith(Base + QStringLiteral("/session/")) || it == m_sessions.end() || it->owner != caller)
        return std::nullopt;
    SecretStruct s;
    s.session = QDBusObjectPath(sessionPath);
    s.contentType = item.contentType;
    if (it->dh)
        s.value = it->dh->encrypt(item.secret, &s.parameters);
    else
        s.value = item.secret;
    return s;
}

std::optional<QByteArray> SecretService::decode(const SecretStruct &secret, const QString &caller)
{
    const QString path = secret.session.path();
    const QString id = path.section(QLatin1Char('/'), -1);
    auto it = m_sessions.find(id);
    if (!path.startsWith(Base + QStringLiteral("/session/")) || it == m_sessions.end() || it->owner != caller)
        return std::nullopt;
    if (!it->dh)
        return secret.value;
    QByteArray plain;
    if (!it->dh->decrypt(secret.parameters, secret.value, &plain))
        return std::nullopt;
    return plain;
}

void SecretService::sessionCall(const QDBusMessage &m, const Target &t)
{
    if (m.member() != QLatin1String("Close"))
        return error(m, ErrUnknownMethod, QStringLiteral("No such method: ") + m.member());
    if (m_sessions.value(t.id).owner != m.service())
        return error(m, ErrAccessDenied, QStringLiteral("Not your session"));
    m_sessions.remove(t.id);
    reply(m);
}

void SecretService::ownerGone(const QString &owner)
{
    for (auto it = m_sessions.begin(); it != m_sessions.end();)
        it = it->owner == owner ? m_sessions.erase(it) : std::next(it);
    // its prompts: nobody to tell
    for (const QString &id : m_prompts.keys()) {
        if (m_prompts.value(id).owner != owner)
            continue;
        m_prompts.remove(id);
        m_promptQueue.removeAll(id);
        if (m_asking == id) {
            m_asking.clear();
            ++m_askGeneration;
            m_bus.asyncCall(QDBusMessage::createMethodCall(PrompterService, PrompterPath, PrompterIface, QStringLiteral("Cancel")));
            nextPrompt();
        }
    }
    m_watcher->removeWatchedService(owner);
}

// --- org.freedesktop.Secret.Service -------------------------------------------

void SecretService::serviceCall(const QDBusMessage &m)
{
    const QString member = m.member();
    const QString sig = m.signature();
    const QVariantList a = m.arguments();

    if (member == QLatin1String("OpenSession") && sig == QLatin1String("sv")) {
        const QString algorithm = a.at(0).toString();
        Session s;
        s.owner = m.service();
        QVariant output = QString();
        if (algorithm == QLatin1String("dh-ietf1024-sha256-aes128-cbc-pkcs7")) {
            const QByteArray peer = unwrap(a.at(1)).toByteArray();
            s.dh = std::make_shared<Crypto::DhSession>();
            if (!s.dh->start(peer))
                return error(m, ErrInvalidArgs, QStringLiteral("Invalid public key"));
            output = s.dh->publicKey();
        } else if (algorithm != QLatin1String("plain")) {
            return error(m, ErrNotSupported, QStringLiteral("Algorithm not supported: ") + algorithm);
        }
        const QString id = QString::number(m_nextSession++);
        m_sessions.insert(id, std::move(s));
        m_watcher->addWatchedService(m.service());
        reply(m, { QVariant::fromValue(QDBusVariant(output)),
                   QVariant::fromValue(QDBusObjectPath(Base + QStringLiteral("/session/") + id)) });
    } else if (member == QLatin1String("SearchItems") && sig == QLatin1String("a{ss}")) {
        const StringMap query = fromDBus<StringMap>(a.at(0));
        QList<QDBusObjectPath> unlocked, locked;
        for (const QString &name : collectionNames()) {
            const bool isLockedNow = isLocked(name);
            for (const KeyringItem &i : collection(name)->items) {
                if (matches(i, query, isLockedNow))
                    (isLockedNow ? locked : unlocked) << QDBusObjectPath(itemPath(name, i.id));
            }
        }
        reply(m, { QVariant::fromValue(unlocked), QVariant::fromValue(locked) });
    } else if (member == QLatin1String("Unlock") && sig == QLatin1String("ao")) {
        const QList<QDBusObjectPath> objects = fromDBus<QList<QDBusObjectPath>>(a.at(0));
        QList<QDBusObjectPath> already, need;
        for (const QDBusObjectPath &o : objects) {
            const Target t = resolve(o.path());
            if (t.kind != Target::Collection && t.kind != Target::Item)
                continue;
            (isLocked(t.collection) ? need : already) << o;
        }
        if (need.isEmpty())
            return reply(m, { QVariant::fromValue(already), QVariant::fromValue(NoPrompt) });
        const QDBusObjectPath prompt = newPrompt(m.service(), Need::Unlock, [objects] {
            return std::optional<QVariant>(QVariant::fromValue(objects));
        }, QVariant::fromValue(QList<QDBusObjectPath>()));
        reply(m, { QVariant::fromValue(already), QVariant::fromValue(prompt) });
    } else if (member == QLatin1String("Lock") && sig == QLatin1String("ao")) {
        const QList<QDBusObjectPath> objects = fromDBus<QList<QDBusObjectPath>>(a.at(0));
        QList<QDBusObjectPath> locked;
        bool any = false;
        for (const QDBusObjectPath &o : objects) {
            const Target t = resolve(o.path());
            if ((t.kind == Target::Collection || t.kind == Target::Item) && t.collection != SessionCollection) {
                any = true;
                locked << o;
            }
        }
        if (any && m_list->state() == PasswordList::Unlocked) {
            m_list->lock();
            emitLockedChanged();
        }
        reply(m, { QVariant::fromValue(locked), QVariant::fromValue(NoPrompt) });
    } else if (member == QLatin1String("GetSecrets") && sig == QLatin1String("aoo")) {
        const QList<QDBusObjectPath> items = fromDBus<QList<QDBusObjectPath>>(a.at(0));
        const QString session = a.at(1).value<QDBusObjectPath>().path();
        if (!m_sessions.contains(session.section(QLatin1Char('/'), -1)))
            return error(m, ErrNoSession, QStringLiteral("No such session"));
        SecretMap out;
        for (const QDBusObjectPath &o : items) {
            const Target t = resolve(o.path());
            if (t.kind != Target::Item || isLocked(t.collection))
                continue;
            const auto s = encode(session, m.service(), collection(t.collection)->items[t.item]);
            if (!s)
                return error(m, ErrNoSession, QStringLiteral("No such session"));
            out.insert(o, *s);
        }
        reply(m, { QVariant::fromValue(out) });
    } else if (member == QLatin1String("ReadAlias") && sig == QLatin1String("s")) {
        const QString name = aliasTarget(a.at(0).toString());
        reply(m, { QVariant::fromValue(name.isEmpty() ? NoPrompt : QDBusObjectPath(collectionPath(name))) });
    } else if (member == QLatin1String("SetAlias") && sig == QLatin1String("so")) {
        const QString alias = a.at(0).toString();
        const QString target = a.at(1).value<QDBusObjectPath>().path();
        if (alias == SessionCollection || alias.isEmpty())
            return error(m, ErrNotSupported, QStringLiteral("That alias can't be changed"));
        if (target == QLatin1String("/")) {
            if (m_list->state() == PasswordList::Unlocked) {
                m_list->aliases.remove(alias);
                m_list->save();
            }
            return reply(m);
        }
        const Target t = resolve(target);
        if (t.kind != Target::Collection || t.collection == SessionCollection)
            return error(m, ErrNoSuchObject, QStringLiteral("No such collection"));
        if (m_list->state() != PasswordList::Unlocked)
            return error(m, ErrIsLocked, QStringLiteral("The password list is locked"));
        m_list->aliases.insert(alias, t.collection);
        m_list->save();
        reply(m);
    } else if (member == QLatin1String("CreateCollection") && sig == QLatin1String("a{sv}s")) {
        const QVariantMap props = fromDBus<QVariantMap>(a.at(0));
        const QString label = unwrap(props.value(PropCollectionLabel)).toString();
        const QString alias = a.at(1).toString();
        // an existing alias: that collection (as gnome-keyring does)
        if (!alias.isEmpty() && !aliasTarget(alias).isEmpty())
            return reply(m, { QVariant::fromValue(QDBusObjectPath(collectionPath(aliasTarget(alias)))),
                              QVariant::fromValue(NoPrompt) });
        auto finish = [this, label, alias]() -> std::optional<QVariant> {
            const auto name = createCollection(label, alias);
            if (!name)
                return std::nullopt;
            return QVariant::fromValue(QDBusObjectPath(collectionPath(*name)));
        };
        if (m_list->state() == PasswordList::Unlocked) {
            const auto result = finish();
            if (!result)
                return error(m, QStringLiteral("org.freedesktop.DBus.Error.Failed"), QStringLiteral("Couldn't create it"));
            return reply(m, { *result, QVariant::fromValue(NoPrompt) });
        }
        const Need need = m_list->state() == PasswordList::NoFile ? Need::Create : Need::Unlock;
        reply(m, { QVariant::fromValue(NoPrompt), QVariant::fromValue(newPrompt(m.service(), need, finish)) });
    } else {
        error(m, ErrUnknownMethod, QStringLiteral("No such method: %1(%2)").arg(member, sig));
    }
}

std::optional<QString> SecretService::createCollection(const QString &label, const QString &alias)
{
    if (m_list->state() != PasswordList::Unlocked)
        return std::nullopt;
    // the list's own "login" collection, when it's what's asked for
    // (libsecret asks for "default", labelled "Login", when there's none)
    if (alias == QLatin1String("default") && m_list->collections.contains(QStringLiteral("login"))
        && !m_list->aliases.contains(alias)) {
        m_list->aliases.insert(alias, QStringLiteral("login"));
        m_list->save();
        return QStringLiteral("login");
    }
    QString base = label.toLower();
    base.replace(QRegularExpression(QStringLiteral("[^a-z0-9_]")), QStringLiteral("_"));
    if (base.isEmpty())
        base = QStringLiteral("collection");
    QString name = base;
    for (int n = 2; collection(name) || name == SessionCollection; ++n)
        name = base + QLatin1Char('_') + QString::number(n);
    KeyringCollection c;
    c.name = name;
    c.label = label.isEmpty() ? name : label;
    c.created = c.modified = now();
    m_list->collections.insert(name, c);
    if (!alias.isEmpty() && alias != SessionCollection)
        m_list->aliases.insert(alias, name);
    m_list->save();
    emitSignal(Base, ServiceIface, QStringLiteral("CollectionCreated"),
               { QVariant::fromValue(QDBusObjectPath(collectionPath(name))) });
    emitPropertiesChanged(Base, ServiceIface, properties({ Target::Service }, ServiceIface));
    return name;
}

// --- org.freedesktop.Secret.Collection ---------------------------------------

std::optional<QString> SecretService::createItem(const QString &collectionName, const QVariantMap &props,
                                                 const QByteArray &secret, const QString &contentType, bool replace)
{
    KeyringCollection *c = collection(collectionName);
    if (!c || isLocked(collectionName))
        return std::nullopt;
    const StringMap attributes = fromDBus<StringMap>(unwrap(props.value(PropAttributes)));
    const QString label = unwrap(props.value(PropLabel)).toString();
    const QString type = props.contains(PropType) ? unwrap(props.value(PropType)).toString() : contentType;

    KeyringItem *item = nullptr;
    if (replace) {
        for (KeyringItem &i : c->items) {
            if (i.attributes == attributes) {
                item = &i;
                break;
            }
        }
    }
    const bool isNew = !item;
    if (isNew) {
        KeyringItem i;
        i.id = c->nextId++;
        i.created = now();
        item = &*c->items.insert(i.id, i);
    }
    item->label = label;
    item->attributes = attributes;
    Crypto::wipe(item->secret);
    item->secret = secret;
    item->contentType = type.isEmpty() ? QStringLiteral("text/plain") : type;
    item->modified = now();
    m_list->rehash(*item);
    const QString path = itemPath(collectionName, item->id);
    changed(collectionName);

    const QString cpath = collectionPath(collectionName);
    emitSignal(cpath, CollectionIface, isNew ? QStringLiteral("ItemCreated") : QStringLiteral("ItemChanged"),
               { QVariant::fromValue(QDBusObjectPath(path)) });
    if (isNew)
        emitPropertiesChanged(cpath, CollectionIface, { { QStringLiteral("Items"), QVariant::fromValue(itemPaths(*c)) } });
    return path;
}

void SecretService::collectionCall(const QDBusMessage &m, const Target &t)
{
    const QString member = m.member();
    const QString sig = m.signature();
    const QVariantList a = m.arguments();
    KeyringCollection *c = collection(t.collection);

    if (member == QLatin1String("SearchItems") && sig == QLatin1String("a{ss}")) {
        const StringMap query = fromDBus<StringMap>(a.at(0));
        QList<QDBusObjectPath> found;
        for (const KeyringItem &i : c->items) {
            if (matches(i, query, isLocked(t.collection)))
                found << QDBusObjectPath(itemPath(t.collection, i.id));
        }
        reply(m, { QVariant::fromValue(found) });
    } else if (member == QLatin1String("CreateItem") && sig == QLatin1String("a{sv}(oayays)b")) {
        const QVariantMap props = fromDBus<QVariantMap>(a.at(0));
        const SecretStruct secret = fromDBus<SecretStruct>(a.at(1));
        const bool replace = a.at(2).toBool();
        const auto value = decode(secret, m.service());
        if (!value)
            return error(m, ErrNoSession, QStringLiteral("No such session (or a secret it can't read)"));
        const QString name = t.collection;
        const QByteArray plain = *value;
        const QString type = secret.contentType;
        if (!isLocked(name)) {
            const auto path = createItem(name, props, plain, type, replace);
            return reply(m, { QVariant::fromValue(QDBusObjectPath(path.value_or(QStringLiteral("/")))),
                              QVariant::fromValue(NoPrompt) });
        }
        const QDBusObjectPath prompt = newPrompt(m.service(), Need::Unlock, [this, name, props, plain, type, replace] {
            const auto path = createItem(name, props, plain, type, replace);
            return path ? std::optional<QVariant>(QVariant::fromValue(QDBusObjectPath(*path))) : std::nullopt;
        });
        reply(m, { QVariant::fromValue(NoPrompt), QVariant::fromValue(prompt) });
    } else if (member == QLatin1String("Delete") && sig.isEmpty()) {
        const QString path = collectionPath(t.collection);
        if (t.collection == SessionCollection) {
            for (KeyringItem &i : m_session.items)
                Crypto::wipe(i.secret);
            m_session.items.clear();
            emitPropertiesChanged(path, CollectionIface, { { QStringLiteral("Items"), QVariant::fromValue(QList<QDBusObjectPath>()) } });
            return reply(m, { QVariant::fromValue(NoPrompt) });
        }
        if (isLocked(t.collection))
            return error(m, ErrIsLocked, QStringLiteral("The password list is locked"));
        m_list->collections.remove(t.collection);
        for (const QString &alias : m_list->aliases.keys()) {
            if (m_list->aliases.value(alias) == t.collection)
                m_list->aliases.remove(alias);
        }
        m_list->save();
        emitSignal(Base, ServiceIface, QStringLiteral("CollectionDeleted"), { QVariant::fromValue(QDBusObjectPath(path)) });
        emitPropertiesChanged(Base, ServiceIface, properties({ Target::Service }, ServiceIface));
        reply(m, { QVariant::fromValue(NoPrompt) });
    } else {
        error(m, ErrUnknownMethod, QStringLiteral("No such method: %1(%2)").arg(member, sig));
    }
}

// --- org.freedesktop.Secret.Item -----------------------------------------------

void SecretService::itemCall(const QDBusMessage &m, const Target &t)
{
    const QString member = m.member();
    const QString sig = m.signature();
    const QVariantList a = m.arguments();
    KeyringCollection *c = collection(t.collection);
    const QString path = itemPath(t.collection, t.item);

    if (member == QLatin1String("GetSecret") && sig == QLatin1String("o")) {
        if (isLocked(t.collection))
            return error(m, ErrIsLocked, QStringLiteral("The password list is locked"));
        const auto s = encode(a.at(0).value<QDBusObjectPath>().path(), m.service(), c->items[t.item]);
        if (!s)
            return error(m, ErrNoSession, QStringLiteral("No such session"));
        reply(m, { QVariant::fromValue(*s) });
    } else if (member == QLatin1String("SetSecret") && sig == QLatin1String("(oayays)")) {
        if (isLocked(t.collection))
            return error(m, ErrIsLocked, QStringLiteral("The password list is locked"));
        const SecretStruct secret = fromDBus<SecretStruct>(a.at(0));
        const auto value = decode(secret, m.service());
        if (!value)
            return error(m, ErrNoSession, QStringLiteral("No such session (or a secret it can't read)"));
        KeyringItem &i = c->items[t.item];
        Crypto::wipe(i.secret);
        i.secret = *value;
        if (!secret.contentType.isEmpty())
            i.contentType = secret.contentType;
        i.modified = now();
        changed(t.collection);
        emitSignal(collectionPath(t.collection), CollectionIface, QStringLiteral("ItemChanged"),
                   { QVariant::fromValue(QDBusObjectPath(path)) });
        reply(m);
    } else if (member == QLatin1String("Delete") && sig.isEmpty()) {
        if (isLocked(t.collection))
            return error(m, ErrIsLocked, QStringLiteral("The password list is locked"));
        Crypto::wipe(c->items[t.item].secret);
        c->items.remove(t.item);
        changed(t.collection);
        const QString cpath = collectionPath(t.collection);
        emitSignal(cpath, CollectionIface, QStringLiteral("ItemDeleted"), { QVariant::fromValue(QDBusObjectPath(path)) });
        emitPropertiesChanged(cpath, CollectionIface, { { QStringLiteral("Items"), QVariant::fromValue(itemPaths(*c)) } });
        reply(m, { QVariant::fromValue(NoPrompt) });
    } else {
        error(m, ErrUnknownMethod, QStringLiteral("No such method: %1(%2)").arg(member, sig));
    }
}

// --- prompts -----------------------------------------------------------------

QDBusObjectPath SecretService::newPrompt(const QString &owner, Need need, std::function<std::optional<QVariant>()> finish,
                                         const QVariant &dismissedResult)
{
    const QString id = QString::number(m_nextPrompt++);
    PendingPrompt p;
    p.owner = owner;
    p.need = need;
    p.finish = std::move(finish);
    p.dismissedResult = dismissedResult;
    m_prompts.insert(id, p);
    m_watcher->addWatchedService(owner);
    return QDBusObjectPath(Base + QStringLiteral("/prompt/") + id);
}

void SecretService::promptCall(const QDBusMessage &m, const Target &t)
{
    const QString member = m.member();
    PendingPrompt &p = m_prompts[t.id];
    if (member == QLatin1String("Prompt") && m.signature() == QLatin1String("s")) {
        reply(m);
        if (!p.started) {
            p.started = true;
            m_promptQueue << t.id;
            nextPrompt();
        }
    } else if (member == QLatin1String("Dismiss") && m.signature().isEmpty()) {
        reply(m);
        if (m_asking == t.id) {
            m_asking.clear();
            ++m_askGeneration;
            m_bus.asyncCall(QDBusMessage::createMethodCall(PrompterService, PrompterPath, PrompterIface, QStringLiteral("Cancel")));
        }
        completePrompt(t.id, true);
        nextPrompt();
    } else {
        error(m, ErrUnknownMethod, QStringLiteral("No such method: ") + member);
    }
}

void SecretService::completePrompt(const QString &id, bool dismissed)
{
    if (!m_prompts.contains(id))
        return;
    PendingPrompt p = m_prompts.take(id);
    m_promptQueue.removeAll(id);
    std::optional<QVariant> result;
    if (!dismissed && p.finish)
        result = p.finish();
    if (!result)
        dismissed = true;
    emitSignal(Base + QStringLiteral("/prompt/") + id, PromptIface, QStringLiteral("Completed"),
               { dismissed, QVariant::fromValue(QDBusVariant(dismissed ? p.dismissedResult : *result)) });
}

// The prompts one at a time: each is done as soon as the list is unlocked
// (or made), by this one or one before it, or the login.
void SecretService::nextPrompt()
{
    while (m_asking.isEmpty() && !m_promptQueue.isEmpty()) {
        const QString id = m_promptQueue.first();
        PendingPrompt &p = m_prompts[id];
        // what's needed now (the list may have been made or unlocked since)
        if (m_list->state() == PasswordList::Unlocked) {
            completePrompt(id, false);
            continue;
        }
        p.need = m_list->state() == PasswordList::NoFile ? Need::Create : Need::Unlock;
        // no list yet, and the login gave its password: that's its password
        if (p.need == Need::Create && !m_loginPassword.isEmpty()) {
            if (m_list->create(m_loginPassword)) {
                created(m_loginPassword);
                continue;
            }
        }
        m_asking = id;
        m_askTries = 0;
        askShell(false);
    }
}

void SecretService::askShell(bool retry)
{
    const int generation = ++m_askGeneration;
    const PendingPrompt &p = m_prompts[m_asking];
    QDBusMessage call = QDBusMessage::createMethodCall(PrompterService, PrompterPath, PrompterIface, QStringLiteral("AskPassword"));
    call << (p.need == Need::Create ? QStringLiteral("create") : QStringLiteral("unlock")) << retry;
    // as long as the person takes (the desktop gives up first)
    auto *w = new QDBusPendingCallWatcher(m_bus.asyncCall(call, 60 * 60 * 1000), this);
    connect(w, &QDBusPendingCallWatcher::finished, this, [this, w, generation, retry] {
        w->deleteLater();
        if (generation != m_askGeneration || m_asking.isEmpty())
            return; // dismissed meanwhile
        const QString id = m_asking;
        const QDBusMessage r = w->reply();
        if (r.type() == QDBusMessage::ErrorMessage) {
            const QString name = r.errorName();
            // no desktop to ask (yet: the login may still be starting it)
            if ((name == QLatin1String("org.freedesktop.DBus.Error.ServiceUnknown")
                 || name == QLatin1String("org.freedesktop.DBus.Error.NameHasNoOwner"))
                && ++m_askTries < 30) {
                QTimer::singleShot(2000, this, [this, generation, retry] {
                    if (generation == m_askGeneration && !m_asking.isEmpty())
                        askShell(retry);
                });
                return;
            }
            if (!name.endsWith(QLatin1String(".Canceled")))
                qWarning().noquote() << "win95keyring: can't ask for the password:" << r.errorMessage();
            m_asking.clear();
            completePrompt(id, true);
            nextPrompt();
            return;
        }
        QByteArray password = r.arguments().value(0).toString().toUtf8();
        bool ok = false;
        if (m_list->state() == PasswordList::NoFile) {
            ok = m_list->create(password);
            if (ok)
                created(password);
        } else if (m_list->state() == PasswordList::Locked) {
            ok = m_list->unlock(password);
            if (ok)
                unlocked(password);
        } else {
            ok = true;
        }
        Crypto::wipe(password);
        if (!ok) {
            askShell(true);
            return;
        }
        m_asking.clear();
        nextPrompt();
    });
}

void SecretService::unlocked(const QByteArray &password)
{
    qInfo() << "win95keyring: password list unlocked";
    // unlocked with a password other than the login's: from now on it's the
    // login's, so the next login unlocks it by itself
    if (!m_loginPassword.isEmpty() && password != m_loginPassword) {
        if (m_list->setPassword(m_loginPassword))
            qInfo() << "win95keyring: the password list now has your login password";
    }
    emitLockedChanged();
}

void SecretService::created(const QByteArray &)
{
    qInfo().noquote() << "win95keyring: made a new password list," << m_list->path();
    const QString path = collectionPath(QStringLiteral("login"));
    emitSignal(Base, ServiceIface, QStringLiteral("CollectionCreated"), { QVariant::fromValue(QDBusObjectPath(path)) });
    emitPropertiesChanged(Base, ServiceIface, properties({ Target::Service }, ServiceIface));
    emitLockedChanged();
}

// --- org.wayland95.Keyring ------------------------------------------------------

void SecretService::loginPassword(const QByteArray &password)
{
    Crypto::wipe(m_loginPassword);
    m_loginPassword = password;
    switch (m_list->state()) {
    case PasswordList::Locked:
        if (m_list->unlock(password)) {
            unlocked(password);
            nextPrompt();
        } else {
            qInfo() << "win95keyring: the login password doesn't open the password list; it'll be asked for";
        }
        break;
    case PasswordList::Unlocked:
        // (unlocked another way since: the login's password from now on)
        if (!m_list->checkPassword(password) && m_list->setPassword(password))
            qInfo() << "win95keyring: the password list now has your login password";
        break;
    case PasswordList::NoFile:
        // made with it when something's first kept
        nextPrompt();
        break;
    }
}

void SecretService::keyringCall(const QDBusMessage &m)
{
    const QString member = m.member();
    const QString sig = m.signature();
    const QVariantList a = m.arguments();
    if (member == QLatin1String("LoginPassword") && sig == QLatin1String("s")) {
        QByteArray password = a.at(0).toString().toUtf8();
        loginPassword(password);
        Crypto::wipe(password);
        reply(m);
    } else if (member == QLatin1String("ChangePassword") && sig == QLatin1String("ss")) {
        QByteArray oldPassword = a.at(0).toString().toUtf8();
        QByteArray newPassword = a.at(1).toString().toUtf8();
        bool ok = false;
        switch (m_list->state()) {
        case PasswordList::NoFile:
            ok = true;
            break;
        case PasswordList::Locked:
            ok = m_list->unlock(oldPassword) && m_list->setPassword(newPassword);
            if (ok)
                emitLockedChanged();
            break;
        case PasswordList::Unlocked:
            // (anything running as you can read it while it's unlocked anyway)
            ok = m_list->setPassword(newPassword);
            break;
        }
        if (ok) {
            Crypto::wipe(m_loginPassword);
            m_loginPassword = newPassword;
            qInfo() << "win95keyring: the password list's password was changed with your login password";
        } else {
            qWarning() << "win95keyring: couldn't change the password list's password";
        }
        Crypto::wipe(oldPassword);
        Crypto::wipe(newPassword);
        reply(m, { ok });
    } else if (member == QLatin1String("Lock") && sig.isEmpty()) {
        if (m_list->state() == PasswordList::Unlocked) {
            m_list->lock();
            emitLockedChanged();
        }
        reply(m);
    } else {
        error(m, ErrUnknownMethod, QStringLiteral("No such method: %1(%2)").arg(member, sig));
    }
}
