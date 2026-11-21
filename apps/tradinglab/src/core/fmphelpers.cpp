// Pure chart-data helpers. Port of parts of renderer/src/data/fmp.ts and renderer/src/chart/useChartData.ts.

#include "fmphelpers.h"

#include "core/nytime.h"
#include "core/timeframe.h"

#include <QDateTime>
#include <QTimeZone>

#include <cmath>

namespace tl {

static bool present(const QVariant &v)
{
    return v.isValid() && !isNull(v) && !(v.metaType().id() == QMetaType::QString && v.toString().isEmpty());
}

std::optional<double> pick(const Rec &o, const QStringList &names)
{
    for (const QString &n : names) {
        const QVariant v = o.value(n);
        if (!present(v))
            continue;
        const double d = toNumber(v);
        if (std::isfinite(d))
            return d;
    }
    return std::nullopt;
}

QString pickStr(const Rec &o, const QStringList &names)
{
    for (const QString &n : names) {
        const QVariant v = o.value(n);
        if (present(v))
            return v.toString();
    }
    return QString();
}

QString dash(std::optional<double> v, const std::function<QString(double)> &fmt)
{
    return v ? fmt(*v) : QStringLiteral("—");
}

int refreshEveryMs(const QString &interval)
{
    if (interval == QLatin1String("1min")) return 30'000;
    if (interval == QLatin1String("5min")) return 60'000;
    if (interval == QLatin1String("15min")) return 120'000;
    if (interval == QLatin1String("30min")) return 180'000;
    if (interval == QLatin1String("1hour") || interval == QLatin1String("4hour")) return 300'000;
    if (interval == QLatin1String("1day")) return 600'000;
    return 900'000; // 1week, 1month
}

int calendarDays(const QString &range, qint64 nowMs)
{
    if (range == QLatin1String("1D")) return 5;
    if (range == QLatin1String("5D")) return 9;
    if (range == QLatin1String("1M")) return 35;
    if (range == QLatin1String("3M")) return 100;
    if (range == QLatin1String("6M")) return 190;
    if (range == QLatin1String("YTD")) {
        if (nowMs < 0)
            nowMs = tl::nowMs();
        const int year = QDateTime::fromMSecsSinceEpoch(nowMs, QTimeZone::UTC).date().year();
        const qint64 jan1 = QDateTime(QDate(year, 1, 1), QTime(0, 0), QTimeZone::UTC).toMSecsSinceEpoch();
        return int(std::ceil(double(nowMs - jan1) / 86400000)) + 5;
    }
    if (range == QLatin1String("1Y")) return 370;
    if (range == QLatin1String("5Y")) return 1830;
    if (range == QLatin1String("MAX")) return 7300;
    return 0;
}

QString fetchInterval(const QString &interval)
{
    return intervalInfo(interval).intraday ? interval : QStringLiteral("1day");
}

QString loadFromDay(const QString &range, const QString &interval, qint64 nowMs)
{
    if (nowMs < 0)
        nowMs = tl::nowMs();
    const bool weekMonth = interval == QLatin1String("1week") || interval == QLatin1String("1month");
    const qint64 days = calendarDays(range, nowMs) + (weekMonth ? 45 : 0);
    return QDateTime::fromMSecsSinceEpoch(nowMs - days * 86400000, QTimeZone::UTC).date().toString(Qt::ISODate);
}

Bars lastBars(const Bars &candles, const QString &range, const QString &interval)
{
    const size_t n = size_t(std::max(0, barCount(range, interval)));
    if (n == 0) // slice(-0) is the whole array
        return candles;
    return candles.size() <= n ? candles : Bars(candles.end() - qsizetype(n), candles.end());
}

QString refreshFromDay(const Bars &prev)
{
    return prev.empty() ? QString() : utcDay(prev.back().time - 86400);
}

Bars mergeRefreshed(const Bars &prev, const Bars &fresh, const QString &range, const QString &interval)
{
    if (fresh.empty())
        return prev;
    Bars next;
    for (const Bar &c : prev)
        if (c.time < fresh.front().time)
            next.push_back(c);
    next.insert(next.end(), fresh.begin(), fresh.end());
    return lastBars(next, range, interval);
}

bool sameCandles(const Bars &a, const Bars &b)
{
    if (a.size() != b.size() || a.empty())
        return false;
    const Bar &x = a.back(), &y = b.back();
    return a.front().time == b.front().time && x.time == y.time && x.open == y.open && x.high == y.high && x.low == y.low
        && x.close == y.close && x.volume == y.volume;
}

QString chartKey(const QString &symbol, const QString &range, const QString &interval)
{
    return symbol + QLatin1Char('|') + range + QLatin1Char('|') + interval;
}

bool sessionActive(qint64 epochSec)
{
    return isMarketOpen(epochSec) || isMarketOpen(epochSec - 180);
}

} // namespace tl
