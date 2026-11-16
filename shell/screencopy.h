#pragma once

// Pictures of the screen for win95shell (wlr-screencopy): Print Screen and
// the screen capture tool (Ctrl+Shift+5), which also needs to know where
// the windows are (asked of the compositor: wayland95-shell-v1). All of it
// answers later, by signal. Exposed to QML as the singleton `ScreenCopy`.

#include <QImage>
#include <QObject>
#include <QRect>
#include <QVariantList>
#include <QtQml/qqmlregistration.h>
#include <QtWaylandClient/QWaylandClientExtensionTemplate>

#include "qwayland-wlr-screencopy-unstable-v1.h"

class ScreencopyManager : public QWaylandClientExtensionTemplate<ScreencopyManager>,
                          public QtWayland::zwlr_screencopy_manager_v1
{
    Q_OBJECT
public:
    ScreencopyManager() : QWaylandClientExtensionTemplate<ScreencopyManager>(3) { initialize(); }
};

class ScreenCopy : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

public:
    explicit ScreenCopy(QObject *parent = nullptr);

    // a picture of `rect` (screen coordinates; empty: all of it), saved as
    // a PNG in `folder` ("Screenshot 2026-11-16 at 10.42.07.png"), or put
    // on the clipboard when `folder` is "Clipboard" (and kept in the cache,
    // for the thumbnail). Answers taken(id, the file or "").
    Q_INVOKABLE int take(const QRect &rect, const QString &folder);
    // the folder for a choice ("Pictures", "Desktop", "Documents"), made if
    // it isn't there; Print Screen's: Pictures, or the home folder
    Q_INVOKABLE QString folder(const QString &name) const;
    Q_INVOKABLE QString printFolder() const;
    // where the windows are, topmost first: windowsListed([{x, y, width, height}...])
    Q_INVOKABLE void listWindows();

Q_SIGNALS:
    void taken(int id, const QString &path);
    void windowsListed(const QVariantList &windows);

private:
    friend class CaptureFrame;
    void finished(int id, const QImage &image, const QString &folder);

    ScreencopyManager *m_manager;
    int m_nextId = 1;
};
