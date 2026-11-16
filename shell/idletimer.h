#pragma once

// "No keyboard or mouse input for this long", from the compositor
// (ext-idle-notify-v1). Programs that keep the screen awake (a playing
// video, through idle-inhibit) hold it off: the compositor doesn't count
// that time. In QML:
//
//   IdleTimer {
//       timeout: 5 * 60000   // ms; 0 turns it off
//       onIdled: ...         // and `idle` is true until
//       onResumed: ...       // the next input
//   }

#include <QObject>
#include <QtQml/qqmlregistration.h>
#include <QtWaylandClient/QWaylandClientExtensionTemplate>

#include "qwayland-ext-idle-notify-v1.h"

class IdleNotifier : public QWaylandClientExtensionTemplate<IdleNotifier>, public QtWayland::ext_idle_notifier_v1
{
    Q_OBJECT
public:
    IdleNotifier() : QWaylandClientExtensionTemplate<IdleNotifier>(1) { initialize(); }
    ~IdleNotifier() override
    {
        if (isInitialized())
            destroy();
    }
    static IdleNotifier *instance();
};

class IdleNotification;

class IdleTimer : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(int timeout READ timeout WRITE setTimeout NOTIFY timeoutChanged)
    Q_PROPERTY(bool idle READ idle NOTIFY idleChanged)

public:
    explicit IdleTimer(QObject *parent = nullptr);
    ~IdleTimer() override;

    int timeout() const { return m_timeout; }
    void setTimeout(int ms);
    bool idle() const { return m_idle; }

Q_SIGNALS:
    void timeoutChanged();
    void idleChanged();
    void idled();
    void resumed();

private:
    friend class IdleNotification;
    void restart();
    void setIdle(bool idle);

    int m_timeout = 0;
    bool m_idle = false;
    IdleNotification *m_notification = nullptr;
};
