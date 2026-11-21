#pragma once

// Chart ranges and bar intervals: which pairs make a sensible chart, and which the user's data plan allows.
// Port of renderer/src/chart/timeframe.ts. Ids are kept as the upstream strings ("1D", "1Y", "5min", "1day"...).
//
// Interval ids mirror FMP's chart endpoints: /historical-chart/{1min,5min,15min,30min,1hour,4hour}
// and the end-of-day series for daily. Weekly and monthly bars are built locally from daily.

#include <QString>
#include <QStringList>
#include <QVariantList>

#include <vector>

namespace tl {

struct IntervalInfo
{
    QString id;    // "1min" | "5min" | "15min" | "30min" | "1hour" | "4hour" | "1day" | "1week" | "1month"
    QString label; // "1 Min" ... "Month"
    double barsPerDay;
    bool intraday;
};

struct RangeInfo
{
    QString id;    // "1D" | "5D" | "1M" | "3M" | "6M" | "YTD" | "1Y" | "5Y" | "MAX"
    QString label; // "1 Day" ... "Max"
};

const std::vector<IntervalInfo> &intervals(); // INTERVALS
const std::vector<RangeInfo> &ranges();       // RANGES
bool isInterval(const QString &id);
bool isRange(const QString &id);

// upstream's `find(...)!`: falls back to the daily entry for an unknown id
const IntervalInfo &intervalInfo(const QString &interval);

// trading days a range spans (YTD counts from 1 Jan UTC of `nowMs`'s year; -1 = now)
int tradingDays(const QString &range, qint64 nowMs = -1);
// bars a range/interval pair needs: ceil(tradingDays * barsPerDay)
int barCount(const QString &range, const QString &interval);

// Intervals the user's FMP plan does not include. Set from the connection check; every control and tool goes
// through isAllowed, so one list keeps the toolbar, Claude's set_chart and strategy setups consistent.
// Daily bars are never blocked: without them there is no chart. Thread-safe.
void setBlockedIntervals(const QStringList &ids);
bool isBlocked(const QString &interval);
// not blocked, and between 4 and 10,000 bars
bool isAllowed(const QString &range, const QString &interval);

std::vector<IntervalInfo> allowedIntervals(const QString &range);
// Ranges that still have at least one usable interval (a 1-day range needs intraday bars).
std::vector<RangeInfo> availableRanges();

// Keep the interval if it works for the range, otherwise the closest one that does (in INTERVALS order).
QString coerceInterval(const QString &range, const QString &interval);

struct Timeframe { QString range, interval; };
// A range/interval pair that works with the current plan: the pair itself if usable, else the closest interval, else a daily chart.
Timeframe usableTimeframe(const QString &range, const QString &interval);

// For QML: [{id, label, barsPerDay, intraday}] and [{id, label}]
QVariantList intervalsAsVariant(const std::vector<IntervalInfo> &list = intervals());
QVariantList rangesAsVariant(const std::vector<RangeInfo> &list = ranges());

} // namespace tl
