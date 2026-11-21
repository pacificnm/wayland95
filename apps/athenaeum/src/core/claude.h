// SPDX-FileCopyrightText: 2026 Jaimie Garner <pacificnm@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QByteArray>
#include <QJsonObject>
#include <QNetworkReply>
#include <QString>
#include <QStringList>

// Anthropic's Messages API (Claude), in the shape the rest of the app already speaks: OllamaClient sends a chat whose
// model is a Claude model here instead of to Ollama. The request is translated from Ollama's /api/chat body, and the
// streamed reply (server-sent events) is turned back into text and an Ollama-like last line. Parsing is separate so it
// can be tested without the network.
namespace Claude
{
inline constexpr char ApiUrl[] = "https://api.anthropic.com/v1/messages";
inline constexpr char ApiVersion[] = "2026-10-21";
inline constexpr char FallbackBeta[] = "server-side-fallback-2026-07-01";

// The models offered in Settings, the default first
QStringList models();
QString defaultModel();
inline bool isClaudeModel(const QString &model)
{
    return model.startsWith(QLatin1String("claude-"));
}

// Ollama's {"model", "messages" [system, user, assistant...], "options": {"num_predict"...}, "think"} as a Messages API
// request: system messages become `system`, Ollama's options and `think` are left out (Claude's own thinking is on and
// counts toward max_tokens, so max_tokens is generous; the prompts set the length), streamed.
QJsonObject request(const QJsonObject &ollamaBody);
// Whether the request should carry the server-side fallback beta header (models that have it)
bool hasFallbacks(const QString &model);

// One server-sent event's data
struct Event {
    QString text; // a text delta
    bool stop = false; // message_stop
    QString stopReason; // from message_delta: end_turn, max_tokens, refusal...
    int outputTokens = -1; // from message_delta's usage
    QString error; // an `error` event, or unreadable data
};
Event parseEvent(const QByteArray &data);

// Splits complete events off the front of `buffer` (events end with a blank line) and returns their data lines,
// joined; what is left is an event still arriving
QList<QByteArray> takeEvents(QByteArray *buffer, bool all = false);

// What to tell the user when a request failed
QString errorMessage(QNetworkReply::NetworkError error, int httpStatus, const QByteArray &body, const QString &model = {});

// The Ollama-like last line Answerer reads: done_reason ("length" when cut off), eval_count, eval_duration (ns)
QJsonObject lastLine(const QString &stopReason, int outputTokens, qint64 writingNs);
}
