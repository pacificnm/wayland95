#pragma once

// Stock screener logic: the filters FMP can apply, the ones the app applies itself, sorting, the summary strip and CSV export.
// Port of renderer/src/data/screener.ts (the PRESETS table lives in the content JSON; presetFilters takes one preset record).
//
// Filters {sector, industry, exchange: ''|'NASDAQ'|'NYSE'|'AMEX', country, capMin, capMax (market cap in $ millions), priceMin, priceMax,
//          volumeMin, betaMin, betaMax, yieldMin (%), relVolMin, kind: 'stocks'|'etfs'|'both', active, limit}
// Numbers in Filters are strings so half-typed values survive; blank means "no limit". Rows are ScreenerRow records (normalize.h).

#include "util.h"

#include <optional>

namespace tl {

// DEFAULT_FILTERS
Rec defaultScreenerFilters();
bool hasClientFilters(const Rec &f);
// The part FMP can filter (a ScreenerQuery record; absent keys mean "not set"). When a client-side filter is on we pull a big batch.
Rec toScreenerQuery(const Rec &f);
std::optional<double> relVolume(const Rec &row);
// annual dividend per share / price, in %
std::optional<double> dividendYield(const Rec &row);
// Filters FMP can't do (yield, relative volume) plus a safety net for the type filters.
List applyClientFilters(const List &rows, const Rec &f);
// key: marketCap | price | volume | relVol | beta | yield | symbol | name; dir 1 or -1. Missing values always sort last.
List sortScreenerRows(const List &rows, const QString &key, int dir);
// DEFAULT_FILTERS with the preset's `apply` on top: preset is {id, label, what, apply}
Rec presetFilters(const Rec &preset);
// {count, medianCap, sectors: [{name, count}] (top 8), exchanges: [{name, count}]}
Rec summarizeScreener(const List &rows);
QString screenerCsv(const List &rows);
// The same filters in the names Claude's run_screener tool uses (market cap in $ millions).
Rec screenerToolArgs(const Rec &f);

} // namespace tl
