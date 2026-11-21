#pragma once

// Paper market orders for a linked portfolio's buy or sell plan. Port of main/portfolioOrders.ts.
//
// INVARIANT 1: this exists only for two buttons the user clicks after a confirmation ("Buy these in the paper account" and
// "Sell these in the paper account"). No Claude tool may ever call it, directly or indirectly.

#include "util.h"

#include <functional>
#include <optional>

namespace tl {

struct OrderDeps
{
    // the portfolio as PortfolioStore::get returns it (a Pf record), or none
    std::function<std::optional<Rec>(qint64 id)> portfolio;
    std::function<qint64()> activeAccountId;
    // the engine's place(): spec is an OrderSpec record; returns {ok: true, orderIds, filledNow, events: [{text}]} or {ok: false, errors}
    std::function<Rec(const Rec &spec)> place;
};

// side: 'buy' | 'sell'; orders: [{symbol, qty}].
// -> {ok: true, outcomes: [{symbol, qty, ok, filledNow?, error?}], events: [{text}]} or {ok: false, error}
Rec placePortfolioOrders(const OrderDeps &deps, qint64 portfolioId, const QString &side, const List &orders);

} // namespace tl
