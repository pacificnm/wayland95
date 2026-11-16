import QtQuick
import QtWayland.Compositor
import QtWayland.Compositor.XdgShell
import Win95Ui as Ui
import Win95Shell

// The Win95-styled chrome wrapped around one client's xdg_toplevel surface.
// One WindowFrame exists per mapped toplevel; it registers itself with the
// C++ WindowManager singleton so the taskbar knows it exists.
Item {
    id: frame

    required property var toplevel     // XdgToplevel
    required property var xdgSurface   // XdgSurface (the ShellSurface)
    property var inputFixes: null      // InputFixes (Main.qml)
    readonly property string program: toplevel && toplevel.appId ? toplevel.appId : "a program"

    property bool minimized: false
    property bool isActive: false
    property bool maximized: false

    // saved geometry so "restore" after maximize has something to go back to
    property real restoreX: 0
    property real restoreY: 0
    property size restoreSize: Qt.size(0, 0)

    // The program draws its own title bar and buttons (GTK header bars,
    // Firefox's tabs, VS Code): no Win95 title bar or border around it then,
    // just the window as the program draws it. Programs that let the desktop
    // decorate them (xdg-decoration: server side) get the Win95 frame.
    readonly property bool csd: !!toplevel && toplevel.decorationMode !== XdgToplevel.ServerSideDecoration

    // width of the bevel around the client surface
    readonly property int border: csd ? 0 : 3
    readonly property int titleHeight: csd ? 0 : titleBar.height

    // The window itself within the surface: programs that draw their own
    // frame add a drop shadow around it, outside this rectangle (surface
    // coordinates; the same as ours at scale 1).
    readonly property rect geometry: {
        var g = xdgSurface ? xdgSurface.windowGeometry : Qt.rect(0, 0, 0, 0);
        return g.width > 0 && g.height > 0 ? g : Qt.rect(0, 0, surfaceItem.width, surfaceItem.height);
    }

    // The frame always wraps the client's actual buffer size. To change the
    // window size we *ask* the client (xdg_toplevel configure) and the frame
    // follows once it commits a buffer at the new size.
    x: 60 + Math.round(Math.random() * 160)
    y: 50 + Math.round(Math.random() * 120)
    width: geometry.width + 2 * border
    height: titleHeight + geometry.height + 2 * border
    visible: !minimized
    z: 1

    function requestActivate() {
        frame.z = WindowManager.nextStackingZ();
        surfaceItem.forceActiveFocus();
        WindowManager.setActive(frame, true);
    }

    // Sizes we *send* to the client are in surface coordinates, which only
    // match our item coordinates when the output scale equals the window's
    // devicePixelRatio. (Nested in a fractionally-scaled desktop they don't.)
    function toSurfaceSize(w, h) {
        var p = surfaceItem.mapToSurface(Qt.point(w, h));
        return Qt.size(Math.round(p.x), Math.round(p.y));
    }

    // Every configure says the whole state: maximized, resizing, and
    // whether it's the active window. (Qt's sendMaximized and friends leave
    // "activated" out, and Firefox, for one, then ignores all typing.)
    function activeStates(extra) {
        var states = extra ? extra.slice() : [];
        if (frame.maximized)
            states.push(XdgToplevel.MaximizedState);
        if (frame.isActive)
            states.push(XdgToplevel.ActivatedState);
        return states;
    }
    function currentSize() {
        return Qt.size(geometry.width, geometry.height);
    }

    // The size to put in a configure that only changes the state (active or
    // not). Never the size the window has right now: after we've asked for
    // another size (maximize, restore, a resize) the program takes a moment
    // to redraw, and re-sending its old size would undo our request: VS Code
    // starting maximized got "maximized at 1104x674" that way.
    //  - maximized: the maximized size
    //  - a size we asked for that hasn't arrived yet: that size
    //  - otherwise 0x0, which lets the program keep the size it has
    property size requestedSize: Qt.size(0, 0)
    property bool awaitingSize: false
    function maximizedSize() {
        var area = frame.parent;
        return toSurfaceSize(area.width - 2 * border, area.height - titleHeight - 2 * border);
    }
    function stateSize() {
        if (frame.maximized)
            return maximizedSize();
        return awaitingSize ? requestedSize : Qt.size(0, 0);
    }
    // all our configures go through here
    function configure(size, extraStates) {
        requestedSize = size;
        awaitingSize = size.width > 0 && size.height > 0
                       && (size.width !== geometry.width || size.height !== geometry.height);
        if (awaitingSize)
            sizeTimeout.restart();
        toplevel.sendConfigure(size, activeStates(extraStates));
    }
    // the program drew the size we asked for (or never will)
    onGeometryChanged: if (awaitingSize && geometry.width === requestedSize.width
                           && geometry.height === requestedSize.height) awaitingSize = false
    Timer { id: sizeTimeout; interval: 2000; onTriggered: frame.awaitingSize = false }

    // tell it when it becomes (or stops being) the active window
    onIsActiveChanged: if (toplevel) toplevel.sendConfigure(stateSize(), activeStates())

    // the taskbar's window menu: Close asks the program; End Task ends it
    // even if it isn't answering
    function closeWindow() { toplevel.sendClose(); }
    function forceClose() {
        var client = xdgSurface && xdgSurface.surface ? xdgSurface.surface.client : null;
        if (client)
            client.kill(9); // SIGKILL
    }

    function toggleMaximize() {
        if (!frame.maximized) {
            restoreX = frame.x;
            restoreY = frame.y;
            // the window's own size, without any drop shadow around it
            restoreSize = Qt.size(geometry.width, geometry.height);

            // (none yet if it asked to start maximized: then 0x0 below lets
            // it pick its own size when restored)
            if (!(xdgSurface.surface && xdgSurface.surface.hasContent))
                restoreSize = Qt.size(0, 0);

            frame.x = 0;
            frame.y = 0;
            frame.maximized = true;
            Ui.Wayland95Settings.playSound("maximize");
            configure(maximizedSize());
        } else {
            frame.x = restoreX;
            frame.y = restoreY;
            frame.maximized = false;
            Ui.Wayland95Settings.playSound("restore");
            configure(restoreSize);
        }
    }

    Component.onCompleted: {
        WindowManager.addWindow(frame, toplevel.title);
        toplevel.titleChanged.connect(function () {
            WindowManager.setTitle(frame, toplevel.title);
        });
        // Activate once there's something on screen: a program that's given
        // the keyboard before its first frame (Firefox, say) can miss it, and
        // then never sees typing, since later clicks don't give it again.
        if (frame.xdgSurface.surface && frame.xdgSurface.surface.hasContent)
            frame.requestActivate();
        Ui.Wayland95Settings.playSound("open");
    }
    property bool shown: false
    // a moment after the first frame: GTK programs (Firefox) only take the
    // keyboard once they've finished showing the window
    Timer { id: firstActivation; interval: 200; onTriggered: frame.requestActivate() }
    Connections {
        target: frame.xdgSurface ? frame.xdgSurface.surface : null
        function onHasContentChanged() {
            if (!frame.shown && frame.xdgSurface.surface.hasContent) {
                frame.shown = true;
                firstActivation.start();
            }
        }
    }

    Component.onDestruction: {
        WindowManager.removeWindow(frame);
        Ui.Wayland95Settings.playSound("close");
    }

    // minimize / restore from the title bar or the taskbar
    onMinimizedChanged: Ui.Wayland95Settings.playSound(minimized ? "minimize" : "restore")

    // see ShellSurfaceItem.moveItem below
    Item { id: positionSink; visible: false }

    // The client asked to be (un)maximized itself, e.g. a double-click on a
    // client-drawn title bar, or QWindow::showMaximized(). Qt's built-in
    // handler also answers this, with the whole output size (no room for
    // our title bar or the taskbar); callLater makes our configure the last
    // one the client sees, so ours wins.
    Connections {
        target: frame.toplevel
        // its own minimize button
        function onSetMinimized() { WindowManager.setMinimized(frame, true); }
        function onSetMaximized() {
            Qt.callLater(function () { if (!frame.maximized) frame.toggleMaximize(); });
        }
        function onUnsetMaximized() {
            Qt.callLater(function () { if (frame.maximized) frame.toggleMaximize(); });
        }
        // Qt's own handlers run first and start following the mouse; if the
        // button is already up, that would hijack the next drag
        function onStartMove(seat) {
            if (frame.inputFixes) frame.inputFixes.endGrabIfReleased(surfaceItem, "move", frame.program);
        }
        function onStartResize(seat, edges) {
            if (frame.inputFixes) frame.inputFixes.endGrabIfReleased(surfaceItem, "resize", frame.program);
        }
    }

    Bevel {
        id: chrome
        anchors.fill: parent
        visible: !frame.csd
        raised: true
    }

    TitleBar {
        id: titleBar
        visible: !frame.csd
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        text: frame.toplevel ? frame.toplevel.title : ""
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
        onCloseClicked: frame.toplevel.sendClose()
    }

    ShellSurfaceItem {
        id: surfaceItem
        shellSurface: frame.xdgSurface
        // Qt's built-in xdg-shell handling repositions `moveItem` when a
        // window is maximized/restored (and on client move requests),
        // assuming the surface *is* the window. Here the surface sits inside
        // our frame below the title bar, so letting it move the surface would
        // slide it up over the title bar. Point it at a dummy item instead;
        // WindowFrame does all positioning itself.
        // A program with its own title bar moves the window itself (dragging
        // its title bar asks the desktop to move it): then this frame is what
        // moves. Otherwise the Win95 title bar moves the frame, and Qt's own
        // repositioning goes to a dummy item.
        moveItem: frame.csd ? frame : positionSink
        // not anchored: sizeFollowsSurface keeps this item at the client's
        // buffer size, and the frame sizes itself around it
        // the window's top left at the frame's inner corner; any drop shadow
        // hangs outside the frame
        x: frame.border - frame.geometry.x
        y: frame.titleHeight + frame.border - frame.geometry.y
        onSurfaceDestroyed: frame.destroy()
        // sub-surfaces and popups where the program put them
        Component.onCompleted: if (frame.inputFixes) frame.inputFixes.watch(surfaceItem)
    }

    // Simple edge resize handle, bottom-right corner only - enough to make
    // windows usable without building a full per-edge resize system yet.
    Item {
        id: resizeHandle
        z: 1 // stay above the surface, which Qt raise()s on activation
        width: 14
        height: 14
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        visible: !frame.maximized && !frame.csd // CSD programs resize from their own edges

        MouseArea {
            anchors.fill: parent
            cursorShape: Qt.SizeFDiagCursor
            // Track the drag in scene coordinates: the handle itself moves as
            // the window grows, so its local coordinates aren't stable.
            property point startScene
            property size startSize
            property size requested
            function sizeFor(mouse) {
                var p = mapToItem(null, mouse.x, mouse.y);
                return frame.toSurfaceSize(Math.max(120, startSize.width + p.x - startScene.x),
                                           Math.max(60, startSize.height + p.y - startScene.y));
            }
            onPressed: (mouse) => {
                startScene = mapToItem(null, mouse.x, mouse.y);
                startSize = Qt.size(frame.geometry.width, frame.geometry.height);
                requested = frame.toSurfaceSize(startSize.width, startSize.height);
            }
            onPositionChanged: (mouse) => {
                if (!pressed)
                    return;
                var s = sizeFor(mouse);
                if (s.width !== requested.width || s.height !== requested.height) {
                    requested = s;
                    frame.configure(s, [XdgToplevel.ResizingState]);
                }
            }
            onReleased: (mouse) => {
                // final configure without the "resizing" state
                frame.configure(sizeFor(mouse));
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
