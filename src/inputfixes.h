#pragma once

#include <QObject>
#include <QPointer>
#include <QQuickItem>
#include <QtQmlIntegration/qqmlintegration.h>

#include <QWaylandClient>
#include <QWaylandCompositor>
class QWaylandSurface;

// Works around places where Qt's Wayland compositor and other programs'
// windows (Chromium, Electron, GTK) get out of step, so the mouse stops
// doing what you see:
//
//  - A program asks to be moved or resized (dragging its own title bar or
//    tab strip) just after the button went up. Qt then waits for the next
//    release, and meanwhile the next press-and-drag in the window moves it
//    instead of reaching the program. endGrabIfReleased() ends that.
//  - A drag and drop (a link, a tab, some text) that starts just after the
//    button went up: Qt stays in "dragging" and keeps the next release from
//    the program, which then acts as if the button were still down (links
//    don't open). The drag is cancelled instead.
//  - Clicking in a popup (a menu, a drop-down list) gave the popup the
//    keyboard, which Qt reports to the program as its window going
//    inactive; Chromium then closes the popup and drops the click. Popups
//    and sub-surfaces leave the keyboard with their window (watch()).
//  - A popup that closes on the press (clicking a menu's edge, or the page
//    to dismiss a menu): its item vanishes, Qt drops the release, and the
//    program believes the button is still held (Chromium then ignores
//    clicks). The release is sent to the program anyway.
//  - Popups placed by their surface corner instead of their window
//    geometry: menus with shadows (Chromium's) showed up shifted by the
//    shadow. They're placed as the protocol says.
//  - A sub-surface (part of a window drawn separately: Chromium's page,
//    video, tooltips) that existed before the window was shown is put at
//    0,0 instead of where the program placed it, so what you see and where
//    clicks land disagree. Positions are remembered as programs set them
//    and applied to the items Qt makes (watch()).
//
// Each case is written to the log ("input: ..."), so a problem can be traced.
//
// Input trace: while ~/.config/wayland95/trace-input exists (or with
// WAYLAND95_TRACE_INPUT=1), every press and release is logged too: where on
// the screen, which window took it, and the position the program was sent.
class InputFixes : public QObject
{
    Q_OBJECT
    QML_ELEMENT

    Q_PROPERTY(QWaylandCompositor *compositor READ compositor WRITE setCompositor NOTIFY compositorChanged)

public:
    explicit InputFixes(QObject *parent = nullptr);

    QWaylandCompositor *compositor() const { return m_compositor; }
    void setCompositor(QWaylandCompositor *compositor);

    // a move or resize the program just asked for: if no button is down,
    // ends Qt's grab on `item` (the window's ShellSurfaceItem)
    Q_INVOKABLE void endGrabIfReleased(QQuickItem *item, const QString &what, const QString &program);
    // keeps the sub-surface items under `item` (and popups' too) where
    // their programs put them
    Q_INVOKABLE void watch(QQuickItem *item);

signals:
    void compositorChanged();

private:
    void onSurfaceCreated(QWaylandSurface *surface);
    void onDragStarted();
    void fixPositions(QQuickItem *item);
    static void sendRelease(QQuickItem *item);
    bool eventFilter(QObject *watched, QEvent *event) override;
    void traceDelivered(QQuickWindow *window, const QString &what, const QPointF &scenePos);
    bool tracing() const;
    void placePopup(QQuickItem *popupItem);

    // the press in progress: the program's surface it went to
    QPointer<QWaylandSurface> m_pressSurface;
    QPointer<QWaylandClient> m_pressClient;
    Qt::MouseButton m_pressButton = Qt::NoButton;

    QPointer<QWaylandCompositor> m_compositor;
};
