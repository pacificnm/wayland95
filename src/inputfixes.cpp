#include "inputfixes.h"
#include "pointerframes.h"

#include <algorithm>

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QGuiApplication>
#include <QMouseEvent>
#include <QWheelEvent>
#include <qpa/qwindowsysteminterface.h>
#include <QPointingDevice>
#include <QQuickWindow>
#include <QTimer>
#include <QWaylandClient>
#include <QWaylandCompositor>
#include <QWaylandDrag>
#include <QWaylandOutput>
#include <QWaylandQuickItem>
#include <QWaylandQuickShellSurfaceItem>
#include <QWaylandXdgShell>
#include <QWaylandSeat>
#include <QWaylandSurface>
#include <QWaylandView>

namespace {
// where a sub-surface's program put it (surface coordinates), kept on the
// surface itself
const char *PositionProperty = "wayland95SubsurfacePos";

QString describe(QWaylandSurface *surface)
{
    if (!surface || !surface->client())
        return QStringLiteral("a program");
    return QStringLiteral("pid %1").arg(surface->client()->processId());
}
} // namespace

InputFixes::InputFixes(QObject *parent)
    : QObject(parent)
{
    // sees each mouse event before the window delivers it
    qGuiApp->installEventFilter(this);
}

bool InputFixes::tracing() const
{
    static const bool env = qEnvironmentVariableIntValue("WAYLAND95_TRACE_INPUT") == 1;
    static const QString file = QDir::homePath() + QStringLiteral("/.config/wayland95/trace-input");
    return env || QFileInfo::exists(file);
}

// the program's item that's topmost at a scene position: every item is
// searched (a window's frame has items above the program's own, and
// popups and shadows reach outside their parents)
static QWaylandQuickItem *findSurfaceItem(QQuickItem *item, const QPointF &scenePos)
{
    QList<QQuickItem *> children = item->childItems();
    std::stable_sort(children.begin(), children.end(), [](QQuickItem *a, QQuickItem *b) { return a->z() < b->z(); });
    for (auto it = children.crbegin(); it != children.crend(); ++it) {
        QQuickItem *child = *it;
        if (!child->isVisible() || child->opacity() <= 0)
            continue;
        if (QWaylandQuickItem *found = findSurfaceItem(child, scenePos))
            return found;
        auto *q = qobject_cast<QWaylandQuickItem *>(child);
        if (q && q->surface() && q->contains(q->mapFromScene(scenePos)))
            return q;
    }
    return nullptr;
}

static QWaylandQuickItem *surfaceItemAt(QQuickWindow *window, const QPointF &scenePos)
{
    return window && window->contentItem() ? findSurfaceItem(window->contentItem(), scenePos) : nullptr;
}

void InputFixes::scheduleFrames()
{
    // the program the pointer is over now (and, after delivery, the one it's
    // over then): both may have been sent events
    auto noteFocus = [this] {
        if (m_compositor && m_compositor->defaultSeat() && m_compositor->defaultSeat()->mouseFocus()
            && m_compositor->defaultSeat()->mouseFocus()->surface())
        {
            QWaylandClient *c = m_compositor->defaultSeat()->mouseFocus()->surface()->client();
            if (c && !m_frameClients.contains(c))
                m_frameClients.append(c);
        }
    };
    noteFocus();
    if (m_framesPending)
        return;
    m_framesPending = true;
    QTimer::singleShot(0, this, [this, noteFocus] {
        m_framesPending = false;
        noteFocus();
        const auto clients = m_frameClients;
        m_frameClients.clear();
        for (const QPointer<QWaylandClient> &client : clients)
            if (client)
                sendPointerFrames(client);
    });
}

bool InputFixes::eventFilter(QObject *watched, QEvent *event)
{
    switch (event->type()) {
    case QEvent::MouseMove: case QEvent::MouseButtonPress: case QEvent::MouseButtonRelease:
    case QEvent::MouseButtonDblClick: case QEvent::Wheel: case QEvent::Leave:
        if (qobject_cast<QQuickWindow *>(watched))
            scheduleFrames();
        break;
    default:
        break;
    }
    if (event->type() == QEvent::Wheel) {
        auto *window = qobject_cast<QQuickWindow *>(watched);
        if (window && tracing()) {
            auto *wheel = static_cast<QWheelEvent *>(event);
            QWaylandQuickItem *under = surfaceItemAt(window, wheel->position());
            qInfo().noquote() << QStringLiteral("input: wheel %1,%2 at %3,%4 (under it: %5)")
                                 .arg(wheel->angleDelta().x()).arg(wheel->angleDelta().y())
                                 .arg(wheel->position().x()).arg(wheel->position().y())
                                 .arg(under && under->surface() ? describe(under->surface()) : QStringLiteral("the desktop"));
        }
        return false;
    }
    if (event->type() != QEvent::MouseButtonPress && event->type() != QEvent::MouseButtonRelease)
        return false;
    auto *window = qobject_cast<QQuickWindow *>(watched);
    if (!window)
        return false;
    auto *mouse = static_cast<QMouseEvent *>(event);

    // Testing only (WAYLAND95_TEST_WHEEL_BUTTONS=1): Qt's VNC backend drops
    // the wheel, so a middle click stands for one notch down (with Shift:
    // up), entering Qt the way a real mouse wheel's events do
    static const bool wheelButtons = qEnvironmentVariableIntValue("WAYLAND95_TEST_WHEEL_BUTTONS") == 1;
    if (wheelButtons && mouse->button() == Qt::MiddleButton) {
        if (event->type() == QEvent::MouseButtonPress) {
            const int dy = (mouse->modifiers() & Qt::ShiftModifier) ? 120 : -120;
            QWindowSystemInterface::handleWheelEvent(window, mouse->position(), mouse->globalPosition(),
                                                     QPoint(), QPoint(0, dy));
        }
        return true;
    }

    if (event->type() == QEvent::MouseButtonPress) {
        QWaylandQuickItem *target = surfaceItemAt(window, mouse->scenePosition());
        m_pressSurface = target ? target->surface() : nullptr;
        m_pressClient = m_pressSurface ? m_pressSurface->client() : nullptr;
        m_pressButton = mouse->button();
    } else if (mouse->button() == m_pressButton) {
        // The surface the press went to is gone (a popup that closed on the
        // press): Qt has nowhere to deliver this release and drops it. The
        // program still has the button down, so tell it the button is up.
        // (Qt keeps a destroyed surface's object around for a while.)
        const bool gone = !m_pressSurface || m_pressSurface->isDestroyed() || !m_pressSurface->hasContent();
        if (m_pressClient && gone && m_compositor && m_compositor->defaultSeat()) {
            QTimer::singleShot(0, this, [this, window = QPointer<QQuickWindow>(window), pos = mouse->scenePosition(),
                                         client = m_pressClient, button = m_pressButton] {
                QWaylandSeat *seat = m_compositor ? m_compositor->defaultSeat() : nullptr;
                if (!seat || !client || !window)
                    return;
                // the pointer on the program's window under it, if that's
                // where it is, so the release goes to a surface it knows
                QWaylandQuickItem *under = surfaceItemAt(window, pos);
                if (under && under->surface() && under->surface()->client() == client)
                    seat->sendMouseMoveEvent(under->view(), under->mapToSurface(under->mapFromScene(pos)), pos);
                QWaylandView *focus = seat->mouseFocus();
                if (!focus || !focus->surface() || focus->surface()->client() != client)
                    return;
                qInfo().noquote() << "input: the popup that was clicked closed before the release; sent the release to pid"
                                  << client->processId();
                seat->sendMouseReleaseEvent(button);
                sendPointerFrames(client);
            });
        }
        m_pressSurface = nullptr;
        m_pressClient = nullptr;
        m_pressButton = Qt::NoButton;
    }

    if (!tracing())
        return false;
    const QString what = QStringLiteral("%1 %2")
            .arg(event->type() == QEvent::MouseButtonPress ? QStringLiteral("press") : QStringLiteral("release"))
            .arg(mouse->button() == Qt::LeftButton ? QStringLiteral("left")
                 : mouse->button() == Qt::RightButton ? QStringLiteral("right") : QStringLiteral("other"));
    // who took it is known once the window has delivered it
    QTimer::singleShot(0, this, [this, window = QPointer<QQuickWindow>(window), what, pos = mouse->scenePosition()] {
        if (window)
            traceDelivered(window, what, pos);
    });
    return false;
}

void InputFixes::traceDelivered(QQuickWindow *window, const QString &what, const QPointF &scenePos)
{
    QString target = QStringLiteral("the desktop");
    QQuickItem *grabber = window->mouseGrabberItem();
    // the program's item under the pointer, whether or not it grabbed
    QWaylandQuickItem *surfaceItem = qobject_cast<QWaylandQuickItem *>(grabber);
    if (!surfaceItem)
        surfaceItem = surfaceItemAt(window, scenePos);
    if (grabber)
        target = QString::fromLatin1(grabber->metaObject()->className());
    QString sent;
    if (surfaceItem && surfaceItem->surface()) {
        const QPointF local = surfaceItem->mapFromScene(scenePos);
        const QPointF surf = surfaceItem->mapToSurface(local);
        sent = QStringLiteral(" | under it: %1, item at %2,%3 size %4x%5, surface %6x%7, point in surface %8,%9")
                .arg(describe(surfaceItem->surface()))
                .arg(surfaceItem->mapToScene(QPointF()).x()).arg(surfaceItem->mapToScene(QPointF()).y())
                .arg(surfaceItem->width()).arg(surfaceItem->height())
                .arg(surfaceItem->surface()->destinationSize().width()).arg(surfaceItem->surface()->destinationSize().height())
                .arg(surf.x(), 0, 'f', 1).arg(surf.y(), 0, 'f', 1);
    }
    QString focus = QStringLiteral("none");
    if (m_compositor && m_compositor->defaultSeat() && m_compositor->defaultSeat()->mouseFocus()
        && m_compositor->defaultSeat()->mouseFocus()->surface())
        focus = describe(m_compositor->defaultSeat()->mouseFocus()->surface());
    qInfo().noquote() << QStringLiteral("input: %1 at %2,%3 -> %4 (pointer focus: %5)%6")
                         .arg(what).arg(scenePos.x()).arg(scenePos.y()).arg(target, focus, sent);
}

void InputFixes::setCompositor(QWaylandCompositor *compositor)
{
    if (m_compositor == compositor)
        return;
    if (m_compositor)
        disconnect(m_compositor, nullptr, this, nullptr);
    m_compositor = compositor;
    if (m_compositor) {
        connect(m_compositor, &QWaylandCompositor::surfaceCreated, this, &InputFixes::onSurfaceCreated);
        auto hookSeat = [this] {
            if (QWaylandSeat *seat = m_compositor->defaultSeat())
                if (QWaylandDrag *drag = seat->drag())
                    connect(drag, &QWaylandDrag::dragStarted, this, &InputFixes::onDragStarted, Qt::UniqueConnection);
        };
        // the default seat exists once the compositor is created
        if (m_compositor->isCreated())
            hookSeat();
        else
            connect(m_compositor, &QWaylandCompositor::createdChanged, this, hookSeat);
    }
    emit compositorChanged();
}

void InputFixes::onSurfaceCreated(QWaylandSurface *surface)
{
    // Qt only tells items about positions set after they exist
    connect(surface, &QWaylandSurface::subsurfacePositionChanged, surface, [surface](const QPoint &pos) {
        surface->setProperty(PositionProperty, pos);
    });
}

void InputFixes::sendRelease(QQuickItem *item)
{
    // as if the button had just gone up over the item: Qt's move/resize
    // filter and its drag handling both finish on a release
    const QPointF local(0, 0);
    QMouseEvent release(QEvent::MouseButtonRelease, local, item->mapToScene(local),
                        item->mapToGlobal(local), Qt::LeftButton, Qt::NoButton, Qt::NoModifier,
                        QPointingDevice::primaryPointingDevice());
    QCoreApplication::sendEvent(item, &release);
}

void InputFixes::endGrabIfReleased(QQuickItem *item, const QString &what, const QString &program)
{
    if (!item)
        return;
    if (QGuiApplication::mouseButtons() != Qt::NoButton) {
        qInfo().noquote() << "input:" << program << "started a window" << what;
        return;
    }
    qInfo().noquote() << "input:" << program << "asked for a window" << what
                      << "after the button was released; ignored";
    sendRelease(item);
}

void InputFixes::onDragStarted()
{
    QWaylandDrag *drag = qobject_cast<QWaylandDrag *>(sender());
    // the origin's item learns of the drag right after this signal
    QTimer::singleShot(0, this, [drag = QPointer<QWaylandDrag>(drag)] {
        if (!drag)
            return;
        QWaylandSurface *origin = drag->origin();
        if (QGuiApplication::mouseButtons() != Qt::NoButton) {
            qInfo().noquote() << "input: drag and drop started by" << describe(origin);
            return;
        }
        qInfo().noquote() << "input: drag and drop started by" << describe(origin)
                          << "after the button was released; cancelled";
        if (!origin)
            return;
        // ends Qt's "dragging" on the window it started from: the drop
        // finds no target and the program is told the drag was cancelled
        for (QWaylandView *view : origin->views())
            if (auto *item = qobject_cast<QQuickItem *>(view->renderObject()))
                sendRelease(item);
    });
}

void InputFixes::watch(QQuickItem *item)
{
    if (!item || item->property("wayland95Watched").toBool())
        return;
    item->setProperty("wayland95Watched", true);
    connect(item, &QQuickItem::childrenChanged, this, [this, item = QPointer<QQuickItem>(item)] {
        // after Qt has finished setting up the new child
        QTimer::singleShot(0, this, [this, item] {
            if (item)
                fixPositions(item);
        });
    });
    fixPositions(item);
}

void InputFixes::fixPositions(QQuickItem *item)
{
    const auto children = item->childItems();
    for (QQuickItem *child : children) {
        auto *quickItem = qobject_cast<QWaylandQuickItem *>(child);
        // A popup (menu, list, address bar suggestions) or sub-surface
        // mustn't take the keyboard when clicked: Qt would then tell the
        // window it isn't active any more, and Chromium closes its popups
        // on that, dropping the click (the release never arrives). Real
        // desktops keep the keyboard on the window.
        if (quickItem && quickItem->focusOnClick()) {
            quickItem->setFocusOnClick(false);
            qInfo().noquote() << "input: a popup or sub-surface of" << describe(quickItem->surface())
                              << "will leave the keyboard with its window";
        }
        if (quickItem && quickItem->surface()) {
            const QVariant pos = quickItem->surface()->property(PositionProperty);
            // only sub-surfaces have one (set_position); popups don't
            if (pos.isValid()) {
                qreal scale = 1;
                if (QWaylandOutput *output = quickItem->view()->output())
                    scale = output->scaleFactor();
                if (quickItem->window())
                    scale /= quickItem->window()->devicePixelRatio();
                const QPointF want = QPointF(pos.toPoint()) * scale;
                if (quickItem->position() != want) {
                    qInfo().noquote() << "input: moved a sub-surface of" << describe(quickItem->surface())
                                      << "from" << quickItem->position() << "to" << want;
                    quickItem->setPosition(want);
                }
            }
        }
        // popups: placed by their window geometry
        if (auto *shellItem = qobject_cast<QWaylandQuickShellSurfaceItem *>(child)) {
            auto *xdg = qobject_cast<QWaylandXdgSurface *>(shellItem->shellSurface());
            if (xdg && xdg->popup() && !shellItem->property("wayland95Placed").toBool()) {
                shellItem->setProperty("wayland95Placed", true);
                QPointer<QQuickItem> guard(shellItem);
                auto later = [this, guard] {
                    // after Qt's own placement, which this corrects
                    QTimer::singleShot(0, this, [this, guard] { if (guard) placePopup(guard); });
                };
                connect(xdg->popup(), &QWaylandXdgPopup::configuredGeometryChanged, this, later);
                connect(xdg, &QWaylandXdgSurface::windowGeometryChanged, this, later);
                if (QWaylandXdgSurface *parentXdg = xdg->popup()->parentXdgSurface())
                    connect(parentXdg, &QWaylandXdgSurface::windowGeometryChanged, this, later);
                placePopup(shellItem);
            }
        }
        // popups and sub-surfaces of sub-surfaces
        if (qobject_cast<QWaylandQuickItem *>(child))
            watch(child);
    }
}

void InputFixes::placePopup(QQuickItem *popupItem)
{
    auto *shellItem = qobject_cast<QWaylandQuickShellSurfaceItem *>(popupItem);
    auto *xdg = shellItem ? qobject_cast<QWaylandXdgSurface *>(shellItem->shellSurface()) : nullptr;
    QWaylandXdgPopup *popup = xdg ? xdg->popup() : nullptr;
    if (!popup || !popup->parentXdgSurface())
        return;
    // xdg-shell: the configured position is where the popup's window
    // geometry goes, relative to the parent's window geometry. Qt leaves out
    // the popup's own geometry offset (its shadow).
    const QPoint surfacePos = popup->configuredGeometry().topLeft()
            + popup->parentXdgSurface()->windowGeometry().topLeft()
            - xdg->windowGeometry().topLeft();
    qreal scale = 1;
    if (shellItem->view() && shellItem->view()->output())
        scale = shellItem->view()->output()->scaleFactor();
    if (shellItem->window())
        scale /= shellItem->window()->devicePixelRatio();
    QQuickItem *target = shellItem->moveItem() ? shellItem->moveItem() : shellItem;
    const QPointF want = QPointF(surfacePos) * scale;
    if (target->position() != want) {
        if (tracing())
            qInfo().noquote() << "input: placed a popup of" << describe(xdg->surface()) << "at" << want
                              << "(Qt had" << target->position() << ")";
        target->setPosition(want);
    }
}
