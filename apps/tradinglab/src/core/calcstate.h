#pragma once

// The position calculator's screen state and how it becomes a calculation input. Port of renderer/src/chart/calcState.ts
// (the pure parts; fetchLast/fetchAtr did I/O: latestAtr below is the maths fetchAtr ran on 3 months of daily bars).
//
// CalcState Rec (values are strings so half-typed numbers survive re-renders):
//   {symbol, side: "long"|"short", followLive: bool, entry, accountBasis: "paper"|"custom", customAccount,
//    riskMode: "percent"|"dollars", riskPct, riskDollars, stopMode: "price"|"percent"|"atr", stopPrice, stopPct, stopAtr,
//    targetMode: "none"|"price"|"percent"|"r", targetPrice, targetPct, targetR, winRate, commission}
//
// CalcInput Rec (shared/position.ts), as toInput builds it:
//   {side, entry, stop: {mode:"price",price}|{mode:"percent",pct}|{mode:"atr",mult}, target: null|{mode:"price",price}|
//    {mode:"percent",pct}|{mode:"r",r}, risk: {mode:"percent",pct}|{mode:"dollars",dollars}, account, buyingPower: number|null,
//    investedNow, atr: number|null, winRate: number(0..1)|null, commission}
//   Unparseable numbers stay NaN (a double NaN in the Rec), exactly as upstream passed NaN on.

#include "core/bar.h"
#include "core/util.h"

#include <optional>

namespace tl {

// DEFAULT_CALC
Rec defaultCalc();
// `{...DEFAULT_CALC, ...saved}` (App.tsx on load and setCalc patches)
Rec normalizeCalc(const QVariant &stored);

struct CalcContext
{
    std::optional<double> last;
    std::optional<double> atr;
    double equity = 0;
    std::optional<double> buyingPower;
    double investedNow = 0;
};

Rec calcToInput(const Rec &calcState, const CalcContext &ctx);

// the last available ATR(n) value of the bars (14-day ATR from daily bars, so stops can be sized in ATRs)
std::optional<double> latestAtr(const Bars &bars, int n = 14);

} // namespace tl
