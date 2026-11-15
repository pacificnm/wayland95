#pragma once

#include <QObject>
#include <QQuickWindow>
#include <QRectF>
#include <QWaylandCompositor>
#include <QtQmlIntegration/qqmlintegration.h>

// Takes the pictures for the screen capture tool (Ctrl+Shift+5) and Print
// Screen: the whole screen as the desktop draws it, cropped to a part.
//
// Exposed to QML as the singleton `ScreenGrab`.
class ScreenGrab : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

public:
    explicit ScreenGrab(QObject *parent = nullptr);

    // saves `rect` (in the window's coordinates; empty = all of it) of the
    // screen as a PNG in `folder`, named like "Screenshot 2026-11-15 at
    // 10.42.07.png"; returns the file, or "" if it couldn't
    Q_INVOKABLE QString save(QQuickWindow *window, const QRectF &rect, const QString &folder);
    // puts it on the clipboard, for every program to paste; also kept as a
    // file in the cache (returned) for the thumbnail
    Q_INVOKABLE QString copy(QWaylandCompositor *compositor, QQuickWindow *window, const QRectF &rect);
    // the folder for a choice ("Pictures", "Desktop", "Documents"), made if
    // it isn't there
    Q_INVOKABLE QString folder(const QString &name) const;

private:
    QImage grab(QQuickWindow *window, const QRectF &rect) const;
};
