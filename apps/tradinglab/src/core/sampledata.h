#pragma once

// Deterministic per-symbol sample candles, shown only while no market-data key is configured, and the weekly/monthly
// aggregation of daily bars that real loads use too. Port of renderer/src/data/candles.ts (and `rng` from
// renderer/src/data/sample.ts).

#include "core/bar.h"

#include <QString>

#include <functional>

namespace tl {

// sample.ts rng: a seeded generator (mulberry32) per symbol; same symbol + salt, same sequence
std::function<double()> sampleRng(const QString &symbol, quint32 salt = 0);

// Daily bars into weekly ("week") or monthly ("month") bars, keyed in UTC.
Bars aggregate(const Bars &daily, const QString &by);

// Synthetic candles for a symbol/range/interval (weekdays only, the newest ending today UTC).
// nowMs: the clock (-1 = now), for tests.
Bars sampleCandles(const QString &symbol, const QString &range, const QString &interval, qint64 nowMs = -1);

} // namespace tl
