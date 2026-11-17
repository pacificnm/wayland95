#pragma once

#include <QHash>
#include <QJSValue>
#include <QObject>
#include <QPointer>
#include <QTimer>
#include <QVariantList>
#include <QVariantMap>
#include <QtQmlIntegration/qqmlintegration.h>

#include "imap.h"
#include "mime.h"

// Email's mail: accounts (IMAP to read, SMTP to send), their folders, the
// messages in the folder shown, the message shown, and what's done to them.
//
// Settings: ~/.config/wayland95/email.json (the accounts, never their
// passwords: those are in the keyring, see secrets.h). Messages read are
// kept in ~/.cache/wayland95/email (only you can read it), so a folder
// shows at once while it's brought up to date.
//
// Exposed to QML as the singleton `Mail`.
class MailStore : public QObject
{
    Q_OBJECT
    QML_NAMED_ELEMENT(Mail)
    QML_SINGLETON

    // [{ id, name, email, displayName, imapHost, imapPort, imapSecurity,
    //    smtpHost, smtpPort, smtpSecurity, user }]
    Q_PROPERTY(QVariantList accounts READ accounts NOTIFY accountsChanged)
    // the folder tree: [{ key, account, name, depth, kind (inbox, sent,
    //   drafts, trash, junk, account, other), unread, selectable }]
    Q_PROPERTY(QVariantList folders READ folders NOTIFY foldersChanged)
    Q_PROPERTY(QString folder READ folder NOTIFY folderChanged)
    // the folder's messages, newest first: [{ uid, from, fromEmail,
    //   subject, date (ms), size, seen, flagged, answered, attachment }]
    Q_PROPERTY(QVariantList messages READ messages NOTIFY messagesChanged)
    // the message shown: { uid, subject, from, to, cc, date, text, html,
    //   remoteImages, attachments: [{ index, name, size, type }] } or {}
    Q_PROPERTY(QVariantMap message READ message NOTIFY messageChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY statusChanged)
    Q_PROPERTY(QString status READ status NOTIFY statusChanged)
    // minutes between checks for new mail (0: never)
    Q_PROPERTY(int checkMinutes READ checkMinutes WRITE setCheckMinutes NOTIFY settingsChanged)

public:
    explicit MailStore(QObject *parent = nullptr);

    QVariantList accounts() const { return m_accounts; }
    QVariantList folders() const { return m_folders; }
    QString folder() const { return m_folder; }
    QVariantList messages() const { return m_messages; }
    QVariantMap message() const { return m_message; }
    bool busy() const { return m_busy > 0; }
    QString status() const { return m_status; }
    int checkMinutes() const { return m_checkMinutes; }
    void setCheckMinutes(int minutes);

    // --- accounts. Adding one connects first (to check it): done(ok, error)
    Q_INVOKABLE void saveAccount(const QVariantMap &account, const QString &password, bool remember, const QJSValue &done);
    Q_INVOKABLE void removeAccount(const QString &id);
    // a password asked for (passwordNeeded): remember keeps it in the keyring
    Q_INVOKABLE void providePassword(const QString &account, const QString &password, bool remember);
    Q_INVOKABLE void cancelPassword(const QString &account);
    // the usual servers for an address's provider (gmail.com...), or {}
    Q_INVOKABLE QVariantMap serversFor(const QString &email) const;

    // --- reading
    Q_INVOKABLE void selectFolder(const QString &key);
    Q_INVOKABLE void refresh(); // Send/Recv
    Q_INVOKABLE void openMessage(quint32 uid);
    Q_INVOKABLE void closeMessage();
    Q_INVOKABLE void setSeen(const QVariantList &uids, bool seen);
    Q_INVOKABLE void setFlagged(const QVariantList &uids, bool flagged);
    // to Deleted Items (from there: for good)
    Q_INVOKABLE void deleteMessages(const QVariantList &uids);
    Q_INVOKABLE void moveMessages(const QVariantList &uids, const QString &folderKey);
    Q_INVOKABLE QString saveAttachment(int index, const QString &path) const;
    Q_INVOKABLE bool openAttachment(int index) const;
    // the shown message as it came (File > Save As...)
    Q_INVOKABLE QString saveMessage(const QString &path) const;
    Q_INVOKABLE QString homePath() const;

    // --- writing: a compose window's fields { account, to, cc, bcc,
    //     subject, text, attachments: [paths], inReplyTo, references,
    //     replyUid, replyFolder }
    Q_INVOKABLE QVariantMap replyTo(bool all) const;
    Q_INVOKABLE QVariantMap forward() const;
    // sendFinished(id, ok, error) when it's sent (or not)
    Q_INVOKABLE void send(const QString &id, const QVariantMap &compose);
    Q_INVOKABLE void saveDraft(const QVariantMap &compose, const QJSValue &done);

signals:
    void accountsChanged();
    void foldersChanged();
    void folderChanged();
    void messagesChanged();
    void messageChanged();
    void statusChanged();
    void settingsChanged();
    void passwordNeeded(const QString &account, const QString &name, const QString &error);
    void sendFinished(const QString &id, bool ok, const QString &error);
    void error(const QString &title, const QString &message);

private:
    // One thing done on an account's connection at a time (its folder
    // selected first): so two never get each other's folder.
    using Next = std::function<void()>;
    struct Job {
        QByteArray folder;    // to SELECT first ("" for none)
        bool reselect = false; // even if it's the one selected (fresh counts)
        std::function<void(Imap *imap, const QString &error, const Next &next)> run;
    };
    struct Account {
        QVariantMap settings;
        QString id() const { return settings.value(QStringLiteral("id")).toString(); }
        QPointer<Imap> imap;
        QString password;
        bool connecting = false, asking = false;
        bool rememberPending = false; // keep the typed password once it's been accepted
        QList<std::function<void(Imap *imap, const QString &error)>> waiting; // for a connection
        QList<Imap::Folder> serverFolders;
        QByteArray selected; // the folder SELECTed on the connection
        quint32 exists = 0;
        QList<Job> jobs;
        bool running = false;
    };
    void job(const QString &accountId, const QByteArray &folder, bool reselect,
             const std::function<void(Imap *imap, const QString &error, const Next &next)> &run);
    void pump(const QString &accountId);
    void withPassword(const QString &accountId, const std::function<void(bool ok, const QString &password)> &then);
    void loadSettings();
    void saveSettings();
    Account *account(const QString &id);
    // a logged-in connection, then `then` (or the error)
    void withImap(const QString &accountId, const std::function<void(Imap *imap, const QString &error)> &then);
    void connectAccount(const QString &accountId);
    void flushWaiting(const QString &accountId, Imap *imap, const QString &error);
    void syncFolders(const QString &accountId);
    void syncMessages(const QString &key);
    void rebuildFolders();
    QString specialFolder(const QString &accountId, const QString &kind) const;
    void setStatus(const QString &status);
    void work(int delta);
    QString cacheDir(const QString &key) const;
    void cacheList(const QString &key) const;
    void loadCachedList(const QString &key);
    QVariantMap headerEntry(const Imap::Header &h) const;
    void showMessage(quint32 uid, const QByteArray &raw);
    void notifyNewMail(int count, const QString &from, const QString &subject);
    static QList<quint32> toUids(const QVariantList &list);
    static QString keyFor(const QString &account, const QByteArray &raw);
    static void splitKey(const QString &key, QString *account, QByteArray *raw);

    QVariantList m_accounts;
    QHash<QString, Account> m_state;
    QVariantList m_folders;
    QHash<QString, int> m_unread; // folder key -> unread
    QString m_folder;
    QVariantList m_messages;
    QVariantMap m_message;
    Mime::Message m_parsed;
    QByteArray m_raw;
    int m_busy = 0;
    QString m_status;
    int m_checkMinutes = 10;
    QTimer m_check;
    QHash<QString, quint32> m_uidNext; // inbox key -> UIDNEXT last seen, for "new mail"
    QHash<QString, QList<std::function<void(bool, const QString &)>>> m_passwordWaiters;
};
