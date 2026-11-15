#pragma once

#include <QObject>
#include <QTimer>
#include <QtQmlIntegration/qqmlintegration.h>

// The laptop battery, from the kernel's /sys/class/power_supply (no
// daemon needed). Re-read every 15 seconds.
//
// Exposed to QML as the singleton `Battery`.
class BatteryInfo : public QObject
{
    Q_OBJECT
    QML_NAMED_ELEMENT(Battery)
    QML_SINGLETON

    // there is a battery at all (false on desktops and most VMs)
    Q_PROPERTY(bool present READ present NOTIFY changed)
    // 0-100, averaged over all batteries
    Q_PROPERTY(int percent READ percent NOTIFY changed)
    Q_PROPERTY(bool charging READ charging NOTIFY changed)
    // running on mains power (charging, full, or holding its charge)
    Q_PROPERTY(bool pluggedIn READ pluggedIn NOTIFY changed)
    // estimate, -1 if unknown: time to empty, or to full while charging
    Q_PROPERTY(int minutesLeft READ minutesLeft NOTIFY changed)
    // what it holds when full, now and when new (Wh; 0 if not reported)
    Q_PROPERTY(double fullWh READ fullWh NOTIFY changed)
    Q_PROPERTY(double designWh READ designWh NOTIFY changed)
    Q_PROPERTY(QString model READ model NOTIFY changed)

public:
    explicit BatteryInfo(QObject *parent = nullptr);

    bool present() const { return m_present; }
    int percent() const { return m_percent; }
    bool charging() const { return m_charging; }
    bool pluggedIn() const { return m_pluggedIn; }
    int minutesLeft() const { return m_minutesLeft; }
    double fullWh() const { return m_fullWh; }
    double designWh() const { return m_designWh; }
    QString model() const { return m_model; }

    Q_INVOKABLE void refresh();

signals:
    void changed();

private:
    QTimer m_timer;
    bool m_present = false;
    int m_percent = 0;
    bool m_charging = false;
    bool m_pluggedIn = false;
    int m_minutesLeft = -1;
    double m_fullWh = 0;
    double m_designWh = 0;
    QString m_model;
};
