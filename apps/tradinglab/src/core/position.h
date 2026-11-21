#pragma once

// Position sizing and risk/reward math, shared by the Calculator screen and Claude's tools. Port of shared/position.ts.
//
// The input and the result are records with upstream's field names:
//   input  {side: 'long'|'short', entry, stop: {mode: 'price', price} | {mode: 'percent', pct} | {mode: 'atr', mult},
//           target: {mode: 'price', price} | {mode: 'percent', pct} | {mode: 'r', r} | null, risk: {mode: 'percent', pct} | {mode: 'dollars', dollars},
//           account, buyingPower (null when unknown), investedNow, atr (or null), winRate (0..1 or null), commission}
//   rules  {maxRiskPct, maxPositionPct, maxTotalMode: 'percent'|'dollars', maxTotalPct, maxTotalDollars}
//   result {ok, errors, warnings, entry, stop, target, riskPerShare, shares, binding, limits {byRisk, byPosition, byTotal, byBuyingPower},
//           positionValue, pctOfAccount, risk, riskPct, riskBudget, riskBudgetRequested, reward, rr, breakEvenWinRate, ev, evR, stopPct, stopAtr,
//           maxPositionDollars, maxTotalDollars, remainingTotalDollars, whatIf [{riskPct, shares, value, risk}], scenarios [{label, price, pl}]}

#include "util.h"

namespace tl {

// The user's standing limits.
struct Rules
{
    // max % of the account to lose if the stop is hit
    double maxRiskPct = 1;
    // max % of the account in one position
    double maxPositionPct = 25;
    // max total ever invested across all positions: a % of the account or a fixed dollar amount ('percent' | 'dollars')
    QString maxTotalMode = QStringLiteral("percent");
    double maxTotalPct = 80;
    double maxTotalDollars = 50000;

    static Rules fromRec(const Rec &r); // missing fields take DEFAULT_RULES' values
    Rec toRec() const;
};

// DEFAULT_RULES
inline Rules defaultRules() { return Rules{}; }
inline Rec defaultRulesRec() { return Rules{}.toRec(); }

Rec calculate(const Rec &input, const Rules &rules);
inline Rec calculate(const Rec &input, const Rec &rules) { return calculate(input, Rules::fromRec(rules)); }

} // namespace tl
