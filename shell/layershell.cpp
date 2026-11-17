#include "layershell.h"

#include <QGuiApplication>
#include <QScreen>
#include <QPlatformSurfaceEvent>
#include <QtWaylandClient/private/qwaylanddisplay_p.h>
#include <QtWaylandClient/private/qwaylandintegration_p.h>
#include <QtWaylandClient/private/qwaylandscreen_p.h>
#include <QtWaylandClient/private/qwaylandshellintegration_p.h>
#include <QtWaylandClient/private/qwaylandshellsurface_p.h>
#include <QtWaylandClient/private/qwaylandwindow_p.h>

#include "qwayland-wlr-layer-shell-unstable-v1.h"

using namespace QtWaylandClient;

// ------------------------------------------------------------- the surface

class LayerShellIntegration;

class LayerSurface : public QWaylandShellSurface, public QtWayland::zwlr_layer_surface_v1
{
public:
    LayerSurface(LayerShellIntegration *shell, QWaylandWindow *window);
    ~LayerSurface() override
    {
        if (m_attached)
            m_attached->setSurface(nullptr);
        destroy();
    }

    // until the first configure, nothing may be drawn (the protocol says so)
    bool isExposed() const override { return m_configured; }
    void applyConfigure() override { place(m_pendingSize); }
    void setWindowGeometry(const QRect &rect) override
    {
        // the program resized the window (its QML width/height): ask for it,
        // and commit, so the compositor hears even if nothing is redrawn
        // (otherwise its answer to an earlier size can be the last word:
        // a panel that grew and shrank again stayed big)
        // (not the compositor's own answer being applied: an answer to an
        // earlier size, echoed back as a request, was the last word)
        if (!m_attached || m_applying || rect.size() == m_requested)
            return;
        sendSize(rect.size());
        if (m_configured)
            window()->commit();
    }
    void setWindowPosition(const QPoint &) override {} // the anchors decide

    void sendAll();
    void sendSize(const QSize &windowSize);

protected:
    void zwlr_layer_surface_v1_configure(uint32_t serial, uint32_t width, uint32_t height) override
    {
        ack_configure(serial);
        QSize size{int(width), int(height)};
        const QSize own = window()->windowContentGeometry().size();
        if (size.width() == 0)
            size.setWidth(own.width());
        if (size.height() == 0)
            size.setHeight(own.height());
        m_pendingSize = size;
        m_configured = true; // Qt exposes (draws) the window once it's applied
        applyConfigureWhenPossible();
    }
    void zwlr_layer_surface_v1_closed() override
    {
        // the compositor took it away (its output went): Qt hides the window
        window()->window()->close();
    }

private:
    // the window's place on the screen, from its anchors and margins, so Qt
    // maps between the shell's windows correctly
    void place(const QSize &size)
    {
        QRect screen = window()->window()->screen() ? window()->window()->screen()->geometry() : QRect();
        int a = m_attached ? m_attached->anchors() : 0;
        QMargins m = m_attached ? m_attached->margins() : QMargins();
        QPoint p;
        if ((a & LayerShell::AnchorLeft) && !(a & LayerShell::AnchorRight))
            p.setX(screen.left() + m.left());
        else if ((a & LayerShell::AnchorRight) && !(a & LayerShell::AnchorLeft))
            p.setX(screen.right() + 1 - m.right() - size.width());
        else if (a & LayerShell::AnchorLeft) // both: stretched
            p.setX(screen.left() + m.left());
        else
            p.setX(screen.left() + (screen.width() - size.width()) / 2);
        if ((a & LayerShell::AnchorTop) && !(a & LayerShell::AnchorBottom))
            p.setY(screen.top() + m.top());
        else if ((a & LayerShell::AnchorBottom) && !(a & LayerShell::AnchorTop))
            p.setY(screen.bottom() + 1 - m.bottom() - size.height());
        else if (a & LayerShell::AnchorTop)
            p.setY(screen.top() + m.top());
        else
            p.setY(screen.top() + (screen.height() - size.height()) / 2);
        m_applying = true;
        setGeometryFromApplyConfigure(p, size);
        m_applying = false;
    }

    QPointer<LayerShellAttached> m_attached;
    bool m_configured = false;
    bool m_applying = false;
    QSize m_pendingSize;
    QSize m_requested;
};

// ------------------------------------------------------- the integration

class LayerShellIntegration : public QWaylandShellIntegrationTemplate<LayerShellIntegration>,
                              public QtWayland::zwlr_layer_shell_v1
{
public:
    LayerShellIntegration() : QWaylandShellIntegrationTemplate<LayerShellIntegration>(4) {}
    ~LayerShellIntegration() override
    {
        if (object())
            zwlr_layer_shell_v1_destroy(object());
    }

    QWaylandShellSurface *createShellSurface(QWaylandWindow *window) override
    {
        return new LayerSurface(this, window);
    }

    static LayerShellIntegration *instance()
    {
        static LayerShellIntegration *shell = [] {
            auto *s = new LayerShellIntegration;
            auto *integration = QWaylandIntegration::instance();
            if (!integration || !s->initialize(integration->display())) {
                qWarning("LayerShell: the compositor has no zwlr_layer_shell_v1; panels are ordinary windows");
                delete s;
                return static_cast<LayerShellIntegration *>(nullptr);
            }
            return s;
        }();
        return shell;
    }
};

LayerSurface::LayerSurface(LayerShellIntegration *shell, QWaylandWindow *window)
    : QWaylandShellSurface(window)
    , m_attached(LayerShellAttached::get(window->window()))
{
    ::wl_output *output = nullptr;
    if (auto *screen = window->waylandScreen()) // (none: the compositor picks)
        output = screen->output();
    const auto layer = m_attached ? m_attached->layer() : LayerShell::Top;
    const QString scope = m_attached ? m_attached->scope() : QStringLiteral("wayland95");
    init(shell->get_layer_surface(window->wlSurface(), output, uint32_t(layer), scope));
    if (m_attached)
        m_attached->setSurface(this);
    sendAll();
}

void LayerSurface::sendSize(const QSize &windowSize)
{
    int a = m_attached ? m_attached->anchors() : 0;
    // stretched between two edges: the compositor gives that dimension
    int w = (a & LayerShell::AnchorLeft) && (a & LayerShell::AnchorRight) ? 0 : windowSize.width();
    int h = (a & LayerShell::AnchorTop) && (a & LayerShell::AnchorBottom) ? 0 : windowSize.height();
    m_requested = windowSize;
    set_size(uint32_t(qMax(w, 0)), uint32_t(qMax(h, 0)));
}

void LayerSurface::sendAll()
{
    if (!m_attached)
        return;
    set_anchor(uint32_t(m_attached->anchors()));
    set_exclusive_zone(m_attached->exclusiveZone());
    const QMargins m = m_attached->margins();
    set_margin(m.top(), m.right(), m.bottom(), m.left());
    set_keyboard_interactivity(uint32_t(m_attached->keyboard()));
    // a window that takes no clicks (Qt::WindowTransparentForInput: the
    // tooltip layer) gets an empty input region; Qt doesn't send it when the
    // flag was set before its surface existed
    if (window()->window()->flags() & Qt::WindowTransparentForInput) {
        ::wl_region *none = window()->display()->compositor()->create_region();
        wl_surface_set_input_region(window()->wlSurface(), none);
        wl_region_destroy(none);
    }
    if (version() >= 2)
        set_layer(uint32_t(m_attached->layer()));
    sendSize(window()->windowContentGeometry().size());
}

// ------------------------------------------------- the attached properties

LayerShellAttached *LayerShell::qmlAttachedProperties(QObject *object)
{
    auto *window = qobject_cast<QWindow *>(object);
    if (!window) {
        qWarning("LayerShell: only a Window can be a panel");
        return nullptr;
    }
    return new LayerShellAttached(window);
}

LayerShellAttached::LayerShellAttached(QWindow *window) : QObject(window), m_window(window)
{
    // QML hasn't shown the window yet: give it our shell integration now,
    // before its Wayland surface gets a role
    window->setProperty("_w95_layershell", QVariant::fromValue(static_cast<QObject *>(this)));
    if (!QGuiApplication::platformName().startsWith(QLatin1String("wayland")))
        return;
    auto *shell = LayerShellIntegration::instance();
    if (!shell)
        return;
    window->create();
    m_madeOn = window->screen();
    if (auto *wl = dynamic_cast<QWaylandWindow *>(window->handle())) {
        wl->setShellIntegration(shell);
        m_supported = true;
    }
    // moved to another screen (the main display changed), Qt makes the
    // window's Wayland side anew: it's a panel again
    window->installEventFilter(this);
    // Qt doesn't make it anew by itself for another screen (to Qt, Wayland's
    // screens are one desktop), and a panel belongs to one display: so we do
    connect(window, &QWindow::screenChanged, this, [this] {
        QMetaObject::invokeMethod(this, &LayerShellAttached::remake, Qt::QueuedConnection);
    });
}

void LayerShellAttached::remake()
{
    QScreen *target = m_window->screen();
    if (!m_supported || !m_window->handle() || !target)
        return;
    if (!m_surface || m_madeOn == target) {
        // not shown yet (it's made for the screen it has when it's shown),
        // or there already
        m_madeOn = target;
        return;
    }
    const bool shown = m_window->isVisible();
    m_window->destroy();
    // (Qt takes a new window's screen from where it is)
    m_window->setGeometry(QRect(target->geometry().topLeft(), m_window->size()));
    m_window->setScreen(target);
    m_window->create(); // (the event filter makes it a panel)
    m_madeOn = target;
    if (shown)
        m_window->setVisible(true);
}

bool LayerShellAttached::eventFilter(QObject *object, QEvent *event)
{
    if (object == m_window && event->type() == QEvent::PlatformSurface
            && static_cast<QPlatformSurfaceEvent *>(event)->surfaceEventType() == QPlatformSurfaceEvent::SurfaceCreated) {
        if (auto *shell = LayerShellIntegration::instance())
            if (auto *wl = dynamic_cast<QWaylandWindow *>(m_window->handle()))
                wl->setShellIntegration(shell);
    }
    return QObject::eventFilter(object, event);
}

LayerShellAttached *LayerShellAttached::get(QWindow *window)
{
    return qobject_cast<LayerShellAttached *>(window->property("_w95_layershell").value<QObject *>());
}

void LayerShellAttached::update()
{
    if (m_surface) {
        m_surface->sendAll();
        if (auto *wl = dynamic_cast<QWaylandWindow *>(m_window->handle()))
            wl->commit();
    }
    Q_EMIT changed();
}

void LayerShellAttached::setLayer(LayerShell::Layer layer)
{
    if (layer != m_layer) { m_layer = layer; update(); }
}
void LayerShellAttached::setAnchors(int anchors)
{
    if (anchors != m_anchors) { m_anchors = anchors; update(); }
}
void LayerShellAttached::setExclusiveZone(int zone)
{
    if (zone != m_exclusiveZone) { m_exclusiveZone = zone; update(); }
}
void LayerShellAttached::setKeyboard(LayerShell::Keyboard keyboard)
{
    if (keyboard != m_keyboard) { m_keyboard = keyboard; update(); }
}
void LayerShellAttached::setMarginLeft(int m)
{
    if (m != m_margins.left()) { m_margins.setLeft(m); update(); }
}
void LayerShellAttached::setMarginTop(int m)
{
    if (m != m_margins.top()) { m_margins.setTop(m); update(); }
}
void LayerShellAttached::setMarginRight(int m)
{
    if (m != m_margins.right()) { m_margins.setRight(m); update(); }
}
void LayerShellAttached::setMarginBottom(int m)
{
    if (m != m_margins.bottom()) { m_margins.setBottom(m); update(); }
}
void LayerShellAttached::setScope(const QString &scope)
{
    // (fixed once shown: the protocol has no request to change it)
    if (scope != m_scope) { m_scope = scope; Q_EMIT changed(); }
}
