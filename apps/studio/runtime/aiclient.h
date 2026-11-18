#pragma once

#include <QByteArray>
#include <QNetworkAccessManager>
#include <QObject>
#include <QPointer>
#include <QVariantList>
#include <QVariantMap>
#include <QtQmlIntegration/qqmlintegration.h>

#include <QNetworkReply>

// Claude, for the AIChat element: one request of Anthropic's Messages API
// at a time, streamed (Server-Sent Events), put back together as the
// message the API would have answered without streaming.
//
//   client.send({ model, max_tokens, system, messages, tools, ... })
//     textDelta(index, text)      a piece of a text block, as it comes
//     blockStarted(index, block)  a block began ({ type: "tool_use", name... })
//     finished(message)           { id, model, content: [...], stop_reason,
//                                   stop_details, usage, invalidInputs }
//     failed(message, status, type)
//
// A tool_use block's input is parsed when the block ends; input that isn't
// a JSON object is given as {} and its text kept in invalidInputs (tool_use
// id -> text), to answer with an error rather than run the tool.
//
// The key is the project's (Project > Properties: `ai.apiKey`, which the
// runner gives setProjectKey), or apiKey when it's set (the Studio's Test);
// `${env:NAME}` in either is that environment variable; blank is
// ANTHROPIC_API_KEY. ANTHROPIC_BASE_URL points it at another server (the
// tests' mock).
class AiClient : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QString apiKey READ apiKey WRITE setApiKey NOTIFY keyChanged)
    Q_PROPERTY(bool hasKey READ hasKey NOTIFY keyChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)

public:
    explicit AiClient(QObject *parent = nullptr);
    ~AiClient() override;

    QString apiKey() const { return m_apiKey; }
    void setApiKey(const QString &key);
    bool hasKey() const { return !key().isEmpty(); }
    // the project's key, for every chat in the app (the runner sets it)
    static void setProjectKey(const QString &key);
    bool busy() const { return m_reply != nullptr || m_retrying; }

    // start a request (`stream` is set here); false if one is under way
    Q_INVOKABLE bool send(const QVariantMap &body);
    Q_INVOKABLE void cancel();
    // whether the key works: asks Anthropic for its models (which costs
    // nothing), then tested(ok, message)
    Q_INVOKABLE void testKey();
    // `${env:NAME}` replaced by that variable
    Q_INVOKABLE static QString resolved(const QString &text);
    // ANTHROPIC_BASE_URL is set: requests go to another server (a proxy,
    // the tests' mock), which may not know the newest request fields
    Q_INVOKABLE static bool customServer() { return !qEnvironmentVariableIsEmpty("ANTHROPIC_BASE_URL"); }

signals:
    void keyChanged();
    void busyChanged();
    void textDelta(int index, const QString &text);
    void blockStarted(int index, const QVariantMap &block);
    void finished(const QVariantMap &message);
    void failed(const QString &message, int status, const QString &type);
    void tested(bool ok, const QString &message);

private:
    QString key() const;
    QString baseUrl() const;
    void start();
    void readEvents();
    void event(const QByteArray &name, const QVariantMap &data);
    void done();
    void fail(const QString &message, int status = 0, const QString &type = QString());

    QNetworkAccessManager m_net;
    QString m_apiKey;
    QByteArray m_body;
    bool m_fallbacks = false;
    QPointer<QNetworkReply> m_reply;
    QByteArray m_buffer;
    int m_attempt = 0;
    bool m_retrying = false;
    bool m_gotEvents = false;
    bool m_ended = false;

    // the message as it comes
    QVariantMap m_message;
    QVariantList m_content;
    QHash<int, QString> m_partialJson;
    QVariantMap m_invalid;
};
