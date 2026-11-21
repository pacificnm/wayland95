#pragma once

// Options chain analytics shown on the Option Stats screen (pure, so the screen and Claude share them). Port of shared/options.ts.
// (renderer/src/data/options.ts only fetches: the chain through IPC and closes for historicalVol, so it has no pure logic to port.)
//
// Records keep upstream's field names:
//   OptionContract {exp: 'YYYY-MM-DD', type: 'C'|'P', strike, bid, ask, last, volume, oi, iv, delta, gamma, theta, vega, theo, change}
//                  (iv is a fraction; nullable fields are null)
//   OptionsChain   {symbol, price, change, changePct, iv30, fetchedAt, expirations: [{date, dte}], contracts: [OptionContract], source}
// There is no Black-Scholes here: the greeks come from the feed.

#include "util.h"

#include <QVector>

#include <optional>

namespace tl {

double optionMid(const Rec &c);
List contractsFor(const Rec &chain, const QString &exp);
// The strike closest to the underlying price among a set of contracts.
std::optional<double> atmStrike(const List &cs, double price);
// Average IV of the at-the-money call and put.
std::optional<double> atmIv(const List &cs, double price);
// {strike, cost} or null
QVariant straddle(const List &cs, double price);
// One-standard-deviation move implied by IV over `dte` days.
inline double expectedMove(double price, double iv, double dte) { return price * iv * std::sqrt(std::max(dte, 0.5) / 365); }
// {callVolume, putVolume, callOi, putOi, pcVolume, pcOi}
Rec activity(const List &cs);
// The strike at which option holders would collectively lose the most (writers pay the least).
std::optional<double> maxPain(const List &cs);
// [{date, dte, iv}]
List termStructure(const Rec &chain);
// Strikes for one expiration, `n` each side of the money (or all when n is empty): [{strike, call?, put?}]
List strikeRows(const List &cs, double price, std::optional<int> n);
// Annualised 30-day realised volatility from closing prices (oldest first).
std::optional<double> historicalVol(const QVector<double> &closes, int days = 30);
// e.g. "Sep 30 335 C"
QString contractLabel(const Rec &c);

} // namespace tl
