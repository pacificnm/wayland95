#pragma once

#include <QHash>
#include <QImage>
#include <QObject>
#include <QQuickImageProvider>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>
#include <QtQmlIntegration/qqmlintegration.h>

class QQmlEngine;
class QJSEngine;
class NotificationServer;

// org.freedesktop.Notifications on the session bus: what notify-send,
// libnotify and most programs use to show a notification.
class FreedesktopNotifications : public QObject
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.freedesktop.Notifications")

public:
    explicit FreedesktopNotifications(NotificationServer *server);

public slots:
    QStringList GetCapabilities();
    uint Notify(const QString &appName, uint replacesId, const QString &appIcon, const QString &summary,
                const QString &body, const QStringList &actions, const QVariantMap &hints, int expireTimeout);
    void CloseNotification(uint id);
    QString GetServerInformation(QString &vendor, QString &version, QString &specVersion);

signals:
    // reason: 1 expired, 2 dismissed by the user, 3 closed by a call, 4 other
    void NotificationClosed(uint id, uint reason);
    void ActionInvoked(uint id, const QString &actionKey);

private:
    NotificationServer *m_server;
};

// The notifications themselves: those on screen (pop-ups) and the recent
// history, for the desktop to show. Programs send them over D-Bus; the
// desktop itself sends its own with notify().
//
// Exposed to QML as the singleton `Notifications`.
class NotificationServer : public QObject
{
    Q_OBJECT
    QML_NAMED_ELEMENT(Notifications)
    QML_SINGLETON

    // we own org.freedesktop.Notifications (not when nested in a desktop
    // that has its own)
    Q_PROPERTY(bool serviceAvailable READ serviceAvailable CONSTANT)
    // on screen now, oldest first:
    // [{ id, appName, summary, body, icon, urgency, timeout, actions:
    //    [{ key, label }], hasDefault, time }]
    Q_PROPERTY(QVariantList popups READ popups NOTIFY popupsChanged)
    // recent ones, newest first (the same fields, plus `unread`)
    Q_PROPERTY(QVariantList history READ history NOTIFY historyChanged)
    Q_PROPERTY(int unreadCount READ unreadCount NOTIFY historyChanged)
    // Do not disturb: new notifications only go to the history
    Q_PROPERTY(bool quiet READ quiet WRITE setQuiet NOTIFY quietChanged)

public:
    static NotificationServer *instance();
    static NotificationServer *create(QQmlEngine *, QJSEngine *);

    bool serviceAvailable() const { return m_service != nullptr; }
    QVariantList popups() const { return m_popups; }
    QVariantList history() const { return m_history; }
    int unreadCount() const;
    bool quiet() const { return m_quiet; }
    void setQuiet(bool quiet);

    // the desktop's own notifications (urgency 0 low, 1 normal, 2 critical)
    Q_INVOKABLE uint notify(const QString &appName, const QString &summary, const QString &body,
                            const QString &icon = {}, int urgency = 1, int timeoutMs = -1);
    // the same with action buttons: actions is [key, label, key, label...]
    // ("default" = clicking the notification itself); a click comes back
    // as actionInvoked
    Q_INVOKABLE uint notifyWithActions(const QString &appName, const QString &summary, const QString &body,
                                       const QStringList &actions, int urgency = 1, int timeoutMs = -1);
    // the user clicked an action button ("default" = the notification itself)
    Q_INVOKABLE void invokeAction(uint id, const QString &key);
    // closed with its X
    Q_INVOKABLE void dismiss(uint id);
    // its time ran out while nobody looked: stays unread in the history
    Q_INVOKABLE void expire(uint id);
    Q_INVOKABLE void markAllRead();
    Q_INVOKABLE void clearHistory();

    QImage image(const QString &key) const;

    // from the D-Bus side
    uint add(const QString &appName, uint replacesId, const QString &appIcon, const QString &summary,
             const QString &body, const QStringList &actions, const QVariantMap &hints, int expireTimeout);
    void close(uint id, uint reason);

signals:
    void popupsChanged();
    void historyChanged();
    void quietChanged();
    // a new notification (shown or not) - for the sound
    void arrived(const QVariantMap &notification, bool shown);
    // an action of any notification was clicked (the desktop's own too)
    void actionInvoked(uint id, const QString &key);

private:
    explicit NotificationServer(QObject *parent = nullptr);
    int popupIndex(uint id) const;
    int historyIndex(uint id) const;
    void markRead(uint id);

    FreedesktopNotifications *m_service = nullptr;
    QVariantList m_popups;
    QVariantList m_history;
    QHash<uint, QImage> m_images;
    QHash<uint, bool> m_resident;
    uint m_nextId = 1;
    bool m_quiet = false;
};

class NotificationImageProvider : public QQuickImageProvider
{
public:
    NotificationImageProvider() : QQuickImageProvider(QQuickImageProvider::Image) {}
    QImage requestImage(const QString &id, QSize *size, const QSize &requestedSize) override;
};
