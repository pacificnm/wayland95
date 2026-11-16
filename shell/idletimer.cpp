#include "idletimer.h"

#include <QGuiApplication>
#include <cstdio>

class IdleNotification : public QtWayland::ext_idle_notification_v1
{
public:
    IdleNotification(IdleTimer *timer, ::ext_idle_notification_v1 *object)
        : QtWayland::ext_idle_notification_v1(object), m_timer(timer) {}
    ~IdleNotification() override { destroy(); }

protected:
    void ext_idle_notification_v1_idled() override { m_timer->setIdle(true); }
    void ext_idle_notification_v1_resumed() override { m_timer->setIdle(false); }

private:
    IdleTimer *m_timer;
};

IdleNotifier *IdleNotifier::instance()
{
    static IdleNotifier *notifier = new IdleNotifier;
    return notifier;
}

IdleTimer::IdleTimer(QObject *parent) : QObject(parent)
{
    // the global may arrive after us
    connect(IdleNotifier::instance(), &IdleNotifier::activeChanged, this, &IdleTimer::restart);
}

IdleTimer::~IdleTimer()
{
    delete m_notification;
}

void IdleTimer::setTimeout(int ms)
{
    if (ms == m_timeout)
        return;
    m_timeout = ms;
    emit timeoutChanged();
    restart();
}

void IdleTimer::restart()
{
    delete m_notification;
    m_notification = nullptr;
    setIdle(false);
    IdleNotifier *notifier = IdleNotifier::instance();
    if (m_timeout <= 0 || !notifier->isActive())
        return;
    auto *wayland = qGuiApp->nativeInterface<QNativeInterface::QWaylandApplication>();
    if (!wayland || !wayland->seat())
        return;
    m_notification = new IdleNotification(this, notifier->get_idle_notification(uint32_t(m_timeout), wayland->seat()));
}

void IdleTimer::setIdle(bool idle)
{
    if (idle == m_idle)
        return;
    m_idle = idle;
    emit idleChanged();
    if (idle)
        emit idled();
    else
        emit resumed();
}
