#include "nytime.h"

#include <QDateTime>
#include <QRegularExpression>
#include <QTimeZone>

namespace tl {

static const QTimeZone &ny()
{
    static const QTimeZone tz(QByteArrayLiteral("America/New_York"));
    return tz;
}

NyParts nyParts(qint64 epochSec)
{
    const QDateTime t = QDateTime::fromSecsSinceEpoch(epochSec, ny());
    const QDate d = t.date();
    const QTime tm = t.time();
    return {d.year(), d.month(), d.day(), tm.hour(), tm.minute(), tm.second(), d.dayOfWeek() % 7};
}

qint64 nyToEpoch(const QString &s)
{
    const QStringList dt = s.trimmed().split(QRegularExpression(QStringLiteral("[ T]")));
    const QStringList d = dt.value(0).split(QLatin1Char('-'));
    const QStringList t = dt.value(1, QStringLiteral("00:00:00")).split(QLatin1Char(':'));
    const QDate date(d.value(0).toInt(), d.value(1).toInt(), d.value(2).toInt());
    const QTime time(t.value(0).toInt(), t.value(1).toInt(), t.value(2).toInt());
    if (!date.isValid())
        return 0;
    return QDateTime(date, time, ny()).toSecsSinceEpoch();
}

QString nyDate(qint64 epochSec)
{
    return QDateTime::fromSecsSinceEpoch(epochSec, ny()).date().toString(Qt::ISODate);
}

QString utcDay(qint64 epochSec)
{
    return QDateTime::fromSecsSinceEpoch(epochSec, QTimeZone::UTC).date().toString(Qt::ISODate);
}

bool isMarketOpen(qint64 epochSec)
{
    const NyParts p = nyParts(epochSec);
    const int m = p.hour * 60 + p.minute;
    return p.weekday >= 1 && p.weekday <= 5 && m >= 570 && m < 960;
}

qint64 sessionCloseAfter(qint64 epochSec)
{
    for (int d = 0; d < 10; ++d) {
        const qint64 t = epochSec + d * 86400;
        const NyParts p = nyParts(t);
        if (p.weekday < 1 || p.weekday > 5)
            continue;
        const qint64 close = nyToEpoch(nyDate(t) + QStringLiteral(" 16:00:00"));
        if (close > epochSec)
            return close;
    }
    return epochSec + 86400;
}

bool isExtendedHours(qint64 epochSec)
{
    const NyParts p = nyParts(epochSec);
    const int m = p.hour * 60 + p.minute;
    return p.weekday >= 1 && p.weekday <= 5 && ((m >= 240 && m < 570) || (m >= 960 && m < 1200));
}

qint64 secondsToNextSessionBoundary(qint64 epochSec)
{
    static const char *const marks[] = {" 04:00:00", " 09:30:00", " 16:00:00", " 20:00:00"};
    for (int d = 0; d < 10; ++d) {
        const qint64 t = epochSec + d * 86400;
        const NyParts p = nyParts(t);
        if (p.weekday < 1 || p.weekday > 5)
            continue;
        for (const char *hm : marks) {
            const qint64 b = nyToEpoch(nyDate(t) + QLatin1String(hm));
            if (b > epochSec)
                return b - epochSec;
        }
    }
    return 3600;
}

} // namespace tl
