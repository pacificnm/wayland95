#include "smtp.h"

#include <QHostInfo>

#include "imap.h"

Smtp::Smtp(QObject *parent)
    : QObject(parent)
{
    m_timeout.setSingleShot(true);
    m_timeout.setInterval(60000);
    connect(&m_timeout, &QTimer::timeout, this, [this] { finish(false, QStringLiteral("The outgoing mail server stopped answering.")); });
}

void Smtp::send(const Server &server, const QString &from, const QStringList &recipients, const QByteArray &message,
                const Done &done)
{
    auto *s = new Smtp;
    s->m_server = server;
    s->m_from = from;
    s->m_to = recipients;
    s->m_message = message;
    s->m_done = done;
    s->start();
}

void Smtp::start()
{
    m_socket = new QSslSocket(this);
    Imap::trustTestCa(m_socket);
    connect(m_socket, &QSslSocket::readyRead, this, &Smtp::readData);
    connect(m_socket, &QSslSocket::errorOccurred, this, [this](QAbstractSocket::SocketError) {
        if (!m_finished && m_step != Step::Quit)
            finish(false, m_socket->errorString());
    });
    connect(m_socket, &QSslSocket::sslErrors, this, [this](const QList<QSslError> &errors) {
        if (!errors.isEmpty())
            finish(false, QStringLiteral("The outgoing server's certificate isn't right: %1").arg(errors.first().errorString()));
    });
    m_timeout.start();
    if (m_server.security == QLatin1String("ssl"))
        m_socket->connectToHostEncrypted(m_server.host, quint16(m_server.port));
    else
        m_socket->connectToHost(m_server.host, quint16(m_server.port));
}

void Smtp::write(const QByteArray &line)
{
    m_timeout.start();
    if (m_socket)
        m_socket->write(line + "\r\n");
}

void Smtp::readData()
{
    m_buf += m_socket->readAll();
    int nl;
    while ((nl = int(m_buf.indexOf("\r\n"))) >= 0) {
        const QByteArray line = m_buf.left(nl);
        m_buf.remove(0, nl + 2);
        m_lines += line.mid(4) + '\n';
        // "250-..." goes on; "250 ..." is the last line of the reply
        if (line.size() >= 4 && line.at(3) == '-')
            continue;
        const QByteArray text = m_lines.trimmed();
        m_lines.clear();
        reply(line.left(3).toInt(), text);
        if (m_finished)
            return;
    }
}

void Smtp::reply(int code, const QByteArray &text)
{
    auto bad = [&](const QString &what) {
        finish(false, QStringLiteral("%1\n\n(The server said: %2 %3)").arg(what).arg(code).arg(QString::fromUtf8(text)));
    };
    const QByteArray helo = "EHLO " + (QHostInfo::localHostName().isEmpty() ? QByteArray("localhost") : QHostInfo::localHostName().toUtf8());
    switch (m_step) {
    case Step::Greeting:
        if (code != 220) return bad(QStringLiteral("The outgoing mail server didn't say hello."));
        m_step = Step::Ehlo;
        write(helo);
        break;
    case Step::Ehlo:
    case Step::Ehlo2:
        if (code != 250) return bad(QStringLiteral("The outgoing mail server didn't answer EHLO."));
        m_caps = text.toUpper();
        if (m_step == Step::Ehlo && m_server.security == QLatin1String("starttls")) {
            m_step = Step::StartTls;
            write("STARTTLS");
            break;
        }
        if (m_server.user.isEmpty()) {
            m_step = Step::From;
            write("MAIL FROM:<" + m_from.toUtf8() + ">");
        } else if (m_caps.contains("PLAIN")) {
            m_step = Step::AuthPlain;
            write("AUTH PLAIN " + (QByteArray(1, '\0') + m_server.user.toUtf8() + QByteArray(1, '\0') + m_server.password.toUtf8()).toBase64());
        } else {
            m_step = Step::AuthLogin;
            write("AUTH LOGIN");
        }
        break;
    case Step::StartTls:
        if (code != 220) return bad(QStringLiteral("The outgoing server can't encrypt the connection (STARTTLS)."));
        connect(m_socket, &QSslSocket::encrypted, this, [this, helo] {
            m_step = Step::Ehlo2;
            write(helo);
        }, Qt::SingleShotConnection);
        m_socket->startClientEncryption();
        break;
    case Step::AuthLogin:
        if (code != 334) return bad(QStringLiteral("The outgoing server doesn't take a password this way."));
        m_step = Step::AuthUser;
        write(m_server.user.toUtf8().toBase64());
        break;
    case Step::AuthUser:
        if (code != 334) return bad(QStringLiteral("The outgoing server didn't accept the user name."));
        m_step = Step::AuthPass;
        write(m_server.password.toUtf8().toBase64());
        break;
    case Step::AuthPlain:
    case Step::AuthPass:
        if (code != 235) return bad(QStringLiteral("The outgoing mail server didn't accept the user name and password."));
        m_step = Step::From;
        write("MAIL FROM:<" + m_from.toUtf8() + ">");
        break;
    case Step::From:
        if (code != 250) return bad(QStringLiteral("The outgoing server didn't accept you as the sender."));
        m_step = Step::To;
        m_toIndex = 0;
        write("RCPT TO:<" + m_to.value(0).toUtf8() + ">");
        break;
    case Step::To:
        if (code != 250 && code != 251)
            return bad(QStringLiteral("The outgoing server didn't accept %1.").arg(m_to.value(m_toIndex)));
        if (++m_toIndex < m_to.size()) {
            write("RCPT TO:<" + m_to.at(m_toIndex).toUtf8() + ">");
            break;
        }
        m_step = Step::Data;
        write("DATA");
        break;
    case Step::Data: {
        if (code != 354) return bad(QStringLiteral("The outgoing server wouldn't take the message."));
        m_step = Step::Body;
        // lines starting with "." get another one (the end is a line of just ".")
        QByteArray body = m_message;
        body.replace("\r\n.", "\r\n..");
        if (body.startsWith('.'))
            body.prepend('.');
        if (!body.endsWith("\r\n"))
            body += "\r\n";
        m_timeout.start();
        m_socket->write(body + ".\r\n");
        break;
    }
    case Step::Body:
        if (code != 250) return bad(QStringLiteral("The outgoing server didn't send the message."));
        m_step = Step::Quit;
        write("QUIT");
        finish(true, QString());
        break;
    case Step::Quit:
        break;
    }
}

void Smtp::finish(bool ok, const QString &error)
{
    if (m_finished)
        return;
    m_finished = true;
    m_timeout.stop();
    if (m_done)
        m_done(ok, error);
    if (m_socket)
        m_socket->disconnectFromHost();
    deleteLater();
}
