#pragma once

#include <QDBusObjectPath>
#include <QObject>
#include <QStringList>
#include <QTimer>
#include <QtQmlIntegration/qqmlintegration.h>

// Looks for updates in the background, like Windows Update's tray notice:
// a few minutes after logging on, then twice a day, it has PackageKit fetch
// the latest package lists (no password needed for that) and counts the
// updates: Debian's and Wayland 95's own (the wayland95 package, from the
// Wayland 95 update repository the package sets up). Main.qml turns a new
// result into a notification; clicking it opens Add/Remove Programs >
// Updates. Installing is left to that, which asks for the password.
//
// Only in the real session (nested test copies would all check too), unless
// WAYLAND95_CHECK_UPDATES is set.
class UpdateCheck : public QObject
{
    Q_OBJECT
    QML_ELEMENT

    Q_PROPERTY(bool enabled READ enabled CONSTANT)
    // updates found by the last check
    Q_PROPERTY(int count READ count NOTIFY checked)
    Q_PROPERTY(int securityCount READ securityCount NOTIFY checked)
    // one of them is Wayland 95 itself (with its new version)
    Q_PROPERTY(bool desktopUpdate READ desktopUpdate NOTIFY checked)
    Q_PROPERTY(QString desktopVersion READ desktopVersion NOTIFY checked)
    // first check this long after logging on, then every `interval`
    Q_PROPERTY(int firstDelay MEMBER m_firstDelay NOTIFY settingsChanged)
    Q_PROPERTY(int interval MEMBER m_interval NOTIFY settingsChanged)

public:
    explicit UpdateCheck(QObject *parent = nullptr);

    bool enabled() const { return m_enabled; }
    int count() const { return m_count; }
    int securityCount() const { return m_security; }
    bool desktopUpdate() const { return m_desktop; }
    QString desktopVersion() const { return m_desktopVersion; }

    // check now (refreshing the package lists first)
    Q_INVOKABLE void check();

signals:
    void checked();
    void settingsChanged();

private slots:
    void onPackage(uint info, const QString &id, const QString &summary);
    void onFinished(uint exit, uint runtime);

private:
    void transaction(const QString &method, const QVariantList &args);
    void scheduleNext();

    bool m_enabled = false;
    bool m_busy = false;
    QString m_step;                   // "refresh" or "list"
    QString m_transaction;            // its object path
    int m_count = 0, m_security = 0;
    int m_found = 0, m_foundSecurity = 0;
    bool m_desktop = false, m_foundDesktop = false;
    QString m_desktopVersion, m_foundVersion;
    int m_firstDelay = 3 * 60 * 1000;
    int m_interval = 12 * 60 * 60 * 1000;
    QTimer m_timer;
};
