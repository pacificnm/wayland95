#pragma once

#include <QDate>
#include <QDateTime>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QTime>
#include <QVariant>
#include <QtQmlIntegration/qqmlintegration.h>

// Dates and times as Wayland Studio's apps keep them: "yyyy-MM-dd",
// "HH:mm:ss" and "yyyy-MM-ddTHH:mm:ss", in the computer's own time. What
// can be read as one: those, M/d/yyyy, H:mm, h:mm AP, "today", "now", and
// from now: "today+7" (days), "today-1w" (weeks), "now+2h" (hours),
// "now+30m" (minutes).
namespace DateTimes {

extern const QString kDateFormat;
extern const QString kTimeFormat;
extern const QString kDateTimeFormat;

// "today+7", "now - 2h": the moment, and how far from it. False if it isn't one.
bool relative(const QString &text, QString *base, qint64 *seconds, bool *dayUnits);
QDate parseDate(const QVariant &v);
QTime parseTime(const QVariant &v);
QDateTime parseDateTime(const QVariant &v);

} // namespace DateTimes

// `AppDates` in QML: what the DateTimePicker (and an app's code) uses to
// show, read and convert dates and times. `mode` is "Date", "Date and
// Time" or "Time"; a format is "Short", "Long" (the computer's) or a
// pattern (d dd ddd dddd, M MM MMM MMMM, yy yyyy; h hh H HH, mm, ss, AP);
// a zone is "" (the computer's) or an IANA name like "America/New_York".
class AppDates : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

public:
    explicit AppDates(QObject *parent = nullptr);

    // anything that can be read as one ("today+7", a JS Date, 11/17/2026...)
    // as it's kept for `mode` ("" if it can't be)
    Q_INVOKABLE static QString stored(const QVariant &value, const QString &mode);
    // a kept value as it's shown, in `zone` (with the zone's abbreviation)
    Q_INVOKABLE static QString display(const QString &stored, const QString &mode, const QString &dateFormat,
                                       const QString &timeFormat, const QString &zone = QString());
    // what's typed (as it's shown, or as `stored` reads it) as it's kept
    // ("" if it can't be read)
    Q_INVOKABLE static QString read(const QString &text, const QString &mode, const QString &dateFormat,
                                    const QString &timeFormat, const QString &zone = QString());
    // a kept value as the time in `zone` (still "yyyy-MM-ddTHH:mm:ss"), and back
    Q_INVOKABLE static QString toZone(const QString &stored, const QString &mode, const QString &zone);
    Q_INVOKABLE static QString fromZone(const QString &shown, const QString &mode, const QString &zone);
    // every zone this computer knows
    Q_INVOKABLE static QStringList timeZones();
    // now in a format, to show what it looks like
    Q_INVOKABLE static QString example(const QString &format, bool time);
    // the pattern a format names ("Short" -> the computer's)
    Q_INVOKABLE static QString pattern(const QString &format, bool time);
};
