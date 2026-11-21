#pragma once

// Portfolios: planned allocations and what the user holds, in SQLite (tables `portfolios` and `portfolio_items`, created here like
// watchlists). Pure bookkeeping: nothing here sends an order. Port of main/portfolio.ts.
//
// Results are upstream's PfResult records: {ok: true, ...} or {ok: false, error}. Portfolios come back as Pf records
// (see portfolio.h). Every call uses this thread's connection (Db::local()).

#include "util.h"

#include <functional>
#include <optional>

namespace tl {

class PortfolioStore
{
public:
    using Clock = std::function<qint64()>;

    // creates the tables (and the account_id column) when they are missing
    explicit PortfolioStore(Clock clock = &nowSec);
    static void initSchema();

    List list();
    std::optional<Rec> get(qint64 id);
    // {ok, id}; amount is optional (an invalid QVariant = not given)
    Rec create(const QVariant &name, const QVariant &amount = QVariant());
    // patch: {name?, amount?, mode?, fractional?, leftover?, accountId? (null unlinks)}
    Rec update(qint64 id, const Rec &patch);
    Rec remove(qint64 id);
    // input: {symbol, name?, kind?} -> {ok, id, symbol}
    Rec addItem(qint64 pid, const Rec &input);
    // patch: {targetPct?, shares?, cost?, manualPrice? (null clears), kind?, name?}
    Rec updateItem(qint64 id, const Rec &patch);
    Rec removeItem(qint64 id);
    // Sets several target percentages at once ("equal weight", "make it add up to 100"). targets: [{id, pct}]
    Rec setTargets(qint64 pid, const List &targets);
    // "I bought these": [{id, shares, price}] -> {ok, recorded}. Records only; it places no order.
    Rec recordBuys(qint64 pid, const List &buys);
    // "I sold these": [{id, shares}] -> {ok, recorded}. Records only; it places no order.
    Rec recordSells(qint64 pid, const List &sells);

private:
    Clock m_clock;
};

} // namespace tl
