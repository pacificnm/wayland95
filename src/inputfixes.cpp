#include "inputfixes.h"

#include <QCoreApplication>
#include <QGuiApplication>
#include <QMouseEvent>
#include <QPointingDevice>
#include <QQuickWindow>
#include <QTimer>
#include <QWaylandClient>
#include <QWaylandCompositor>
#include <QWaylandDrag>
#include <QWaylandOutput>
#include <QWaylandQuickItem>
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
        // popups and sub-surfaces of sub-surfaces
        if (qobject_cast<QWaylandQuickItem *>(child))
            watch(child);
    }
}
