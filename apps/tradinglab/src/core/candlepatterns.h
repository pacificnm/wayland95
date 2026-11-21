#pragma once

// Candlestick pattern detectors: exact rules for every pattern in the library, shared by the Strategies gallery
// ("Measure") and Claude's find_candle_pattern tool. The teaching text and drawn examples live in content/patterns.json.
// Port of the detector half of shared/candlePatterns.ts.

#include "bar.h"

#include <QHash>
#include <QString>
#include <QStringList>

#include <vector>

namespace tl {

// Pattern ids in library order (upstream PATTERN_IDS; the same order as patterns.json "patternIds").
const QStringList &patternIds();
bool isPatternId(const QString &id);
// Number of candles that make up the pattern (the last bars of its example), or 0 for an unknown id.
int patternSize(const QString &id);

// Whether the pattern completes on bars[index], judged the way findPattern judges it (the average range of the up to 10
// bars before the pattern's first candle sets the scale; fewer than 5 bars of history means no match). Unknown id: false.
bool detect(const QString &id, const Bars &bars, int index);

// Indexes of the bar that completes the pattern, oldest first. Bars before index 5 are skipped (no context to judge by).
// Unknown id: empty.
std::vector<int> findPattern(const QString &id, const Bars &bars);

// Match count for every pattern (the gallery's Measure button).
QHash<QString, int> countPatterns(const Bars &bars);

} // namespace tl
