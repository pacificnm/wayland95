#pragma once

// The window list for the taskbar and Alt+Tab, from the compositor through
// wlr-foreign-toplevel-management. It keeps the interface the Qt
// compositor's WindowManager has (src/windowmanager.h), so the same QML
// (Taskbar.qml) works with either: the model's roles title, minimized,
// active and frame, where "frame" is a Toplevel with the methods
// Taskbar.qml calls (requestActivate, toggleMaximize, closeWindow...).
//
// Exposed to QML as the singleton `WindowManager`.

#include <QAbstractListModel>
#include <QList>
#include <QPointer>
#include <QtQml/qqmlregistration.h>
#include <QtWaylandClient/QWaylandClientExtensionTemplate>

#include "qwayland-wlr-foreign-toplevel-management-unstable-v1.h"

class Toplevel : public QObject, public QtWayland::zwlr_foreign_toplevel_handle_v1
{
    Q_OBJECT
    QML_ANONYMOUS
    Q_PROPERTY(QString title READ title NOTIFY changed)
    Q_PROPERTY(QString appId READ appId NOTIFY changed)
    Q_PROPERTY(bool minimized READ minimized NOTIFY changed)
    Q_PROPERTY(bool maximized READ maximized NOTIFY changed)
    Q_PROPERTY(bool fullscreen READ fullscreen NOTIFY changed)
    Q_PROPERTY(bool active READ active NOTIFY changed)

public:
    explicit Toplevel(::zwlr_foreign_toplevel_handle_v1 *handle);
    ~Toplevel() override;

    QString title() const { return m_title; }
    QString appId() const { return m_appId; }
    bool minimized() const { return m_minimized; }
    bool maximized() const { return m_maximized; }
    bool fullscreen() const { return m_fullscreen; }
    bool active() const { return m_active; }

    // what Taskbar.qml calls on a "frame"
    Q_INVOKABLE void requestActivate();
    Q_INVOKABLE void toggleMaximize();
    Q_INVOKABLE void setMinimizedState(bool minimized);
    Q_INVOKABLE void closeWindow();
    // End Task: until the compositor can name the program's process (step
    // 4c), a close request
    Q_INVOKABLE void forceClose();

Q_SIGNALS:
    void changed();
    void gone();

protected:
    void zwlr_foreign_toplevel_handle_v1_title(const QString &title) override { m_pending.title = title; }
    void zwlr_foreign_toplevel_handle_v1_app_id(const QString &appId) override { m_pending.appId = appId; }
    void zwlr_foreign_toplevel_handle_v1_state(wl_array *state) override;
    void zwlr_foreign_toplevel_handle_v1_done() override;
    void zwlr_foreign_toplevel_handle_v1_closed() override { Q_EMIT gone(); }

private:
    struct State {
        QString title, appId;
        bool minimized = false, maximized = false, fullscreen = false, active = false;
    } m_pending;
    QString m_title, m_appId;
    bool m_minimized = false, m_maximized = false, m_fullscreen = false, m_active = false;
};

class ToplevelManager : public QWaylandClientExtensionTemplate<ToplevelManager>,
                        public QtWayland::zwlr_foreign_toplevel_manager_v1
{
    Q_OBJECT
public:
    ToplevelManager() : QWaylandClientExtensionTemplate<ToplevelManager>(3) { initialize(); }

Q_SIGNALS:
    void toplevelAdded(Toplevel *toplevel);

protected:
    void zwlr_foreign_toplevel_manager_v1_toplevel(::zwlr_foreign_toplevel_handle_v1 *handle) override
    {
        Q_EMIT toplevelAdded(new Toplevel(handle));
    }
};

class WindowManager : public QAbstractListModel
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON
    Q_PROPERTY(int count READ count NOTIFY countChanged)

public:
    enum Roles {
        TitleRole = Qt::UserRole + 1,
        MinimizedRole,
        ActiveRole,
        FrameRole,
        AppIdRole,
        MaximizedRole,
    };

    explicit WindowManager(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;
    int count() const { return int(m_toplevels.size()); }

    // as in the Qt compositor's WindowManager
    Q_INVOKABLE void setMinimized(QObject *frame, bool minimized);
    Q_INVOKABLE void toggleFromTaskbar(int index);
    Q_INVOKABLE QObject *activeFrame() const;
    // a program's window to the front (brought back if it's minimized): the
    // one of this app id that was active last; false if it has none (the
    // sound menu's Show the Player: an MPRIS player's DesktopEntry)
    Q_INVOKABLE bool showApp(const QString &appId);
    // Alt+Tab's order: [{ frame, title, minimized }], the most recently
    // active first
    Q_INVOKABLE QVariantList switchOrder() const;

Q_SIGNALS:
    void countChanged();

private:
    void add(Toplevel *toplevel);
    void remove(Toplevel *toplevel);
    void changed(Toplevel *toplevel);

    ToplevelManager *m_manager;
    QList<Toplevel *> m_toplevels;       // in the order they appeared (the taskbar's)
    QList<QPointer<Toplevel>> m_recent;  // most recently active first
};
