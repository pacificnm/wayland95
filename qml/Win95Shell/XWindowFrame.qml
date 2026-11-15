import QtQuick
import QtWayland.Compositor
import Win95Ui as Ui
import Win95Shell

// The Win95 frame around one X11 program's window (via Xwayland). Behaves
// like WindowFrame, but talks to the X window through our window manager
// (XWaylandWindow) instead of xdg-shell.
//
// X windows have a real position in X's root window, which the client uses
// to place its own menus; syncPosition() keeps it matching where the frame
// shows the window. X coordinates are surface coordinates, so everything
// goes through surfaceItem.mapToSurface() (they differ from item units
// under fractional scaling).
Item {
    id: frame

    required property var xwindow   // XWaylandWindow
    required property var xwayland  // XWayland

    property bool minimized: false
    property bool isActive: false
    property bool maximized: false
    property bool fullscreen: false

    property real restoreX: 0
    property real restoreY: 0
    property size restoreSize: Qt.size(0, 0)
    property bool placed: false

    // no Win95 chrome for splash screens, full screen, or clients that ask
    // for none (they usually draw their own)
    readonly property bool chrome: xwindow.decorated && !fullscreen
    readonly property int border: chrome ? 3 : 0
    readonly property int titleHeight: chrome ? titleBar.height : 0

    x: 60 + Math.round(Math.random() * 160)
    y: 50 + Math.round(Math.random() * 120)
    width: surfaceItem.width + 2 * border
    height: titleHeight + surfaceItem.height + 2 * border
    visible: !minimized && placed
    z: 1

    function requestActivate() {
        frame.z = WindowManager.nextStackingZ();
        surfaceItem.forceActiveFocus();
        WindowManager.setActive(frame, true);
        // windows that aren't in the taskbar never get isActive from it
        frame.xwindow.activate();
    }

    function toSurfaceSize(w, h) {
        var p = surfaceItem.mapToSurface(Qt.point(w, h));
        return Qt.size(Math.round(p.x), Math.round(p.y));
    }

    function syncPosition() {
        if (!frame.xwindow)
            return;
        var scene = surfaceItem.mapToItem(null, 0, 0);
        var p = surfaceItem.mapToSurface(scene);
        frame.xwindow.move(Math.round(p.x), Math.round(p.y));
    }

    function syncStates() {
        if (frame.xwindow)
            frame.xwindow.setStates(frame.minimized, frame.maximized, frame.fullscreen);
    }

    function saveRestore() {
        if (frame.maximized || frame.fullscreen)
            return;
        restoreX = frame.x;
        restoreY = frame.y;
        restoreSize = toSurfaceSize(surfaceItem.width, surfaceItem.height);
    }

    function restore() {
        frame.x = restoreX;
        frame.y = restoreY;
        frame.xwindow.resize(restoreSize.width, restoreSize.height);
    }

    // the taskbar's window menu (see WindowFrame)
    function closeWindow() { xwindow.close(); }
    function forceClose() { xwindow.kill(); }

    function toggleMaximize() {
        var area = frame.parent;
        if (!frame.maximized) {
            saveRestore();
            frame.fullscreen = false;
            frame.maximized = true;
            frame.x = 0;
            frame.y = 0;
            Ui.Wayland95Settings.playSound("maximize");
            var s = toSurfaceSize(area.width - 2 * border, area.height - titleHeight - 2 * border);
            frame.xwindow.resize(s.width, s.height);
        } else {
            frame.maximized = false;
            Ui.Wayland95Settings.playSound("restore");
            restore();
        }
        syncStates();
    }

    // "Full screen" fills the window area (the taskbar stays visible)
    function setFullscreen(on) {
        if (on === frame.fullscreen)
            return;
        if (on) {
            saveRestore();
            frame.maximized = false;
            frame.fullscreen = true;
            frame.x = 0;
            frame.y = 0;
            var s = toSurfaceSize(frame.parent.width, frame.parent.height);
            frame.xwindow.resize(s.width, s.height);
        } else {
            frame.fullscreen = false;
            restore();
        }
        syncStates();
    }

    // Put the window somewhere sensible once its size is known: dialogs
    // over their parent window, everything else where it landed.
    function place() {
        if (frame.placed || surfaceItem.width <= 0 || surfaceItem.height <= 0)
            return;
        var owner = frame.xwindow.transientFor;
        if (owner) {
            var siblings = frame.parent.children;
            for (var i = 0; i < siblings.length; ++i) {
                var other = siblings[i];
                if (other !== frame && other.xwindow === owner) {
                    frame.x = Math.max(0, Math.round(other.x + (other.width - frame.width) / 2));
                    frame.y = Math.max(0, Math.round(other.y + (other.height - frame.height) / 2));
                    break;
                }
            }
        }
        frame.x = Math.max(0, Math.min(frame.x, frame.parent.width - frame.width));
        frame.y = Math.max(0, Math.min(frame.y, frame.parent.height - frame.height));
        frame.placed = true;
        syncPosition();
        if (frame.xwindow.wantsFullscreen)
            setFullscreen(true);
        else if (frame.xwindow.wantsMaximized)
            toggleMaximize();
        requestActivate();
    }

    onXChanged: Qt.callLater(syncPosition)
    onYChanged: Qt.callLater(syncPosition)
    onTitleHeightChanged: Qt.callLater(syncPosition)
    onIsActiveChanged: {
        if (isActive)
            xwindow.activate();
        else
            xwindow.deactivate();
    }
    onMinimizedChanged: {
        Ui.Wayland95Settings.playSound(minimized ? "minimize" : "restore");
        syncStates();
    }

    Component.onCompleted: {
        if (frame.xwindow.windowType !== "splash")
            WindowManager.addWindow(frame, frame.xwindow.title);
        Ui.Wayland95Settings.playSound("open");
        place();
    }

    Component.onDestruction: {
        WindowManager.removeWindow(frame);
        Ui.Wayland95Settings.playSound("close");
    }

    Connections {
        target: frame.xwindow
        function onTitleChanged() { WindowManager.setTitle(frame, frame.xwindow.title); }
        function onMaximizeRequested(on) { if (on !== frame.maximized) frame.toggleMaximize(); }
        function onFullscreenRequested(on) { frame.setFullscreen(on); }
        function onMinimizeRequested() { WindowManager.setMinimized(frame, true); }
        function onActivateRequested() {
            if (frame.minimized)
                WindowManager.setMinimized(frame, false);
            frame.requestActivate();
        }
        function onMoveRequested() {
            if (!frame.maximized && !frame.fullscreen)
                frame.xwayland.beginMove(frame);
        }
        function onUnmapped() { frame.destroy(); }
    }

    Bevel {
        anchors.fill: parent
        raised: true
        visible: frame.chrome
    }

    TitleBar {
        id: titleBar
        visible: frame.chrome
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        text: frame.xwindow ? frame.xwindow.title : ""
        active: frame.isActive
        maximized: frame.maximized
        onDragged: (dx, dy) => {
            if (!frame.maximized) {
                frame.x += dx;
                frame.y += dy;
            }
        }
        onMinimizeClicked: WindowManager.setMinimized(frame, true)
        onMaximizeClicked: frame.toggleMaximize()
        onCloseClicked: frame.xwindow.close()
    }

    WaylandQuickItem {
        id: surfaceItem
        surface: frame.xwindow ? frame.xwindow.surface : null
        x: frame.border
        y: frame.titleHeight + frame.border
        onWidthChanged: Qt.callLater(frame.place)
        onHeightChanged: Qt.callLater(frame.place)
    }

    // bottom-right resize grip, as in WindowFrame
    Item {
        z: 1
        width: 14
        height: 14
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        visible: frame.chrome && !frame.maximized

        MouseArea {
            anchors.fill: parent
            cursorShape: Qt.SizeFDiagCursor
            property point startScene
            property size startSize
            function sizeFor(mouse) {
                var p = mapToItem(null, mouse.x, mouse.y);
                return frame.toSurfaceSize(Math.max(120, startSize.width + p.x - startScene.x),
                                           Math.max(60, startSize.height + p.y - startScene.y));
            }
            onPressed: (mouse) => {
                startScene = mapToItem(null, mouse.x, mouse.y);
                startSize = Qt.size(surfaceItem.width, surfaceItem.height);
            }
            onPositionChanged: (mouse) => {
                if (pressed) {
                    var s = sizeFor(mouse);
                    frame.xwindow.resize(s.width, s.height);
                }
            }
        }
    }

    // A press anywhere in the window (title bar, program, border) raises and
    // activates it. This sits on top of everything, so it sees each press
    // first, then lets it through to whatever is underneath.
    MouseArea {
        anchors.fill: parent
        z: 2 // above the surface, which Qt raise()s when clicked
        acceptedButtons: Qt.AllButtons
        onPressed: (mouse) => { frame.requestActivate(); mouse.accepted = false; }
    }
}
