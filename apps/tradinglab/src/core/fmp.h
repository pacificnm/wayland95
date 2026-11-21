#pragma once

// The FMP (Financial Modeling Prep) market-data client: every fmp:* handler, the plan check, and what the paper engine
// reads (quote, grain, barsFine). Port of main/fmp.ts.
//
// One Fmp is shared by every handler thread. Methods block on the network (call them from worker threads, never the GUI
// thread). Handler methods never throw: they return the FmpResult shape {ok: true, data} or {ok: false, error, kind}
// with kind nokey | auth | plan | rate | network | other.
//
// The API key never leaves this object (invariant 3): it is read through the `key` getter on every request, is put only
// into the request URL, and is never logged or returned.

#include "bar.h"
#include "fmpcache.h"
#include "fmpcaps.h"
#include "util.h"

#include <QString>
#include <QStringList>
#include <QThreadPool>

#include <atomic>
#include <condition_variable>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>

namespace tl {

// An FMP failure with its kind (nokey | auth | plan | rate | network | other)
class FmpError : public Error
{
public:
    FmpError(const QString &message, const QString &kind) : Error(message), m_kind(kind) {}
    QString kind() const { return m_kind; }

private:
    QString m_kind;
};

class Fmp
{
public:
    using KeyGetter = std::function<QString()>;                 // the stored key, empty when none
    using KeySetter = std::function<void(const QString &key)>;  // store a key (desktop keyring)
    using KeyClearer = std::function<void()>;                   // forget the stored key
    using Broadcast = std::function<void(const QString &channel, const QVariant &payload)>;

    // Needs tl::Db::setPath done (the cache table is created on this thread). When a key is configured and the plan
    // was never checked, a check starts 6 s later, as upstream did at start-up.
    Fmp(KeyGetter getKey, KeySetter storeKey, KeyClearer forgetKey, Broadcast broadcast);
    ~Fmp();
    Fmp(const Fmp &) = delete;
    Fmp &operator=(const Fmp &) = delete;

    // ---- handlers (IPC channel in brackets); each returns {ok, data} or {ok: false, error, kind} ----
    // [fmp:bars] p = {symbol, interval: 1min|5min|15min|30min|1hour|4hour|1day, from: YYYY-MM-DD, to?: YYYY-MM-DD}; data = [Bar]
    Rec bars(const Rec &p);
    // [fmp:quotes] data = [quote Rec] (symbols that failed are left out)
    Rec quotes(const QStringList &symbols);
    // [fmp:search] data = [{symbol, name, exchange}] (at most 10)
    Rec search(const QString &q);
    // [fmp:news] p = {symbol?, symbols?: [..], general?: bool, limit?: 1..50, page?}; data = [NewsArticle]
    Rec news(const Rec &p);
    // [fmp:overview] data = {quote, profile, quoteShort, afterTrade, afterQuote, priceChange, asOf}
    Rec overview(const QString &symbol, bool force = false);
    // [fmp:analyst] data = {consensus, targets, grades, snapshot, targetSummary, ratingsHistory, estimates, actuals, asOf}
    Rec analyst(const QString &symbol, bool force = false);
    // [fmp:market] p = {date?, exchange?, force?}; data = {date, exchange, sectors, industries, sectorPe, industryPe, gainers, losers, actives, asOf}
    Rec market(const Rec &p = {});
    // [fmp:congress] p = {kind: latest|symbol|name, chamber: senate|house|both, symbol?, name?, page?, limit?, force?}; data = {trades, asOf}
    Rec congress(const Rec &p);
    // [fmp:screener] p = {query: ScreenerQuery, force?}; data = {rows, asOf}
    Rec screener(const Rec &p);
    // [fmp:screenerOptions] data = {sectors, industries, countries}
    Rec screenerOptions();
    // [fmp:fundamentals] data = {ratios, metrics, income, balance, cashflow}
    Rec fundamentals(const QString &symbol);
    // [fmp:test] runs the plan check now; data = [FmpTestRow {name, path, cap, ok, status, detail, fields?}]
    Rec test();

    // ---- plain (unwrapped) channels ----
    // [fmp:caps:get] FmpCaps {checkedAt, unavailable} or null
    QVariant capsGet();
    // [fmp:key:status] {source: stored|env|none, encrypted}
    Rec keyStatus();
    // [fmp:key:set] / [fmp:key:clear]
    void setKey(const QString &key);
    void clearKey();
    // [fmp:cache:stats] {entries, bytes, oldest} / [fmp:cache:clear]
    Rec cacheStats();
    void clearCache();

    // ---- used by the paper-trading engine (these throw FmpError on failure) ----
    std::optional<double> quote(const QString &symbol);
    // What the plan allows for filling working orders: 1-minute bars if included, otherwise the finest it has.
    std::optional<FillGrain> grain();
    // Bars of the current grain from `fromEpoch` (UTC seconds) until now, ascending. Looks back at most 10 days.
    Bars barsFine(const QString &symbol, qint64 fromEpoch);
    std::optional<Rec> caps();

private:
    QString key() const;
    QVariant fetchJson(const QString &path, const Rec &params);
    List rows(const QString &path, const Rec &params, std::optional<qint64> ttl = std::nullopt, Ctx *ctx = nullptr);
    QVariant first(const QString &path, const Rec &params, std::optional<qint64> ttl = std::nullopt, Ctx *ctx = nullptr);
    Bars fetchBars(const QString &symbol, const QString &interval, const QString &from, const QString &to, std::optional<qint64> ttl = std::nullopt);
    QList<double> parseNewsTimes(const QStringList &dates);

    struct Probed
    {
        List rows;
        bool aborted = false;
    };
    Probed runProbes();
    std::optional<List> checkPlan();
    void writeCaps(const std::optional<Rec> &caps);
    void checkSoon(int ms);
    void noteRefusal();

    KeyGetter m_key;
    KeySetter m_setKey;
    KeyClearer m_clearKey;
    Broadcast m_broadcast;
    QString m_base;
    ResponseCache m_store;
    std::unique_ptr<Requester> m_request;

    // FMP doesn't say which time zone news timestamps use (see parseNewsTimes)
    std::atomic_bool m_newsIsUtc{false};

    // the plan check in progress, joined by anyone who asks for one meanwhile
    struct Probe
    {
        std::mutex mx;
        std::condition_variable cv;
        bool done = false;
        std::optional<List> rows;
    };
    std::mutex m_probeMx;
    std::shared_ptr<Probe> m_probing;
    std::atomic<qint64> m_lastCheckStart{0};

    // delayed checks (checkSoon) run on this private pool, so no event loop is needed and the shared pools are not held up
    QThreadPool m_timers;
    std::mutex m_stopMx;
    std::condition_variable m_stopCv;
    bool m_stopping = false;
};

} // namespace tl
