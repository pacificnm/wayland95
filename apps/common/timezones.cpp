#include "timezones.h"

#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QDBusReply>
#include <QDBusVariant>
#include <QDateTime>
#include <QFileInfo>
#include <QJSEngine>
#include <QJSValue>
#include <QQmlEngine>
#include <QTimeZone>
#include <ctime>

namespace {

const QString Service = QStringLiteral("org.freedesktop.timedate1");
const QString Path = QStringLiteral("/org/freedesktop/timedate1");

bool testBus()
{
    return qEnvironmentVariableIsSet("WAYLAND95_NO_SYSTEM_AGENTS")
           && qEnvironmentVariableIsSet("WAYLAND95_TEST_AGENTS_ON_SESSION_BUS");
}
QDBusConnection bus() { return testBus() ? QDBusConnection::sessionBus() : QDBusConnection::systemBus(); }

const QStringList Regions = { QStringLiteral("Africa"), QStringLiteral("America"), QStringLiteral("Antarctica"),
                              QStringLiteral("Arctic"), QStringLiteral("Asia"), QStringLiteral("Atlantic"),
                              QStringLiteral("Australia"), QStringLiteral("Europe"), QStringLiteral("Indian"),
                              QStringLiteral("Pacific") };

QString offsetText(int seconds)
{
    const QChar sign = seconds < 0 ? QLatin1Char('-') : QLatin1Char('+');
    seconds = qAbs(seconds);
    return QStringLiteral("UTC%1%2:%3").arg(sign).arg(seconds / 3600, 2, 10, QLatin1Char('0'))
        .arg(seconds % 3600 / 60, 2, 10, QLatin1Char('0'));
}

// "America/Argentina/Buenos_Aires" -> "Buenos Aires, Argentina"
QString cityOf(const QString &id)
{
    QStringList parts = id.split(QLatin1Char('/'));
    if (parts.size() > 1)
        parts.removeFirst();
    for (QString &p : parts)
        p.replace(QLatin1Char('_'), QLatin1Char(' '));
    if (parts.size() > 1)
        return parts.last() + QStringLiteral(", ") + parts.mid(0, parts.size() - 1).join(QStringLiteral(", "));
    return parts.value(0);
}

} // namespace

TimeZones::TimeZones(QObject *parent) : QObject(parent)
{
    bus().connect(Service, Path, QStringLiteral("org.freedesktop.DBus.Properties"), QStringLiteral("PropertiesChanged"),
                  this, SLOT(propertiesChanged(QString,QVariantMap,QStringList)));
    // once a minute: a change made by hand (ln -sf .../zoneinfo/X /etc/localtime)
    m_poll.setInterval(60 * 1000);
    connect(&m_poll, &QTimer::timeout, this, &TimeZones::check);
    if (!testBus())
        m_poll.start();
    check();
}

QStringList TimeZones::regions() const
{
    return Regions + QStringList{ QStringLiteral("UTC") };
}

QString TimeZones::systemZone() const
{
    if (testBus()) {
        // (the stand-in's)
        QDBusMessage call = QDBusMessage::createMethodCall(Service, Path, QStringLiteral("org.freedesktop.DBus.Properties"),
                                                           QStringLiteral("Get"));
        call << Service << QStringLiteral("Timezone");
        const QDBusReply<QDBusVariant> reply = bus().call(call, QDBus::Block, 2000);
        return reply.isValid() ? reply.value().variant().toString() : m_current;
    }
    // /etc/localtime -> /usr/share/zoneinfo/Area/City (as timedated keeps it)
    const QString target = QFileInfo(QStringLiteral("/etc/localtime")).symLinkTarget();
    const int at = target.indexOf(QLatin1String("zoneinfo/"));
    if (at >= 0)
        return target.mid(at + 9);
    return QString::fromUtf8(QTimeZone::systemTimeZoneId());
}

void TimeZones::check()
{
    const QString zone = systemZone();
    if (!zone.isEmpty() && zone != m_current)
        follow(zone);
}

void TimeZones::propertiesChanged(const QString &interface, const QVariantMap &changed, const QStringList &)
{
    if (interface != Service)
        return;
    const QString zone = changed.value(QStringLiteral("Timezone")).toString();
    if (!zone.isEmpty() && zone != m_current)
        follow(zone);
    else if (changed.isEmpty())
        check();
}

void TimeZones::follow(const QString &id)
{
    const bool first = m_current.isEmpty();
    m_current = id;
    // the C library and Qt read the zone once; told by TZ, they read it
    // again (QML's Date too). Programs started from here get it as well.
    if (!first || qEnvironmentVariableIsSet("TZ") || testBus()) {
        qputenv("TZ", (QStringLiteral(":") + id).toUtf8());
        tzset();
    }
    if (!first)
        qInfo().noquote() << "Time zone:" << id;
    Q_EMIT currentChanged();
}

QVariantList TimeZones::zones(const QString &region) const
{
    QVariantList out;
    const QDateTime now = QDateTime::currentDateTimeUtc();
    if (region == QLatin1String("UTC")) {
        out << QVariantMap{ { QStringLiteral("id"), QStringLiteral("UTC") }, { QStringLiteral("city"), QStringLiteral("UTC") },
                            { QStringLiteral("offset"), 0 }, { QStringLiteral("label"), QStringLiteral("(UTC+00:00) Coordinated Universal Time") } };
        return out;
    }
    QList<QPair<int, QString>> found;
    const QList<QByteArray> ids = QTimeZone::availableTimeZoneIds();
    for (const QByteArray &raw : ids) {
        const QString id = QString::fromUtf8(raw);
        if (!id.startsWith(region + QLatin1Char('/')))
            continue;
        const QTimeZone zone(raw);
        if (!zone.isValid())
            continue;
        found << qMakePair(zone.offsetFromUtc(now), id);
    }
    std::sort(found.begin(), found.end(), [](const auto &a, const auto &b) {
        return a.first != b.first ? a.first < b.first : cityOf(a.second).localeAwareCompare(cityOf(b.second)) < 0;
    });
    for (const auto &f : std::as_const(found)) {
        out << QVariantMap{ { QStringLiteral("id"), f.second }, { QStringLiteral("city"), cityOf(f.second) },
                            { QStringLiteral("offset"), f.first },
                            { QStringLiteral("label"), QStringLiteral("(%1) %2").arg(offsetText(f.first), cityOf(f.second)) } };
    }
    return out;
}

QString TimeZones::regionOf(const QString &id) const
{
    const QString region = id.section(QLatin1Char('/'), 0, 0);
    return Regions.contains(region) ? region : QStringLiteral("UTC");
}

QString TimeZones::label(const QString &id) const
{
    if (id.isEmpty())
        return QString();
    const QTimeZone zone(id.toUtf8());
    if (!zone.isValid())
        return id;
    const QDateTime now = QDateTime::currentDateTimeUtc();
    const QString abbreviation = zone.abbreviation(now);
    QString text = QStringLiteral("(%1) %2").arg(offsetText(zone.offsetFromUtc(now)),
                                                 id == QLatin1String("UTC") ? QStringLiteral("Coordinated Universal Time") : cityOf(id));
    if (!abbreviation.isEmpty() && !abbreviation.startsWith(QLatin1Char('+')) && !abbreviation.startsWith(QLatin1Char('-'))
        && abbreviation != QLatin1String("UTC"))
        text += QStringLiteral(" - ") + abbreviation;
    return text;
}

QString TimeZones::timeIn(const QString &id, bool hours24) const
{
    const QTimeZone zone(id.toUtf8());
    if (!zone.isValid())
        return QString();
    return QDateTime::currentDateTimeUtc().toTimeZone(zone).toString(hours24 ? QStringLiteral("HH:mm") : QStringLiteral("h:mm AP"));
}

void TimeZones::setZone(const QString &id, const QJSValue &done)
{
    QJSValue cb = done;
    auto answer = [this, cb](bool ok, const QString &message) mutable {
        if (cb.isCallable())
            cb.call({ ok, message });
    };
    if (!QTimeZone(id.toUtf8()).isValid()) {
        answer(false, QStringLiteral("There's no time zone called %1.").arg(id));
        return;
    }
    QDBusMessage call = QDBusMessage::createMethodCall(Service, Path, Service, QStringLiteral("SetTimezone"));
    call << id << true; // (interactive: polkit may ask for a password)
    call.setInteractiveAuthorizationAllowed(true);
    auto *watcher = new QDBusPendingCallWatcher(bus().asyncCall(call, 120000), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this, watcher, id, answer]() mutable {
        watcher->deleteLater();
        const QDBusPendingReply<> reply = *watcher;
        if (reply.isError()) {
            const QString name = reply.error().name();
            const QString message = name.contains(QLatin1String("AccessDenied")) || name.contains(QLatin1String("InteractiveAuthorization"))
                    ? QStringLiteral("You need an administrator's password to change the time zone.")
                    : reply.error().message();
            answer(false, message);
            return;
        }
        follow(id);
        answer(true, QString());
    });
}
