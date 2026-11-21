// SPDX-FileCopyrightText: 2026 Jaimie Garner <pacificnm@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later
#include "claude.h"

#include <KLocalizedString>

#include <QJsonArray>
#include <QJsonDocument>

namespace
{
constexpr int MaxTokens = 16000; // thinking counts toward it; the prompts ask for short answers
}

QStringList Claude::models()
{
    return {QStringLiteral("claude-opus-5-5"), QStringLiteral("claude-sonnet-5-5"), QStringLiteral("claude-haiku-5-5")};
}

QString Claude::defaultModel()
{
    return models().constFirst();
}

bool Claude::hasFallbacks(const QString &model)
{
    // Claude Haiku has no server-side fallback
    return !model.startsWith(QLatin1String("claude-haiku"));
}

QJsonObject Claude::request(const QJsonObject &ollamaBody)
{
    const QString model = ollamaBody.value(QLatin1String("model")).toString();
    QStringList system;
    QJsonArray messages;
    for (const auto list = ollamaBody.value(QLatin1String("messages")).toArray(); const QJsonValue &v : list) {
        const QJsonObject m = v.toObject();
        const QString role = m.value(QLatin1String("role")).toString();
        const QString content = m.value(QLatin1String("content")).toString();
        if (role == QLatin1String("system")) {
            if (messages.isEmpty())
                system << content;
            else // a mid-conversation instruction (the API takes it after a user turn)
                messages.append(QJsonObject{{QStringLiteral("role"), QStringLiteral("system")}, {QStringLiteral("content"), content}});
        } else if (role == QLatin1String("user") || role == QLatin1String("assistant")) {
            messages.append(QJsonObject{{QStringLiteral("role"), role}, {QStringLiteral("content"), content}});
        }
    }
    QJsonObject out{{QStringLiteral("model"), model},
                    {QStringLiteral("max_tokens"), MaxTokens},
                    {QStringLiteral("stream"), true},
                    {QStringLiteral("messages"), messages},
                    // answers from passages it is given: quick and short beats long deliberation here
                    {QStringLiteral("output_config"), QJsonObject{{QStringLiteral("effort"), QStringLiteral("low")}}}};
    if (!system.isEmpty())
        out.insert(QStringLiteral("system"), system.join(QStringLiteral("\n\n")));
    if (hasFallbacks(model))
        out.insert(QStringLiteral("fallbacks"), QStringLiteral("default")); // a declined request is retried on a model that may answer
    return out;
}

QList<QByteArray> Claude::takeEvents(QByteArray *buffer, bool all)
{
    QList<QByteArray> out;
    buffer->replace("\r\n", "\n");
    auto take = [&out](const QByteArray &event) {
        QByteArray data;
        for (const QByteArray &line : event.split('\n')) {
            if (!line.startsWith("data:"))
                continue; // event:, id:, comments
            if (!data.isEmpty())
                data += '\n';
            data += line.mid(5).trimmed();
        }
        if (!data.isEmpty())
            out << data;
    };
    qsizetype end;
    while ((end = buffer->indexOf("\n\n")) >= 0) {
        take(buffer->left(end));
        buffer->remove(0, end + 2);
    }
    if (all && !buffer->trimmed().isEmpty()) {
        take(*buffer);
        buffer->clear();
    }
    return out;
}

Claude::Event Claude::parseEvent(const QByteArray &data)
{
    Event out;
    const QJsonDocument doc = QJsonDocument::fromJson(data);
    if (!doc.isObject()) {
        out.error = i18n("Unreadable reply from Claude: %1", QString::fromUtf8(data.left(200)));
        return out;
    }
    const QJsonObject o = doc.object();
    const QString type = o.value(QLatin1String("type")).toString();
    if (type == QLatin1String("content_block_delta")) {
        const QJsonObject delta = o.value(QLatin1String("delta")).toObject();
        if (delta.value(QLatin1String("type")).toString() == QLatin1String("text_delta"))
            out.text = delta.value(QLatin1String("text")).toString();
    } else if (type == QLatin1String("message_delta")) {
        out.stopReason = o.value(QLatin1String("delta")).toObject().value(QLatin1String("stop_reason")).toString();
        const QJsonValue tokens = o.value(QLatin1String("usage")).toObject().value(QLatin1String("output_tokens"));
        if (tokens.isDouble())
            out.outputTokens = tokens.toInt();
    } else if (type == QLatin1String("message_stop")) {
        out.stop = true;
    } else if (type == QLatin1String("error")) {
        out.error = o.value(QLatin1String("error")).toObject().value(QLatin1String("message")).toString(i18n("Claude reported an error."));
    }
    return out;
}

QString Claude::errorMessage(QNetworkReply::NetworkError error, int httpStatus, const QByteArray &body, const QString &model)
{
    const QString said = QJsonDocument::fromJson(body).object().value(QLatin1String("error")).toObject().value(QLatin1String("message")).toString();
    switch (httpStatus) {
    case 401:
        return i18n("Claude did not accept the API key. Check it in Settings > Models.");
    case 403:
        return said.isEmpty() ? i18n("This API key may not use Claude.") : i18nc("@info %1 is an error message from Anthropic", "Claude: %1", said);
    case 404:
        return i18n("Claude has no model “%1”. Choose another in Settings > Models.", model);
    case 429:
        return i18n("Claude's rate limit was reached. Wait a moment and ask again.");
    case 529:
        return i18n("Claude is overloaded right now. Try again in a little while.");
    default:
        break;
    }
    if (!said.isEmpty())
        return i18nc("@info %1 is an error message from Anthropic", "Claude: %1", said);
    switch (error) {
    case QNetworkReply::HostNotFoundError:
    case QNetworkReply::ConnectionRefusedError:
    case QNetworkReply::NetworkSessionFailedError:
    case QNetworkReply::TemporaryNetworkFailureError:
        return i18n("Could not reach Claude. Is the computer online?");
    case QNetworkReply::OperationCanceledError:
    case QNetworkReply::TimeoutError:
        return i18n("Claude did not answer in time.");
    default:
        break;
    }
    if (httpStatus >= 400)
        return i18n("Claude returned HTTP %1.", httpStatus);
    return i18n("Could not talk to Claude.");
}

QJsonObject Claude::lastLine(const QString &stopReason, int outputTokens, qint64 writingNs)
{
    return {{QStringLiteral("done"), true},
            {QStringLiteral("done_reason"), stopReason == QLatin1String("max_tokens") ? QStringLiteral("length") : QStringLiteral("stop")},
            {QStringLiteral("eval_count"), qMax(0, outputTokens)},
            {QStringLiteral("eval_duration"), double(writingNs)}};
}
