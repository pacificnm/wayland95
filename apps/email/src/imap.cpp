#include "imap.h"

#include <QRegularExpression>
#include <QSslConfiguration>

namespace {
constexpr int kTimeoutMs = 60000;
}

QMap<QByteArray, Imap::Token> Imap::Token::pairs() const
{
    QMap<QByteArray, Token> out;
    for (int i = 0; i + 1 < list.size(); i += 2)
        out.insert(list.at(i).text.toUpper(), list.at(i + 1));
    return out;
}

Imap::Imap(QObject *parent)
    : QObject(parent)
{
    m_timeout.setSingleShot(true);
    m_timeout.setInterval(kTimeoutMs);
    connect(&m_timeout, &QTimer::timeout, this, [this] { fail(QStringLiteral("The mail server stopped answering.")); });
}

Imap::~Imap()
{
    if (m_socket)
        m_socket->abort();
}

// ------------------------------------------------------------- connecting

void Imap::open(const QString &host, int port, const QString &security, const QString &user, const QString &password,
                const Done &done)
{
    close();
    m_socket = new QSslSocket(this);
    QSslSocket *s = m_socket;
    trustTestCa(s);
    connect(s, &QSslSocket::readyRead, this, &Imap::readData);
    connect(s, &QSslSocket::errorOccurred, this, [this, s](QAbstractSocket::SocketError) {
        if (s == m_socket)
            fail(s->errorString());
    });
    connect(s, &QSslSocket::sslErrors, this, [this, s](const QList<QSslError> &errors) {
        if (s == m_socket && !errors.isEmpty())
            fail(QStringLiteral("The server's certificate isn't right: %1").arg(errors.first().errorString()));
    });
    connect(s, &QSslSocket::disconnected, this, [this, s] {
        if (s == m_socket && m_loggedIn)
            fail(QStringLiteral("The mail server closed the connection."));
    });
    auto afterTls = [this, user, password, done]() { login(user, password, done); };
    m_greeting = [this, security, afterTls, done](const QByteArray &line) {
        if (!line.startsWith("* OK") && !line.startsWith("* PREAUTH")) {
            done(false, QStringLiteral("The mail server didn't say hello: %1").arg(QString::fromUtf8(line)));
            close();
            return;
        }
        if (security == QLatin1String("starttls")) {
            command("STARTTLS", [this, afterTls, done](bool ok, const QByteArray &status, const QList<QList<Token>> &) {
                if (!ok) {
                    done(false, QStringLiteral("The server can't encrypt the connection (STARTTLS): %1").arg(QString::fromUtf8(status)));
                    close();
                    return;
                }
                connect(m_socket, &QSslSocket::encrypted, this, afterTls, Qt::SingleShotConnection);
                m_socket->startClientEncryption();
            });
            return;
        }
        afterTls();
    };
    m_timeout.start();
    // (a failure before logging in goes to done, through fail's queue: none
    // yet, so it's kept here)
    connect(this, &Imap::lost, this, [done](const QString &err) { done(false, err); }, Qt::SingleShotConnection);
    if (security == QLatin1String("ssl"))
        s->connectToHostEncrypted(host, quint16(port));
    else
        s->connectToHost(host, quint16(port));
}

void Imap::login(const QString &user, const QString &password, const Done &done)
{
    QObject::disconnect(this, &Imap::lost, this, nullptr);
    command("LOGIN " + quote(user.toUtf8()) + ' ' + quote(password.toUtf8()),
            [this, done](bool ok, const QByteArray &status, const QList<QList<Token>> &) {
        if (!ok) {
            QString why = QString::fromUtf8(status);
            why.remove(QRegularExpression(QStringLiteral("^\\[[^]]*\\]\\s*")));
            done(false, QStringLiteral("The mail server didn't accept the user name and password.%1")
                            .arg(why.isEmpty() ? QString() : QStringLiteral("\n\n(It said: %1)").arg(why)));
            close();
            return;
        }
        m_loggedIn = true;
        command("CAPABILITY", [this, done](bool, const QByteArray &, const QList<QList<Token>> &untagged) {
            for (const QList<Token> &t : untagged)
                if (t.size() > 1 && t.at(1).text.toUpper() == "CAPABILITY")
                    for (int i = 2; i < t.size(); ++i)
                        m_caps += ' ' + t.at(i).text.toUpper();
            m_caps += ' ';
            done(true, QString());
        });
    });
}

void Imap::close()
{
    m_loggedIn = false;
    m_greeting = nullptr;
    m_buf.clear();
    m_cur.clear();
    m_lits.clear();
    m_untagged.clear();
    m_queue.clear();
    m_busy = false;
    m_timeout.stop();
    m_caps.clear();
    if (m_socket) {
        QSslSocket *s = m_socket;
        m_socket = nullptr;
        s->disconnect(this);
        s->abort();
        s->deleteLater();
    }
}

void Imap::fail(const QString &error)
{
    // everything waiting fails with it
    QList<Command> waiting;
    if (m_busy)
        waiting << m_active;
    waiting << m_queue;
    close();
    for (const Command &c : waiting)
        if (c.done)
            c.done(false, error.toUtf8(), {});
    emit lost(error);
}

// --------------------------------------------------------------- commands

void Imap::command(const QByteArray &text, const std::function<void(bool, const QByteArray &, const QList<QList<Token>> &)> &done,
                   const QByteArray &literal)
{
    Command c;
    c.tag = 'a' + QByteArray::number(++m_tag);
    c.text = text;
    c.literal = literal;
    c.done = done;
    m_queue.enqueue(c);
    sendNext();
}

void Imap::sendNext()
{
    if (m_busy || m_queue.isEmpty() || !m_socket)
        return;
    m_active = m_queue.dequeue();
    m_busy = true;
    m_untagged.clear();
    m_timeout.start();
    m_socket->write(m_active.tag + ' ' + m_active.text + "\r\n");
}

void Imap::readData()
{
    if (!m_socket)
        return;
    m_buf += m_socket->readAll();
    static const QRegularExpression literalAtEnd(QStringLiteral("\\{(\\d+)\\+?\\}$"));
    while (m_socket) {
        const int nl = int(m_buf.indexOf("\r\n"));
        if (nl < 0)
            break;
        const QByteArray line = m_buf.left(nl);
        const QRegularExpressionMatch m = literalAtEnd.match(QString::fromLatin1(line));
        if (m.hasMatch()) {
            const int n = m.captured(1).toInt();
            if (m_buf.size() < nl + 2 + n)
                break; // the rest of the literal is still coming
            // a placeholder where the literal was; the bytes kept aside
            m_cur += line.left(m.capturedStart()) + '\x01' + QByteArray::number(m_lits.size()) + '\x01';
            m_lits << m_buf.mid(nl + 2, n);
            m_buf.remove(0, nl + 2 + n);
            continue;
        }
        m_cur += line;
        m_buf.remove(0, nl + 2);
        const QByteArray whole = m_cur;
        const QList<QByteArray> lits = m_lits;
        m_cur.clear();
        m_lits.clear();
        handle(whole, lits);
    }
}

void Imap::handle(const QByteArray &line, const QList<QByteArray> &literals)
{
    if (m_greeting && line.startsWith("* ")) {
        auto g = m_greeting;
        m_greeting = nullptr;
        m_timeout.stop();
        g(line);
        return;
    }
    if (line.startsWith('+')) {
        // go on: the literal (APPEND's message)
        if (m_busy && !m_active.literal.isEmpty() && !m_active.literalSent && m_socket) {
            m_active.literalSent = true;
            m_socket->write(m_active.literal + "\r\n");
        }
        return;
    }
    const QList<Token> tokens = tokenize(line, literals);
    if (tokens.isEmpty())
        return;
    if (tokens.first().text == "*") {
        if (tokens.size() > 1 && tokens.at(1).text.toUpper() == "BYE" && !m_busy) {
            fail(QStringLiteral("The mail server closed the connection."));
            return;
        }
        m_untagged << tokens;
        return;
    }
    if (!m_busy || tokens.first().text != m_active.tag)
        return;
    const QByteArray status = tokens.value(1).text.toUpper();
    const int sp = int(line.indexOf(' ', line.indexOf(' ') + 1));
    const QByteArray rest = sp > 0 ? line.mid(sp + 1) : QByteArray();
    Command c = m_active;
    const QList<QList<Token>> untagged = m_untagged;
    m_busy = false;
    m_untagged.clear();
    m_timeout.stop();
    if (c.done)
        c.done(status == "OK", rest, untagged);
    sendNext();
}

QList<Imap::Token> Imap::tokenize(const QByteArray &s, const QList<QByteArray> &literals) const
{
    int pos = 0;
    std::function<Token()> one = [&]() -> Token {
        while (pos < s.size() && s.at(pos) == ' ')
            ++pos;
        Token t;
        if (pos >= s.size())
            return t;
        const char c = s.at(pos);
        if (c == '(') {
            ++pos;
            t.kind = Token::List;
            while (pos < s.size()) {
                while (pos < s.size() && s.at(pos) == ' ')
                    ++pos;
                if (pos < s.size() && s.at(pos) == ')') {
                    ++pos;
                    break;
                }
                if (pos >= s.size())
                    break;
                t.list << one();
            }
            return t;
        }
        if (c == '"') {
            ++pos;
            t.kind = Token::String;
            while (pos < s.size() && s.at(pos) != '"') {
                if (s.at(pos) == '\\' && pos + 1 < s.size())
                    ++pos;
                t.text += s.at(pos++);
            }
            ++pos;
            return t;
        }
        if (c == '\x01') {
            const int end = int(s.indexOf('\x01', pos + 1));
            t.kind = Token::String;
            t.text = literals.value(s.mid(pos + 1, end - pos - 1).toInt());
            pos = end + 1;
            return t;
        }
        // an atom, with [...] in it taken whole (BODY[HEADER.FIELDS (FROM)])
        int depth = 0;
        const int start = pos;
        while (pos < s.size()) {
            const char d = s.at(pos);
            if (d == '[')
                ++depth;
            else if (d == ']')
                --depth;
            else if (depth == 0 && (d == ' ' || d == '(' || d == ')'))
                break;
            ++pos;
        }
        t.text = s.mid(start, pos - start);
        t.kind = t.text.toUpper() == "NIL" ? Token::Nil : Token::Atom;
        return t;
    };
    QList<Token> out;
    while (pos < s.size()) {
        while (pos < s.size() && s.at(pos) == ' ')
            ++pos;
        if (pos >= s.size())
            break;
        out << one();
    }
    return out;
}

// ------------------------------------------------------------ operations

void Imap::list(const std::function<void(bool, const QList<Folder> &, const QString &)> &done)
{
    command("LIST \"\" \"*\"", [done](bool ok, const QByteArray &status, const QList<QList<Token>> &untagged) {
        QList<Folder> folders;
        for (const QList<Token> &t : untagged) {
            if (t.size() < 5 || t.at(1).text.toUpper() != "LIST")
                continue;
            Folder f;
            for (const Token &a : t.at(2).list)
                f.attributes << a.text.toLower();
            f.delimiter = t.at(3).text;
            f.raw = t.at(4).text;
            f.name = decodeFolderName(f.raw);
            folders << f;
        }
        done(ok, folders, ok ? QString() : QString::fromUtf8(status));
    });
}

void Imap::select(const QByteArray &folder, const std::function<void(bool, quint32, quint32, const QString &)> &done)
{
    command("SELECT " + quote(folder), [done](bool ok, const QByteArray &status, const QList<QList<Token>> &untagged) {
        quint32 exists = 0, validity = 0;
        for (const QList<Token> &t : untagged) {
            if (t.size() >= 3 && t.at(2).text.toUpper() == "EXISTS")
                exists = t.at(1).text.toUInt();
            for (const Token &x : t) {
                // [UIDVALIDITY 123] (one atom: brackets are kept whole)
                const QByteArray u = x.text.toUpper();
                if (u.startsWith("[UIDVALIDITY "))
                    validity = u.mid(13, u.indexOf(']') - 13).trimmed().toUInt();
            }
        }
        done(ok, exists, validity, ok ? QString() : QString::fromUtf8(status));
    });
}

void Imap::fetchHeaders(quint32 exists, int count, const std::function<void(bool, const QList<Header> &, const QString &)> &done)
{
    if (exists == 0) {
        done(true, {}, QString());
        return;
    }
    const quint32 from = exists > quint32(count) ? exists - quint32(count) + 1 : 1;
    command("FETCH " + QByteArray::number(from) + ":* (UID FLAGS RFC822.SIZE BODY.PEEK[HEADER.FIELDS "
            "(FROM TO CC REPLY-TO SUBJECT DATE MESSAGE-ID CONTENT-TYPE)])",
            [done](bool ok, const QByteArray &status, const QList<QList<Token>> &untagged) {
        QList<Header> out;
        for (const QList<Token> &t : untagged) {
            if (t.size() < 4 || t.at(2).text.toUpper() != "FETCH")
                continue;
            const QMap<QByteArray, Token> p = t.at(3).pairs();
            Header h;
            h.uid = p.value("UID").text.toUInt();
            for (const Token &f : p.value("FLAGS").list)
                h.flags << f.text;
            h.size = p.value("RFC822.SIZE").text.toLongLong();
            for (auto it = p.cbegin(); it != p.cend(); ++it)
                if (it.key().startsWith("BODY["))
                    h.headers = it.value().text;
            if (h.uid)
                out << h;
        }
        done(ok, out, ok ? QString() : QString::fromUtf8(status));
    });
}

void Imap::status(const QByteArray &folder, const std::function<void(bool, const QMap<QByteArray, quint32> &)> &done)
{
    command("STATUS " + quote(folder) + " (MESSAGES UNSEEN UIDNEXT)",
            [done](bool ok, const QByteArray &, const QList<QList<Token>> &untagged) {
        QMap<QByteArray, quint32> values;
        for (const QList<Token> &t : untagged) {
            if (t.size() < 4 || t.at(1).text.toUpper() != "STATUS")
                continue;
            const QMap<QByteArray, Token> p = t.at(3).pairs();
            for (auto it = p.cbegin(); it != p.cend(); ++it)
                values.insert(it.key(), it.value().text.toUInt());
        }
        done(ok, values);
    });
}

void Imap::fetchMessage(quint32 uid, const std::function<void(bool, const QByteArray &, const QString &)> &done)
{
    command("UID FETCH " + QByteArray::number(uid) + " (BODY.PEEK[])",
            [done](bool ok, const QByteArray &status, const QList<QList<Token>> &untagged) {
        QByteArray raw;
        for (const QList<Token> &t : untagged) {
            if (t.size() < 4 || t.at(2).text.toUpper() != "FETCH")
                continue;
            const QMap<QByteArray, Token> p = t.at(3).pairs();
            for (auto it = p.cbegin(); it != p.cend(); ++it)
                if (it.key().startsWith("BODY["))
                    raw = it.value().text;
        }
        if (ok && raw.isEmpty())
            done(false, raw, QStringLiteral("The message isn't on the server any more."));
        else
            done(ok, raw, ok ? QString() : QString::fromUtf8(status));
    });
}

QByteArray Imap::uidSet(const QList<quint32> &uids)
{
    QList<QByteArray> s;
    for (quint32 u : uids)
        s << QByteArray::number(u);
    return s.join(',');
}

void Imap::store(const QList<quint32> &uids, bool add, const QByteArray &flags, const Done &done)
{
    command("UID STORE " + uidSet(uids) + (add ? " +FLAGS.SILENT (" : " -FLAGS.SILENT (") + flags + ")",
            [done](bool ok, const QByteArray &status, const QList<QList<Token>> &) {
        if (done)
            done(ok, ok ? QString() : QString::fromUtf8(status));
    });
}

void Imap::expungeDeleted(const QList<quint32> &uids, const Done &done)
{
    // only those (UIDPLUS); otherwise every message marked deleted goes
    const QByteArray cmd = m_caps.contains(" UIDPLUS ") ? "UID EXPUNGE " + uidSet(uids) : QByteArray("EXPUNGE");
    command(cmd, [done](bool ok, const QByteArray &status, const QList<QList<Token>> &) {
        if (done)
            done(ok, ok ? QString() : QString::fromUtf8(status));
    });
}

void Imap::move(const QList<quint32> &uids, const QByteArray &to, const Done &done)
{
    if (m_caps.contains(" MOVE ")) {
        command("UID MOVE " + uidSet(uids) + ' ' + quote(to), [done](bool ok, const QByteArray &status, const QList<QList<Token>> &) {
            done(ok, ok ? QString() : QString::fromUtf8(status));
        });
        return;
    }
    command("UID COPY " + uidSet(uids) + ' ' + quote(to), [this, uids, done](bool ok, const QByteArray &status, const QList<QList<Token>> &) {
        if (!ok) {
            done(false, QString::fromUtf8(status));
            return;
        }
        store(uids, true, "\\Deleted", [this, uids, done](bool ok2, const QString &err) {
            if (!ok2) {
                done(false, err);
                return;
            }
            expungeDeleted(uids, done);
        });
    });
}

void Imap::append(const QByteArray &folder, const QByteArray &flags, const QByteArray &message, const Done &done)
{
    command("APPEND " + quote(folder) + " (" + flags + ") {" + QByteArray::number(message.size()) + "}",
            [done](bool ok, const QByteArray &status, const QList<QList<Token>> &) {
        if (done)
            done(ok, ok ? QString() : QString::fromUtf8(status));
    }, message);
}

// ---------------------------------------------------------- folder names

void Imap::trustTestCa(QSslSocket *socket)
{
    const QString ca = qEnvironmentVariable("W95_EMAIL_TEST_CA");
    if (ca.isEmpty())
        return;
    QSslConfiguration conf = socket->sslConfiguration();
    conf.addCaCertificates(ca);
    socket->setSslConfiguration(conf);
}

QByteArray Imap::quote(const QByteArray &s)
{
    QByteArray out = s;
    out.replace('\\', "\\\\").replace('"', "\\\"");
    return '"' + out + '"';
}

QString Imap::decodeFolderName(const QByteArray &m)
{
    // modified UTF-7 (RFC 3501 5.1.3): &...- is base64 (with , for /) of UTF-16BE
    QString out;
    for (int i = 0; i < m.size(); ++i) {
        if (m.at(i) != '&') {
            out += QLatin1Char(m.at(i));
            continue;
        }
        const int end = int(m.indexOf('-', i));
        if (end < 0) {
            out += QString::fromLatin1(m.mid(i));
            break;
        }
        if (end == i + 1) {
            out += QLatin1Char('&');
        } else {
            QByteArray b64 = m.mid(i + 1, end - i - 1).replace(',', '/');
            while (b64.size() % 4)
                b64 += '=';
            const QByteArray utf16 = QByteArray::fromBase64(b64);
            for (int k = 0; k + 1 < utf16.size(); k += 2)
                out += QChar(ushort((uchar(utf16.at(k)) << 8) | uchar(utf16.at(k + 1))));
        }
        i = end;
    }
    return out;
}

QByteArray Imap::encodeFolderName(const QString &name)
{
    QByteArray out;
    QByteArray run;
    auto flush = [&]() {
        if (run.isEmpty())
            return;
        QByteArray b = run.toBase64().replace('/', ',');
        while (b.endsWith('='))
            b.chop(1);
        out += '&' + b + '-';
        run.clear();
    };
    for (QChar c : name) {
        const ushort u = c.unicode();
        if (u >= 0x20 && u <= 0x7e) {
            flush();
            out += u == '&' ? QByteArray("&-") : QByteArray(1, char(u));
        } else {
            run += char(u >> 8);
            run += char(u & 0xff);
        }
    }
    flush();
    return out;
}
