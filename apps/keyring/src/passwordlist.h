#pragma once

#include <QByteArray>
#include <QList>
#include <QMap>
#include <QString>

// The password list: everything programs keep in the keyring, in one file,
// ~/.local/share/wayland95/passwords.pwl (like Windows 95's .PWL, unlocked
// by your logon password), mode 0600.
//
// The file:
//   "W95PWL" 0 1            magic and version
//   logN r p 0              scrypt's cost (N = 2^logN)
//   salt[16]
//   u32 length, index       not encrypted: the collections, their item
//                           numbers and each item's attribute hashes, so a
//                           locked list can still answer "is there one
//                           for this?" (the spec's SearchItems) without
//                           giving away what's in it
//   nonce[12]
//   AES-256-GCM(everything) the labels, attributes and secrets; the bytes
//                           before the nonce are authenticated with it
//
// A collection named "login" (alias "default") is made with the list.
struct KeyringItem {
    quint32 id = 0;
    QString label;
    QMap<QString, QString> attributes;
    QByteArray secret;
    QString contentType = QStringLiteral("text/plain");
    qint64 created = 0;
    qint64 modified = 0;
    // sha256(salt, name, value) per attribute: what a locked item has
    QList<QByteArray> hashes;
};

struct KeyringCollection {
    QString name; // the object path's last part
    QString label;
    qint64 created = 0;
    qint64 modified = 0;
    quint32 nextId = 1;
    QMap<quint32, KeyringItem> items;
};

class PasswordList
{
public:
    enum State { NoFile, Locked, Unlocked };

    explicit PasswordList(const QString &path);
    ~PasswordList();

    QString path() const { return m_path; }
    State state() const { return m_state; }

    // reads the file's index (Locked), or finds there's none (NoFile)
    void load();
    // a new, empty list with this password (NoFile -> Unlocked)
    bool create(const QByteArray &password);
    // false: wrong password (or a damaged file)
    bool unlock(const QByteArray &password);
    // forgets the secrets until the next unlock
    void lock();
    // does this password open it (without unlocking)?
    bool checkPassword(const QByteArray &password) const;
    // encrypts it with another password from now on (Unlocked)
    bool setPassword(const QByteArray &password);
    // after changes (Unlocked)
    bool save();

    QMap<QString, KeyringCollection> collections;
    QMap<QString, QString> aliases; // alias -> collection name

    // an item's hashes for these attributes (to match a locked item)
    QList<QByteArray> hashes(const QMap<QString, QString> &attributes) const;
    void rehash(KeyringItem &item) const { item.hashes = hashes(item.attributes); }

    QString lastError() const { return m_error; }

private:
    bool readFile(QByteArray *header, QByteArray *nonce, QByteArray *sealed);
    void readIndex(const QByteArray &index);

    QString m_path;
    State m_state = NoFile;
    QByteArray m_salt;
    QByteArray m_key; // scrypt's, while unlocked
    int m_logN = 15, m_r = 8, m_p = 1;
    QString m_error;
};
