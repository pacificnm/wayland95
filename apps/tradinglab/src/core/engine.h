#pragma once

// Paper-trading engine. Port of main/trading.ts (createEngine).
// Orders fill against real intraday bars (1-minute when the plan has them, otherwise the finest it does): a stop or limit is filled if the bar's
// range touched it (at the trigger price, or at the open if the bar gapped through). When a stop and a
// target are both touched in the same bar, the stop is assumed to have been hit first.
// Not modelled: partial fills, commissions/slippage, extended hours, market holidays.
//
// Calls block (market data comes over the network); they may come from several handler threads at once,
// so every public method holds the engine's lock: one order book, changed by one caller at a time.

#include "bar.h"
#include "fmpcaps.h"
#include "trade.h"
#include "util.h"

#include <functional>
#include <mutex>
#include <optional>

namespace tl {

struct Market
{
    std::function<std::optional<double>(const QString &symbol)> quote;
    // bars of the current grain from `fromEpoch` until now, ascending
    std::function<Bars(const QString &symbol, qint64 fromEpoch)> bars;
    // the bar size the plan can supply for filling orders (nullopt = no intraday bars at all). Unset = 1-minute.
    std::function<std::optional<FillGrain>()> grain;
};

class Engine
{
public:
    Engine(Market market, std::function<qint64()> activeAccount, std::function<qint64()> clock = &nowSec);

    // {account, positions, orders, fills, at, fillBars} (TradeSnapshot)
    Rec snapshot();
    // one account's balances and positions, for screens that follow an account without being on it (the Portfolio screen)
    Rec view(qint64 accountId);
    // {ok:true, orderIds, filledNow, events} or {ok:false, errors}
    Rec place(const Rec &spec);
    Rec cancel(qint64 id);
    Rec modify(qint64 id, const Rec &patch);
    Rec closePosition(const QString &symbol);
    // clears the active account's orders, fills and positions and restarts it from startingCash
    void reset(double startingCash);
    // checks every symbol with working orders; returns the events ({text})
    List tick();
    List process(const QString &symbol);
    // {cash, startingCash, equity, buyingPower, positions, realized, unrealized}
    Rec values(qint64 accountId);
    double positionQty(const QString &symbol);

private:
    struct St { std::optional<double> extreme; bool triggered; };
    std::optional<FillGrain> grain() const;
    qint64 nextBarTime(qint64 t) const;
    Rec account(qint64 id);
    double positionQtyOf(qint64 accountId, const QString &symbol);
    double reserved(qint64 accountId);
    qint64 insert(qint64 accountId, const QString &group, const OrderSpec &spec, const Rec &o, qint64 now);
    void cancelRow(qint64 id, const QString &reason, const QString &status = QStringLiteral("cancelled"));
    bool fill(const Rec &o, double price, qint64 time, List &events);
    std::optional<double> tryBar(const Rec &o, const Bar &bar, St &st, bool closed);
    std::optional<double> quoteOrNull(const QString &symbol);

    Market m_market;
    std::function<qint64()> m_active;
    std::function<qint64()> m_clock;
    std::recursive_mutex m_lock;
};

} // namespace tl
