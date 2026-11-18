#include "aiclient.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRegularExpression>
#include <QTimer>
#include <QUrl>

namespace {

const char kVersion[] = "2026-10-21";
const char kFallbackBeta[] = "server-side-fallback-2026-07-01";
const int kRetries = 2;

QString errorText(int status, const QString &type, const QString &said)
{
    const QString tail = said.isEmpty() ? QString() : QStringLiteral(" (Anthropic said: %1)").arg(said);
    if (status == 401 || type == QLatin1String("authentication_error"))
        return QStringLiteral("The API key wasn't accepted.") + tail;
    if (status == 403 || type == QLatin1String("permission_error"))
        return QStringLiteral("This API key isn't allowed to do that.") + tail;
    if (status == 404 || type == QLatin1String("not_found_error"))
        return QStringLiteral("Anthropic doesn't know that model.") + tail;
    if (status == 413 || type == QLatin1String("request_too_large"))
        return QStringLiteral("The conversation is too long to send: clear it and start again.") + tail;
    if (status == 429 || type == QLatin1String("rate_limit_error"))
        return QStringLiteral("Too many requests just now: try again in a moment.") + tail;
    if (status == 529 || type == QLatin1String("overloaded_error"))
        return QStringLiteral("Claude is busy just now: try again in a moment.") + tail;
    if (status >= 500 || type == QLatin1String("api_error"))
        return QStringLiteral("Anthropic's servers had a problem: try again in a moment.") + tail;
    if (!said.isEmpty())
        return QStringLiteral("Anthropic refused the request: %1").arg(said);
    return QStringLiteral("Anthropic refused the request (%1).").arg(status);
}

bool retryable(int status)
{
    return status == 0 || status == 408 || status == 429 || status == 500 || status == 502
        || status == 503 || status == 504 || status == 529;
}

} // namespace

AiClient::AiClient(QObject *parent)
    : QObject(parent)
{
}

AiClient::~AiClient()
{
    cancel();
}

void AiClient::setApiKey(const QString &key)
{
    if (key == m_apiKey)
        return;
    m_apiKey = key;
    emit keyChanged();
}

QString AiClient::resolved(const QString &text)
{
    static const QRegularExpression env(QStringLiteral("\\$\\{env:([A-Za-z_][A-Za-z0-9_]*)\\}"));
    QString out = text;
    QRegularExpressionMatch m;
    while ((m = env.match(out)).hasMatch())
        out.replace(m.capturedStart(), m.capturedLength(), qEnvironmentVariable(m.captured(1).toLatin1().constData()));
    return out.trimmed();
}

namespace {
QString g_projectKey;
}

void AiClient::setProjectKey(const QString &key)
{
    g_projectKey = key;
}

QString AiClient::key() const
{
    QString k = resolved(m_apiKey.isEmpty() ? g_projectKey : m_apiKey);
    if (k.isEmpty())
        k = qEnvironmentVariable("ANTHROPIC_API_KEY").trimmed();
    return k;
}

QString AiClient::baseUrl() const
{
    QString base = qEnvironmentVariable("ANTHROPIC_BASE_URL");
    if (base.isEmpty())
        base = QStringLiteral("https://api.anthropic.com");
    while (base.endsWith(QLatin1Char('/')))
        base.chop(1);
    return base;
}

void AiClient::testKey()
{
    const QString k = key();
    if (k.isEmpty()) {
        emit tested(false, QStringLiteral("There's no key: type one in, or set ANTHROPIC_API_KEY."));
        return;
    }
    QNetworkRequest req(QUrl(baseUrl() + QStringLiteral("/v1/models?limit=1")));
    req.setRawHeader("x-api-key", k.toUtf8());
    req.setRawHeader("anthropic-version", kVersion);
    req.setTransferTimeout(30000);
    QNetworkReply *reply = m_net.get(req);
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        reply->deleteLater();
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (status == 200) {
            emit tested(true, QStringLiteral("The key works."));
            return;
        }
        if (status == 0) {
            emit tested(false, QStringLiteral("Anthropic couldn't be reached: %1").arg(reply->errorString()));
            return;
        }
        const QVariantMap error = QJsonDocument::fromJson(reply->readAll()).object().toVariantMap().value(QStringLiteral("error")).toMap();
        emit tested(false, errorText(status, error.value(QStringLiteral("type")).toString(), error.value(QStringLiteral("message")).toString()));
    });
}

bool AiClient::send(const QVariantMap &body)
{
    if (busy())
        return false;
    QVariantMap b = body;
    b.insert(QStringLiteral("stream"), true);
    m_fallbacks = b.contains(QStringLiteral("fallbacks"));
    m_body = QJsonDocument::fromVariant(b).toJson(QJsonDocument::Compact);
    m_attempt = 0;
    start();
    return true;
}

void AiClient::start()
{
    m_retrying = false;
    m_buffer.clear();
    m_gotEvents = false;
    m_ended = false;
    m_message.clear();
    m_content.clear();
    m_partialJson.clear();
    m_invalid.clear();

    const QString k = key();
    if (k.isEmpty()) {
        fail(QStringLiteral("This app has no Anthropic API key (Project > Properties in Wayland Studio, or ANTHROPIC_API_KEY)."));
        return;
    }
    QNetworkRequest req(QUrl(baseUrl() + QStringLiteral("/v1/messages")));
    req.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    req.setRawHeader("Accept", "text/event-stream");
    req.setRawHeader("x-api-key", k.toUtf8());
    req.setRawHeader("anthropic-version", kVersion);
    if (m_fallbacks)
        req.setRawHeader("anthropic-beta", kFallbackBeta);
    // (an answer can think for a while; the stream's pings keep it going)
    req.setTransferTimeout(300000);

    m_reply = m_net.post(req, m_body);
    emit busyChanged();
    connect(m_reply, &QNetworkReply::readyRead, this, &AiClient::readEvents);
    connect(m_reply, &QNetworkReply::finished, this, [this, reply = m_reply]() {
        if (reply != m_reply)
            return;
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (status == 200) {
            readEvents();
            m_reply = nullptr;
            reply->deleteLater();
            if (!m_ended) {
                // (cut off part way, or the network let go)
                if (!m_gotEvents && m_attempt < kRetries) {
                    ++m_attempt;
                    m_retrying = true;
                    QTimer::singleShot(1000 * m_attempt, this, &AiClient::start);
                    return;
                }
                fail(reply->error() != QNetworkReply::NoError
                         ? QStringLiteral("The answer stopped part way: %1").arg(reply->errorString())
                         : QStringLiteral("The answer stopped part way (the connection closed)."));
            }
            return;
        }
        const QByteArray data = reply->readAll();
        const QString networkError = reply->errorString();
        int wait = reply->rawHeader("retry-after").toInt();
        m_reply = nullptr;
        reply->deleteLater();
        if (retryable(status) && m_attempt < kRetries) {
            ++m_attempt;
            m_retrying = true;
            wait = wait > 0 ? qMin(wait, 20) * 1000 : 1000 * m_attempt;
            QTimer::singleShot(wait, this, &AiClient::start);
            return;
        }
        if (status == 0) {
            fail(QStringLiteral("Anthropic couldn't be reached: %1").arg(networkError));
            return;
        }
        const QVariantMap error = QJsonDocument::fromJson(data).object().toVariantMap().value(QStringLiteral("error")).toMap();
        const QString type = error.value(QStringLiteral("type")).toString();
        fail(errorText(status, type, error.value(QStringLiteral("message")).toString()), status, type);
    });
}

void AiClient::cancel()
{
    m_retrying = false;
    if (m_reply) {
        QNetworkReply *reply = m_reply;
        m_reply = nullptr;
        reply->disconnect(this);
        reply->abort();
        reply->deleteLater();
    }
    emit busyChanged();
}

void AiClient::fail(const QString &message, int status, const QString &type)
{
    m_retrying = false;
    emit busyChanged();
    emit failed(message, status, type);
}

// Server-Sent Events: "event: name\ndata: {...}\n\n"
void AiClient::readEvents()
{
    if (!m_reply || m_reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt() != 200)
        return;
    m_buffer += m_reply->readAll();
    m_buffer.replace("\r\n", "\n");
    int end;
    while (!m_ended && (end = m_buffer.indexOf("\n\n")) >= 0) {
        const QByteArray chunk = m_buffer.left(end);
        m_buffer.remove(0, end + 2);
        QByteArray name, data;
        for (const QByteArray &line : chunk.split('\n')) {
            if (line.startsWith("event:"))
                name = line.mid(6).trimmed();
            else if (line.startsWith("data:"))
                data += (data.isEmpty() ? QByteArray() : QByteArray("\n")) + line.mid(5).trimmed();
        }
        if (data.isEmpty())
            continue;
        const QVariantMap parsed = QJsonDocument::fromJson(data).object().toVariantMap();
        if (name.isEmpty())
            name = parsed.value(QStringLiteral("type")).toString().toUtf8();
        m_gotEvents = true;
        event(name, parsed);
    }
}

void AiClient::event(const QByteArray &name, const QVariantMap &data)
{
    if (name == "message_start") {
        m_message = data.value(QStringLiteral("message")).toMap();
        m_content.clear();
    } else if (name == "content_block_start") {
        const int index = data.value(QStringLiteral("index")).toInt();
        const QVariantMap block = data.value(QStringLiteral("content_block")).toMap();
        while (m_content.size() <= index)
            m_content.append(QVariantMap());
        m_content[index] = block;
        emit blockStarted(index, block);
    } else if (name == "content_block_delta") {
        const int index = data.value(QStringLiteral("index")).toInt();
        if (index < 0 || index >= m_content.size())
            return;
        const QVariantMap delta = data.value(QStringLiteral("delta")).toMap();
        const QString type = delta.value(QStringLiteral("type")).toString();
        QVariantMap block = m_content.at(index).toMap();
        auto append = [&](const QString &field, const QString &piece) {
            block.insert(field, block.value(field).toString() + piece);
        };
        if (type == QLatin1String("text_delta")) {
            const QString piece = delta.value(QStringLiteral("text")).toString();
            append(QStringLiteral("text"), piece);
            m_content[index] = block;
            emit textDelta(index, piece);
            return;
        }
        if (type == QLatin1String("input_json_delta"))
            m_partialJson[index] += delta.value(QStringLiteral("partial_json")).toString();
        else if (type == QLatin1String("thinking_delta"))
            append(QStringLiteral("thinking"), delta.value(QStringLiteral("thinking")).toString());
        else if (type == QLatin1String("signature_delta"))
            append(QStringLiteral("signature"), delta.value(QStringLiteral("signature")).toString());
        else if (type == QLatin1String("citations_delta")) {
            QVariantList citations = block.value(QStringLiteral("citations")).toList();
            citations.append(delta.value(QStringLiteral("citation")));
            block.insert(QStringLiteral("citations"), citations);
        }
        m_content[index] = block;
    } else if (name == "content_block_stop") {
        const int index = data.value(QStringLiteral("index")).toInt();
        if (index < 0 || index >= m_content.size() || !m_partialJson.contains(index))
            return;
        // a tool's input: strictly JSON, and an object
        QVariantMap block = m_content.at(index).toMap();
        const QString raw = m_partialJson.take(index);
        if (raw.trimmed().isEmpty()) {
            block.insert(QStringLiteral("input"), QVariantMap());
        } else {
            QJsonParseError error;
            const QJsonDocument doc = QJsonDocument::fromJson(raw.toUtf8(), &error);
            if (error.error == QJsonParseError::NoError && doc.isObject()) {
                block.insert(QStringLiteral("input"), doc.object().toVariantMap());
            } else {
                block.insert(QStringLiteral("input"), QVariantMap());
                m_invalid.insert(block.value(QStringLiteral("id")).toString(), raw);
            }
        }
        m_content[index] = block;
    } else if (name == "message_delta") {
        const QVariantMap delta = data.value(QStringLiteral("delta")).toMap();
        for (auto it = delta.cbegin(); it != delta.cend(); ++it)
            m_message.insert(it.key(), it.value());
        QVariantMap usage = m_message.value(QStringLiteral("usage")).toMap();
        const QVariantMap more = data.value(QStringLiteral("usage")).toMap();
        for (auto it = more.cbegin(); it != more.cend(); ++it)
            usage.insert(it.key(), it.value());
        m_message.insert(QStringLiteral("usage"), usage);
    } else if (name == "message_stop") {
        done();
    } else if (name == "error") {
        const QVariantMap error = data.value(QStringLiteral("error")).toMap();
        const QString type = error.value(QStringLiteral("type")).toString();
        m_ended = true;
        if (m_reply) {
            QNetworkReply *reply = m_reply;
            m_reply = nullptr;
            reply->disconnect(this);
            reply->abort();
            reply->deleteLater();
        }
        fail(errorText(0, type, error.value(QStringLiteral("message")).toString()), 0, type);
    }
}

void AiClient::done()
{
    m_ended = true;
    // (any tool input still open: the stream ended in it)
    for (auto it = m_partialJson.cbegin(); it != m_partialJson.cend(); ++it) {
        QVariantMap block = m_content.value(it.key()).toMap();
        block.insert(QStringLiteral("input"), QVariantMap());
        m_invalid.insert(block.value(QStringLiteral("id")).toString(), it.value());
        m_content[it.key()] = block;
    }
    m_partialJson.clear();
    QVariantMap message = m_message;
    message.insert(QStringLiteral("content"), m_content);
    message.insert(QStringLiteral("invalidInputs"), m_invalid);
    if (m_reply) {
        QNetworkReply *reply = m_reply;
        m_reply = nullptr;
        reply->disconnect(this);
        reply->deleteLater();
    }
    emit busyChanged();
    emit finished(message);
}
