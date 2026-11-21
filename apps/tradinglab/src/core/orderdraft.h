#pragma once

// The order ticket's state, the order it describes, and the price lines the chart shows for tickets, working orders
// and the open position. Port of renderer/src/chart/orderDraft.ts. (SIDE_LABEL / TYPE_LABEL, which that file only
// re-exported from shared/trade.ts, belong to the trade port.)
//
// Draft Rec (inputs are strings so half-typed numbers survive re-renders):
//   {side: "buy"|"sell"|"sell_short"|"buy_to_cover", qty, type: "market"|"limit"|"stop"|"stop_limit"|"trailing_stop",
//    limit, stop, trailAmount, trailUnit: "$"|"%", tif: "day"|"gtc", strategy: "single"|"bracket"|"oco", target,
//    protectType: "stop"|"stop_limit"|"trailing_stop", protectStop, protectLimit, protectTrail, protectTrailUnit: "$"|"%",
//    source: "user"|"claude", note?, journal: bool (record this trade in the journal when sent), journalNote,
//    journalId?: number (an existing journal entry, e.g. Claude's idea, this ticket belongs to)}
// OrderSpec Rec (shared/trade.ts): {symbol, side, qty, type, limit?, stop?, trailAmount?, trailUnit?, tif, strategy,
//    target?, protect?: {type, stop?, limit?, trailAmount?, trailUnit?}, source?, note?}  — absent keys = upstream undefined
// OrderLine Rec (drawings.ts): {id, price, label, color, style: "draft"|"working"|"position", draggable}

#include "core/util.h"

#include <optional>

namespace tl {

Rec emptyDraft(const QString &side = QStringLiteral("buy"));
Rec draftToSpec(const Rec &draft, const QString &symbol);   // upstream toSpec
Rec draftFromSpec(const Rec &spec);                          // upstream fromSpec

// Lines shown on the chart while the ticket is open (ids "draft:entry", "draft:stop", "draft:target").
List draftLines(const Rec &draft, const QString &symbol, std::optional<double> last);
// Working/pending orders and the open position for `symbol`, shown as chart lines (ids "pos:SYM", "order:<id>").
// `orders` are OrderRow Recs {id, symbol, side, qty, type, status, role, limit_price, stop_price}; `positions` are
// PositionView Recs {symbol, qty, avg, unrealized}.
List tradeLines(const QString &symbol, const List &orders, const List &positions);

} // namespace tl
