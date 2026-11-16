#include "sessionlock.h"

#include <QGuiApplication>
#include <QQmlEngine>
#include <QScreen>
#include <QtWaylandClient/private/qwaylanddisplay_p.h>
#include <QtWaylandClient/private/qwaylandintegration_p.h>
#include <QtWaylandClient/private/qwaylandscreen_p.h>
#include <QtWaylandClient/private/qwaylandshellintegration_p.h>
#include <QtWaylandClient/private/qwaylandshellsurface_p.h>
#include <QtWaylandClient/private/qwaylandwindow_p.h>
#include <cstdio>

#include "qwayland-ext-session-lock-v1.h"

using namespace QtWaylandClient;

// ---------------------------------------------------------------- the lock

class SessionLockObject : public QtWayland::ext_session_lock_v1
{
public:
    SessionLockObject(SessionLock *owner, ::ext_session_lock_v1 *object)
        : QtWayland::ext_session_lock_v1(object), m_owner(owner) {}

protected:
    void ext_session_lock_v1_locked() override { m_owner->confirmed(); }
    void ext_session_lock_v1_finished() override { m_owner->finished(); }

private:
    SessionLock *m_owner;
};

// ------------------------------------------------------- a lock window

class LockSurface : public QWaylandShellSurface, public QtWayland::ext_session_lock_surface_v1
{
public:
    // The lock role comes a moment later: Qt finishes setting up a window
    // with a commit of no buffer, which a lock surface mustn't have (the
    // compositor ends the connection). Without a role yet, that commit is
    // harmless; the window isn't drawn until the lock surface is configured.
    LockSurface(QWaylandWindow *window) : QWaylandShellSurface(window)
    {
        // (queued on this object: dropped if the window goes first)
        QMetaObject::invokeMethod(this, [this] {
            QWaylandWindow *w = this->window();
            SessionLockObject *lock = SessionLock::instance()->lockObject();
            ::wl_output *output = w && w->waylandScreen() ? w->waylandScreen()->output() : nullptr;
            if (!w || !lock || !output || isInitialized())
                return;
            init(lock->get_lock_surface(w->wlSurface(), output));
        }, Qt::QueuedConnection);
    }
    ~LockSurface() override
    {
        if (isInitialized())
            destroy();
    }

    bool isExposed() const override { return m_configured; }
    void applyConfigure() override
    {
        const QRect screen = window()->window()->screen() ? window()->window()->screen()->geometry() : QRect();
        setGeometryFromApplyConfigure(screen.topLeft(), m_size);
    }
    void setWindowGeometry(const QRect &) override {} // the compositor sizes it: the whole screen
    void setWindowPosition(const QPoint &) override {}

protected:
    void ext_session_lock_surface_v1_configure(uint32_t serial, uint32_t width, uint32_t height) override
    {
        ack_configure(serial);
        m_size = QSize(int(width), int(height));
        m_configured = true;
        applyConfigureWhenPossible();
    }

private:
    bool m_configured = false;
    QSize m_size;
};

class LockIntegration : public QWaylandShellIntegrationTemplate<LockIntegration>,
                        public QtWayland::ext_session_lock_manager_v1
{
public:
    LockIntegration() : QWaylandShellIntegrationTemplate<LockIntegration>(1) {}

    QWaylandShellSurface *createShellSurface(QWaylandWindow *window) override
    {
        if (!SessionLock::instance()->lockObject())
            qWarning("SessionLock: a lock window shown while not locking; it stays hidden");
        return new LockSurface(window);
    }

    static LockIntegration *instance()
    {
        static LockIntegration *shell = [] {
            auto *s = new LockIntegration;
            auto *integration = QWaylandIntegration::instance();
            if (!integration || !s->initialize(integration->display())) {
                qWarning("SessionLock: the compositor has no ext_session_lock_manager_v1");
                delete s;
                return static_cast<LockIntegration *>(nullptr);
            }
            return s;
        }();
        return shell;
    }
};

// --------------------------------------------------------- the singleton

static SessionLock *s_instance = nullptr;

SessionLock::SessionLock(QObject *parent) : QObject(parent)
{
    s_instance = this;
}

SessionLock::~SessionLock()
{
    if (s_instance == this)
        s_instance = nullptr;
}

SessionLock *SessionLock::instance()
{
    if (!s_instance)
        new SessionLock(qGuiApp);
    return s_instance;
}

SessionLock *SessionLock::create(QQmlEngine *, QJSEngine *)
{
    SessionLock *lock = instance();
    QQmlEngine::setObjectOwnership(lock, QQmlEngine::CppOwnership);
    return lock;
}

bool SessionLock::supported() const
{
    return QGuiApplication::platformName().startsWith(QLatin1String("wayland")) && LockIntegration::instance();
}

bool SessionLock::lock()
{
    if (m_lock)
        return true;
    LockIntegration *shell = supported() ? LockIntegration::instance() : nullptr;
    if (!shell)
        return false;
    m_lock = new SessionLockObject(this, shell->QtWayland::ext_session_lock_manager_v1::lock());
    m_locked = false;
    std::fprintf(stderr, "win95shell: locking the session\n");
    emit changed();
    return true;
}

void SessionLock::unlock()
{
    if (!m_lock)
        return;
    // only once confirmed may it be unlocked; before that it's just dropped
    if (m_locked)
        m_lock->unlock_and_destroy();
    else
        m_lock->destroy();
    delete m_lock;
    m_lock = nullptr;
    m_locked = false;
    std::fprintf(stderr, "win95shell: unlocked the session\n");
    // the compositor has it now; make sure it hears before the windows go
    if (auto *integration = QWaylandIntegration::instance())
        integration->display()->flushRequests();
    emit changed();
}

void SessionLock::confirmed()
{
    m_locked = true;
    std::fprintf(stderr, "win95shell: the session is locked\n");
    emit changed();
}

void SessionLock::finished()
{
    // refused (someone else locked it), or taken away
    const bool wasLocked = m_locked;
    if (m_lock) {
        m_lock->destroy();
        delete m_lock;
        m_lock = nullptr;
    }
    m_locked = false;
    std::fprintf(stderr, "win95shell: the compositor %s the session lock\n", wasLocked ? "ended" : "refused");
    if (!wasLocked)
        emit refused();
    emit changed();
}

void SessionLock::attach(QWindow *window)
{
    if (!window || !supported())
        return;
    window->create();
    if (auto *wl = dynamic_cast<QWaylandWindow *>(window->handle()))
        wl->setShellIntegration(LockIntegration::instance());
}
