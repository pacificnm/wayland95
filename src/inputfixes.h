#pragma once

#include <QObject>
#include <QPointer>
#include <QQuickItem>
#include <QtQmlIntegration/qqmlintegration.h>

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
//  - A sub-surface (part of a window drawn separately: Chromium's page,
//    video, tooltips) that existed before the window was shown is put at
//    0,0 instead of where the program placed it, so what you see and where
//    clicks land disagree. Positions are remembered as programs set them
//    and applied to the items Qt makes (watch()).
//
// Each case is written to the log ("input: ..."), so a problem can be traced.
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

    QPointer<QWaylandCompositor> m_compositor;
};
