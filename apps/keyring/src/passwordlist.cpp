#include "passwordlist.h"
#include "crypto.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QtDebug>
#include <QtEndian>

namespace {
const QByteArray Magic("W95PWL\0\1", 8);
constexpr int SaltSize = 16;
constexpr int NonceSize = 12;
constexpr int FixedHeader = 8 + 4 + SaltSize; // magic, cost, salt

QJsonObject attributesJson(const QMap<QString, QString> &attrs)
{
    QJsonObject o;
    for (auto it = attrs.cbegin(); it != attrs.cend(); ++it)
        o.insert(it.key(), it.value());
    return o;
}

QMap<QString, QString> attributesFrom(const QJsonObject &o)
{
    QMap<QString, QString> attrs;
    for (auto it = o.constBegin(); it != o.constEnd(); ++it)
        attrs.insert(it.key(), it.value().toString());
    return attrs;
}
} // namespace

PasswordList::PasswordList(const QString &path)
    : m_path(path)
{
}

PasswordList::~PasswordList()
{
    lock();
}

QList<QByteArray> PasswordList::hashes(const QMap<QString, QString> &attributes) const
{
    QList<QByteArray> out;
    for (auto it = attributes.cbegin(); it != attributes.cend(); ++it)
        out << Crypto::sha256(m_salt + it.key().toUtf8() + '\0' + it.value().toUtf8());
    return out;
}

bool PasswordList::readFile(QByteArray *header, QByteArray *nonce, QByteArray *sealed)
{
    QFile f(m_path);
    if (!f.open(QIODevice::ReadOnly)) {
        m_error = f.errorString();
        return false;
    }
    const QByteArray data = f.readAll();
    if (data.size() < FixedHeader + 4 || !data.startsWith(Magic)) {
        m_error = QStringLiteral("not a password list");
        return false;
    }
    const quint32 indexLen = qFromBigEndian<quint32>(data.constData() + FixedHeader);
    const qint64 headerLen = qint64(FixedHeader) + 4 + indexLen;
    if (headerLen + NonceSize + 16 > data.size()) {
        m_error = QStringLiteral("the password list is cut short");
        return false;
    }
    *header = data.left(headerLen);
    *nonce = data.mid(headerLen, NonceSize);
    *sealed = data.mid(headerLen + NonceSize);
    return true;
}

void PasswordList::load()
{
    lock();
    collections.clear();
    aliases.clear();
    if (!QFileInfo::exists(m_path)) {
        m_state = NoFile;
        return;
    }
    QByteArray header, nonce, sealed;
    if (!readFile(&header, &nonce, &sealed)) {
        // there, but unreadable: treat as locked (nothing will open it, and
        // it's never overwritten unless the person makes a new one)
        qWarning().noquote() << "win95keyring:" << m_path << ":" << m_error;
        m_state = Locked;
        return;
    }
    m_logN = quint8(header.at(8));
    m_r = quint8(header.at(9));
    m_p = quint8(header.at(10));
    m_salt = header.mid(12, SaltSize);
    readIndex(header.mid(FixedHeader + 4));
    m_state = Locked;
}

void PasswordList::readIndex(const QByteArray &index)
{
    const QJsonObject root = QJsonDocument::fromJson(index).object();
    for (const QJsonValue &cv : root.value(QStringLiteral("collections")).toArray()) {
        const QJsonObject c = cv.toObject();
        KeyringCollection col;
        col.name = c.value(QStringLiteral("name")).toString();
        col.label = c.value(QStringLiteral("label")).toString();
        col.created = c.value(QStringLiteral("created")).toInteger();
        col.modified = c.value(QStringLiteral("modified")).toInteger();
        for (const QJsonValue &iv : c.value(QStringLiteral("items")).toArray()) {
            const QJsonObject i = iv.toObject();
            KeyringItem item;
            item.id = quint32(i.value(QStringLiteral("id")).toInteger());
            for (const QJsonValue &h : i.value(QStringLiteral("hashes")).toArray())
                item.hashes << QByteArray::fromBase64(h.toString().toLatin1());
            col.items.insert(item.id, item);
        }
        if (!col.name.isEmpty())
            collections.insert(col.name, col);
    }
    const QJsonObject a = root.value(QStringLiteral("aliases")).toObject();
    for (auto it = a.constBegin(); it != a.constEnd(); ++it)
        aliases.insert(it.key(), it.value().toString());
}

bool PasswordList::create(const QByteArray &password)
{
    if (m_state != NoFile)
        return false;
    m_salt = Crypto::randomBytes(SaltSize);
    m_logN = 15;
    m_r = 8;
    m_p = 1;
    m_key = Crypto::deriveKey(password, m_salt, m_logN, m_r, m_p);
    if (m_key.isEmpty()) {
        m_error = QStringLiteral("couldn't make a key from the password");
        return false;
    }
    const qint64 now = QDateTime::currentSecsSinceEpoch();
    KeyringCollection login;
    login.name = QStringLiteral("login");
    login.label = QStringLiteral("Login");
    login.created = login.modified = now;
    collections = { { login.name, login } };
    aliases = { { QStringLiteral("default"), login.name } };
    m_state = Unlocked;
    if (!save()) {
        m_state = NoFile;
        Crypto::wipe(m_key);
        collections.clear();
        aliases.clear();
        return false;
    }
    return true;
}

bool PasswordList::checkPassword(const QByteArray &password) const
{
    PasswordList probe(m_path);
    probe.load();
    return probe.unlock(password);
}

bool PasswordList::unlock(const QByteArray &password)
{
    if (m_state == Unlocked)
        return checkPassword(password);
    if (m_state != Locked)
        return false;
    QByteArray header, nonce, sealed;
    if (!readFile(&header, &nonce, &sealed))
        return false;
    QByteArray key = Crypto::deriveKey(password, m_salt, m_logN, m_r, m_p);
    QByteArray plain;
    if (key.isEmpty() || !Crypto::open(key, nonce, header, sealed, &plain)) {
        Crypto::wipe(key);
        m_error = QStringLiteral("wrong password");
        return false;
    }

    // the encrypted contents are what counts (the index is only a guide)
    const QJsonObject root = QJsonDocument::fromJson(plain).object();
    Crypto::wipe(plain);
    collections.clear();
    aliases.clear();
    for (const QJsonValue &cv : root.value(QStringLiteral("collections")).toArray()) {
        const QJsonObject c = cv.toObject();
        KeyringCollection col;
        col.name = c.value(QStringLiteral("name")).toString();
        col.label = c.value(QStringLiteral("label")).toString();
        col.created = c.value(QStringLiteral("created")).toInteger();
        col.modified = c.value(QStringLiteral("modified")).toInteger();
        col.nextId = quint32(c.value(QStringLiteral("nextId")).toInteger(1));
        for (const QJsonValue &iv : c.value(QStringLiteral("items")).toArray()) {
            const QJsonObject i = iv.toObject();
            KeyringItem item;
            item.id = quint32(i.value(QStringLiteral("id")).toInteger());
            item.label = i.value(QStringLiteral("label")).toString();
            item.attributes = attributesFrom(i.value(QStringLiteral("attributes")).toObject());
            item.secret = QByteArray::fromBase64(i.value(QStringLiteral("secret")).toString().toLatin1());
            item.contentType = i.value(QStringLiteral("type")).toString(QStringLiteral("text/plain"));
            item.created = i.value(QStringLiteral("created")).toInteger();
            item.modified = i.value(QStringLiteral("modified")).toInteger();
            rehash(item);
            col.items.insert(item.id, item);
            if (item.id >= col.nextId)
                col.nextId = item.id + 1;
        }
        if (!col.name.isEmpty())
            collections.insert(col.name, col);
    }
    const QJsonObject a = root.value(QStringLiteral("aliases")).toObject();
    for (auto it = a.constBegin(); it != a.constEnd(); ++it)
        aliases.insert(it.key(), it.value().toString());
    m_key = key;
    m_state = Unlocked;
    return true;
}

void PasswordList::lock()
{
    if (m_state != Unlocked)
        return;
    Crypto::wipe(m_key);
    for (KeyringCollection &c : collections) {
        for (KeyringItem &i : c.items) {
            Crypto::wipe(i.secret);
            i.label.clear();
            i.attributes.clear();
        }
    }
    m_state = Locked;
}

bool PasswordList::setPassword(const QByteArray &password)
{
    if (m_state != Unlocked)
        return false;
    const QByteArray oldSalt = m_salt;
    QByteArray oldKey = m_key;
    m_salt = Crypto::randomBytes(SaltSize);
    m_key = Crypto::deriveKey(password, m_salt, m_logN, m_r, m_p);
    for (KeyringCollection &c : collections)
        for (KeyringItem &i : c.items)
            rehash(i);
    if (m_key.isEmpty() || !save()) {
        m_salt = oldSalt;
        m_key = oldKey;
        for (KeyringCollection &c : collections)
            for (KeyringItem &i : c.items)
                rehash(i);
        return false;
    }
    Crypto::wipe(oldKey);
    return true;
}

bool PasswordList::save()
{
    if (m_state != Unlocked)
        return false;

    QJsonArray indexCollections, fullCollections;
    for (const KeyringCollection &c : std::as_const(collections)) {
        QJsonArray indexItems, fullItems;
        for (const KeyringItem &i : c.items) {
            QJsonArray hashes;
            for (const QByteArray &h : i.hashes)
                hashes.append(QString::fromLatin1(h.toBase64()));
            indexItems.append(QJsonObject{ { QStringLiteral("id"), qint64(i.id) }, { QStringLiteral("hashes"), hashes } });
            fullItems.append(QJsonObject{
                { QStringLiteral("id"), qint64(i.id) },
                { QStringLiteral("label"), i.label },
                { QStringLiteral("attributes"), attributesJson(i.attributes) },
                { QStringLiteral("secret"), QString::fromLatin1(i.secret.toBase64()) },
                { QStringLiteral("type"), i.contentType },
                { QStringLiteral("created"), i.created },
                { QStringLiteral("modified"), i.modified },
            });
        }
        const QJsonObject common{
            { QStringLiteral("name"), c.name },
            { QStringLiteral("label"), c.label },
            { QStringLiteral("created"), c.created },
            { QStringLiteral("modified"), c.modified },
        };
        QJsonObject ic = common;
        ic.insert(QStringLiteral("items"), indexItems);
        indexCollections.append(ic);
        QJsonObject fc = common;
        fc.insert(QStringLiteral("nextId"), qint64(c.nextId));
        fc.insert(QStringLiteral("items"), fullItems);
        fullCollections.append(fc);
    }
    QJsonObject aliasJson;
    for (auto it = aliases.cbegin(); it != aliases.cend(); ++it)
        aliasJson.insert(it.key(), it.value());

    const QByteArray index = QJsonDocument(QJsonObject{ { QStringLiteral("collections"), indexCollections },
                                                        { QStringLiteral("aliases"), aliasJson } })
                                 .toJson(QJsonDocument::Compact);
    QByteArray plain = QJsonDocument(QJsonObject{ { QStringLiteral("collections"), fullCollections },
                                                  { QStringLiteral("aliases"), aliasJson } })
                           .toJson(QJsonDocument::Compact);

    QByteArray header = Magic;
    header.append(char(m_logN)).append(char(m_r)).append(char(m_p)).append('\0');
    header.append(m_salt);
    char len[4];
    qToBigEndian<quint32>(quint32(index.size()), len);
    header.append(len, 4);
    header.append(index);
    const QByteArray nonce = Crypto::randomBytes(NonceSize);
    const QByteArray sealed = Crypto::seal(m_key, nonce, header, plain);
    Crypto::wipe(plain);
    if (sealed.isEmpty()) {
        m_error = QStringLiteral("couldn't encrypt the password list");
        return false;
    }

    const QFileInfo info(m_path);
    QDir().mkpath(info.absolutePath());
    QFile::setPermissions(info.absolutePath(), QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner);
    QSaveFile f(m_path);
    if (!f.open(QIODevice::WriteOnly)) {
        m_error = f.errorString();
        return false;
    }
    f.setPermissions(QFile::ReadOwner | QFile::WriteOwner);
    f.write(header);
    f.write(nonce);
    f.write(sealed);
    if (!f.commit()) {
        m_error = f.errorString();
        return false;
    }
    return true;
}
