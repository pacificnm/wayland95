// Chart ranges and intervals. Port of renderer/src/chart/timeframe.ts.

#include "timeframe.h"

#include "core/jsnum.h"
#include "core/util.h"

#include <QDateTime>
#include <QSet>
#include <QTimeZone>

#include <algorithm>
#include <cmath>
#include <mutex>

namespace tl {

const std::vector<IntervalInfo> &intervals()
{
    static const std::vector<IntervalInfo> list{
        {QStringLiteral("1min"), QStringLiteral("1 Min"), 390, true},
        {QStringLiteral("5min"), QStringLiteral("5 Min"), 78, true},
        {QStringLiteral("15min"), QStringLiteral("15 Min"), 26, true},
        {QStringLiteral("30min"), QStringLiteral("30 Min"), 13, true},
        {QStringLiteral("1hour"), QStringLiteral("1 Hour"), 7, true},
        {QStringLiteral("4hour"), QStringLiteral("4 Hour"), 2, true},
        {QStringLiteral("1day"), QStringLiteral("Day"), 1, false},
        {QStringLiteral("1week"), QStringLiteral("Week"), 1.0 / 5, false},
        {QStringLiteral("1month"), QStringLiteral("Month"), 1.0 / 21, false},
    };
    return list;
}

const std::vector<RangeInfo> &ranges()
{
    static const std::vector<RangeInfo> list{
        {QStringLiteral("1D"), QStringLiteral("1 Day")}, {QStringLiteral("5D"), QStringLiteral("5 Days")}, {QStringLiteral("1M"), QStringLiteral("1 Month")},
        {QStringLiteral("3M"), QStringLiteral("3 Months")}, {QStringLiteral("6M"), QStringLiteral("6 Months")}, {QStringLiteral("YTD"), QStringLiteral("Year to Date")},
        {QStringLiteral("1Y"), QStringLiteral("1 Year")}, {QStringLiteral("5Y"), QStringLiteral("5 Years")}, {QStringLiteral("MAX"), QStringLiteral("Max")},
    };
    return list;
}

bool isInterval(const QString &id)
{
    return std::any_of(intervals().begin(), intervals().end(), [&](const IntervalInfo &i) { return i.id == id; });
}

bool isRange(const QString &id)
{
    return std::any_of(ranges().begin(), ranges().end(), [&](const RangeInfo &r) { return r.id == id; });
}

const IntervalInfo &intervalInfo(const QString &interval)
{
    for (const IntervalInfo &i : intervals())
        if (i.id == interval)
            return i;
    return intervals()[6]; // 1day
}

int tradingDays(const QString &range, qint64 nowMs)
{
    if (range == QLatin1String("1D")) return 1;
    if (range == QLatin1String("5D")) return 5;
    if (range == QLatin1String("1M")) return 21;
    if (range == QLatin1String("3M")) return 63;
    if (range == QLatin1String("6M")) return 126;
    if (range == QLatin1String("YTD")) {
        if (nowMs < 0)
            nowMs = tl::nowMs();
        const int year = QDateTime::fromMSecsSinceEpoch(nowMs, QTimeZone::UTC).date().year();
        const qint64 jan1 = QDateTime(QDate(year, 1, 1), QTime(0, 0), QTimeZone::UTC).toMSecsSinceEpoch();
        // Math.round: halves round up
        return std::max(1, int(jsRound((double(nowMs - jan1) / 86400000) * (5.0 / 7))));
    }
    if (range == QLatin1String("1Y")) return 252;
    if (range == QLatin1String("5Y")) return 1260;
    if (range == QLatin1String("MAX")) return 5040;
    return 0;
}

int barCount(const QString &range, const QString &interval)
{
    return int(std::ceil(tradingDays(range) * intervalInfo(interval).barsPerDay));
}

static constexpr int MIN_BARS = 4;
static constexpr int MAX_BARS = 10000;

static std::mutex blockedLock;
static QSet<QString> blockedSet;

void setBlockedIntervals(const QStringList &ids)
{
    QSet<QString> s;
    for (const QString &i : ids)
        if (i != QLatin1String("1day"))
            s.insert(i);
    std::lock_guard<std::mutex> g(blockedLock);
    blockedSet = s;
}

bool isBlocked(const QString &interval)
{
    std::lock_guard<std::mutex> g(blockedLock);
    return blockedSet.contains(interval);
}

bool isAllowed(const QString &range, const QString &interval)
{
    if (isBlocked(interval))
        return false;
    const int n = barCount(range, interval);
    return n >= MIN_BARS && n <= MAX_BARS;
}

std::vector<IntervalInfo> allowedIntervals(const QString &range)
{
    std::vector<IntervalInfo> out;
    for (const IntervalInfo &i : intervals())
        if (isAllowed(range, i.id))
            out.push_back(i);
    return out;
}

std::vector<RangeInfo> availableRanges()
{
    std::vector<RangeInfo> out;
    for (const RangeInfo &r : ranges())
        if (!allowedIntervals(r.id).empty())
            out.push_back(r);
    return out;
}

QString coerceInterval(const QString &range, const QString &interval)
{
    if (isAllowed(range, interval))
        return interval;
    const auto &all = intervals();
    auto indexOf = [&](const QString &id) {
        for (size_t i = 0; i < all.size(); ++i)
            if (all[i].id == id) return int(i);
        return -1;
    };
    const int from = indexOf(interval);
    std::vector<IntervalInfo> options = allowedIntervals(range);
    if (options.empty())
        return QStringLiteral("1day");
    // Array.prototype.sort is stable, so ties keep INTERVALS order (the shorter interval wins)
    std::stable_sort(options.begin(), options.end(), [&](const IntervalInfo &a, const IntervalInfo &b) {
        return std::abs(indexOf(a.id) - from) < std::abs(indexOf(b.id) - from);
    });
    return options.front().id;
}

Timeframe usableTimeframe(const QString &range, const QString &interval)
{
    const QString i = coerceInterval(range, interval);
    if (isAllowed(range, i))
        return {range, i};
    return {QStringLiteral("6M"), QStringLiteral("1day")};
}

QVariantList intervalsAsVariant(const std::vector<IntervalInfo> &list)
{
    QVariantList out;
    for (const IntervalInfo &i : list)
        out << Rec{{QStringLiteral("id"), i.id}, {QStringLiteral("label"), i.label}, {QStringLiteral("barsPerDay"), i.barsPerDay}, {QStringLiteral("intraday"), i.intraday}};
    return out;
}

QVariantList rangesAsVariant(const std::vector<RangeInfo> &list)
{
    QVariantList out;
    for (const RangeInfo &r : list)
        out << Rec{{QStringLiteral("id"), r.id}, {QStringLiteral("label"), r.label}};
    return out;
}

} // namespace tl
