#pragma once

// Market overview logic: sector/industry tables and the movers lists. Port of renderer/src/data/market.ts.
//   FmpMarket   {date, exchange, sectors, industries, sectorPe, industryPe, gainers, losers, actives (FMP records), asOf}
//   MarketModel {date, exchange, sectors: [Sector], industries: [Sector], gainers/losers/actives: [Mover], asOf}
//   Sector {name, change, pe}   Mover {symbol, name, price, change, pct, exchange}

#include "util.h"

namespace tl {

Rec buildMarket(const Rec &fmpMarket);

// MoverFilter {minPrice, hideFunds, exchange: 'all'|'NASDAQ'|'NYSE'|'AMEX'}
Rec defaultMoverFilter();
// Mutual-fund tickers (four letters ending in X) and anything named like an ETF, fund or trust.
bool isFundMover(const Rec &mover);
List filterMovers(const List &list, const Rec &f, int limit = 12);

// data/fmp.ts helpers: the first of `names` that holds a finite number (or a numeric string) / a non-empty value
QVariant pickNum(const Rec &o, std::initializer_list<const char *> names); // number or null
QString pickStr(const Rec &o, std::initializer_list<const char *> names);

} // namespace tl
