// Port of main/chat.ts (the agent loop) and the logic in shared/chat.ts.
#include "chat.h"

#include "db.h"
#include "fmpcaps.h"
#include "fmpmcp.h"
#include "http.h"
#include "nytime.h"

#include <QDateTime>
#include <QRegularExpression>
#include <QThreadPool>
#include <QTimeZone>

namespace tl {

// ------------------------------------------------------------------------------------- shared/chat.ts
namespace chatinfo {

Rec content() { return tl::content(QStringLiteral("chat.json")).toMap(); }
List models() { return content().value(QStringLiteral("models")).toList(); }
List efforts() { return content().value(QStringLiteral("efforts")).toList(); }
QString systemPrompt() { return content().value(QStringLiteral("systemPrompt")).toString(); }
List toolDefs() { return tl::content(QStringLiteral("tools.json")).toList(); }

Rec modelInfo(const QString &id)
{
    const List ms = models();
    for (const QVariant &m : ms)
        if (m.toMap().value(QStringLiteral("id")).toString() == id)
            return m.toMap();
    return ms.isEmpty() ? Rec{} : ms.first().toMap();
}

// Reads a stored default, falling back per field so an old or hand-edited value can never select a model that is gone.
Rec normalizePrefs(const QVariant &p)
{
    const Rec o = p.toMap();
    const Rec dflt = content().value(QStringLiteral("defaultPrefs")).toMap();
    auto hasId = [](const List &l, const QVariant &id) {
        for (const QVariant &x : l)
            if (x.toMap().value(QStringLiteral("id")) == id && id.metaType().id() == QMetaType::QString)
                return true;
        return false;
    };
    const QString model = hasId(models(), o.value(QStringLiteral("model"))) ? o.value(QStringLiteral("model")).toString() : dflt.value(QStringLiteral("model")).toString();
    const QString effort = hasId(efforts(), o.value(QStringLiteral("effort"))) ? o.value(QStringLiteral("effort")).toString()
        : model == dflt.value(QStringLiteral("model")).toString() ? dflt.value(QStringLiteral("effort")).toString()
                                                                    : modelInfo(model).value(QStringLiteral("defaultEffort")).toString();
    return Rec{{QStringLiteral("model"), model}, {QStringLiteral("effort"), effort}};
}

} // namespace chatinfo

// ------------------------------------------------------------------------------------- main/chat.ts
namespace {
const int MAX_TOOL_ROUNDS = 25;
// Above this many FMP tools, send them deferred behind Anthropic's tool search instead of all up front.
const int MAX_UPFRONT_MCP_TOOLS = 25;

struct ApiError
{
    int status = 0;
    QString type, message;
};

QString pad2(int n) { return QStringLiteral("%1").arg(n, 2, 10, QLatin1Char('0')); }
}

Chat::Chat(std::function<QString()> key, FmpMcp *mcp, Broadcast broadcast)
    : m_key(std::move(key))
    , m_mcp(mcp)
    , m_broadcast(std::move(broadcast))
{
}

List Chat::list() { return Db::local().all(QStringLiteral("SELECT id, title, updated_at FROM chats ORDER BY updated_at DESC, rowid DESC LIMIT 100")); }
List Chat::get(const QString &id) { return Db::local().all(QStringLiteral("SELECT role, content FROM chat_messages WHERE chat_id = ? ORDER BY id"), {id}); }
void Chat::remove(const QString &id)
{
    Db::local().run(QStringLiteral("DELETE FROM chat_messages WHERE chat_id = ?"), {id});
    Db::local().run(QStringLiteral("DELETE FROM chats WHERE id = ?"), {id});
}

void Chat::stop(const QString &requestId)
{
    QMutexLocker g(&m_mutex);
    if (auto a = m_inflight.value(requestId))
        a->store(true);
    m_cond.wakeAll();
}

void Chat::toolResult(const QString &callId, const Rec &reply)
{
    QMutexLocker g(&m_mutex);
    if (auto p = m_pending.value(callId)) {
        p->done = true;
        p->reply = reply;
        m_cond.wakeAll();
    }
}

// Tools run in the interface (that is where the chart lives); this thread waits for the reply.
Rec Chat::callTool(const QString &name, const QVariant &input, const std::atomic_bool &abort)
{
    const QString callId = uuid();
    auto p = std::make_shared<Pending>();
    QMutexLocker g(&m_mutex);
    m_pending.insert(callId, p);
    m_broadcast(QStringLiteral("tool:call"), Rec{{QStringLiteral("callId"), callId}, {QStringLiteral("name"), name}, {QStringLiteral("input"), input}});
    const qint64 deadline = nowMs() + 60000;
    Rec reply;
    while (!p->done) {
        if (abort.load()) { reply = Rec{{QStringLiteral("ok"), false}, {QStringLiteral("text"), QStringLiteral("Cancelled by the user.")}}; break; }
        const qint64 left = deadline - nowMs();
        if (left <= 0) { reply = Rec{{QStringLiteral("ok"), false}, {QStringLiteral("text"), QStringLiteral("Tool \"%1\" timed out.").arg(name)}}; break; }
        m_cond.wait(&m_mutex, QDeadlineTimer(std::min<qint64>(left, 250)));
    }
    if (p->done)
        reply = p->reply;
    m_pending.remove(callId);
    return reply;
}

Rec Chat::send(const Rec &req)
{
    const QString requestId = req.value(QStringLiteral("requestId")).toString();
    {
        QMutexLocker g(&m_mutex);
        m_inflight.insert(requestId, std::make_shared<std::atomic_bool>(false));
    }
    QThreadPool::globalInstance()->start([this, req] { run(req); });
    return ok();
}

void Chat::run(const Rec &req)
{
    const QString requestId = req.value(QStringLiteral("requestId")).toString();
    const QString chatId = req.value(QStringLiteral("chatId")).toString();
    const QString reqText = req.value(QStringLiteral("text")).toString();
    std::shared_ptr<std::atomic_bool> abort;
    {
        QMutexLocker g(&m_mutex);
        abort = m_inflight.value(requestId);
    }
    auto emitEv = [&](Rec ev) {
        ev.insert(QStringLiteral("requestId"), requestId);
        m_broadcast(QStringLiteral("chat:event"), ev);
    };
    Db &db = Db::local();
    const Rec info = chatinfo::modelInfo(req.value(QStringLiteral("model")).toString());
    const QString modelId = info.value(QStringLiteral("id")).toString();
    QString text;
    QStringList segments;
    qint64 inTokens = 0, outTokens = 0;
    auto saveText = [&] {
        QStringList parts;
        for (const QString &s : segments + QStringList{text})
            if (!s.isEmpty())
                parts << s;
        if (!parts.isEmpty())
            db.run(QStringLiteral("INSERT INTO chat_messages (chat_id, role, content) VALUES (?, 'assistant', ?)"), {chatId, parts.join(QStringLiteral("\n\n"))});
    };

    try {
        if (!db.one(QStringLiteral("SELECT 1 AS x FROM chats WHERE id = ?"), {chatId})) {
            QString title = reqText;
            title = title.replace(QRegularExpression(QStringLiteral("\\s+")), QStringLiteral(" ")).trimmed().left(60);
            db.run(QStringLiteral("INSERT INTO chats (id, title) VALUES (?, ?)"), {chatId, title.isEmpty() ? QStringLiteral("New chat") : title});
        }
        db.run(QStringLiteral("INSERT INTO chat_messages (chat_id, role, content) VALUES (?, 'user', ?)"), {chatId, reqText});

        const List rows = get(chatId);
        // Live app state rides on the newest user message only, so earlier turns stay byte-identical (cache-friendly).
        const qint64 now = nowSec();
        const NyParts ny = nyParts(now);
        Rec context = req.value(QStringLiteral("context")).toMap();
        context.insert(QStringLiteral("clock"), Rec{
            {QStringLiteral("nowUtc"), QDateTime::fromSecsSinceEpoch(now, QTimeZone::UTC).toString(QStringLiteral("yyyy-MM-ddTHH:mm")) + QLatin1Char('Z')},
            {QStringLiteral("newYork"), QStringLiteral("%1-%2-%3 %4:%5").arg(ny.year).arg(pad2(ny.month), pad2(ny.day), pad2(ny.hour), pad2(ny.minute))},
            {QStringLiteral("usMarketOpen"), isMarketOpen(now)}});
        List messages;
        for (int i = 0; i < rows.size(); ++i) {
            const Rec r = rows[i].toMap();
            QString c = r.value(QStringLiteral("content")).toString();
            if (i == rows.size() - 1 && r.value(QStringLiteral("role")).toString() == QLatin1String("user"))
                c += QStringLiteral("\n\n<app_context>\n%1\n</app_context>").arg(toJson(context));
            messages.append(Rec{{QStringLiteral("role"), r.value(QStringLiteral("role"))}, {QStringLiteral("content"), c}});
        }

        const QString key = m_key();
        if (key.isEmpty()) {
            emitEv(Rec{{QStringLiteral("type"), QStringLiteral("error")}, {QStringLiteral("code"), QStringLiteral("auth")}, {QStringLiteral("message"), QStringLiteral("No API key found. Add one in File → Settings.")}});
            QMutexLocker g(&m_mutex);
            m_inflight.remove(requestId);
            return;
        }

        // tools whose data the user's FMP plan does not include are left out, and Claude is told why
        const QStringList unavailable = unavailableOf(readCaps());
        List toolParams;
        for (const QVariant &tv : chatinfo::toolDefs()) {
            const Rec t = tv.toMap();
            const QString name = t.value(QStringLiteral("name")).toString();
            if (missingAll(unavailable, TOOL_CAPS.value(name)))
                continue;
            toolParams.append(Rec{{QStringLiteral("name"), name}, {QStringLiteral("description"), t.value(QStringLiteral("description"))},
                                  {QStringLiteral("input_schema"), t.value(QStringLiteral("input_schema"))}});
        }
        if (m_mcp && m_mcp->has()) {
            try {
                const List fmpTools = m_mcp->tools();
                if (fmpTools.size() > MAX_UPFRONT_MCP_TOOLS) {
                    toolParams.append(Rec{{QStringLiteral("type"), QStringLiteral("tool_search_tool_bm25_20251119")}, {QStringLiteral("name"), QStringLiteral("tool_search_tool_bm25")}});
                    for (const QVariant &t : fmpTools) {
                        Rec d = t.toMap();
                        d.insert(QStringLiteral("defer_loading"), true);
                        toolParams.append(d);
                    }
                } else
                    toolParams += fmpTools;
            } catch (...) {
                // FMP data tools are optional; the chart tools still work
            }
        }

        QString finalModel = modelId, stopReason;
        for (int round = 0;; ++round) {
            text.clear();
            Rec body{{QStringLiteral("model"), modelId}, {QStringLiteral("max_tokens"), 32000},
                     {QStringLiteral("system"), chatinfo::systemPrompt() + planNote(unavailable)},
                     {QStringLiteral("messages"), messages}, {QStringLiteral("tools"), toolParams}, {QStringLiteral("stream"), true}};
            if (info.value(QStringLiteral("reasoning")).toBool()) {
                body.insert(QStringLiteral("thinking"), Rec{{QStringLiteral("type"), QStringLiteral("adaptive")}, {QStringLiteral("display"), QStringLiteral("summarized")}});
                body.insert(QStringLiteral("output_config"), Rec{{QStringLiteral("effort"), req.value(QStringLiteral("effort")).toString()}});
            }
            Headers h{{"x-api-key", key.toUtf8()}, {"anthropic-version", "2023-06-01"}, {"content-type", "application/json"}};
            if (info.value(QStringLiteral("fallback")).toBool()) {
                h.append(qMakePair(QByteArray("anthropic-beta"), QByteArray("server-side-fallback-2026-07-01")));
                body.insert(QStringLiteral("fallbacks"), QStringLiteral("default"));
            }

            // ---- one streamed message: rebuild its content blocks from the events
            QList<Rec> blocks;
            QHash<int, QString> partialJson;
            std::optional<ApiError> streamError;
            QByteArray buffer;
            auto handle = [&](const Rec &ev) {
                const QString type = ev.value(QStringLiteral("type")).toString();
                if (type == QLatin1String("message_start")) {
                    const Rec msg = ev.value(QStringLiteral("message")).toMap();
                    if (!msg.value(QStringLiteral("model")).toString().isEmpty())
                        finalModel = msg.value(QStringLiteral("model")).toString();
                    inTokens += msg.value(QStringLiteral("usage")).toMap().value(QStringLiteral("input_tokens")).toLongLong();
                } else if (type == QLatin1String("content_block_start")) {
                    const int idx = ev.value(QStringLiteral("index")).toInt();
                    while (blocks.size() <= idx)
                        blocks.append(Rec{});
                    blocks[idx] = ev.value(QStringLiteral("content_block")).toMap();
                } else if (type == QLatin1String("content_block_delta")) {
                    const int idx = ev.value(QStringLiteral("index")).toInt();
                    if (idx >= blocks.size())
                        return;
                    const Rec d = ev.value(QStringLiteral("delta")).toMap();
                    const QString dt = d.value(QStringLiteral("type")).toString();
                    Rec &b = blocks[idx];
                    if (dt == QLatin1String("text_delta")) {
                        const QString t = d.value(QStringLiteral("text")).toString();
                        b.insert(QStringLiteral("text"), b.value(QStringLiteral("text")).toString() + t);
                        text += t;
                        emitEv(Rec{{QStringLiteral("type"), QStringLiteral("text")}, {QStringLiteral("text"), t}});
                    } else if (dt == QLatin1String("thinking_delta")) {
                        const QString t = d.value(QStringLiteral("thinking")).toString();
                        b.insert(QStringLiteral("thinking"), b.value(QStringLiteral("thinking")).toString() + t);
                        emitEv(Rec{{QStringLiteral("type"), QStringLiteral("thinking")}, {QStringLiteral("text"), t}});
                    } else if (dt == QLatin1String("signature_delta")) {
                        b.insert(QStringLiteral("signature"), b.value(QStringLiteral("signature")).toString() + d.value(QStringLiteral("signature")).toString());
                    } else if (dt == QLatin1String("input_json_delta")) {
                        partialJson[idx] += d.value(QStringLiteral("partial_json")).toString();
                    } else if (dt == QLatin1String("citations_delta")) {
                        List c = b.value(QStringLiteral("citations")).toList();
                        c.append(d.value(QStringLiteral("citation")));
                        b.insert(QStringLiteral("citations"), c);
                    }
                } else if (type == QLatin1String("content_block_stop")) {
                    const int idx = ev.value(QStringLiteral("index")).toInt();
                    if (idx < blocks.size() && partialJson.contains(idx)) {
                        const QString pj = partialJson.take(idx);
                        blocks[idx].insert(QStringLiteral("input"), pj.trimmed().isEmpty() ? QVariant(Rec{}) : fromJson(pj, Rec{}));
                    }
                } else if (type == QLatin1String("message_delta")) {
                    const Rec d = ev.value(QStringLiteral("delta")).toMap();
                    if (d.contains(QStringLiteral("stop_reason")))
                        stopReason = d.value(QStringLiteral("stop_reason")).toString();
                    outTokens += ev.value(QStringLiteral("usage")).toMap().value(QStringLiteral("output_tokens")).toLongLong();
                } else if (type == QLatin1String("error")) {
                    const Rec e = ev.value(QStringLiteral("error")).toMap();
                    streamError = ApiError{0, e.value(QStringLiteral("type")).toString(), e.value(QStringLiteral("message")).toString()};
                }
            };
            auto feed = [&](const QByteArray &chunk) {
                buffer += chunk;
                buffer.replace("\r\n", "\n");
                int cut;
                while ((cut = buffer.indexOf("\n\n")) >= 0) {
                    const QByteArray event = buffer.left(cut);
                    buffer.remove(0, cut + 2);
                    QByteArray data;
                    for (const QByteArray &line : event.split('\n'))
                        if (line.startsWith("data:"))
                            data += line.mid(5).trimmed();
                    if (!data.isEmpty())
                        handle(fromJson(QString::fromUtf8(data)).toMap());
                }
            };
            stopReason.clear();
            const HttpResponse r = httpPostStream(QUrl(QStringLiteral("https://api.anthropic.com/v1/messages")), h, toJson(body).toUtf8(), feed, abort.get());
            if (abort->load())
                throw std::runtime_error("stopped");
            if (!r.ok()) {
                if (r.status == 0)
                    throw Error(r.networkError.isEmpty() ? QStringLiteral("Network error.") : r.networkError);
                const Rec e = fromJson(QString::fromUtf8(r.body)).toMap().value(QStringLiteral("error")).toMap();
                throw ApiError{r.status, e.value(QStringLiteral("type")).toString(), (e.value(QStringLiteral("message")).toString().isEmpty() ? QString::fromUtf8(r.body.left(300)) : e.value(QStringLiteral("message")).toString())};
            }
            if (streamError)
                throw *streamError;
            if (stopReason != QLatin1String("tool_use"))
                break;
            if (round >= MAX_TOOL_ROUNDS) {
                emitEv(Rec{{QStringLiteral("type"), QStringLiteral("text")}, {QStringLiteral("text"), QStringLiteral("\n\n_(Stopped after too many tool calls.)_")}});
                text += QStringLiteral("\n\n(Stopped after too many tool calls.)");
                break;
            }

            // Echo the assistant turn back unchanged (thinking blocks included), then answer every tool_use.
            segments << text;
            List content;
            for (const Rec &b : blocks)
                content.append(b);
            messages.append(Rec{{QStringLiteral("role"), QStringLiteral("assistant")}, {QStringLiteral("content"), content}});
            List results;
            for (const Rec &b : blocks) {
                if (b.value(QStringLiteral("type")).toString() != QLatin1String("tool_use"))
                    continue;
                const QString id = b.value(QStringLiteral("id")).toString(), name = b.value(QStringLiteral("name")).toString();
                const QVariant input = b.value(QStringLiteral("input"));
                emitEv(Rec{{QStringLiteral("type"), QStringLiteral("tool_start")}, {QStringLiteral("id"), id}, {QStringLiteral("name"), name}, {QStringLiteral("input"), input}});
                const Rec rep = m_mcp && m_mcp->owns(name) ? m_mcp->call(name, input) : callTool(name, input, *abort);
                const QString rtext = rep.value(QStringLiteral("text")).toString();
                const bool rok = rep.value(QStringLiteral("ok")).toBool();
                emitEv(Rec{{QStringLiteral("type"), QStringLiteral("tool_end")}, {QStringLiteral("id"), id}, {QStringLiteral("ok"), rok}, {QStringLiteral("result"), rtext.left(4000)}});
                QVariant rc = rtext;
                const QString image = rep.value(QStringLiteral("image")).toString();
                if (!image.isEmpty())
                    rc = List{Rec{{QStringLiteral("type"), QStringLiteral("image")},
                                  {QStringLiteral("source"), Rec{{QStringLiteral("type"), QStringLiteral("base64")}, {QStringLiteral("media_type"), QStringLiteral("image/png")}, {QStringLiteral("data"), image}}}},
                              Rec{{QStringLiteral("type"), QStringLiteral("text")}, {QStringLiteral("text"), rtext}}};
                Rec res{{QStringLiteral("type"), QStringLiteral("tool_result")}, {QStringLiteral("tool_use_id"), id}, {QStringLiteral("content"), rc}};
                if (!rok)
                    res.insert(QStringLiteral("is_error"), true);
                results.append(res);
            }
            messages.append(Rec{{QStringLiteral("role"), QStringLiteral("user")}, {QStringLiteral("content"), results}});
        }
        saveText();
        db.run(QStringLiteral("UPDATE chats SET updated_at = CURRENT_TIMESTAMP WHERE id = ?"), {chatId});
        emitEv(Rec{{QStringLiteral("type"), QStringLiteral("done")}, {QStringLiteral("model"), finalModel}, {QStringLiteral("stopReason"), stopReason.isEmpty() ? null() : QVariant(stopReason)},
                   {QStringLiteral("inputTokens"), inTokens}, {QStringLiteral("outputTokens"), outTokens}});
    } catch (const ApiError &e) {
        if (e.status == 401 || e.type == QLatin1String("authentication_error"))
            emitEv(Rec{{QStringLiteral("type"), QStringLiteral("error")}, {QStringLiteral("code"), QStringLiteral("auth")}, {QStringLiteral("message"), QStringLiteral("The API key was rejected. Check it in File → Settings.")}});
        else if (e.status == 429 || e.type == QLatin1String("rate_limit_error"))
            emitEv(Rec{{QStringLiteral("type"), QStringLiteral("error")}, {QStringLiteral("message"), QStringLiteral("Rate limited. Wait a moment and try again.")}});
        else
            emitEv(Rec{{QStringLiteral("type"), QStringLiteral("error")}, {QStringLiteral("message"), QStringLiteral("API error %1: %2").arg(e.status ? QString::number(e.status) : QString(), e.message)}});
    } catch (const Error &e) {
        emitEv(Rec{{QStringLiteral("type"), QStringLiteral("error")}, {QStringLiteral("message"), e.message()}});
    } catch (...) {
        if (abort && abort->load()) {
            try { saveText(); } catch (...) { }
            emitEv(Rec{{QStringLiteral("type"), QStringLiteral("done")}, {QStringLiteral("model"), modelId}, {QStringLiteral("stopReason"), QStringLiteral("stopped")},
                       {QStringLiteral("inputTokens"), 0}, {QStringLiteral("outputTokens"), 0}});
        } else
            emitEv(Rec{{QStringLiteral("type"), QStringLiteral("error")}, {QStringLiteral("message"), QStringLiteral("Something went wrong.")}});
    }
    QMutexLocker g(&m_mutex);
    m_inflight.remove(requestId);
}

} // namespace tl
