#pragma once

#include <QByteArray>
#include <QList>
#include <QObject>
#include <QPointer>
#include <QQueue>
#include <QSslSocket>
#include <QTimer>
#include <QVariant>

#include <functional>

// One IMAP4rev1 connection (RFC 3501): connect (SSL, STARTTLS or plain),
// log in, then commands one at a time from a queue, each answered by a
// callback. Responses are parsed into Tokens (atoms, strings, literals,
// lists), which is all IMAP has.
class Imap : public QObject
{
    Q_OBJECT
public:
    struct Token {
        enum Kind { Atom, String, List, Nil } kind = Nil;
        QByteArray text;
        QList<Token> list;
        bool isList() const { return kind == List; }
        // (FLAGS (\Seen) UID 4 ...) as KEY -> value
        QMap<QByteArray, Token> pairs() const;
    };
    struct Folder {
        QString name;      // decoded ("Sent Items")
        QByteArray raw;    // as the server names it (modified UTF-7)
        QByteArray delimiter;
        QList<QByteArray> attributes; // \Noselect, \Sent, \Trash, \Drafts, \Junk...
    };
    struct Header {
        quint32 uid = 0;
        QList<QByteArray> flags;
        qint64 size = 0;
        QByteArray headers; // the header block asked for
    };
    using Done = std::function<void(bool ok, const QString &error)>;

    explicit Imap(QObject *parent = nullptr);
    ~Imap() override;

    // security: "ssl" (993), "starttls" (143) or "none"
    void open(const QString &host, int port, const QString &security, const QString &user, const QString &password,
              const Done &done);
    void close();
    bool isOpen() const { return m_loggedIn; }
    QByteArray capabilities() const { return m_caps; }

    void list(const std::function<void(bool ok, const QList<Folder> &folders, const QString &error)> &done);
    // exists: how many messages; uidNext, uidValidity
    void select(const QByteArray &folder, const std::function<void(bool ok, quint32 exists, quint32 uidValidity,
                                                                   const QString &error)> &done);
    // the newest `count` messages' UIDs, flags, sizes and headers
    void fetchHeaders(quint32 exists, int count,
                      const std::function<void(bool ok, const QList<Header> &headers, const QString &error)> &done);
    // STATUS: { UNSEEN, MESSAGES, UIDNEXT... } of a folder (not the selected one)
    void status(const QByteArray &folder, const std::function<void(bool ok, const QMap<QByteArray, quint32> &values)> &done);
    void fetchMessage(quint32 uid, const std::function<void(bool ok, const QByteArray &raw, const QString &error)> &done);
    void store(const QList<quint32> &uids, bool add, const QByteArray &flags, const Done &done);
    // moves (MOVE, or COPY then delete and expunge)
    void move(const QList<quint32> &uids, const QByteArray &to, const Done &done);
    void expungeDeleted(const QList<quint32> &uids, const Done &done);
    void append(const QByteArray &folder, const QByteArray &flags, const QByteArray &message, const Done &done);

    static QString decodeFolderName(const QByteArray &mutf7);
    static QByteArray encodeFolderName(const QString &name);
    static QByteArray quote(const QByteArray &s);
    static QByteArray uidSet(const QList<quint32> &uids);
    // W95_EMAIL_TEST_CA: a certificate authority to trust as well (the
    // tests' stand-in server's); otherwise only the system's
    static void trustTestCa(QSslSocket *socket);

signals:
    void lost(const QString &error);

private:
    struct Command {
        QByteArray tag;
        QByteArray text;
        QByteArray literal;     // sent after the server's "+" (APPEND)
        bool literalSent = false;
        std::function<void(bool ok, const QByteArray &status, const QList<QList<Token>> &untagged)> done;
    };
    void command(const QByteArray &text, const std::function<void(bool, const QByteArray &, const QList<QList<Token>> &)> &done,
                 const QByteArray &literal = QByteArray());
    void sendNext();
    void readData();
    void handle(const QByteArray &line, const QList<QByteArray> &literals);
    void fail(const QString &error);
    QList<Token> tokenize(const QByteArray &line, const QList<QByteArray> &literals) const;
    void login(const QString &user, const QString &password, const Done &done);

    QPointer<QSslSocket> m_socket;
    QByteArray m_buf, m_cur;
    QList<QByteArray> m_lits;
    QQueue<Command> m_queue;
    Command m_active;
    bool m_busy = false;
    QList<QList<Token>> m_untagged;
    int m_tag = 0;
    bool m_loggedIn = false;
    QByteArray m_caps;
    std::function<void(const QByteArray &line)> m_greeting; // before login
    QTimer m_timeout;
};
