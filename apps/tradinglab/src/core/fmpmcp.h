#pragma once

// Client-side connection to FMP's MCP server. Port of main/fmpMcp.ts. The FMP key stays in this process (it only
// ever goes to FMP); the server's tools are handed to Claude as ordinary tools and executed here.
// Speaks MCP's streamable-HTTP transport directly (JSON-RPC over POST, answers as JSON or one SSE stream);
// upstream's SDK also fell back to the older SSE transport, which FMP's server does not need.

#include "util.h"

#include <QHash>

#include <functional>
#include <mutex>
#include <optional>

namespace tl {

class FmpMcp
{
public:
    explicit FmpMcp(std::function<QString()> key);

    bool has() const { return !m_key().isEmpty(); }
    // tool definitions for Claude: [{name, description, input_schema}]
    List tools();
    bool owns(const QString &name);
    // {ok, text}
    Rec call(const QString &name, const QVariant &args);
    // {ok, count?, transport?, sample?, error?}
    Rec status();

private:
    struct Conn
    {
        QString key;
        QByteArray session;
        List tools;
        QHash<QString, QString> byName;
        int nextId = 1;
    };
    Conn &ensure();
    Conn connect(const QString &key);
    QVariant rpc(Conn &c, const QString &method, const Rec &params, bool notification = false);

    std::function<QString()> m_key;
    std::optional<Conn> m_conn;
    struct Failure { QString key; qint64 at; QString error; };
    std::optional<Failure> m_lastFailure;
    std::recursive_mutex m_lock;
};

} // namespace tl
