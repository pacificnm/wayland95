#pragma once

// JavaScript number <-> string conversions the chart code relies on: `String(n)`, `parseFloat(s)` and `parseInt(s, 10)`.
// Used where upstream kept half-typed numbers as strings (order ticket, position calculator) or printed numbers into text.
// Written for the port of renderer/src/chart (orderDraft.ts, calcState.ts, setup.ts); not a port of one upstream file.

#include <QString>

#include <cmath>

namespace tl {

// `String(n)` / `${n}`: shortest round-trip digits, fixed notation for 1e-7 <= |n| < 1e21, otherwise "1.5e+21" style
QString jsString(double n);
// `parseFloat(s)`: the longest numeric prefix after leading whitespace, NaN when there is none
double jsParseFloat(const QString &s);
// `parseInt(s, 10)`: leading decimal digits (with sign) after whitespace, NaN when there are none
double jsParseInt(const QString &s);
// `Math.round(x)`: nearest integer, halves toward +Infinity
inline double jsRound(double x)
{
    if (!std::isfinite(x))
        return x;
    const double f = std::floor(x);
    return x - f >= 0.5 ? f + 1 : f;
}

} // namespace tl
