#pragma once

// The Claude assistant: a manual streaming agent loop against the Messages API. Port of main/chat.ts and the
// logic of shared/chat.ts (modelInfo, normalizePrefs). Model list, efforts and the system prompt come from
// content/chat.json; the tool list from content/tools.json.
//
// Tools run in the interface (that is where the chart, ticket and calculator live): the loop sends a
// `tool:call` event and waits for the matching `tool:result`. Invariant: no tool may place, change or cancel an order.

#include "util.h"

#include <QHash>
#include <QMutex>
#include <QWaitCondition>

#include <atomic>
#include <functional>
#include <memory>

namespace tl {

class FmpMcp;

namespace chatinfo {
Rec content(); // chat.json
List models();
List efforts();
Rec modelInfo(const QString &id);
Rec normalizePrefs(const QVariant &p);
QString systemPrompt();
List toolDefs(); // tools.json (with labels)
}

class Chat
{
public:
    using Broadcast = std::function<void(const QString &channel, const QVariant &payload)>;
    Chat(std::function<QString()> key, FmpMcp *mcp, Broadcast broadcast);

    // starts the request on its own thread and returns {ok: true}; progress comes as `chat:event`s
    Rec send(const Rec &req);
    void stop(const QString &requestId);
    void toolResult(const QString &callId, const Rec &reply);
    List list();
    List get(const QString &id);
    void remove(const QString &id);

private:
    struct Pending
    {
        bool done = false;
        Rec reply;
    };
    void run(const Rec &req);
    Rec callTool(const QString &name, const QVariant &input, const std::atomic_bool &abort);

    std::function<QString()> m_key;
    FmpMcp *m_mcp;
    Broadcast m_broadcast;
    QMutex m_mutex;
    QWaitCondition m_cond;
    QHash<QString, std::shared_ptr<std::atomic_bool>> m_inflight;
    QHash<QString, std::shared_ptr<Pending>> m_pending;
};

} // namespace tl
