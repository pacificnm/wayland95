#pragma once

#include <QByteArray>
#include <QObject>
#include <QPointer>
#include <QSslSocket>
#include <QStringList>
#include <QTimer>

#include <functional>

// Sends one message over SMTP (RFC 5321): SSL (465), STARTTLS (587) or
// plain, AUTH PLAIN or LOGIN, then MAIL FROM / RCPT TO / DATA. One Smtp
// per message; it deletes itself when done.
class Smtp : public QObject
{
    Q_OBJECT
public:
    struct Server {
        QString host;
        int port = 587;
        QString security = QStringLiteral("starttls");
        QString user, password;
    };
    using Done = std::function<void(bool ok, const QString &error)>;

    static void send(const Server &server, const QString &from, const QStringList &recipients, const QByteArray &message,
                     const Done &done);

private:
    explicit Smtp(QObject *parent = nullptr);
    void start();
    void readData();
    void reply(int code, const QByteArray &text);
    void write(const QByteArray &line);
    void finish(bool ok, const QString &error);

    enum class Step { Greeting, Ehlo, StartTls, Ehlo2, AuthPlain, AuthLogin, AuthUser, AuthPass, From, To, Data, Body, Quit };
    Server m_server;
    QString m_from;
    QStringList m_to;
    int m_toIndex = 0;
    QByteArray m_message;
    Done m_done;
    QPointer<QSslSocket> m_socket;
    QByteArray m_buf, m_lines;
    Step m_step = Step::Greeting;
    QByteArray m_caps;
    QTimer m_timeout;
    bool m_finished = false;
};
