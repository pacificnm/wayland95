#pragma once

// New York wall-clock helpers (FMP intraday timestamps and the regular trading session are in ET). Port of shared/nytime.ts.

#include <QString>

namespace tl {

struct NyParts
{
    int year, month, day, hour, minute, second;
    int weekday; // 0 = Sunday
};

NyParts nyParts(qint64 epochSec);
// "2026-03-04 09:30:00" (New York time) -> UTC epoch seconds
qint64 nyToEpoch(const QString &s);
// YYYY-MM-DD in New York
QString nyDate(qint64 epochSec);
// Regular session: Mon–Fri 09:30–16:00 ET (market holidays are not modelled)
bool isMarketOpen(qint64 epochSec);
// The next 16:00 ET weekday close strictly after epochSec
qint64 sessionCloseAfter(qint64 epochSec);
// Pre-market 04:00–09:30 and after-hours 16:00–20:00 ET, weekdays
bool isExtendedHours(qint64 epochSec);
// Seconds until the next session boundary (04:00, 09:30, 16:00 or 20:00 ET on a weekday)
qint64 secondsToNextSessionBoundary(qint64 epochSec);
// YYYY-MM-DD in UTC (`new Date(ms).toISOString().slice(0, 10)`)
QString utcDay(qint64 epochSec);

} // namespace tl
