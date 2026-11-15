#pragma once

#include <QDBusContext>
#include <QElapsedTimer>
#include <QHash>
#include <QObject>
#include <QPointF>
#include <QPointer>
#include <QQmlParserStatus>
#include <QTimer>
#include <QWaylandCompositor>
#include <QtQmlIntegration/qqmlintegration.h>

class QDBusServiceWatcher;
class QWaylandSurface;
class ScreenSaverController;

// org.freedesktop.ScreenSaver: what browsers and video players call to keep
// the screen saver away while something is playing. Only offered when no
// other desktop already provides it (i.e. not when nested inside one).
class FreedesktopScreenSaver : public QObject, protected QDBusContext
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.freedesktop.ScreenSaver")

public:
    explicit FreedesktopScreenSaver(ScreenSaverController *controller);

public slots:
    uint Inhibit(const QString &application, const QString &reason);
    void UnInhibit(uint cookie);
    bool GetActive();
    bool SetActive(bool active);
    uint GetActiveTime();
    uint GetSessionIdleTime();
    void SimulateUserActivity();

signals:
    void ActiveChanged(bool active);

private:
    ScreenSaverController *m_controller;
};

// org.wayland95.ScreenSaver: Control Panel > Display's Preview button.
class Wayland95ScreenSaver : public QObject
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.wayland95.ScreenSaver")

public:
    explicit Wayland95ScreenSaver(ScreenSaverController *controller);

public slots:
    void Preview();

private:
    ScreenSaverController *m_controller;
};

// Starts the screen saver after `waitMinutes` without keyboard or mouse
// input, unless something inhibits it (a program over D-Bus, or a Wayland
// surface using the idle-inhibit protocol, e.g. a video player). While it
// runs, input dismisses it and doesn't reach the programs underneath.
//
// Instantiated in Main.qml, which shows the saver while `active`.
class ScreenSaverController : public QObject, public QQmlParserStatus
{
    Q_OBJECT
    Q_INTERFACES(QQmlParserStatus)
    QML_ELEMENT

    Q_PROPERTY(QWaylandCompositor *compositor READ compositor WRITE setCompositor NOTIFY compositorChanged)
    // Display > Screen Saver: (None) turns it off
    Q_PROPERTY(bool enabled READ enabled WRITE setEnabled NOTIFY enabledChanged)
    Q_PROPERTY(int waitMinutes READ waitMinutes WRITE setWaitMinutes NOTIFY waitMinutesChanged)
    Q_PROPERTY(bool active READ active NOTIFY activeChanged)
    Q_PROPERTY(bool inhibited READ inhibited NOTIFY inhibitedChanged)
    // the display is turned off (Control Panel > Power); input turns it on
    Q_PROPERTY(bool displayOff READ displayOff WRITE setDisplayOff NOTIFY displayOffChanged)
    // Display > Screen Saver > Password protected: when it started by
    // itself, waking it asks for the password (`unlocking`) instead of
    // going straight back to the desktop
    Q_PROPERTY(bool requirePassword READ requirePassword WRITE setRequirePassword NOTIFY requirePasswordChanged)
    Q_PROPERTY(bool unlocking READ unlocking NOTIFY unlockingChanged)

public:
    explicit ScreenSaverController(QObject *parent = nullptr);
    ~ScreenSaverController() override;

    QWaylandCompositor *compositor() const { return m_compositor; }
    void setCompositor(QWaylandCompositor *compositor);
    bool enabled() const { return m_enabled; }
    void setEnabled(bool enabled);
    int waitMinutes() const { return m_waitMinutes; }
    void setWaitMinutes(int minutes);
    bool active() const { return m_active; }
    bool inhibited() const;
    bool displayOff() const { return m_displayOff; }
    void setDisplayOff(bool off);
    bool requirePassword() const { return m_requirePassword; }
    void setRequirePassword(bool on);
    bool unlocking() const { return m_unlocking; }
    // the password was right: back to the desktop
    Q_INVOKABLE void unlock();
    // Cancel (or no typing for a while): back to the screen saver
    Q_INVOKABLE void cancelUnlock();
    // seconds since the last keyboard or mouse input
    Q_INVOKABLE int idleSeconds() const { return int(m_lastInput.elapsed() / 1000); }

    Q_INVOKABLE void activate();
    Q_INVOKABLE void deactivate();
    // show it now, ignoring input for a moment (the click that asked for it)
    Q_INVOKABLE void preview();
    // counts as user activity: restarts the idle countdown
    Q_INVOKABLE void poke();

    // for the D-Bus interface
    uint addInhibitor(const QString &owner);
    void removeInhibitor(uint cookie);
    qint64 activeMsecs() const { return m_active ? m_activeSince.elapsed() : 0; }
    qint64 idleMsecs() const { return m_lastInput.elapsed(); }

    void classBegin() override {}
    void componentComplete() override;

signals:
    void compositorChanged();
    void enabledChanged();
    void waitMinutesChanged();
    void activeChanged();
    void inhibitedChanged();
    void displayOffChanged();
    void requirePasswordChanged();
    void unlockingChanged();
    // a laptop key: brightness, volume, power, sleep (Qt::Key values).
    // These don't reach programs.
    void hardwareKey(int key);

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    void restartTimer();
    void timeout();
    void watchSurface(QWaylandSurface *surface);
    void updateSurfaceInhibit();
    void setupDBus();

    QPointer<QWaylandCompositor> m_compositor;
    bool m_enabled = true;
    int m_waitMinutes = 10;
    bool m_active = false;
    bool m_displayOff = false;
    bool m_requirePassword = false;
    bool m_locked = false;     // started by itself with requirePassword on
    bool m_unlocking = false;
    QTimer m_unlockTimeout;
    void wake(); // input arrived while it runs
    QTimer m_idleTimer;
    QElapsedTimer m_lastInput;
    QElapsedTimer m_activeSince;
    QElapsedTimer m_graceTimer;
    qint64 m_graceMs = 0;
    bool m_haveMouseOrigin = false;
    QPointF m_mouseOrigin;
    bool m_swallowRelease = false;

    QList<QPointer<QWaylandSurface>> m_surfaces;
    bool m_surfaceInhibit = false;
    QHash<uint, QString> m_inhibitors; // cookie -> D-Bus owner
    uint m_nextCookie = 1;
    QDBusServiceWatcher *m_inhibitorWatcher = nullptr;
    FreedesktopScreenSaver *m_freedesktop = nullptr;
};
