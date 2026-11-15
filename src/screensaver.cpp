#include "screensaver.h"

#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusServiceWatcher>
#include <QGuiApplication>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QScreen>
#include <QWaylandSurface>
#include <qpa/qplatformscreen.h>
#include <QtDebug>

// ---------------------------------------------------------------------------
// D-Bus interfaces

FreedesktopScreenSaver::FreedesktopScreenSaver(ScreenSaverController *controller)
    : QObject(controller)
    , m_controller(controller)
{
    connect(controller, &ScreenSaverController::activeChanged, this,
            [this] { emit ActiveChanged(m_controller->active()); });
}

uint FreedesktopScreenSaver::Inhibit(const QString &application, const QString &reason)
{
    qInfo().noquote() << "Screen saver inhibited by" << application << (reason.isEmpty() ? QString() : "(" + reason + ")");
    return m_controller->addInhibitor(calledFromDBus() ? message().service() : QString());
}

void FreedesktopScreenSaver::UnInhibit(uint cookie) { m_controller->removeInhibitor(cookie); }
bool FreedesktopScreenSaver::GetActive() { return m_controller->active(); }

bool FreedesktopScreenSaver::SetActive(bool active)
{
    // a program can start it, but not get past the password
    if (active)
        m_controller->activate();
    else if (!m_controller->unlocking() && !m_controller->requirePassword())
        m_controller->deactivate();
    return true;
}

uint FreedesktopScreenSaver::GetActiveTime() { return uint(m_controller->activeMsecs() / 1000); }
uint FreedesktopScreenSaver::GetSessionIdleTime() { return uint(m_controller->idleMsecs() / 1000); }
void FreedesktopScreenSaver::SimulateUserActivity() { m_controller->poke(); }

Wayland95ScreenSaver::Wayland95ScreenSaver(ScreenSaverController *controller)
    : QObject(controller)
    , m_controller(controller)
{
}

void Wayland95ScreenSaver::Preview() { m_controller->preview(); }

// ---------------------------------------------------------------------------
// ScreenSaverController

ScreenSaverController::ScreenSaverController(QObject *parent)
    : QObject(parent)
{
    m_idleTimer.setSingleShot(true);
    connect(&m_idleTimer, &QTimer::timeout, this, &ScreenSaverController::timeout);
    m_lastInput.start();
    m_unlockTimeout.setSingleShot(true);
    m_unlockTimeout.setInterval(30000);
    connect(&m_unlockTimeout, &QTimer::timeout, this, &ScreenSaverController::cancelUnlock);
    qApp->installEventFilter(this);
}

ScreenSaverController::~ScreenSaverController()
{
    qApp->removeEventFilter(this);
}

void ScreenSaverController::componentComplete()
{
    restartTimer();
    setupDBus();
}

void ScreenSaverController::setupDBus()
{
    QDBusConnection bus = QDBusConnection::sessionBus();
    if (!bus.isConnected())
        return;

    if (bus.registerService(QStringLiteral("org.wayland95.ScreenSaver")))
        bus.registerObject(QStringLiteral("/org/wayland95/ScreenSaver"), new Wayland95ScreenSaver(this),
                           QDBusConnection::ExportAllSlots);

    // the standard service, unless the desktop we're nested in has it
    if (bus.registerService(QStringLiteral("org.freedesktop.ScreenSaver"))) {
        m_freedesktop = new FreedesktopScreenSaver(this);
        const auto options = QDBusConnection::ExportAllSlots | QDBusConnection::ExportAllSignals;
        bus.registerObject(QStringLiteral("/org/freedesktop/ScreenSaver"), m_freedesktop, options);
        bus.registerObject(QStringLiteral("/ScreenSaver"), m_freedesktop, options);
        // a program that inhibited and then quit (or crashed) no longer does
        m_inhibitorWatcher = new QDBusServiceWatcher(this);
        m_inhibitorWatcher->setConnection(bus);
        m_inhibitorWatcher->setWatchMode(QDBusServiceWatcher::WatchForUnregistration);
        connect(m_inhibitorWatcher, &QDBusServiceWatcher::serviceUnregistered, this, [this](const QString &name) {
            const bool before = inhibited();
            for (auto it = m_inhibitors.begin(); it != m_inhibitors.end();) {
                if (it.value() == name)
                    it = m_inhibitors.erase(it);
                else
                    ++it;
            }
            m_inhibitorWatcher->removeWatchedService(name);
            if (before != inhibited())
                emit inhibitedChanged();
        });
    }
}

void ScreenSaverController::setCompositor(QWaylandCompositor *compositor)
{
    if (m_compositor == compositor)
        return;
    m_compositor = compositor;
    if (compositor)
        connect(compositor, &QWaylandCompositor::surfaceCreated, this, &ScreenSaverController::watchSurface);
    emit compositorChanged();
}

void ScreenSaverController::setEnabled(bool enabled)
{
    if (m_enabled == enabled)
        return;
    m_enabled = enabled;
    emit enabledChanged();
    restartTimer();
}

void ScreenSaverController::setWaitMinutes(int minutes)
{
    minutes = qMax(1, minutes);
    if (m_waitMinutes == minutes)
        return;
    m_waitMinutes = minutes;
    emit waitMinutesChanged();
    restartTimer();
}

bool ScreenSaverController::inhibited() const
{
    return m_surfaceInhibit || !m_inhibitors.isEmpty();
}

void ScreenSaverController::restartTimer()
{
    m_idleTimer.start(m_waitMinutes * 60 * 1000);
}

void ScreenSaverController::timeout()
{
    if (m_enabled && !inhibited()) {
        activate();
        m_locked = m_requirePassword;
    }
    else
        restartTimer(); // check again later
}

void ScreenSaverController::poke()
{
    m_lastInput.restart();
    if (!m_active)
        restartTimer();
}

void ScreenSaverController::activate()
{
    if (m_active)
        return;
    m_active = true;
    m_haveMouseOrigin = false;
    m_graceMs = 0;
    m_activeSince.start();
    emit activeChanged();
}

void ScreenSaverController::preview()
{
    activate();
    m_locked = false; // trying it out doesn't lock you out
    // the mouse is probably still moving from clicking Preview
    m_graceMs = 1500;
    m_graceTimer.start();
}

void ScreenSaverController::wake()
{
    if (!m_locked) {
        deactivate();
        return;
    }
    // locked: ask for the password, over the screen saver
    if (!m_unlocking) {
        m_unlocking = true;
        emit unlockingChanged();
    }
    m_unlockTimeout.start();
}

void ScreenSaverController::unlock()
{
    m_locked = false;
    m_unlockTimeout.stop();
    if (m_unlocking) {
        m_unlocking = false;
        emit unlockingChanged();
    }
    deactivate();
}

void ScreenSaverController::cancelUnlock()
{
    m_unlockTimeout.stop();
    if (!m_unlocking)
        return;
    m_unlocking = false;
    m_haveMouseOrigin = false;
    m_graceMs = 1000; // the click on Cancel mustn't wake it again at once
    m_graceTimer.start();
    emit unlockingChanged();
}

void ScreenSaverController::setRequirePassword(bool on)
{
    if (on == m_requirePassword)
        return;
    m_requirePassword = on;
    emit requirePasswordChanged();
}

void ScreenSaverController::deactivate()
{
    if (!m_active)
        return;
    m_locked = false;
    m_active = false;
    poke();
    emit activeChanged();
}

void ScreenSaverController::setDisplayOff(bool off)
{
    if (off == m_displayOff)
        return;
    m_displayOff = off;
    m_haveMouseOrigin = false;
    // Really off on the hardware (DPMS through Qt's KMS backend); nested in
    // another desktop this does nothing, and the black overlay stands in.
    if (QScreen *screen = QGuiApplication::primaryScreen()) {
        if (QPlatformScreen *platform = screen->handle())
            platform->setPowerState(off ? QPlatformScreen::PowerStateOff : QPlatformScreen::PowerStateOn);
    }
    if (!off)
        poke();
    emit displayOffChanged();
}

uint ScreenSaverController::addInhibitor(const QString &owner)
{
    const bool before = inhibited();
    const uint cookie = m_nextCookie++;
    m_inhibitors.insert(cookie, owner);
    if (m_inhibitorWatcher && !owner.isEmpty())
        m_inhibitorWatcher->addWatchedService(owner);
    if (before != inhibited())
        emit inhibitedChanged();
    return cookie;
}

void ScreenSaverController::removeInhibitor(uint cookie)
{
    const bool before = inhibited();
    m_inhibitors.remove(cookie);
    if (before != inhibited())
        emit inhibitedChanged();
}

void ScreenSaverController::watchSurface(QWaylandSurface *surface)
{
    m_surfaces << surface;
    connect(surface, &QWaylandSurface::inhibitsIdleChanged, this, &ScreenSaverController::updateSurfaceInhibit);
    connect(surface, &QWaylandSurface::surfaceDestroyed, this, [this, surface] {
        m_surfaces.removeAll(surface);
        updateSurfaceInhibit();
    });
}

void ScreenSaverController::updateSurfaceInhibit()
{
    bool inhibit = false;
    for (const QPointer<QWaylandSurface> &s : std::as_const(m_surfaces))
        if (s && s->inhibitsIdle())
            inhibit = true;
    if (inhibit == m_surfaceInhibit)
        return;
    const bool before = inhibited();
    m_surfaceInhibit = inhibit;
    if (before != inhibited())
        emit inhibitedChanged();
}

bool ScreenSaverController::eventFilter(QObject *watched, QEvent *event)
{
    // input reaches the output window first; items get it afterwards
    if (!watched->isWindowType())
        return false;

    // laptop keys: the desktop handles them (brightness, volume, power...)
    if (event->type() == QEvent::KeyPress || event->type() == QEvent::KeyRelease) {
        switch (static_cast<QKeyEvent *>(event)->key()) {
        case Qt::Key_MonBrightnessUp: case Qt::Key_MonBrightnessDown:
        case Qt::Key_VolumeUp: case Qt::Key_VolumeDown: case Qt::Key_VolumeMute:
        case Qt::Key_PowerOff: case Qt::Key_PowerDown: case Qt::Key_Sleep: case Qt::Key_Suspend:
        case Qt::Key_Hibernate: case Qt::Key_Standby:
            poke();
            if (m_displayOff)
                setDisplayOff(false);
            if (event->type() == QEvent::KeyPress && !static_cast<QKeyEvent *>(event)->isAutoRepeat())
                emit hardwareKey(static_cast<QKeyEvent *>(event)->key());
            else if (event->type() == QEvent::KeyPress) // held: brightness/volume repeat
                emit hardwareKey(static_cast<QKeyEvent *>(event)->key());
            return true;
        default:
            break;
        }
    }

    // a turned-off display: any input turns it back on, and goes no further
    if (m_displayOff) {
        switch (event->type()) {
        case QEvent::KeyPress: case QEvent::MouseButtonPress: case QEvent::MouseButtonDblClick:
        case QEvent::Wheel: case QEvent::TouchBegin:
            setDisplayOff(false);
            m_swallowRelease = event->type() != QEvent::Wheel;
            return true;
        case QEvent::MouseMove: {
            const QPointF pos = static_cast<QMouseEvent *>(event)->scenePosition();
            if (!m_haveMouseOrigin) {
                m_haveMouseOrigin = true;
                m_mouseOrigin = pos;
            } else if ((pos - m_mouseOrigin).manhattanLength() > 10) {
                setDisplayOff(false);
            }
            return true;
        }
        case QEvent::KeyRelease: case QEvent::MouseButtonRelease: case QEvent::TouchEnd:
            return true;
        default:
            return false;
        }
    }

    // asking for the password: input is for the password box (the screen
    // saver's cover keeps it from the programs underneath)
    if (m_unlocking) {
        switch (event->type()) {
        case QEvent::KeyPress: case QEvent::MouseButtonPress: case QEvent::MouseMove:
            m_unlockTimeout.start();
            break;
        default:
            break;
        }
        return false;
    }

    switch (event->type()) {
    case QEvent::KeyPress:
    case QEvent::MouseButtonPress:
    case QEvent::MouseButtonDblClick:
    case QEvent::Wheel:
    case QEvent::TouchBegin:
        poke();
        if (!m_active)
            return false;
        if (m_graceMs > 0 && m_graceTimer.elapsed() < m_graceMs && event->type() == QEvent::Wheel)
            return true;
        wake();
        // this press only wakes the screen; its release mustn't reach a
        // program either
        m_swallowRelease = event->type() != QEvent::Wheel;
        return true;
    case QEvent::KeyRelease:
    case QEvent::MouseButtonRelease:
    case QEvent::TouchEnd:
        if (m_swallowRelease || m_active) {
            m_swallowRelease = false;
            return true;
        }
        return false;
    case QEvent::MouseMove:
    case QEvent::TouchUpdate: {
        poke();
        if (!m_active)
            return false;
        if (m_graceMs > 0 && m_graceTimer.elapsed() < m_graceMs)
            return true;
        // a nudge of the desk shouldn't end it; a real move does
        const QPointF pos = event->type() == QEvent::MouseMove
                                ? static_cast<QMouseEvent *>(event)->scenePosition() : QPointF();
        if (!m_haveMouseOrigin) {
            m_haveMouseOrigin = true;
            m_mouseOrigin = pos;
        } else if ((pos - m_mouseOrigin).manhattanLength() > 10) {
            wake();
        }
        return true;
    }
    default:
        return false;
    }
}
