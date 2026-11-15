#pragma once

#include <QHash>
#include <QObject>
#include <QPointer>
#include <QPointF>
#include <QQmlParserStatus>
#include <QRect>
#include <QSize>
#include <QWaylandCompositor>
#include <QWaylandSurface>
#include <QtQmlIntegration/qqmlintegration.h>

#include <cstdint>

class QProcess;
#include <QQuickItem>
class QSocketNotifier;
class XWayland;
struct wl_client;
struct wl_listener;
typedef struct xcb_connection_t xcb_connection_t;

// One X11 window, as seen by our window manager. X coordinates and sizes
// are in X root-window pixels, which are Wayland surface coordinates (not
// necessarily QML item units; see WaylandQuickItem.mapToSurface()).
class XWaylandWindow : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Created by XWayland")

    Q_PROPERTY(QWaylandSurface *surface READ surface NOTIFY surfaceChanged)
    Q_PROPERTY(QString title READ title NOTIFY titleChanged)
    Q_PROPERTY(QString appId READ appId NOTIFY propertiesChanged)
    // menus, tooltips, drag icons: placed by the client itself, no frame
    Q_PROPERTY(bool overrideRedirect READ overrideRedirect CONSTANT)
    Q_PROPERTY(bool decorated READ decorated NOTIFY propertiesChanged)
    // normal, dialog, utility, splash, toolbar, menu, tooltip, notification, dnd, dock...
    Q_PROPERTY(QString windowType READ windowType NOTIFY propertiesChanged)
    Q_PROPERTY(XWaylandWindow *transientFor READ transientFor NOTIFY propertiesChanged)
    Q_PROPERTY(QSize minSize READ minSize NOTIFY propertiesChanged)
    Q_PROPERTY(QSize maxSize READ maxSize NOTIFY propertiesChanged)
    Q_PROPERTY(QRect geometry READ geometry NOTIFY geometryChanged)
    // the state the client asked for before mapping (_NET_WM_STATE)
    Q_PROPERTY(bool wantsMaximized READ wantsMaximized NOTIFY propertiesChanged)
    Q_PROPERTY(bool wantsFullscreen READ wantsFullscreen NOTIFY propertiesChanged)

public:
    XWaylandWindow(XWayland *wm, uint32_t id, bool overrideRedirect, const QRect &geometry);

    uint32_t id() const { return m_id; }
    QWaylandSurface *surface() const { return m_surface; }
    QString title() const { return m_title; }
    QString appId() const { return m_appId; }
    bool overrideRedirect() const { return m_overrideRedirect; }
    bool decorated() const;
    QString windowType() const { return m_windowType; }
    XWaylandWindow *transientFor() const { return m_transientFor; }
    QSize minSize() const { return m_minSize; }
    QSize maxSize() const { return m_maxSize; }
    QRect geometry() const { return m_geometry; }
    bool wantsMaximized() const { return m_wantsMaximized; }
    bool wantsFullscreen() const { return m_wantsFullscreen; }

    Q_INVOKABLE void move(int x, int y);
    Q_INVOKABLE void resize(int width, int height);
    // gives the window X keyboard focus and raises it
    Q_INVOKABLE void activate();
    // drops X focus, unless another X window has taken it meanwhile
    Q_INVOKABLE void deactivate();
    // asks politely (WM_DELETE_WINDOW) when the client supports it
    Q_INVOKABLE void close();
    // End Task: cut the program off from the X server, which ends it
    // (unlike close(), it doesn't ask)
    Q_INVOKABLE void kill();
    // mirrors our frame's state into WM_STATE / _NET_WM_STATE
    Q_INVOKABLE void setStates(bool minimized, bool maximized, bool fullscreen);

signals:
    void surfaceChanged();
    void titleChanged();
    void propertiesChanged();
    void geometryChanged();
    // the client asks the window manager for something
    void maximizeRequested(bool on);
    void fullscreenRequested(bool on);
    void minimizeRequested();
    void activateRequested();
    void moveRequested();
    // the window is gone from the screen; drop any frame around it
    void unmapped();

private:
    friend class XWayland;

    XWayland *m_wm;
    uint32_t m_id;
    bool m_overrideRedirect;
    QRect m_geometry;
    QPointer<QWaylandSurface> m_surface;
    QString m_title;
    QString m_appId;
    QString m_windowType = QStringLiteral("normal");
    QPointer<XWaylandWindow> m_transientFor;
    QSize m_minSize;
    QSize m_maxSize;
    bool m_noDecorations = false;   // _MOTIF_WM_HINTS
    bool m_acceptsInput = true;     // WM_HINTS
    bool m_deleteWindow = false;    // WM_PROTOCOLS
    bool m_takeFocus = false;
    bool m_wantsMaximized = false;
    bool m_wantsFullscreen = false;
    bool m_maximized = false;       // as our frame has it
    bool m_fullscreen = false;
    bool m_xMapped = false;         // mapped on the X side
    bool m_shown = false;           // handed to QML
    bool m_exposed = false;         // ...at least once
};

// Runs Xwayland (rootless) as a client of our compositor and acts as its
// X11 window manager, so X-only programs get Win95 frames too. The X
// display (":N") is picked by Xwayland itself and handed to the Launcher.
//
// Instantiated once in Main.qml.
class XWayland : public QObject, public QQmlParserStatus
{
    Q_OBJECT
    Q_INTERFACES(QQmlParserStatus)
    QML_ELEMENT

    Q_PROPERTY(QWaylandCompositor *compositor READ compositor WRITE setCompositor NOTIFY compositorChanged)
    Q_PROPERTY(bool enabled READ enabled WRITE setEnabled NOTIFY enabledChanged)
    // ":N" once Xwayland is ready, empty otherwise
    Q_PROPERTY(QString displayName READ displayName NOTIFY displayNameChanged)

public:
    explicit XWayland(QObject *parent = nullptr);
    ~XWayland() override;

    QWaylandCompositor *compositor() const { return m_compositor; }
    void setCompositor(QWaylandCompositor *compositor);
    bool enabled() const { return m_enabled; }
    void setEnabled(bool enabled);
    QString displayName() const { return m_displayName; }

    // The client asked to be dragged (_NET_WM_MOVERESIZE) while the mouse
    // button is down: move `item` with the pointer until it's released.
    Q_INVOKABLE void beginMove(QQuickItem *item);

    void classBegin() override {}
    void componentComplete() override;

signals:
    void compositorChanged();
    void enabledChanged();
    void displayNameChanged();
    // a window is on screen and has its surface; give it a frame (or, for
    // override-redirect windows, just show it at its own position)
    void windowMapped(XWaylandWindow *window);

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    friend class XWaylandWindow;

    enum Atom {
        WM_PROTOCOLS, WM_DELETE_WINDOW, WM_TAKE_FOCUS, WM_STATE, WM_CHANGE_STATE,
        WM_NAME, WM_CLASS, WM_HINTS, WM_NORMAL_HINTS, WM_TRANSIENT_FOR, WM_S0,
        UTF8_STRING, WL_SURFACE_ID, MOTIF_WM_HINTS,
        NET_SUPPORTED, NET_SUPPORTING_WM_CHECK, NET_WM_NAME, NET_ACTIVE_WINDOW,
        NET_CLIENT_LIST, NET_CLOSE_WINDOW, NET_WM_PID, NET_WM_MOVERESIZE, NET_WM_CM_S0,
        NET_WM_STATE, NET_WM_STATE_MAXIMIZED_VERT, NET_WM_STATE_MAXIMIZED_HORZ,
        NET_WM_STATE_FULLSCREEN, NET_WM_STATE_HIDDEN, NET_WM_STATE_FOCUSED,
        NET_WM_WINDOW_TYPE, NET_WM_WINDOW_TYPE_NORMAL, NET_WM_WINDOW_TYPE_DIALOG,
        NET_WM_WINDOW_TYPE_UTILITY, NET_WM_WINDOW_TYPE_TOOLBAR, NET_WM_WINDOW_TYPE_SPLASH,
        NET_WM_WINDOW_TYPE_MENU, NET_WM_WINDOW_TYPE_DROPDOWN_MENU,
        NET_WM_WINDOW_TYPE_POPUP_MENU, NET_WM_WINDOW_TYPE_TOOLTIP,
        NET_WM_WINDOW_TYPE_NOTIFICATION, NET_WM_WINDOW_TYPE_COMBO, NET_WM_WINDOW_TYPE_DND,
        NET_WM_WINDOW_TYPE_DOCK, NET_WM_WINDOW_TYPE_DESKTOP,
        AtomCount
    };

    void start();
    void stop();
    void readDisplayNumber();
    void readServerOutput();
    bool initWindowManager();
    void readEvents();
    void flush();
    void handleEvent(void *event);
    void handleClientMessage(XWaylandWindow *window, uint32_t type, const uint32_t *data);
    void readProperties(XWaylandWindow *window);
    void associateSurface(XWaylandWindow *window, uint32_t surfaceId);
    void onSurfaceCreated(QWaylandSurface *surface);
    void tryShow(XWaylandWindow *window);
    void hide(XWaylandWindow *window);
    void forget(XWaylandWindow *window);
    void setWmState(XWaylandWindow *window, uint32_t state);
    void setNetWmState(XWaylandWindow *window, bool minimized);
    void sendProtocolMessage(uint32_t window, uint32_t protocol);
    void sendConfigureNotify(XWaylandWindow *window);
    void updateClientList();
    void endMove();

    QPointer<QWaylandCompositor> m_compositor;
    bool m_enabled = true;
    bool m_completed = false;
    QString m_displayName;

    QProcess *m_process = nullptr;
    wl_client *m_client = nullptr;
    wl_listener *m_clientDestroyed = nullptr;
    int m_wmFd = -1;
    int m_displayFd = -1;
    QSocketNotifier *m_displayNotifier = nullptr;
    QByteArray m_displayBuffer;
    QByteArray m_serverOutput;

    xcb_connection_t *m_conn = nullptr;
    QSocketNotifier *m_xcbNotifier = nullptr;
    uint32_t m_root = 0;
    uint32_t m_wmWindow = 0;
    uint32_t m_atoms[AtomCount] = {};
    QHash<uint32_t, XWaylandWindow *> m_windows;
    QHash<uint32_t, uint32_t> m_pendingSurfaces; // wl_surface id -> X window
    QPointer<XWaylandWindow> m_focused;

    // interactive move (beginMove)
    QPointer<QQuickItem> m_moveItem;
    QObject *m_moveWindow = nullptr;
    bool m_moveStarted = false;
    QPointF m_moveStartScene;
    QPointF m_moveStartPos;
};
