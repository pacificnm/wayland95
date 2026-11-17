#pragma once

// The computer's time zone: what it is, the zones there are, and changing
// it (systemd-timedated over D-Bus; polkit asks for an administrator's
// password through the desktop's own prompt). Every program using this
// follows a change at once (its clocks too), not only after it's started
// again: a C library and Qt read the zone once, so `TZ` is set to it.
//
// Exposed to QML as the singleton `TimeZones`.
//
// The tests: with WAYLAND95_NO_SYSTEM_AGENTS and
// WAYLAND95_TEST_AGENTS_ON_SESSION_BUS, timedate1 is the test's stand-in
// on its own session bus (tests/fakes/timedate.py), and /etc/localtime
// isn't looked at.

#include <QJSValue>
#include <QObject>
#include <QTimer>
#include <QVariantList>
#include <QtQmlIntegration/qqmlintegration.h>

class TimeZones : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

    // "America/New_York"
    Q_PROPERTY(QString current READ current NOTIFY currentChanged)
    // "(UTC-05:00) Eastern Time (New York)"-style, for the current zone
    Q_PROPERTY(QString currentLabel READ currentLabel NOTIFY currentChanged)
    // the regions, in order: "Africa", "America", ... "UTC"
    Q_PROPERTY(QStringList regions READ regions CONSTANT)

public:
    explicit TimeZones(QObject *parent = nullptr);

    QString current() const { return m_current; }
    QString currentLabel() const { return label(m_current); }
    QStringList regions() const;

    // a region's zones, by their offset from UTC now, then name:
    // [{ id, city ("New York", "Argentina/Buenos Aires" -> "Buenos Aires, Argentina"),
    //    offset (seconds), label ("(UTC-05:00) New York") }]
    Q_INVOKABLE QVariantList zones(const QString &region) const;
    Q_INVOKABLE QString regionOf(const QString &id) const;
    Q_INVOKABLE QString label(const QString &id) const;
    // the time there now, "h:mm AP" or "HH:mm"
    Q_INVOKABLE QString timeIn(const QString &id, bool hours24) const;

    // changes the computer's time zone: done(ok, message)
    Q_INVOKABLE void setZone(const QString &id, const QJSValue &done);

Q_SIGNALS:
    void currentChanged();

private Q_SLOTS:
    void propertiesChanged(const QString &interface, const QVariantMap &changed, const QStringList &invalidated);

private:
    void check();                  // what the zone is now (and follow it)
    void follow(const QString &id); // this process, and its clocks, in that zone
    QString systemZone() const;

    QString m_current;
    QTimer m_poll; // a change made without timedated (by hand)
};
