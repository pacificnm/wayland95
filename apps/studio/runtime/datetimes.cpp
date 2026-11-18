#include "datetimes.h"

#include <QLocale>
#include <QRegularExpression>
#include <QTimeZone>

namespace DateTimes {

const QString kDateFormat = QStringLiteral("yyyy-MM-dd");
const QString kTimeFormat = QStringLiteral("HH:mm:ss");
const QString kDateTimeFormat = QStringLiteral("yyyy-MM-ddTHH:mm:ss");

// "today+7", "now - 2h": the moment, and how far from it (d days, w weeks,
// h hours, m minutes; days if there's no unit). False if it isn't one.
bool relative(const QString &text, QString *base, qint64 *seconds, bool *dayUnits)
{
    static const QRegularExpression re(QStringLiteral("^(today|now)\\s*(?:([+-])\\s*(\\d+)\\s*([dwhm]?))?$"),
                                       QRegularExpression::CaseInsensitiveOption);
    const QRegularExpressionMatch m = re.match(text.trimmed());
    if (!m.hasMatch())
        return false;
    *base = m.captured(1).toLower();
    qint64 n = m.captured(3).isEmpty() ? 0 : m.captured(3).toLongLong();
    if (m.captured(2) == QLatin1String("-"))
        n = -n;
    const QString unit = m.captured(4).toLower();
    *dayUnits = unit.isEmpty() || unit == QLatin1String("d") || unit == QLatin1String("w");
    *seconds = n * (unit == QLatin1String("w") ? 7 * 86400 : unit == QLatin1String("h") ? 3600
                    : unit == QLatin1String("m") ? 60 : 86400);
    return true;
}

QDate parseDate(const QVariant &v)
{
    if (v.typeId() == QMetaType::QDate)
        return v.toDate();
    if (v.typeId() == QMetaType::QDateTime)
        return v.toDateTime().toLocalTime().date();
    const QString s = v.toString().trimmed();
    QString base;
    qint64 seconds = 0;
    bool days = true;
    if (relative(s, &base, &seconds, &days))
        return days ? QDate::currentDate().addDays(seconds / 86400)
                    : QDateTime::currentDateTime().addSecs(seconds).date();
    QDate d = QDate::fromString(s, kDateFormat);
    if (!d.isValid())
        d = QDate::fromString(s, QStringLiteral("M/d/yyyy"));
    if (!d.isValid()) {
        // (a date and time is fine: its date)
        const QDateTime dt = QDateTime::fromString(QString(s).replace(QLatin1Char(' '), QLatin1Char('T')), Qt::ISODate);
        if (dt.isValid())
            d = dt.date();
    }
    return d;
}

QTime parseTime(const QVariant &v)
{
    if (v.typeId() == QMetaType::QTime)
        return v.toTime();
    if (v.typeId() == QMetaType::QDateTime)
        return v.toDateTime().toLocalTime().time();
    const QString s = v.toString().trimmed();
    QString base;
    qint64 seconds = 0;
    bool days = true;
    if (relative(s, &base, &seconds, &days))
        return base == QLatin1String("today") ? QTime(0, 0) : QTime::currentTime().addSecs(int(seconds % 86400));
    QTime t = QTime::fromString(s, kTimeFormat);
    if (!t.isValid())
        t = QTime::fromString(s, QStringLiteral("H:mm"));
    if (!t.isValid())
        t = QTime::fromString(s, QStringLiteral("h:mm AP"));
    return t;
}

QDateTime parseDateTime(const QVariant &v)
{
    if (v.typeId() == QMetaType::QDateTime)
        return v.toDateTime().toLocalTime();
    if (v.typeId() == QMetaType::QDate)
        return v.toDate().startOfDay();
    QString s = v.toString().trimmed();
    QString base;
    qint64 seconds = 0;
    bool days = true;
    if (relative(s, &base, &seconds, &days)) {
        const QDateTime from = base == QLatin1String("today") ? QDate::currentDate().startOfDay() : QDateTime::currentDateTime();
        // (a whole day on: the same time of day, whatever the clocks did)
        return days ? from.addDays(seconds / 86400) : from.addSecs(seconds);
    }
    if (s.size() > 10 && s.at(10) == QLatin1Char(' '))
        s[10] = QLatin1Char('T');
    QDateTime dt = QDateTime::fromString(s, Qt::ISODate);
    if (!dt.isValid()) {
        const QDate d = parseDate(s);
        if (d.isValid())
            dt = d.startOfDay();
    }
    return dt.isValid() ? dt.toLocalTime() : dt;
}

} // namespace DateTimes

using namespace DateTimes;

namespace {

bool hasDate(const QString &mode) { return mode != QLatin1String("Time"); }
bool hasTime(const QString &mode) { return mode != QLatin1String("Date"); }

QTimeZone zoneNamed(const QString &zone)
{
    if (zone.isEmpty())
        return QTimeZone::systemTimeZone();
    const QTimeZone tz(zone.toUtf8());
    return tz.isValid() ? tz : QTimeZone::systemTimeZone();
}

// a kept value as a QDateTime in this computer's time (a time alone: today's)
QDateTime localOf(const QString &stored, const QString &mode)
{
    if (mode == QLatin1String("Date"))
        return QDate::fromString(stored, kDateFormat).startOfDay();
    if (mode == QLatin1String("Time")) {
        const QTime t = QTime::fromString(stored, kTimeFormat);
        return t.isValid() ? QDateTime(QDate::currentDate(), t) : QDateTime();
    }
    return QDateTime::fromString(stored, Qt::ISODate);
}

QString keep(const QDateTime &dt, const QString &mode)
{
    if (!dt.isValid())
        return QString();
    if (mode == QLatin1String("Date"))
        return dt.date().toString(kDateFormat);
    if (mode == QLatin1String("Time"))
        return dt.time().toString(kTimeFormat);
    return dt.toString(kDateTimeFormat);
}

} // namespace

AppDates::AppDates(QObject *parent)
    : QObject(parent)
{
}

QString AppDates::stored(const QVariant &value, const QString &mode)
{
    if (!value.isValid() || value.isNull() || value.toString().trimmed().isEmpty())
        return QString();
    if (mode == QLatin1String("Date")) {
        const QDate d = parseDate(value);
        return d.isValid() ? d.toString(kDateFormat) : QString();
    }
    if (mode == QLatin1String("Time")) {
        const QTime t = parseTime(value);
        return t.isValid() ? t.toString(kTimeFormat) : QString();
    }
    const QDateTime dt = parseDateTime(value);
    return dt.isValid() ? dt.toString(kDateTimeFormat) : QString();
}

QString AppDates::pattern(const QString &format, bool time)
{
    const QLocale locale = QLocale::system();
    if (format.isEmpty() || format == QLatin1String("Short"))
        return time ? locale.timeFormat(QLocale::ShortFormat) : locale.dateFormat(QLocale::ShortFormat);
    if (format == QLatin1String("Long"))
        return time ? locale.timeFormat(QLocale::LongFormat) : locale.dateFormat(QLocale::LongFormat);
    return format;
}

QString AppDates::display(const QString &stored, const QString &mode, const QString &dateFormat,
                          const QString &timeFormat, const QString &zone)
{
    QDateTime dt = localOf(stored, mode);
    if (!dt.isValid())
        return QString();
    const bool zoned = !zone.isEmpty() && hasTime(mode);
    if (zoned)
        dt = dt.toTimeZone(zoneNamed(zone));
    const QLocale locale = QLocale::system();
    QStringList parts;
    if (hasDate(mode))
        parts << locale.toString(dt.date(), pattern(dateFormat, false));
    if (hasTime(mode))
        parts << locale.toString(dt.time(), pattern(timeFormat, true));
    if (zoned)
        parts << dt.timeZoneAbbreviation();
    return parts.join(QLatin1Char(' '));
}

QString AppDates::read(const QString &text, const QString &mode, const QString &dateFormat,
                       const QString &timeFormat, const QString &zone)
{
    QString s = text.trimmed();
    if (s.isEmpty())
        return QString();
    const bool zoned = !zone.isEmpty() && hasTime(mode);
    const QTimeZone tz = zoneNamed(zone);
    if (zoned) {
        // (as it's shown: the zone's abbreviation on the end)
        const QString abbreviation = QDateTime::currentDateTime().toTimeZone(tz).timeZoneAbbreviation();
        if (!abbreviation.isEmpty() && s.endsWith(abbreviation))
            s = s.left(s.size() - abbreviation.size()).trimmed();
    }
    const QLocale locale = QLocale::system();
    // as it's shown, then the computer's own short and long ways
    QStringList patterns;
    const QString dp = pattern(dateFormat, false), tp = pattern(timeFormat, true);
    const QStringList dates{ dp, pattern(QStringLiteral("Short"), false), pattern(QStringLiteral("Long"), false) };
    const QStringList times{ tp, pattern(QStringLiteral("Short"), true), QStringLiteral("H:mm"), QStringLiteral("h:mm AP"),
                             QStringLiteral("h:mm ap"), QStringLiteral("H:mm:ss") };
    QDateTime found;
    // (a two-digit year is in the hundred years around now, not 19xx)
    const int baseYear = QDate::currentDate().year() - 49;
    if (mode == QLatin1String("Date")) {
        for (const QString &p : dates) {
            QDate d = locale.toDate(s, p, baseYear);
            if (!d.isValid())
                d = QDate::fromString(s, p, baseYear);
            if (d.isValid()) {
                found = d.startOfDay();
                break;
            }
        }
    } else if (mode == QLatin1String("Time")) {
        for (const QString &p : times) {
            QTime t = locale.toTime(s, p);
            if (!t.isValid())
                t = QTime::fromString(s, p);
            if (t.isValid()) {
                found = QDateTime(QDate::currentDate(), t);
                break;
            }
        }
    } else {
        for (const QString &d : dates) {
            for (const QString &t : times) {
                QDateTime dt = locale.toDateTime(s, d + QLatin1Char(' ') + t, baseYear);
                if (!dt.isValid())
                    dt = QDateTime::fromString(s, d + QLatin1Char(' ') + t, baseYear);
                if (dt.isValid()) {
                    found = dt;
                    break;
                }
            }
            if (found.isValid())
                break;
        }
    }
    if (found.isValid()) {
        if (zoned)
            found = QDateTime(found.date(), found.time(), tz).toLocalTime();
        return keep(found, mode);
    }
    // the way the database reads it (ISO, today+7...): this computer's time
    return stored(s, mode);
}

QString AppDates::toZone(const QString &stored, const QString &mode, const QString &zone)
{
    const QDateTime dt = localOf(stored, mode);
    if (!dt.isValid())
        return QString();
    if (zone.isEmpty() || !hasTime(mode))
        return dt.toString(kDateTimeFormat);
    const QDateTime there = dt.toTimeZone(zoneNamed(zone));
    return QDateTime(there.date(), there.time()).toString(kDateTimeFormat);
}

QString AppDates::fromZone(const QString &shown, const QString &mode, const QString &zone)
{
    const QDateTime there = QDateTime::fromString(shown, Qt::ISODate);
    if (!there.isValid())
        return QString();
    if (zone.isEmpty() || !hasTime(mode))
        return keep(there, mode);
    return keep(QDateTime(there.date(), there.time(), zoneNamed(zone)).toLocalTime(), mode);
}

QStringList AppDates::timeZones()
{
    QStringList out;
    const auto ids = QTimeZone::availableTimeZoneIds();
    for (const QByteArray &id : ids) {
        if (id.contains('/') && !id.startsWith("Etc/"))
            out << QString::fromUtf8(id);
    }
    out << QStringLiteral("UTC");
    return out;
}

QString AppDates::example(const QString &format, bool time)
{
    const QLocale locale = QLocale::system();
    const QDateTime now = QDateTime::currentDateTime();
    return time ? locale.toString(QTime(14, 30, 0), pattern(format, true)) : locale.toString(now.date(), pattern(format, false));
}
