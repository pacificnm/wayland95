#pragma once

// FMP response cache: per-endpoint lifetimes that follow market sessions (ttlFor), a memory + SQLite store
// (ResponseCache, table fmp_cache), and the requester that serves fresh hits, merges identical requests in flight
// and falls back to the last good copy when a failure is transient. Port of main/fmpCache.ts.
//
// Threads: handlers run on several threads at once. The memory map and the in-flight table are guarded by mutexes;
// SQLite is reached through each thread's own Db::local() connection.

#include "util.h"

#include <QHash>
#include <QString>

#include <atomic>
#include <climits>
#include <condition_variable>
#include <exception>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <optional>

namespace tl {

// How long a response stays fresh (ms). Live-price data follows the market session; company data changes slowly.
qint64 ttlFor(const QString &path, qint64 nowMs = tl::nowMs(), const Rec &params = {});

struct CacheEntry
{
    QVariant body;
    qint64 fetchedAt = 0; // ms
    qint64 expiresAt = 0; // ms
};

// Memory first, then SQLite, so cached responses survive restarts. Intraday bars stay in memory only.
class ResponseCache
{
public:
    // persist = false keeps everything in memory (upstream's `db = null`). Creates and prunes fmp_cache on this thread.
    explicit ResponseCache(bool persist = true, std::function<qint64()> clock = [] { return tl::nowMs(); });
    std::optional<CacheEntry> get(const QString &key);
    CacheEntry set(const QString &key, const QVariant &body, qint64 ttlMs);
    void clear();
    // {entries, bytes, oldest (ms or null)}
    Rec stats();

private:
    bool m_persist;
    std::function<qint64()> m_clock;
    std::mutex m_mx;
    QHash<QString, CacheEntry> m_mem;
};

// Request context: `force` skips fresh hits; `asOf` collects the oldest fetch time (ms) among the responses used.
// Safe to share between the parallel requests of one handler.
struct Ctx
{
    explicit Ctx(bool force = false) : force(force) {}
    bool force;
    std::atomic<qint64> asOf{LLONG_MAX};
    bool hasAsOf() const { return asOf.load() != LLONG_MAX; }
    void touch(qint64 fetchedAt);
};

// path?k=v&k=v with keys sorted and values URI-encoded
QString cacheKey(const QString &path, const Rec &params);
// String(v) for a query parameter value (integers without a decimal point, booleans as true/false)
QString paramString(const QVariant &v);
// encodeURIComponent
QString encodeUriComponent(const QString &s);

// Cached GET with: fresh hits served without a request, identical concurrent requests merged into one,
// and (when the failure is transient) the last good copy served instead of an error.
class Requester
{
public:
    // fetchJson throws on failure; staleOk decides which failures may be answered from the cache
    using FetchJson = std::function<QVariant(const QString &path, const Rec &params)>;
    using StaleOk = std::function<bool(const std::exception_ptr &)>;

    Requester(ResponseCache &store, FetchJson fetchJson, StaleOk staleOk, std::function<qint64()> clock = [] { return tl::nowMs(); });
    // ttl: nullopt uses ttlFor(path)
    QVariant get(const QString &path, const Rec &params = {}, std::optional<qint64> ttl = std::nullopt, Ctx *ctx = nullptr);

private:
    struct Flight
    {
        std::mutex mx;
        std::condition_variable cv;
        bool done = false;
        CacheEntry entry;
        std::exception_ptr error;
    };
    ResponseCache &m_store;
    FetchJson m_fetch;
    StaleOk m_staleOk;
    std::function<qint64()> m_clock;
    std::mutex m_mx;
    std::map<QString, std::shared_ptr<Flight>> m_inflight;
};

} // namespace tl
