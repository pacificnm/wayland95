#pragma once

// Locking the session (ext-session-lock-v1): the password-protected screen
// saver. While locked the compositor shows nothing but our lock windows
// and gives them all input, until we unlock; if the shell dies, the screen
// stays locked. Exposed to QML as the singleton `SessionLock`:
//
//   SessionLock.lock()            ask for it; `locked` once the compositor
//                                 confirms (everything is covered)
//   Window { visible: SessionLock.active
//            Component.onCompleted: SessionLock.attach(this) }
//                                 a lock window (one screen)
//   SessionLock.unlock()

#include <QObject>
#include <QWindow>
#include <QtQml/qqmlregistration.h>

class QQmlEngine;
class QJSEngine;

class SessionLockObject;

class SessionLock : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON
    // lock() was called and unlock() not yet: show the lock windows
    Q_PROPERTY(bool active READ active NOTIFY changed)
    // the compositor confirmed it: nothing else shows or gets input
    Q_PROPERTY(bool locked READ locked NOTIFY changed)
    Q_PROPERTY(bool supported READ supported CONSTANT)

public:
    explicit SessionLock(QObject *parent = nullptr);
    ~SessionLock() override;
    static SessionLock *instance();
    static SessionLock *create(QQmlEngine *, QJSEngine *);

    bool active() const { return m_lock != nullptr; }
    bool locked() const { return m_locked; }
    bool supported() const;

    Q_INVOKABLE bool lock();
    Q_INVOKABLE void unlock();
    // makes `window` a lock window (before it's first shown)
    Q_INVOKABLE void attach(QWindow *window);

    SessionLockObject *lockObject() const { return m_lock; }

Q_SIGNALS:
    void changed();
    // the compositor wouldn't lock (another locker has it)
    void refused();

private:
    friend class SessionLockObject;
    void confirmed();
    void finished();

    SessionLockObject *m_lock = nullptr;
    bool m_locked = false;
};
