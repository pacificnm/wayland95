#include "xwayland.h"

#include "launcher.h"

#include <QMouseEvent>
#include <QProcess>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickWindow>
#include <QSocketNotifier>
#include <QStandardPaths>
#include <QWaylandClient>
#include <QtDebug>

#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <sys/socket.h>
#include <unistd.h>

#include <wayland-server-core.h>
#include <xcb/xcb.h>
#include <xcb/xcbext.h>

// ---------------------------------------------------------------------------
// constants from ICCCM / EWMH / Motif that xcb doesn't define

namespace {

constexpr uint32_t WithdrawnState = 0;
constexpr uint32_t NormalState = 1;
constexpr uint32_t IconicState = 3;

constexpr uint32_t InputHint = 1;          // WM_HINTS.flags
constexpr uint32_t PMinSize = 1 << 4;      // WM_NORMAL_HINTS.flags
constexpr uint32_t PMaxSize = 1 << 5;
constexpr uint32_t MwmHintsDecorations = 1 << 1;

constexpr uint32_t NetWmStateRemove = 0;
constexpr uint32_t NetWmStateAdd = 1;
constexpr uint32_t NetWmMoveResizeMove = 8;

const char *const atomNames[] = {
    "WM_PROTOCOLS", "WM_DELETE_WINDOW", "WM_TAKE_FOCUS", "WM_STATE", "WM_CHANGE_STATE",
    "WM_NAME", "WM_CLASS", "WM_HINTS", "WM_NORMAL_HINTS", "WM_TRANSIENT_FOR", "WM_S0",
    "UTF8_STRING", "WL_SURFACE_ID", "_MOTIF_WM_HINTS",
    "_NET_SUPPORTED", "_NET_SUPPORTING_WM_CHECK", "_NET_WM_NAME", "_NET_ACTIVE_WINDOW",
    "_NET_CLIENT_LIST", "_NET_CLOSE_WINDOW", "_NET_WM_PID", "_NET_WM_MOVERESIZE", "_NET_WM_CM_S0",
    "_NET_WM_STATE", "_NET_WM_STATE_MAXIMIZED_VERT", "_NET_WM_STATE_MAXIMIZED_HORZ",
    "_NET_WM_STATE_FULLSCREEN", "_NET_WM_STATE_HIDDEN", "_NET_WM_STATE_FOCUSED",
    "_NET_WM_WINDOW_TYPE", "_NET_WM_WINDOW_TYPE_NORMAL", "_NET_WM_WINDOW_TYPE_DIALOG",
    "_NET_WM_WINDOW_TYPE_UTILITY", "_NET_WM_WINDOW_TYPE_TOOLBAR", "_NET_WM_WINDOW_TYPE_SPLASH",
    "_NET_WM_WINDOW_TYPE_MENU", "_NET_WM_WINDOW_TYPE_DROPDOWN_MENU",
    "_NET_WM_WINDOW_TYPE_POPUP_MENU", "_NET_WM_WINDOW_TYPE_TOOLTIP",
    "_NET_WM_WINDOW_TYPE_NOTIFICATION", "_NET_WM_WINDOW_TYPE_COMBO", "_NET_WM_WINDOW_TYPE_DND",
    "_NET_WM_WINDOW_TYPE_DOCK", "_NET_WM_WINDOW_TYPE_DESKTOP",
};
static_assert(sizeof(atomNames) / sizeof(atomNames[0]) == 44, "atom table out of sync");

// The two Composite requests we need, sent raw so building doesn't need
// the xcb-composite development package.
xcb_extension_t compositeExtension = { "Composite", 0 };

bool redirectSubwindows(xcb_connection_t *c, uint32_t window)
{
    struct {
        uint8_t major, minor;
        uint16_t length;
        uint32_t clientMajor, clientMinor;
    } version = { 0, 0, 0, 0, 4 };
    static const xcb_protocol_request_t versionRequest = { 2, &compositeExtension, 0 /* QueryVersion */, 0 };
    struct iovec parts[4];
    parts[2].iov_base = &version;
    parts[2].iov_len = sizeof(version);
    parts[3].iov_base = nullptr;
    parts[3].iov_len = 0;
    const unsigned int sequence = xcb_send_request(c, XCB_REQUEST_CHECKED, parts + 2, &versionRequest);
    xcb_generic_error_t *error = nullptr;
    void *reply = xcb_wait_for_reply(c, sequence, &error);
    free(reply);
    if (!reply) {
        free(error);
        return false;
    }

    struct {
        uint8_t major, minor;
        uint16_t length;
        uint32_t window;
        uint8_t update, pad[3];
    } redirect = { 0, 0, 0, window, 1 /* Manual */, { 0, 0, 0 } };
    static const xcb_protocol_request_t redirectRequest = { 2, &compositeExtension, 2 /* RedirectSubwindows */, 1 };
    parts[2].iov_base = &redirect;
    parts[2].iov_len = sizeof(redirect);
    const xcb_void_cookie_t cookie = { xcb_send_request(c, XCB_REQUEST_CHECKED, parts + 2, &redirectRequest) };
    error = xcb_request_check(c, cookie);
    free(error);
    return !error;
}

struct ClientDestroyListener {
    wl_listener listener; // first, so the wl_listener* converts back
    XWayland *self;
};

// a property's value as raw bytes (empty if unset)
struct Property {
    uint32_t type = XCB_NONE;
    QByteArray data;

    const uint32_t *words() const { return reinterpret_cast<const uint32_t *>(data.constData()); }
    int wordCount() const { return int(data.size() / 4); }
};

Property readProperty(xcb_connection_t *c, xcb_get_property_cookie_t cookie)
{
    Property p;
    xcb_get_property_reply_t *reply = xcb_get_property_reply(c, cookie, nullptr);
    if (!reply)
        return p;
    p.type = reply->type;
    const int length = xcb_get_property_value_length(reply);
    if (reply->type != XCB_NONE && length > 0)
        p.data = QByteArray(static_cast<const char *>(xcb_get_property_value(reply)), length);
    free(reply);
    return p;
}

} // namespace

// ---------------------------------------------------------------------------
// XWaylandWindow

XWaylandWindow::XWaylandWindow(XWayland *wm, uint32_t id, bool overrideRedirect, const QRect &geometry)
    : QObject(wm)
    , m_wm(wm)
    , m_id(id)
    , m_overrideRedirect(overrideRedirect)
    , m_geometry(geometry)
{
    QQmlEngine::setObjectOwnership(this, QQmlEngine::CppOwnership);
}

bool XWaylandWindow::decorated() const
{
    if (m_overrideRedirect || m_noDecorations)
        return false;
    static const QStringList framed = { QStringLiteral("normal"), QStringLiteral("dialog"),
                                        QStringLiteral("utility"), QStringLiteral("toolbar") };
    return framed.contains(m_windowType);
}

void XWaylandWindow::move(int x, int y)
{
    if (!m_wm->m_conn || (x == m_geometry.x() && y == m_geometry.y()))
        return;
    const uint32_t values[] = { uint32_t(x), uint32_t(y) };
    xcb_configure_window(m_wm->m_conn, m_id, XCB_CONFIG_WINDOW_X | XCB_CONFIG_WINDOW_Y, values);
    m_wm->flush();
}

void XWaylandWindow::resize(int width, int height)
{
    if (!m_wm->m_conn)
        return;
    width = qMax(1, width);
    height = qMax(1, height);
    if (m_maxSize.width() > 0)
        width = qMin(width, m_maxSize.width());
    if (m_maxSize.height() > 0)
        height = qMin(height, m_maxSize.height());
    width = qMax(width, m_minSize.width());
    height = qMax(height, m_minSize.height());
    if (width == m_geometry.width() && height == m_geometry.height())
        return;
    const uint32_t values[] = { uint32_t(width), uint32_t(height) };
    xcb_configure_window(m_wm->m_conn, m_id, XCB_CONFIG_WINDOW_WIDTH | XCB_CONFIG_WINDOW_HEIGHT, values);
    m_wm->flush();
}

void XWaylandWindow::activate()
{
    xcb_connection_t *c = m_wm->m_conn;
    if (!c || !m_xMapped)
        return;

    const uint32_t stack = XCB_STACK_MODE_ABOVE;
    xcb_configure_window(c, m_id, XCB_CONFIG_WINDOW_STACK_MODE, &stack);
    if (m_acceptsInput)
        xcb_set_input_focus(c, XCB_INPUT_FOCUS_POINTER_ROOT, m_id, XCB_CURRENT_TIME);
    if (m_takeFocus)
        m_wm->sendProtocolMessage(m_id, m_wm->m_atoms[XWayland::WM_TAKE_FOCUS]);
    xcb_change_property(c, XCB_PROP_MODE_REPLACE, m_wm->m_root, m_wm->m_atoms[XWayland::NET_ACTIVE_WINDOW],
                        XCB_ATOM_WINDOW, 32, 1, &m_id);

    XWaylandWindow *previous = m_wm->m_focused;
    m_wm->m_focused = this;
    if (previous && previous != this)
        m_wm->setNetWmState(previous, false);
    m_wm->setNetWmState(this, false);
    m_wm->flush();
}

void XWaylandWindow::deactivate()
{
    xcb_connection_t *c = m_wm->m_conn;
    if (!c || m_wm->m_focused != this)
        return;
    m_wm->m_focused = nullptr;
    xcb_set_input_focus(c, XCB_INPUT_FOCUS_NONE, XCB_NONE, XCB_CURRENT_TIME);
    const uint32_t none = XCB_NONE;
    xcb_change_property(c, XCB_PROP_MODE_REPLACE, m_wm->m_root, m_wm->m_atoms[XWayland::NET_ACTIVE_WINDOW],
                        XCB_ATOM_WINDOW, 32, 1, &none);
    if (m_xMapped)
        m_wm->setNetWmState(this, false);
    m_wm->flush();
}

void XWaylandWindow::close()
{
    if (!m_wm->m_conn)
        return;
    if (m_deleteWindow)
        m_wm->sendProtocolMessage(m_id, m_wm->m_atoms[XWayland::WM_DELETE_WINDOW]);
    else
        xcb_kill_client(m_wm->m_conn, m_id);
    m_wm->flush();
}

void XWaylandWindow::kill()
{
    if (!m_wm->m_conn)
        return;
    xcb_kill_client(m_wm->m_conn, m_id);
    m_wm->flush();
}

void XWaylandWindow::setStates(bool minimized, bool maximized, bool fullscreen)
{
    if (!m_wm->m_conn || !m_xMapped)
        return;
    m_maximized = maximized;
    m_fullscreen = fullscreen;
    // The window stays mapped while minimized: unmapping it would make
    // Xwayland destroy its surface. WM_STATE tells the client instead.
    m_wm->setWmState(this, minimized ? IconicState : NormalState);
    m_wm->setNetWmState(this, minimized);
    m_wm->flush();
}

// ---------------------------------------------------------------------------
// XWayland: starting and stopping the server

XWayland::XWayland(QObject *parent)
    : QObject(parent)
{
}

XWayland::~XWayland()
{
    endMove();
    if (m_process) {
        m_process->disconnect(this);
        m_process->kill();
        m_process->waitForFinished(1000);
    }
    if (m_client && m_clientDestroyed)
        wl_list_remove(&m_clientDestroyed->link);
    delete reinterpret_cast<ClientDestroyListener *>(m_clientDestroyed);
    m_clientDestroyed = nullptr;
    if (m_conn)
        xcb_disconnect(m_conn);
    else if (m_wmFd >= 0)
        ::close(m_wmFd);
    if (m_displayFd >= 0)
        ::close(m_displayFd);
}

void XWayland::setCompositor(QWaylandCompositor *compositor)
{
    if (m_compositor == compositor)
        return;
    m_compositor = compositor;
    emit compositorChanged();
}

void XWayland::setEnabled(bool enabled)
{
    if (m_enabled == enabled)
        return;
    m_enabled = enabled;
    emit enabledChanged();
    if (!m_completed)
        return;
    if (enabled)
        start();
    else
        stop();
}

void XWayland::componentComplete()
{
    m_completed = true;
    if (!m_enabled)
        return;
    if (m_compositor && !m_compositor->isCreated()) {
        connect(m_compositor, &QWaylandCompositor::createdChanged, this, [this] {
            if (m_compositor->isCreated() && m_enabled)
                start();
        }, Qt::SingleShotConnection);
        return;
    }
    start();
}

void XWayland::start()
{
    if (m_process || !m_compositor || !m_compositor->isCreated())
        return;

    const QString program = QStandardPaths::findExecutable(QStringLiteral("Xwayland"));
    if (program.isEmpty()) {
        qWarning("XWayland: Xwayland isn't installed, X11 programs won't run "
                 "(sudo apt-get install xwayland)");
        return;
    }

    // Three channels to the new server: its Wayland connection (to us), the
    // window manager's X connection (also us), and a pipe on which it
    // reports which display number it took.
    int wlFds[2], wmFds[2], displayFds[2];
    if (::socketpair(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0, wlFds) < 0)
        return;
    if (::socketpair(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0, wmFds) < 0) {
        ::close(wlFds[0]); ::close(wlFds[1]);
        return;
    }
    if (::pipe2(displayFds, O_CLOEXEC) < 0) {
        ::close(wlFds[0]); ::close(wlFds[1]); ::close(wmFds[0]); ::close(wmFds[1]);
        return;
    }

    m_client = wl_client_create(m_compositor->display(), wlFds[0]);
    if (!m_client) {
        qWarning("XWayland: couldn't create the Wayland client for Xwayland");
        ::close(wlFds[0]); ::close(wlFds[1]); ::close(wmFds[0]); ::close(wmFds[1]);
        ::close(displayFds[0]); ::close(displayFds[1]);
        return;
    }
    auto *listener = new ClientDestroyListener;
    listener->self = this;
    listener->listener.notify = [](wl_listener *l, void *) {
        XWayland *self = reinterpret_cast<ClientDestroyListener *>(l)->self;
        self->m_client = nullptr;
    };
    wl_client_add_destroy_listener(m_client, &listener->listener);
    m_clientDestroyed = &listener->listener;

    m_wmFd = wmFds[0];
    m_displayFd = displayFds[0];

    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    env.insert(QStringLiteral("WAYLAND_SOCKET"), QString::number(wlFds[1]));
    env.remove(QStringLiteral("WAYLAND_DISPLAY"));
    env.remove(QStringLiteral("DISPLAY"));

    m_process = new QProcess(this);
    m_process->setProgram(program);
    m_process->setArguments({ QStringLiteral("-rootless"),
                              QStringLiteral("-displayfd"), QString::number(displayFds[1]),
                              QStringLiteral("-wm"), QString::number(wmFds[1]),
                              QStringLiteral("-nolisten"), QStringLiteral("tcp") });
    m_process->setProcessEnvironment(env);
    // Xwayland is chatty about harmless things; pass on only the rest
    m_process->setProcessChannelMode(QProcess::MergedChannels);
    connect(m_process, &QProcess::readyRead, this, &XWayland::readServerOutput);
    const int keep[] = { wlFds[1], wmFds[1], displayFds[1] };
    m_process->setChildProcessModifier([keep] {
        for (int fd : keep)
            ::fcntl(fd, F_SETFD, 0); // let these three survive exec
    });
    connect(m_process, &QProcess::finished, this, [this](int code, QProcess::ExitStatus status) {
        qWarning("XWayland: Xwayland exited (%s %d); X11 programs are unavailable",
                 status == QProcess::CrashExit ? "crashed, signal" : "exit code", code);
        stop();
    });
    connect(m_process, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart) {
            qWarning() << "XWayland: couldn't start Xwayland:" << m_process->errorString();
            stop();
        }
    });
    m_process->start();

    // our copies of the child's ends
    ::close(wlFds[1]);
    ::close(wmFds[1]);
    ::close(displayFds[1]);

    m_displayNotifier = new QSocketNotifier(m_displayFd, QSocketNotifier::Read, this);
    connect(m_displayNotifier, &QSocketNotifier::activated, this, &XWayland::readDisplayNumber);

    connect(m_compositor, &QWaylandCompositor::surfaceCreated, this, &XWayland::onSurfaceCreated,
            Qt::UniqueConnection);
}

void XWayland::readServerOutput()
{
    if (!m_process)
        return;
    m_serverOutput += m_process->readAll();
    qsizetype nl;
    while ((nl = m_serverOutput.indexOf('\n')) >= 0) {
        const QByteArray line = m_serverOutput.left(nl);
        m_serverOutput.remove(0, nl + 1);
        // Known noise:
        //  - xkbcomp converting our keymap to X11's format, which can't
        //    hold keycodes above 255 or a few newer keysyms; it skips them
        //    and says itself that this isn't fatal
        //  - the server trying display numbers the host desktop already uses
        if (line.startsWith("The XKEYBOARD keymap compiler (xkbcomp) reports:") || line.startsWith(">")
            || line.startsWith("Errors from xkbcomp are not fatal")
            || line.startsWith("_XSERVTransSocketUNIXCreateListener")
            || line.startsWith("_XSERVTransMakeAllCOTSServerListeners") || line.trimmed().isEmpty())
            continue;
        std::fprintf(stderr, "Xwayland: %s\n", line.constData());
    }
}

void XWayland::readDisplayNumber()
{
    char buffer[32];
    const ssize_t n = ::read(m_displayFd, buffer, sizeof(buffer));
    if (n > 0)
        m_displayBuffer.append(buffer, n);
    if (n > 0 && !m_displayBuffer.contains('\n'))
        return;

    delete m_displayNotifier;
    m_displayNotifier = nullptr;
    ::close(m_displayFd);
    m_displayFd = -1;

    bool ok = false;
    const int display = m_displayBuffer.trimmed().toInt(&ok);
    if (n <= 0 || !ok) {
        qWarning("XWayland: Xwayland didn't report a display number");
        stop();
        return;
    }
    if (!initWindowManager()) {
        stop();
        return;
    }

    m_displayName = QStringLiteral(":%1").arg(display);
    Launcher::setX11Display(m_displayName);
    std::fprintf(stderr, "win95compositor: X11 programs connect with DISPLAY=%s\n",
                 qPrintable(m_displayName));
    emit displayNameChanged();
}

void XWayland::stop()
{
    endMove();
    const auto windows = m_windows.values();
    for (XWaylandWindow *window : windows)
        forget(window);
    m_pendingSurfaces.clear();

    delete m_xcbNotifier;
    m_xcbNotifier = nullptr;
    if (m_conn) {
        xcb_disconnect(m_conn); // closes m_wmFd
        m_conn = nullptr;
    } else if (m_wmFd >= 0) {
        ::close(m_wmFd);
    }
    m_wmFd = -1;

    delete m_displayNotifier;
    m_displayNotifier = nullptr;
    if (m_displayFd >= 0)
        ::close(m_displayFd);
    m_displayFd = -1;
    m_displayBuffer.clear();

    if (m_client) {
        wl_list_remove(&m_clientDestroyed->link);
        wl_client_destroy(m_client);
        m_client = nullptr;
    }
    delete reinterpret_cast<ClientDestroyListener *>(m_clientDestroyed);
    m_clientDestroyed = nullptr;

    if (m_process) {
        QProcess *process = m_process;
        m_process = nullptr;
        process->disconnect(this);
        if (process->state() != QProcess::NotRunning) {
            process->terminate();
            if (!process->waitForFinished(1000))
                process->kill();
        }
        process->deleteLater();
    }

    if (!m_displayName.isEmpty()) {
        m_displayName.clear();
        Launcher::setX11Display({});
        emit displayNameChanged();
    }
}

// ---------------------------------------------------------------------------
// the window manager

bool XWayland::initWindowManager()
{
    m_conn = xcb_connect_to_fd(m_wmFd, nullptr);
    if (xcb_connection_has_error(m_conn)) {
        qWarning("XWayland: couldn't connect to Xwayland's window manager socket");
        xcb_disconnect(m_conn); // also closes the fd
        m_conn = nullptr;
        m_wmFd = -1;
        return false;
    }

    xcb_intern_atom_cookie_t cookies[AtomCount];
    for (int i = 0; i < AtomCount; ++i)
        cookies[i] = xcb_intern_atom(m_conn, 0, uint16_t(std::strlen(atomNames[i])), atomNames[i]);
    for (int i = 0; i < AtomCount; ++i) {
        xcb_intern_atom_reply_t *reply = xcb_intern_atom_reply(m_conn, cookies[i], nullptr);
        m_atoms[i] = reply ? reply->atom : XCB_NONE;
        free(reply);
    }

    xcb_screen_t *screen = xcb_setup_roots_iterator(xcb_get_setup(m_conn)).data;
    m_root = screen->root;

    const uint32_t rootMask = XCB_EVENT_MASK_SUBSTRUCTURE_NOTIFY | XCB_EVENT_MASK_SUBSTRUCTURE_REDIRECT
                              | XCB_EVENT_MASK_PROPERTY_CHANGE;
    xcb_generic_error_t *error = xcb_request_check(
        m_conn, xcb_change_window_attributes_checked(m_conn, m_root, XCB_CW_EVENT_MASK, &rootMask));
    if (error) {
        qWarning("XWayland: another window manager is already running on Xwayland");
        free(error);
        return false;
    }

    // Rootless Xwayland only gives a window a Wayland surface once it is
    // redirected (drawn offscreen for a compositor - us).
    if (!redirectSubwindows(m_conn, m_root)) {
        qWarning("XWayland: Xwayland refused to redirect windows (Composite)");
        return false;
    }

    // the EWMH "a compliant window manager is running" window
    m_wmWindow = xcb_generate_id(m_conn);
    xcb_create_window(m_conn, XCB_COPY_FROM_PARENT, m_wmWindow, m_root, -100, -100, 1, 1, 0,
                      XCB_WINDOW_CLASS_INPUT_OUTPUT, screen->root_visual, 0, nullptr);
    for (uint32_t window : { m_root, m_wmWindow })
        xcb_change_property(m_conn, XCB_PROP_MODE_REPLACE, window, m_atoms[NET_SUPPORTING_WM_CHECK],
                            XCB_ATOM_WINDOW, 32, 1, &m_wmWindow);
    static const char wmName[] = "Wayland 95";
    xcb_change_property(m_conn, XCB_PROP_MODE_REPLACE, m_wmWindow, m_atoms[NET_WM_NAME],
                        m_atoms[UTF8_STRING], 8, sizeof(wmName) - 1, wmName);
    xcb_set_selection_owner(m_conn, m_wmWindow, m_atoms[WM_S0], XCB_CURRENT_TIME);
    // we composite everything, so clients may use translucent (ARGB) windows
    xcb_set_selection_owner(m_conn, m_wmWindow, m_atoms[NET_WM_CM_S0], XCB_CURRENT_TIME);

    const uint32_t supported[] = {
        m_atoms[NET_SUPPORTING_WM_CHECK], m_atoms[NET_WM_NAME], m_atoms[NET_ACTIVE_WINDOW],
        m_atoms[NET_CLIENT_LIST], m_atoms[NET_CLOSE_WINDOW], m_atoms[NET_WM_MOVERESIZE],
        m_atoms[NET_WM_STATE], m_atoms[NET_WM_STATE_MAXIMIZED_VERT], m_atoms[NET_WM_STATE_MAXIMIZED_HORZ],
        m_atoms[NET_WM_STATE_FULLSCREEN], m_atoms[NET_WM_STATE_HIDDEN], m_atoms[NET_WM_STATE_FOCUSED],
        m_atoms[NET_WM_WINDOW_TYPE],
    };
    xcb_change_property(m_conn, XCB_PROP_MODE_REPLACE, m_root, m_atoms[NET_SUPPORTED], XCB_ATOM_ATOM, 32,
                        sizeof(supported) / sizeof(supported[0]), supported);
    updateClientList();
    flush();

    m_xcbNotifier = new QSocketNotifier(xcb_get_file_descriptor(m_conn), QSocketNotifier::Read, this);
    connect(m_xcbNotifier, &QSocketNotifier::activated, this, &XWayland::readEvents);
    readEvents();
    return true;
}

void XWayland::flush()
{
    if (!m_conn)
        return;
    xcb_flush(m_conn);
    // A reply we waited for may have queued events behind it, which the
    // socket notifier won't announce; pick them up on the next loop pass.
    QMetaObject::invokeMethod(this, &XWayland::readEvents, Qt::QueuedConnection);
}

void XWayland::readEvents()
{
    if (!m_conn)
        return;
    while (xcb_generic_event_t *event = xcb_poll_for_event(m_conn)) {
        handleEvent(event);
        free(event);
        if (!m_conn)
            return; // stopped while handling
    }
    if (xcb_connection_has_error(m_conn)) {
        qWarning("XWayland: lost the connection to Xwayland");
        stop();
        return;
    }
    xcb_flush(m_conn);
}

void XWayland::handleEvent(void *rawEvent)
{
    auto *event = static_cast<xcb_generic_event_t *>(rawEvent);
    switch (event->response_type & ~0x80) {
    case 0: {
        // Errors are expected now and then (e.g. focusing a window that was
        // just unmapped) and aren't fatal.
        auto *e = reinterpret_cast<xcb_generic_error_t *>(event);
        if (e->error_code == XCB_WINDOW) // a window the client destroyed meanwhile
            break;
        qDebug("XWayland: X error %d (request %d.%d, resource 0x%x)", e->error_code, e->major_code,
               e->minor_code, e->resource_id);
        break;
    }
    case XCB_CREATE_NOTIFY: {
        auto *e = reinterpret_cast<xcb_create_notify_event_t *>(event);
        if (e->window == m_wmWindow || m_windows.contains(e->window))
            break;
        auto *window = new XWaylandWindow(this, e->window, e->override_redirect,
                                          QRect(e->x, e->y, e->width, e->height));
        m_windows.insert(e->window, window);
        const uint32_t mask = XCB_EVENT_MASK_PROPERTY_CHANGE | XCB_EVENT_MASK_FOCUS_CHANGE;
        xcb_change_window_attributes(m_conn, e->window, XCB_CW_EVENT_MASK, &mask);
        break;
    }
    case XCB_DESTROY_NOTIFY: {
        auto *e = reinterpret_cast<xcb_destroy_notify_event_t *>(event);
        if (XWaylandWindow *window = m_windows.value(e->window))
            forget(window);
        break;
    }
    case XCB_MAP_REQUEST: {
        auto *e = reinterpret_cast<xcb_map_request_event_t *>(event);
        XWaylandWindow *window = m_windows.value(e->window);
        if (!window)
            break;
        readProperties(window);
        setWmState(window, NormalState);
        xcb_map_window(m_conn, e->window);
        break;
    }
    case XCB_MAP_NOTIFY: {
        auto *e = reinterpret_cast<xcb_map_notify_event_t *>(event);
        XWaylandWindow *window = m_windows.value(e->window);
        if (!window)
            break;
        if (window->m_overrideRedirect)
            readProperties(window); // menus and tooltips never send MapRequest
        window->m_xMapped = true;
        if (!window->m_overrideRedirect) {
            setNetWmState(window, false);
            updateClientList();
        }
        tryShow(window);
        break;
    }
    case XCB_UNMAP_NOTIFY: {
        auto *e = reinterpret_cast<xcb_unmap_notify_event_t *>(event);
        XWaylandWindow *window = m_windows.value(e->window);
        if (!window || e->event != m_root)
            break;
        window->m_xMapped = false;
        if (!window->m_overrideRedirect) {
            setWmState(window, WithdrawnState);
            updateClientList();
        }
        if (m_focused == window)
            m_focused = nullptr;
        hide(window);
        break;
    }
    case XCB_CONFIGURE_REQUEST: {
        auto *e = reinterpret_cast<xcb_configure_request_event_t *>(event);
        XWaylandWindow *window = m_windows.value(e->window);
        if (!window)
            break;
        // Before the window is on screen the client may place and size it as
        // it likes. After that we own the position (the frame does), and the
        // size too while it's maximized or full screen.
        QRect g = window->m_geometry;
        const bool placed = window->m_shown;
        const bool locked = window->m_maximized || window->m_fullscreen;
        if (!placed && (e->value_mask & XCB_CONFIG_WINDOW_X))
            g.moveLeft(e->x);
        if (!placed && (e->value_mask & XCB_CONFIG_WINDOW_Y))
            g.moveTop(e->y);
        if (!locked && (e->value_mask & XCB_CONFIG_WINDOW_WIDTH))
            g.setWidth(e->width);
        if (!locked && (e->value_mask & XCB_CONFIG_WINDOW_HEIGHT))
            g.setHeight(e->height);
        const uint32_t values[] = { uint32_t(g.x()), uint32_t(g.y()), uint32_t(qMax(1, g.width())),
                                    uint32_t(qMax(1, g.height())), 0 };
        xcb_configure_window(m_conn, e->window,
                             XCB_CONFIG_WINDOW_X | XCB_CONFIG_WINDOW_Y | XCB_CONFIG_WINDOW_WIDTH
                                 | XCB_CONFIG_WINDOW_HEIGHT | XCB_CONFIG_WINDOW_BORDER_WIDTH,
                             values);
        // ICCCM 4.1.5: if nothing changed the client still needs an answer
        window->m_geometry = g;
        sendConfigureNotify(window);
        break;
    }
    case XCB_CONFIGURE_NOTIFY: {
        auto *e = reinterpret_cast<xcb_configure_notify_event_t *>(event);
        XWaylandWindow *window = m_windows.value(e->window);
        if (!window || e->event != m_root)
            break;
        const QRect g(e->x, e->y, e->width, e->height);
        if (g != window->m_geometry) {
            window->m_geometry = g;
            emit window->geometryChanged();
        }
        break;
    }
    case XCB_PROPERTY_NOTIFY: {
        auto *e = reinterpret_cast<xcb_property_notify_event_t *>(event);
        XWaylandWindow *window = m_windows.value(e->window);
        if (!window)
            break;
        static const Atom watched[] = { WM_NAME, NET_WM_NAME, WM_CLASS, WM_TRANSIENT_FOR, WM_PROTOCOLS,
                                        WM_HINTS, WM_NORMAL_HINTS, MOTIF_WM_HINTS, NET_WM_WINDOW_TYPE };
        for (Atom atom : watched) {
            if (e->atom == m_atoms[atom]) {
                readProperties(window);
                break;
            }
        }
        break;
    }
    case XCB_CLIENT_MESSAGE: {
        auto *e = reinterpret_cast<xcb_client_message_event_t *>(event);
        if (XWaylandWindow *window = m_windows.value(e->window))
            handleClientMessage(window, e->type, e->data.data32);
        break;
    }
    default:
        break;
    }
}

void XWayland::handleClientMessage(XWaylandWindow *window, uint32_t type, const uint32_t *data)
{
    if (type == m_atoms[WL_SURFACE_ID]) {
        associateSurface(window, data[0]);
    } else if (type == m_atoms[NET_WM_STATE]) {
        const uint32_t action = data[0];
        auto wanted = [action](bool current) {
            return action == NetWmStateAdd ? true : action == NetWmStateRemove ? false : !current;
        };
        bool fullscreen = false, maximize = false;
        for (int i = 1; i <= 2; ++i) {
            if (data[i] == m_atoms[NET_WM_STATE_FULLSCREEN])
                fullscreen = true;
            else if (data[i] == m_atoms[NET_WM_STATE_MAXIMIZED_VERT]
                     || data[i] == m_atoms[NET_WM_STATE_MAXIMIZED_HORZ])
                maximize = true;
        }
        if (fullscreen)
            emit window->fullscreenRequested(wanted(window->m_fullscreen));
        if (maximize)
            emit window->maximizeRequested(wanted(window->m_maximized));
    } else if (type == m_atoms[NET_CLOSE_WINDOW]) {
        window->close();
    } else if (type == m_atoms[NET_ACTIVE_WINDOW]) {
        emit window->activateRequested();
    } else if (type == m_atoms[WM_CHANGE_STATE]) {
        if (data[0] == IconicState)
            emit window->minimizeRequested();
    } else if (type == m_atoms[NET_WM_MOVERESIZE]) {
        // Only moving is supported; resizing goes through the frame's grip.
        if (data[2] == NetWmMoveResizeMove)
            emit window->moveRequested();
    }
}

void XWayland::readProperties(XWaylandWindow *window)
{
    const Atom atoms[] = { WM_NAME, NET_WM_NAME, WM_CLASS, WM_TRANSIENT_FOR, WM_PROTOCOLS, WM_HINTS,
                           WM_NORMAL_HINTS, MOTIF_WM_HINTS, NET_WM_WINDOW_TYPE, NET_WM_STATE };
    constexpr int count = sizeof(atoms) / sizeof(atoms[0]);
    xcb_get_property_cookie_t cookies[count];
    for (int i = 0; i < count; ++i)
        cookies[i] = xcb_get_property(m_conn, 0, window->m_id, m_atoms[atoms[i]], XCB_GET_PROPERTY_TYPE_ANY,
                                      0, 2048);
    Property p[count];
    for (int i = 0; i < count; ++i)
        p[i] = readProperty(m_conn, cookies[i]);
    const Property &wmName = p[0], &netWmName = p[1], &wmClass = p[2], &transient = p[3],
                   &protocols = p[4], &hints = p[5], &normalHints = p[6], &motif = p[7],
                   &windowType = p[8], &state = p[9];

    QString title;
    if (!netWmName.data.isEmpty())
        title = QString::fromUtf8(netWmName.data);
    else if (wmName.type == m_atoms[UTF8_STRING])
        title = QString::fromUtf8(wmName.data);
    else
        title = QString::fromLatin1(wmName.data);
    if (title != window->m_title) {
        window->m_title = title;
        emit window->titleChanged();
    }

    // "instance\0Class\0"
    const QList<QByteArray> classParts = wmClass.data.split('\0');
    window->m_appId = classParts.size() > 1 ? QString::fromLocal8Bit(classParts.at(1)) : QString();

    XWaylandWindow *parent = transient.wordCount() >= 1 ? m_windows.value(transient.words()[0]) : nullptr;
    window->m_transientFor = parent != window ? parent : nullptr;

    window->m_deleteWindow = window->m_takeFocus = false;
    for (int i = 0; i < protocols.wordCount(); ++i) {
        if (protocols.words()[i] == m_atoms[WM_DELETE_WINDOW])
            window->m_deleteWindow = true;
        else if (protocols.words()[i] == m_atoms[WM_TAKE_FOCUS])
            window->m_takeFocus = true;
    }

    // WM_HINTS: flags, input, ...
    window->m_acceptsInput = !(hints.wordCount() >= 2 && (hints.words()[0] & InputHint) && !hints.words()[1]);

    // WM_NORMAL_HINTS: flags, x, y, w, h, min_w, min_h, max_w, max_h, ...
    window->m_minSize = window->m_maxSize = QSize();
    if (normalHints.wordCount() >= 9) {
        const uint32_t *h = normalHints.words();
        if (h[0] & PMinSize)
            window->m_minSize = QSize(int(h[5]), int(h[6]));
        if (h[0] & PMaxSize)
            window->m_maxSize = QSize(int(h[7]), int(h[8]));
    }

    // _MOTIF_WM_HINTS: flags, functions, decorations, ...
    window->m_noDecorations =
        motif.wordCount() >= 3 && (motif.words()[0] & MwmHintsDecorations) && motif.words()[2] == 0;

    struct { Atom atom; const char *name; } static const types[] = {
        { NET_WM_WINDOW_TYPE_NORMAL, "normal" }, { NET_WM_WINDOW_TYPE_DIALOG, "dialog" },
        { NET_WM_WINDOW_TYPE_UTILITY, "utility" }, { NET_WM_WINDOW_TYPE_TOOLBAR, "toolbar" },
        { NET_WM_WINDOW_TYPE_SPLASH, "splash" }, { NET_WM_WINDOW_TYPE_MENU, "menu" },
        { NET_WM_WINDOW_TYPE_DROPDOWN_MENU, "menu" }, { NET_WM_WINDOW_TYPE_POPUP_MENU, "menu" },
        { NET_WM_WINDOW_TYPE_COMBO, "menu" }, { NET_WM_WINDOW_TYPE_TOOLTIP, "tooltip" },
        { NET_WM_WINDOW_TYPE_NOTIFICATION, "notification" }, { NET_WM_WINDOW_TYPE_DND, "dnd" },
        { NET_WM_WINDOW_TYPE_DOCK, "dock" }, { NET_WM_WINDOW_TYPE_DESKTOP, "desktop" },
    };
    // ICCCM: a transient window without a type is a dialog
    QString type = window->m_transientFor ? QStringLiteral("dialog") : QStringLiteral("normal");
    bool found = false;
    for (int i = 0; i < windowType.wordCount() && !found; ++i) {
        for (const auto &t : types) {
            if (windowType.words()[i] == m_atoms[t.atom]) {
                type = QString::fromLatin1(t.name);
                found = true;
                break;
            }
        }
    }
    window->m_windowType = type;

    // the client's wishes for when it first appears
    if (!window->m_shown) {
        window->m_wantsMaximized = window->m_wantsFullscreen = false;
        for (int i = 0; i < state.wordCount(); ++i) {
            const uint32_t s = state.words()[i];
            if (s == m_atoms[NET_WM_STATE_FULLSCREEN])
                window->m_wantsFullscreen = true;
            else if (s == m_atoms[NET_WM_STATE_MAXIMIZED_VERT] || s == m_atoms[NET_WM_STATE_MAXIMIZED_HORZ])
                window->m_wantsMaximized = true;
        }
    }

    emit window->propertiesChanged();
}

void XWayland::associateSurface(XWaylandWindow *window, uint32_t surfaceId)
{
    // Xwayland tells us the wl_surface object id over X, but the Wayland
    // request creating that surface may not have been processed yet.
    wl_resource *resource = m_client ? wl_client_get_object(m_client, surfaceId) : nullptr;
    QWaylandSurface *surface = nullptr;
    if (resource && std::strcmp(wl_resource_get_class(resource), "wl_surface") == 0)
        surface = QWaylandSurface::fromResource(resource);
    if (!surface) {
        m_pendingSurfaces.insert(surfaceId, window->m_id);
        return;
    }

    if (window->m_surface == surface)
        return;
    if (window->m_surface)
        hide(window);
    window->m_surface = surface;
    connect(surface, &QWaylandSurface::surfaceDestroyed, window, [this, window] {
        hide(window);
    });
    emit window->surfaceChanged();
    tryShow(window);
}

void XWayland::onSurfaceCreated(QWaylandSurface *surface)
{
    if (!m_client || !surface->client() || surface->client()->client() != m_client)
        return;
    const uint32_t id = wl_resource_get_id(surface->resource());
    const auto it = m_pendingSurfaces.constFind(id);
    if (it == m_pendingSurfaces.constEnd())
        return;
    XWaylandWindow *window = m_windows.value(it.value());
    m_pendingSurfaces.erase(it);
    if (window)
        associateSurface(window, id);
}

void XWayland::tryShow(XWaylandWindow *window)
{
    if (window->m_shown || !window->m_xMapped || !window->m_surface)
        return;
    window->m_shown = true;
    window->m_exposed = true;
    emit windowMapped(window);
}

void XWayland::hide(XWaylandWindow *window)
{
    for (auto it = m_pendingSurfaces.begin(); it != m_pendingSurfaces.end();) {
        if (it.value() == window->m_id)
            it = m_pendingSurfaces.erase(it);
        else
            ++it;
    }
    if (window->m_surface) {
        window->m_surface->disconnect(window);
        window->m_surface = nullptr;
        emit window->surfaceChanged();
    }
    if (window->m_shown) {
        window->m_shown = false;
        window->m_maximized = window->m_fullscreen = false;
        emit window->unmapped();
    }
}

void XWayland::forget(XWaylandWindow *window)
{
    hide(window);
    m_windows.remove(window->m_id);
    if (m_focused == window)
        m_focused = nullptr;
    // QML may still hold the object for a moment (a frame on its way out),
    // so let the garbage collector delete it once nothing refers to it.
    if (!window->m_exposed) {
        delete window;
        return;
    }
    window->setParent(nullptr);
    QQmlEngine::setObjectOwnership(window, QQmlEngine::JavaScriptOwnership);
}

void XWayland::setWmState(XWaylandWindow *window, uint32_t state)
{
    const uint32_t value[] = { state, XCB_NONE };
    xcb_change_property(m_conn, XCB_PROP_MODE_REPLACE, window->m_id, m_atoms[WM_STATE], m_atoms[WM_STATE],
                        32, 2, value);
}

void XWayland::setNetWmState(XWaylandWindow *window, bool minimized)
{
    QList<uint32_t> state;
    if (window->m_maximized)
        state << m_atoms[NET_WM_STATE_MAXIMIZED_VERT] << m_atoms[NET_WM_STATE_MAXIMIZED_HORZ];
    if (window->m_fullscreen)
        state << m_atoms[NET_WM_STATE_FULLSCREEN];
    if (minimized)
        state << m_atoms[NET_WM_STATE_HIDDEN];
    if (m_focused == window)
        state << m_atoms[NET_WM_STATE_FOCUSED];
    xcb_change_property(m_conn, XCB_PROP_MODE_REPLACE, window->m_id, m_atoms[NET_WM_STATE], XCB_ATOM_ATOM,
                        32, uint32_t(state.size()), state.constData());
}

void XWayland::sendProtocolMessage(uint32_t window, uint32_t protocol)
{
    xcb_client_message_event_t event = {};
    event.response_type = XCB_CLIENT_MESSAGE;
    event.format = 32;
    event.window = window;
    event.type = m_atoms[WM_PROTOCOLS];
    event.data.data32[0] = protocol;
    event.data.data32[1] = XCB_CURRENT_TIME;
    xcb_send_event(m_conn, 0, window, XCB_EVENT_MASK_NO_EVENT, reinterpret_cast<const char *>(&event));
}

void XWayland::sendConfigureNotify(XWaylandWindow *window)
{
    const QRect g = window->m_geometry;
    xcb_configure_notify_event_t event = {};
    event.response_type = XCB_CONFIGURE_NOTIFY;
    event.event = window->m_id;
    event.window = window->m_id;
    event.above_sibling = XCB_NONE;
    event.x = int16_t(g.x());
    event.y = int16_t(g.y());
    event.width = uint16_t(qMax(1, g.width()));
    event.height = uint16_t(qMax(1, g.height()));
    xcb_send_event(m_conn, 0, window->m_id, XCB_EVENT_MASK_STRUCTURE_NOTIFY,
                   reinterpret_cast<const char *>(&event));
}

void XWayland::updateClientList()
{
    QList<uint32_t> list;
    for (XWaylandWindow *window : std::as_const(m_windows)) {
        if (!window->m_overrideRedirect && window->m_xMapped)
            list << window->m_id;
    }
    xcb_change_property(m_conn, XCB_PROP_MODE_REPLACE, m_root, m_atoms[NET_CLIENT_LIST], XCB_ATOM_WINDOW, 32,
                        uint32_t(list.size()), list.constData());
}

// ---------------------------------------------------------------------------
// client-requested moves (_NET_WM_MOVERESIZE), for windows that draw their
// own title bar

void XWayland::beginMove(QQuickItem *item)
{
    endMove();
    if (!item || !item->window())
        return;
    m_moveItem = item;
    m_moveWindow = item->window();
    m_moveStarted = false;
    m_moveWindow->installEventFilter(this);
}

void XWayland::endMove()
{
    if (m_moveWindow)
        m_moveWindow->removeEventFilter(this);
    m_moveWindow = nullptr;
    m_moveItem = nullptr;
}

bool XWayland::eventFilter(QObject *watched, QEvent *event)
{
    if (watched != m_moveWindow)
        return false;
    if (event->type() == QEvent::MouseMove) {
        auto *me = static_cast<QMouseEvent *>(event);
        if (!m_moveItem || !(me->buttons() & Qt::LeftButton)) {
            endMove();
            return false;
        }
        if (!m_moveStarted) {
            m_moveStarted = true;
            m_moveStartScene = me->scenePosition();
            m_moveStartPos = m_moveItem->position();
        } else {
            m_moveItem->setPosition(m_moveStartPos + (me->scenePosition() - m_moveStartScene));
        }
        return true; // the client asked us to drag; it doesn't need the motion
    }
    if (event->type() == QEvent::MouseButtonRelease)
        endMove(); // and let the client see the release
    return false;
}
