#pragma once

// Technical indicators for the chart's studies. Port of renderer/src/chart/indicators.ts (same maths, same warm-up).
//
// "No value" convention: upstream returned `(number | null)[]`; here a Series is std::vector<double> and NAN stands
// for null (warm-up bars, gaps). Inputs follow the same rule: a NAN input is treated as upstream's null (sma, ema, rma,
// wma skip or reject it exactly as they did null). Lengths are bar counts: upstream received them as JS numbers, so a
// fractional length produced odd results there; here they are ints (callers truncate) and lengths < 1 never index
// outside the series.

#include "core/bar.h"

#include <QString>

#include <array>
#include <vector>

namespace tl::ind {

using Series = std::vector<double>; // NAN = no value (upstream null)

inline bool has(double v) { return v == v; } // not NAN

Series sma(const Series &v, int n);
// Starts at the first run of n non-null values (seeded with their SMA).
Series ema(const Series &v, int n);
// Wilder's smoothing (RMA)
Series rma(const Series &v, int n);
Series wma(const Series &v, int n);
// population standard deviation over n bars
Series stdev(const Series &v, int n);
Series highest(const Series &v, int n);
Series lowest(const Series &v, int n);

Series closes(const Bars &c);
Series highs(const Bars &c);
Series lows(const Bars &c);

Series trueRange(const Bars &c);
Series atr(const Bars &c, int n);
// note: like upstream, an empty input gives a one-element series {NAN}
Series rsi(const Series &close, int n);
// cumulative from the first bar (falls back to the close while volume is 0)
Series vwap(const Bars &c);
Series obv(const Bars &c);

struct AdxResult { Series adx, plus, minus; };
AdxResult adx(const Bars &c, int n);

Series mfi(const Bars &c, int n);
Series cci(const Bars &c, int n);
Series parabolicSar(const Bars &c, double step, double max);
// Heikin Ashi candles (time and volume kept from the source bar)
Bars heikinAshi(const Bars &c);

struct SupertrendResult { Series values; std::vector<bool> up; };
SupertrendResult supertrend(const Bars &c, int n, double mult);

struct IchimokuResult { Series tenkan, kijun, spanA, spanB; };
IchimokuResult ichimoku(const Bars &c, int conv, int base, int spanB);

// Pivot levels for each block of `period` bars, computed from the previous block's H/L/C.
// Keys in upstream's order: P, R1, R2, R3, S1, S2, S3.
struct PivotsResult
{
    static constexpr std::array<const char *, 7> keys{"P", "R1", "R2", "R3", "S1", "S2", "S3"};
    std::array<Series, 7> levels; // same order as keys
};
PivotsResult pivots(const Bars &c, int period, bool fib);

} // namespace tl::ind
