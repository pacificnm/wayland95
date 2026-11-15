#pragma once

#include <QAbstractListModel>
#include <QDBusContext>
#include <QDBusMessage>
#include <QHash>
#include <QImage>
#include <QQuickImageProvider>
#include <QStringList>
#include <QtQmlIntegration/qqmlintegration.h>

class QDBusServiceWatcher;
class QQmlEngine;
class QJSEngine;
class TrayItemsModel;

// org.kde.StatusNotifierWatcher: where programs register their tray icons.
// We provide it when nobody else does (we're the desktop); nested inside a
// desktop that has one, TrayItemsModel uses that one instead.
class StatusNotifierWatcher : public QObject, protected QDBusContext
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.kde.StatusNotifierWatcher")
    Q_PROPERTY(QStringList RegisteredStatusNotifierItems READ registeredItems)
    Q_PROPERTY(bool IsStatusNotifierHostRegistered READ isHostRegistered)
    Q_PROPERTY(int ProtocolVersion READ protocolVersion)

public:
    explicit StatusNotifierWatcher(TrayItemsModel *model);

    QStringList registeredItems() const { return m_items; }
    bool isHostRegistered() const { return true; }
    int protocolVersion() const { return 0; }

public slots:
    void RegisterStatusNotifierItem(const QString &service);
    void RegisterStatusNotifierHost(const QString &service);

signals:
    void StatusNotifierItemRegistered(const QString &service);
    void StatusNotifierItemUnregistered(const QString &service);
    void StatusNotifierHostRegistered();

private:
    void serviceGone(const QString &name);

    TrayItemsModel *m_model;
    QStringList m_items;
    QDBusServiceWatcher *m_serviceWatcher;
};

// Other programs' tray icons (StatusNotifierItem), as a list model for the
// taskbar. Icons are served to QML by TrayIconProvider as
// "image://trayicon/<key>/<revision>".
//
// Exposed to QML as the singleton `AppTray`.
class TrayItemsModel : public QAbstractListModel
{
    Q_OBJECT
    QML_NAMED_ELEMENT(AppTray)
    QML_SINGLETON

public:
    enum Roles {
        KeyRole = Qt::UserRole + 1,
        TitleRole,
        ToolTipRole,
        IconRole,
        StatusRole,
        HasMenuRole,
    };

    static TrayItemsModel *instance();
    static TrayItemsModel *create(QQmlEngine *, QJSEngine *);

    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    // `x`, `y`: where the click happened, for programs that open a window
    // or menu there themselves
    Q_INVOKABLE void activate(const QString &key, int x, int y);
    Q_INVOKABLE void secondaryActivate(const QString &key, int x, int y);
    Q_INVOKABLE void scroll(const QString &key, int delta);
    // fetches the right-click menu; answers with menuReady() (or asks the
    // program to show its own menu if it has no exported one)
    Q_INVOKABLE void requestMenu(const QString &key, int x, int y);
    // a menu entry from menuReady() was chosen
    Q_INVOKABLE void menuItemClicked(const QString &key, int id);

    QImage icon(const QString &key) const;

    // used by StatusNotifierWatcher and our own registration
    void addItem(const QString &service);
    void removeItem(const QString &service);
    void removeOwner(const QString &busName);

signals:
    // items: [{ id, label, enabled, checked, checkable, separator, children: [...] }]
    void menuReady(const QString &key, const QVariantList &items, int x, int y);

private slots:
    void onItemSignal(const QDBusMessage &message);
    void onWatcherItemRegistered(const QString &service);
    void onWatcherItemUnregistered(const QString &service);

private:
    explicit TrayItemsModel(QObject *parent = nullptr);

    struct Item {
        QString key;      // the service string as registered
        QString bus;      // bus name to call
        QString owner;    // its unique name (signals come from this)
        QString path;
        QString id;
        QString title;
        QString toolTip;
        QString status;
        QString menuPath;
        bool itemIsMenu = false;
        QImage image;
        int revision = 0;
    };

    int indexOf(const QString &key) const;
    void fetchProperties(const QString &key);
    void applyProperties(const QString &key, const QVariantMap &props);
    void callItem(const QString &key, const QString &method, const QVariantList &args);
    void setupHost();

    QList<Item> m_items;
    StatusNotifierWatcher *m_watcher = nullptr;
    QString m_hostName;
};

class TrayIconProvider : public QQuickImageProvider
{
public:
    TrayIconProvider() : QQuickImageProvider(QQuickImageProvider::Image) {}
    QImage requestImage(const QString &id, QSize *size, const QSize &requestedSize) override;
};
