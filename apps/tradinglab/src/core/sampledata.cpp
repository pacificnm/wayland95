// Sample candles and weekly/monthly aggregation. Port of renderer/src/data/candles.ts and rng() in data/sample.ts.

#include "sampledata.h"

#include "core/timeframe.h"
#include "core/util.h"

#include <QDateTime>
#include <QTimeZone>

#include <cmath>
#include <deque>

namespace tl {

std::function<double()> sampleRng(const QString &symbol, quint32 salt)
{
    // [...symbol].reduce((a, c) => (a * 31 + c.charCodeAt(0)) >>> 0, 7)
    quint32 seed = 7;
    for (const QChar c : symbol)
        seed = quint32((quint64(seed) * 31 + c.unicode()) & 0xffffffffu);
    seed += salt;
    return [seed]() mutable {
        seed += 0x6d2b79f5u;
        quint32 t = seed;
        t = (t ^ (t >> 15)) * (t | 1u);
        t ^= t + (t ^ (t >> 7)) * (t | 61u);
        return double(t ^ (t >> 14)) / 4294967296.0;
    };
}

namespace {

constexpr int SESSION_MIN = 390;
constexpr int OPEN_UTC_MIN = 13 * 60 + 30; // 09:30 New York, ignoring DST

std::vector<qint64> weekdaysBack(int count, qint64 nowMs)
{
    std::deque<qint64> days;
    QDate d = QDateTime::fromMSecsSinceEpoch(nowMs, QTimeZone::UTC).date();
    while (int(days.size()) < count) {
        const int wd = d.dayOfWeek(); // 1 = Monday ... 7 = Sunday
        if (wd != 6 && wd != 7)
            days.push_front(QDateTime(d, QTime(0, 0), QTimeZone::UTC).toSecsSinceEpoch());
        d = d.addDays(-1);
    }
    return std::vector<qint64>(days.begin(), days.end());
}

quint32 saltOf(const QString &s)
{
    quint32 sum = 0;
    for (const QChar c : s)
        sum += c.unicode();
    return sum;
}

Bars walk(const QString &symbol, const QString &salt, const std::vector<qint64> &times, double sigma, double volBase)
{
    auto r = sampleRng(symbol, saltOf(salt));
    double price = 40 + sampleRng(symbol)() * 400;
    Bars out;
    out.reserve(times.size());
    for (qint64 time : times) {
        const double open = price;
        const double close = open * (1 + (r() - 0.49) * sigma * 2);
        price = close;
        Bar b;
        b.time = time;
        b.open = open;
        b.close = close;
        b.high = std::max(open, close) * (1 + r() * sigma * 0.5);
        b.low = std::min(open, close) * (1 - r() * sigma * 0.5);
        b.volume = std::round(volBase * (0.5 + r()));
        out.push_back(b);
    }
    return out;
}

Bars tail(const Bars &b, int n)
{
    if (n <= 0 || int(b.size()) <= n) // slice(-0) is the whole array
        return b;
    return Bars(b.end() - n, b.end());
}

} // namespace

Bars aggregate(const Bars &daily, const QString &by)
{
    Bars out;
    QString key;
    const bool month = by == QLatin1String("month");
    for (const Bar &c : daily) {
        QString k;
        if (month) {
            const QDate d = QDateTime::fromSecsSinceEpoch(c.time, QTimeZone::UTC).date();
            k = QString::number(d.year()) + QLatin1Char('-') + QString::number(d.month() - 1);
        } else {
            const QDate d = QDateTime::fromSecsSinceEpoch(c.time, QTimeZone::UTC).date();
            k = QString::number(d.year()) + QLatin1Char('-') + QString::number(qint64(std::floor((double(c.time) / 86400 + 3) / 7)));
        }
        if (k != key || out.empty()) {
            out.push_back(c);
            key = k;
        } else {
            Bar &last = out.back();
            last.high = std::max(last.high, c.high);
            last.low = std::min(last.low, c.low);
            last.close = c.close;
            last.volume += c.volume;
        }
    }
    return out;
}

Bars sampleCandles(const QString &symbol, const QString &range, const QString &interval, qint64 nowMs)
{
    if (nowMs < 0)
        nowMs = tl::nowMs();
    const IntervalInfo &info = intervalInfo(interval);
    const int n = barCount(range, interval);
    if (interval == QLatin1String("1week") || interval == QLatin1String("1month")) {
        const Bars daily = walk(symbol, QStringLiteral("1day"), weekdaysBack(tradingDays(range, nowMs) + 25, nowMs), 0.009, 3e7);
        return tail(aggregate(daily, interval == QLatin1String("1week") ? QStringLiteral("week") : QStringLiteral("month")), n);
    }
    if (!info.intraday)
        return walk(symbol, interval, weekdaysBack(n, nowMs), 0.009, 3e7);

    const double step = double(SESSION_MIN) / info.barsPerDay;
    const int nslots = int(std::round(info.barsPerDay));
    const std::vector<qint64> days = weekdaysBack(int(std::ceil(double(n) / nslots)), nowMs);
    std::vector<qint64> times;
    times.reserve(days.size() * size_t(nslots));
    for (qint64 day : days)
        for (int k = 0; k < nslots; ++k) // upstream times can be fractional seconds (390/7 min steps); whole seconds here
            times.push_back(day + qint64((OPEN_UTC_MIN + k * step) * 60));
    if (int(times.size()) > n)
        times.erase(times.begin(), times.end() - n);
    const double sigma = 0.009 * std::sqrt(step / SESSION_MIN);
    return walk(symbol, interval, times, sigma, 3e7 / info.barsPerDay);
}

} // namespace tl
