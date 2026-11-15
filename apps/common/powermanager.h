#pragma once

#include <QDBusUnixFileDescriptor>
#include <QObject>
#include <QStringList>
#include <QTimer>
#include <QtQmlIntegration/qqmlintegration.h>

// Power and laptop features, through the system's own services:
// systemd-logind (sleep, hibernate, shut down, the lid, screen brightness)
// and power-profiles-daemon (power saver / balanced / performance).
//
// Exposed to QML as the singleton `Power`.
class PowerManager : public QObject
{
    Q_OBJECT
    QML_NAMED_ELEMENT(Power)
    QML_SINGLETON

    // Wayland 95 is the desktop (not a window inside another desktop, which
    // then owns power management). Only then does anything sleep, turn the
    // display off or handle the lid by itself.
    Q_PROPERTY(bool primarySession READ primarySession CONSTANT)
    Q_PROPERTY(bool canSuspend READ canSuspend CONSTANT)
    Q_PROPERTY(bool canHibernate READ canHibernate CONSTANT)
    // a laptop, tablet or convertible
    Q_PROPERTY(bool isLaptop READ isLaptop CONSTANT)
    Q_PROPERTY(bool hasLid READ hasLid CONSTANT)
    Q_PROPERTY(bool lidClosed READ lidClosed NOTIFY lidClosedChanged)
    // the built-in screen's backlight, 1-100%
    Q_PROPERTY(bool hasBacklight READ hasBacklight CONSTANT)
    Q_PROPERTY(int brightness READ brightness NOTIFY brightnessChanged)
    // power-profiles-daemon: e.g. ["power-saver", "balanced", "performance"]
    Q_PROPERTY(QStringList powerProfiles READ powerProfiles NOTIFY powerProfileChanged)
    Q_PROPERTY(QString powerProfile READ powerProfile NOTIFY powerProfileChanged)

public:
    explicit PowerManager(QObject *parent = nullptr);

    bool primarySession() const { return m_primary; }
    bool canSuspend() const { return m_canSuspend; }
    bool canHibernate() const { return m_canHibernate; }
    bool isLaptop() const { return m_isLaptop; }
    bool hasLid() const { return m_hasLid; }
    bool lidClosed() const { return m_lidClosed; }
    bool hasBacklight() const { return !m_backlight.isEmpty(); }
    int brightness() const { return m_brightness; }
    QStringList powerProfiles() const { return m_profiles; }
    QString powerProfile() const { return m_profile; }

    Q_INVOKABLE void suspend();
    Q_INVOKABLE void hibernate();
    Q_INVOKABLE void powerOff();
    Q_INVOKABLE void reboot();
    // ends this login session (back to the login screen)
    Q_INVOKABLE void logOff();
    // Debian's flag that an update wants the computer restarted
    // (/run/reboot-required, set by the kernel's and libc's packages)
    Q_INVOKABLE bool restartRequired() const;
    // 1-100 (never 0: a black built-in screen looks broken)
    Q_INVOKABLE void setBrightness(int percent);
    Q_INVOKABLE void changeBrightness(int deltaPercent);
    Q_INVOKABLE void setPowerProfile(const QString &profile);
    // The desktop handles the lid and the power/sleep keys itself (as set
    // in Control Panel > Power), so logind mustn't act on them as well.
    // Only in a primary session; false if logind refused.
    Q_INVOKABLE bool takeOverPowerKeys();

signals:
    void lidClosedChanged();
    void brightnessChanged();
    void powerProfileChanged();

private slots:
    void onLogindProperties(const QString &interface, const QVariantMap &changed, const QStringList &);
    void onProfileProperties(const QString &interface, const QVariantMap &changed, const QStringList &);

private:
    void logindCall(const QString &method);
    void readBrightness();
    void readProfiles();

    bool m_primary = false;
    bool m_canSuspend = false;
    bool m_canHibernate = false;
    bool m_isLaptop = false;
    bool m_hasLid = false;
    bool m_lidClosed = false;
    QString m_backlight;       // /sys/class/backlight/<name>
    int m_maxBrightness = 0;
    int m_brightness = 0;
    QTimer m_poll;             // brightness keys elsewhere, lid if no signals
    QString m_profileService;  // which name power-profiles-daemon answers on
    QString m_profilePath;
    QString m_profileInterface;
    QStringList m_profiles;
    QString m_profile;
    QDBusUnixFileDescriptor m_inhibitor;
};
