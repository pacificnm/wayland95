// FMP response cache and the cached requester. Port of main/fmpCache.ts.

#include "fmpcache.h"

#include "db.h"
#include "nytime.h"

#include <QLocale>
#include <QUrl>

#include <algorithm>
#include <cmath>

namespace tl {

namespace {
constexpr qint64 MIN = 60000, HOUR = 3600000;
constexpr qint64 MAX_PERSISTED_BYTES = 1500000;
constexpr qint64 MAX_ROWS = 1500;
constexpr qint64 MAX_AGE_MS = 7 * 24 * HOUR;
}

qint64 ttlFor(const QString &path, qint64 nowMs, const Rec &params)
{
    const qint64 now = nowMs / 1000;
    const bool open = isMarketOpen(now), ext = isExtendedHours(now);
    const bool live = open || ext;
    qint64 ttl;
    bool sessionBound = false; // may not outlive the next session boundary (a "closed" price is stale once trading resumes)
    static const QStringList grades = {QStringLiteral("grades"), QStringLiteral("grades-consensus"), QStringLiteral("price-target-consensus"),
                                       QStringLiteral("price-target-summary"), QStringLiteral("ratings-snapshot")};
    static const QStringList company = {QStringLiteral("profile"), QStringLiteral("ratios-ttm"), QStringLiteral("key-metrics-ttm"),
                                        QStringLiteral("income-statement"), QStringLiteral("balance-sheet-statement"),
                                        QStringLiteral("cash-flow-statement")};
    if (path == QLatin1String("quote") || path == QLatin1String("quote-short")) { ttl = open ? 15000 : ext ? 45000 : 10 * MIN; sessionBound = true; }
    else if (path.startsWith(QLatin1String("aftermarket-"))) { ttl = live ? 15000 : 30 * MIN; sessionBound = true; }
    else if (path == QLatin1String("stock-price-change")) { ttl = open ? 5 * MIN : ext ? 10 * MIN : 3 * HOUR; sessionBound = true; }
    else if (path.startsWith(QLatin1String("historical-chart/"))) { ttl = open ? 30000 : 10 * MIN; sessionBound = true; }
    else if (path == QLatin1String("historical-price-eod/full")) { ttl = open ? 10 * MIN : 2 * HOUR; sessionBound = true; }
    else if (path.endsWith(QLatin1String("-performance-snapshot")) || path.endsWith(QLatin1String("-pe-snapshot"))) {
        // a past day never changes; today moves while the market is open
        const QVariant date = params.value(QStringLiteral("date"));
        const bool past = date.metaType().id() == QMetaType::QString && date.toString() < nyDate(now);
        ttl = past ? 24 * HOUR : open ? 5 * MIN : 3 * HOUR;
        sessionBound = !past;
    }
    else if (path == QLatin1String("biggest-gainers") || path == QLatin1String("biggest-losers") || path == QLatin1String("most-actives")) { ttl = open ? 60000 : ext ? 3 * MIN : 30 * MIN; sessionBound = true; }
    else if (path == QLatin1String("company-screener")) { ttl = open ? 5 * MIN : ext ? 10 * MIN : HOUR; sessionBound = true; }
    else if (path.startsWith(QLatin1String("available-"))) ttl = 24 * HOUR;
    else if (path.startsWith(QLatin1String("senate-")) || path.startsWith(QLatin1String("house-"))) ttl = 30 * MIN; // disclosures are filed in batches, a few times a day
    else if (path.startsWith(QLatin1String("news/"))) ttl = open ? 3 * MIN : 10 * MIN;
    else if (path == QLatin1String("ratings-historical") || path == QLatin1String("analyst-estimates")) ttl = 12 * HOUR;
    else if (grades.contains(path)) ttl = 4 * HOUR;
    else if (company.contains(path)) ttl = 24 * HOUR;
    else if (path.startsWith(QLatin1String("search-"))) ttl = 30 * MIN;
    else ttl = MIN;
    if (sessionBound)
        ttl = std::min(ttl, secondsToNextSessionBoundary(now) * 1000);
    return std::max<qint64>(1000, ttl);
}

// ---- ResponseCache ----

ResponseCache::ResponseCache(bool persist, std::function<qint64()> clock)
    : m_persist(persist), m_clock(std::move(clock))
{
    if (!m_persist)
        return;
    Db &db = Db::local();
    db.exec(QStringLiteral("CREATE TABLE IF NOT EXISTS fmp_cache (key TEXT PRIMARY KEY, body TEXT NOT NULL, fetched_at INTEGER NOT NULL, expires_at INTEGER NOT NULL, bytes INTEGER NOT NULL)"));
    db.run(QStringLiteral("DELETE FROM fmp_cache WHERE fetched_at < ?"), {m_clock() - MAX_AGE_MS});
    db.run(QStringLiteral("DELETE FROM fmp_cache WHERE key NOT IN (SELECT key FROM fmp_cache ORDER BY fetched_at DESC LIMIT ?)"), {MAX_ROWS});
}

std::optional<CacheEntry> ResponseCache::get(const QString &key)
{
    {
        std::lock_guard<std::mutex> g(m_mx);
        const auto it = m_mem.constFind(key);
        if (it != m_mem.cend())
            return *it;
    }
    if (!m_persist)
        return std::nullopt;
    const auto r = Db::local().one(QStringLiteral("SELECT body, fetched_at, expires_at FROM fmp_cache WHERE key = ?"), {key});
    if (!r)
        return std::nullopt;
    const QVariant body = fromJson(r->value(QStringLiteral("body")).toString());
    if (!body.isValid())
        return std::nullopt;
    CacheEntry e{body, r->value(QStringLiteral("fetched_at")).toLongLong(), r->value(QStringLiteral("expires_at")).toLongLong()};
    std::lock_guard<std::mutex> g(m_mx);
    m_mem.insert(key, e);
    return e;
}

CacheEntry ResponseCache::set(const QString &key, const QVariant &body, qint64 ttlMs)
{
    const qint64 now = m_clock();
    const CacheEntry e{body, now, now + ttlMs};
    {
        std::lock_guard<std::mutex> g(m_mx);
        m_mem.insert(key, e);
    }
    if (m_persist && !key.startsWith(QLatin1String("historical-chart/"))) {
        const QString text = toJson(body);
        if (text.size() <= MAX_PERSISTED_BYTES) {
            // a failed cache write must not fail the request that already has its data
            try {
                Db::local().run(QStringLiteral("INSERT INTO fmp_cache (key, body, fetched_at, expires_at, bytes) VALUES (?,?,?,?,?) ON CONFLICT(key) DO UPDATE SET body = excluded.body, fetched_at = excluded.fetched_at, expires_at = excluded.expires_at, bytes = excluded.bytes"),
                                {key, text, e.fetchedAt, e.expiresAt, qint64(text.size())});
            } catch (const std::exception &) {
            }
        }
    }
    return e;
}

void ResponseCache::clear()
{
    {
        std::lock_guard<std::mutex> g(m_mx);
        m_mem.clear();
    }
    if (m_persist)
        Db::local().exec(QStringLiteral("DELETE FROM fmp_cache"));
}

Rec ResponseCache::stats()
{
    if (!m_persist) {
        std::lock_guard<std::mutex> g(m_mx);
        return Rec{{QStringLiteral("entries"), qint64(m_mem.size())}, {QStringLiteral("bytes"), 0}, {QStringLiteral("oldest"), null()}};
    }
    const auto r = Db::local().one(QStringLiteral("SELECT COUNT(*) AS n, COALESCE(SUM(bytes),0) AS b, MIN(fetched_at) AS o FROM fmp_cache"));
    return Rec{{QStringLiteral("entries"), r ? r->value(QStringLiteral("n")) : QVariant(0)},
               {QStringLiteral("bytes"), r ? r->value(QStringLiteral("b")) : QVariant(0)},
               {QStringLiteral("oldest"), r ? r->value(QStringLiteral("o")) : null()}};
}

// ---- keys ----

void Ctx::touch(qint64 fetchedAt)
{
    qint64 cur = asOf.load();
    while (fetchedAt < cur && !asOf.compare_exchange_weak(cur, fetchedAt)) {
    }
}

QString encodeUriComponent(const QString &s)
{
    return QString::fromLatin1(QUrl::toPercentEncoding(s, QByteArrayLiteral("!*'()")));
}

QString paramString(const QVariant &v)
{
    if (isNull(v))
        return v.isValid() ? QStringLiteral("null") : QStringLiteral("undefined");
    switch (v.metaType().id()) {
    case QMetaType::Bool:
        return v.toBool() ? QStringLiteral("true") : QStringLiteral("false");
    case QMetaType::Double: case QMetaType::Float: {
        const double d = v.toDouble();
        if (std::isfinite(d) && std::floor(d) == d && std::abs(d) < 1e18)
            return QString::number(qint64(d));
        if (std::isnan(d))
            return QStringLiteral("NaN");
        return QString::number(d, 'g', QLocale::FloatingPointShortest);
    }
    default:
        return v.toString();
    }
}

QString cacheKey(const QString &path, const Rec &params)
{
    // QVariantMap keeps keys sorted, which is all the key needs (upstream sorted with localeCompare)
    QStringList parts;
    for (auto it = params.cbegin(); it != params.cend(); ++it)
        parts << it.key() + QLatin1Char('=') + encodeUriComponent(paramString(it.value()));
    return path + QLatin1Char('?') + parts.join(QLatin1Char('&'));
}

// ---- Requester ----

Requester::Requester(ResponseCache &store, FetchJson fetchJson, StaleOk staleOk, std::function<qint64()> clock)
    : m_store(store), m_fetch(std::move(fetchJson)), m_staleOk(std::move(staleOk)), m_clock(std::move(clock))
{
}

QVariant Requester::get(const QString &path, const Rec &params, std::optional<qint64> ttl, Ctx *ctx)
{
    const QString key = cacheKey(path, params);
    const std::optional<CacheEntry> entry = m_store.get(key);
    const auto touch = [ctx](const CacheEntry &e) { if (ctx) ctx->touch(e.fetchedAt); };
    if (entry && !(ctx && ctx->force) && m_clock() < entry->expiresAt) {
        touch(*entry);
        return entry->body;
    }

    std::shared_ptr<Flight> flight;
    bool owner = false;
    {
        std::lock_guard<std::mutex> g(m_mx);
        auto it = m_inflight.find(key);
        if (it == m_inflight.end()) {
            flight = std::make_shared<Flight>();
            m_inflight.emplace(key, flight);
            owner = true;
        } else {
            flight = it->second;
        }
    }
    if (owner) {
        CacheEntry got;
        std::exception_ptr err;
        try {
            const QVariant body = m_fetch(path, params);
            got = m_store.set(key, body, ttl ? *ttl : ttlFor(path, m_clock(), params));
        } catch (...) {
            err = std::current_exception();
        }
        {
            std::lock_guard<std::mutex> g(m_mx);
            m_inflight.erase(key);
        }
        {
            std::lock_guard<std::mutex> g(flight->mx);
            flight->entry = got;
            flight->error = err;
            flight->done = true;
        }
        flight->cv.notify_all();
    } else {
        std::unique_lock<std::mutex> lk(flight->mx);
        flight->cv.wait(lk, [&] { return flight->done; });
    }

    if (!flight->error) {
        touch(flight->entry);
        return flight->entry.body;
    }
    if (entry && m_staleOk(flight->error)) {
        touch(*entry);
        return entry->body;
    }
    std::rethrow_exception(flight->error);
}

} // namespace tl
