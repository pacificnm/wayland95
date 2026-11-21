#pragma once

#include <QImage>
#include <QObject>
#include <QQuickWindow>

// For the tests (TestDriver.qml): a window as it is drawn on the screen,
// web pages and all (grabToImage leaves out what Chromium draws over)
class WindowGrabber : public QObject
{
    Q_OBJECT
public:
    using QObject::QObject;
    Q_INVOKABLE bool grab(QObject *window, const QString &file) const
    {
        auto *w = qobject_cast<QQuickWindow *>(window);
        return w && w->grabWindow().save(file);
    }
};
