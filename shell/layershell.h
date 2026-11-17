#pragma once

// Our own Qt integration for wlr-layer-shell (zwlr_layer_shell_v1): lets a
// QML Window be a desktop panel (the taskbar, the desktop, pop-up layers)
// instead of an ordinary window. Built on Qt's Wayland client private API
// (qt6-wayland-private-dev), per window, so the shell can still open
// ordinary windows too. No KDE library (layer-shell-qt) is used.
//
//   Window {
//       LayerShell.layer: LayerShell.Top
//       LayerShell.anchors: LayerShell.AnchorBottom | LayerShell.AnchorLeft | LayerShell.AnchorRight
//       LayerShell.exclusiveZone: height          // windows stay clear of it
//       LayerShell.keyboard: LayerShell.KeyboardNone
//       LayerShell.scope: "taskbar"
//       height: 28
//       visible: true
//   }
//
// Anchored to two opposite edges, the window is stretched between them
// (its width or height comes from the compositor); otherwise its own size
// is used. Not anchored on an axis, it's centred on that axis. Qt is told
// the window's place on the screen (worked out from its anchors and
// margins), so mapping items between the shell's windows works; that needs
// QT_WAYLAND_DISABLE_FIXED_POSITIONS=1 (main.cpp sets it), or Qt pins every
// window to the screen's corner.

#include <QMargins>
#include <QObject>
#include <QPointer>
#include <QScreen>
#include <QWindow>
#include <QtQml/qqmlregistration.h>

class LayerShellAttached;

class LayerShell : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("only its attached properties")
    QML_ATTACHED(LayerShellAttached)

public:
    enum Layer { Background = 0, Bottom = 1, Top = 2, Overlay = 3 };
    Q_ENUM(Layer)
    // the protocol's values
    enum Anchor { AnchorNone = 0, AnchorTop = 1, AnchorBottom = 2, AnchorLeft = 4, AnchorRight = 8 };
    Q_ENUM(Anchor)
    enum Keyboard { KeyboardNone = 0, KeyboardExclusive = 1, KeyboardOnDemand = 2 };
    Q_ENUM(Keyboard)

    static LayerShellAttached *qmlAttachedProperties(QObject *object);
};

class LayerSurface;

class LayerShellAttached : public QObject
{
    Q_OBJECT
    Q_PROPERTY(LayerShell::Layer layer READ layer WRITE setLayer NOTIFY changed)
    Q_PROPERTY(int anchors READ anchors WRITE setAnchors NOTIFY changed)
    Q_PROPERTY(int exclusiveZone READ exclusiveZone WRITE setExclusiveZone NOTIFY changed)
    Q_PROPERTY(LayerShell::Keyboard keyboard READ keyboard WRITE setKeyboard NOTIFY changed)
    Q_PROPERTY(int marginLeft READ marginLeft WRITE setMarginLeft NOTIFY changed)
    Q_PROPERTY(int marginTop READ marginTop WRITE setMarginTop NOTIFY changed)
    Q_PROPERTY(int marginRight READ marginRight WRITE setMarginRight NOTIFY changed)
    Q_PROPERTY(int marginBottom READ marginBottom WRITE setMarginBottom NOTIFY changed)
    Q_PROPERTY(QString scope READ scope WRITE setScope NOTIFY changed)
    // false if the compositor has no layer-shell (the window is then shown
    // as an ordinary window)
    Q_PROPERTY(bool supported READ supported CONSTANT)

public:
    bool eventFilter(QObject *object, QEvent *event) override; // (the window made anew: a panel again)
    void remake(); // on another screen: its Wayland side anew, there
    explicit LayerShellAttached(QWindow *window);

    LayerShell::Layer layer() const { return m_layer; }
    void setLayer(LayerShell::Layer layer);
    int anchors() const { return m_anchors; }
    void setAnchors(int anchors);
    int exclusiveZone() const { return m_exclusiveZone; }
    void setExclusiveZone(int zone);
    LayerShell::Keyboard keyboard() const { return m_keyboard; }
    void setKeyboard(LayerShell::Keyboard keyboard);
    int marginLeft() const { return m_margins.left(); }
    int marginTop() const { return m_margins.top(); }
    int marginRight() const { return m_margins.right(); }
    int marginBottom() const { return m_margins.bottom(); }
    void setMarginLeft(int m);
    void setMarginTop(int m);
    void setMarginRight(int m);
    void setMarginBottom(int m);
    QMargins margins() const { return m_margins; }
    QString scope() const { return m_scope; }
    void setScope(const QString &scope);
    bool supported() const { return m_supported; }

    QWindow *window() const { return m_window; }
    // the current shell surface, while the window is shown
    void setSurface(LayerSurface *surface) { m_surface = surface; }

    static LayerShellAttached *get(QWindow *window);

Q_SIGNALS:
    void changed();

private:
    QPointer<QScreen> m_madeOn; // the screen its Wayland side was made for
    void update(); // tell the compositor

    QWindow *m_window;
    LayerSurface *m_surface = nullptr; // (it clears this when it goes)
    LayerShell::Layer m_layer = LayerShell::Top;
    int m_anchors = 0;
    int m_exclusiveZone = 0;
    LayerShell::Keyboard m_keyboard = LayerShell::KeyboardNone;
    QMargins m_margins;
    QString m_scope = QStringLiteral("wayland95");
    bool m_supported = false;
};
