#pragma once

// Pure helpers from the chart's data layer: FMP field picking, how far back a chart load reads, how a refresh merges
// new bars, and how often an open chart refreshes. Port of the pure parts of renderer/src/data/fmp.ts plus chartKey /
// sessionActive from renderer/src/chart/useChartData.ts. (The fetching itself — loadCandles/refreshCandles — is I/O
// and lives with the FMP client; these are the pieces it is made of.)

#include "core/bar.h"
#include "core/util.h"

#include <QStringList>

#include <functional>
#include <optional>

namespace tl {

// FMP fields vary a little by endpoint/version; take the first one present (a finite number, or a numeric string).
std::optional<double> pick(const Rec &o, const QStringList &names);
QString pickStr(const Rec &o, const QStringList &names);
// "—" for a missing value, else fmt(v)
QString dash(std::optional<double> v, const std::function<QString(double)> &fmt);

// How often an open chart re-reads its latest bars: roughly once per few candles on short intervals, never faster
// than the feed's own cache (30 s intraday, 10 min daily).
int refreshEveryMs(const QString &interval);

// calendar days loadCandles reads back for a range (YTD from 1 Jan UTC of nowMs's year; -1 = now)
int calendarDays(const QString &range, qint64 nowMs = -1);
// the interval actually requested from FMP: intraday ids as they are; daily, weekly and monthly all read daily bars
QString fetchInterval(const QString &interval);
// `from` (YYYY-MM-DD, UTC) for a full load: calendarDays + 45 more for weekly/monthly (built from daily bars)
QString loadFromDay(const QString &range, const QString &interval, qint64 nowMs = -1);
// the last barCount(range, interval) bars (`candles.slice(-barCount(range, interval))`)
Bars lastBars(const Bars &candles, const QString &range, const QString &interval);

// Intraday refresh: re-read from the day before the last candle ...
QString refreshFromDay(const Bars &prev);
// ... and replace the tail: prev bars older than the first new bar, then the new bars, trimmed to barCount.
Bars mergeRefreshed(const Bars &prev, const Bars &fresh, const QString &range, const QString &interval);
// same length, same first time and identical last bar: nothing changed, so the chart is not rebuilt
bool sameCandles(const Bars &a, const Bars &b);

// "SYMBOL|range|interval": the view (zoom and scroll) survives a rebuild only within the same key
QString chartKey(const QString &symbol, const QString &range, const QString &interval);
// The regular session, plus a few minutes after the close so the last bars are picked up (refreshes run only then)
bool sessionActive(qint64 epochSec);

} // namespace tl
