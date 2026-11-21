#pragma once

// Congressional trading logic: amount ranges, filing delays, the summary strip, filters and member grouping.
// Port of renderer/src/data/congress.ts. Trades are CongressTrade records (normalize.h).

#include "util.h"

#include <optional>

namespace tl {

struct AmountRange
{
    double lo = 0, hi = 0, mid = 0;
};
// "$15,001 - $50,000" -> its bounds and midpoint. Open-ended ranges ("Over $50,000,000") use the floor.
std::optional<AmountRange> amountRange(const QString &s);
// Days between the transaction and its public disclosure (members have up to 45 days to file).
std::optional<double> delayDays(const Rec &t);
bool isCongressBuy(const Rec &t);
bool isCongressSell(const Rec &t);

// {count, buys, sells, other, buyValue, sellValue, medianDelay, maxDelay, topSymbols: [{symbol, count, buys, sells, members}],
//  topMembers: [{member, memberId, chamber, count}], assetTypes: [{type, count}], from, to}
Rec summarizeCongress(const List &list);

// TradeFilter {kind: 'all'|'buys'|'sells', assetType: 'all'|'stocks', q, owner}
Rec defaultTradeFilter();
List filterTrades(const List &list, const Rec &f);
// Distinct members in a result set, most active first: [{key, member, memberId, chamber, district, count}]
List groupMembers(const List &list);

} // namespace tl
