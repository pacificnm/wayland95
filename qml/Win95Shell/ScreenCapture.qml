import QtQuick
import Win95Ui as Ui
import Win95Shell

// The screen capture tool (Ctrl+Shift+5), like the Mac's: a toolbar along
// the bottom to capture the entire screen, a window, or a selected part,
// with Options (where to save, a timer, the thumbnail). Afterwards a
// thumbnail sits in the corner for a few seconds; clicking it opens the
// picture. Esc cancels.
Item {
    id: tool

    // what to capture from
    property QtObject targetWindow       // the QQuickWindow
    property QtObject waylandCompositor    // for the clipboard
    property Item windows       // the windows, for "Window"

    anchors.fill: parent
    visible: active || countdown > 0 || capturing || thumb.showing

    property bool active: false
    property string mode: "selection"       // "screen", "window", "selection"
    property string destination: "Pictures" // or "Desktop", "Documents", "Clipboard"
    property int timer: 0                   // seconds
    property bool thumbnail: true
    property rect selection: Qt.rect(0, 0, 0, 0)
    property rect lastSelection: Qt.rect(0, 0, 0, 0)
    property Item hovered: null             // Window mode: the window under the pointer
    property int countdown: 0
    property bool capturing: false

    function open() {
        if (active)
            return;
        active = true;
        hovered = null;
        // the Mac remembers the last selection
        selection = lastSelection.width > 4 ? lastSelection
                  : Qt.rect(width / 4, height / 4, width / 2, height / 2);
        forceActiveFocus();
    }
    function close() {
        active = false;
        countdown = 0;
        countdownTimer.stop();
    }

    // the rectangle to capture, in screen coordinates; null if nothing yet
    function target() {
        if (mode === "screen")
            return Qt.rect(0, 0, width, height);
        if (mode === "window") {
            if (!hovered)
                return null;
            var p = hovered.mapToItem(tool, 0, 0);
            return Qt.rect(p.x, p.y, hovered.width, hovered.height);
        }
        return selection.width > 2 && selection.height > 2 ? selection : null;
    }

    function capture() {
        var r = target();
        if (!r)
            return;
        if (mode === "selection")
            lastSelection = selection;
        pending = r;
        active = false;
        if (timer > 0) {
            countdown = timer;
            countdownTimer.start();
        } else {
            shoot();
        }
    }
    property rect pending
    // hide everything of ours first, then take the picture next frame
    function shoot() {
        capturing = true;
        shootTimer.start();
    }
    Timer {
        id: shootTimer
        interval: 80
        onTriggered: {
            var r = tool.pending;
            var path = tool.destination === "Clipboard"
                    ? ScreenGrab.copy(tool.waylandCompositor, tool.targetWindow, r)
                    : ScreenGrab.save(tool.targetWindow, r, ScreenGrab.folder(tool.destination));
            tool.capturing = false;
            if (!path) {
                Ui.Wayland95Settings.playSound("critical");
                Notifications.notify("Screen Capture", "The screenshot couldn't be saved", "", "", 1, 5000);
                return;
            }
            Ui.Wayland95Settings.playFile("/usr/share/sounds/freedesktop/stereo/camera-shutter.oga");
            if (tool.thumbnail)
                thumb.show(path, tool.destination === "Clipboard");
        }
    }
    Timer {
        id: countdownTimer
        interval: 1000
        repeat: true
        onTriggered: {
            if (--tool.countdown <= 0) {
                stop();
                tool.shoot();
            }
        }
    }

    Keys.onEscapePressed: tool.close()
    Keys.onReturnPressed: tool.capture()
    Keys.onEnterPressed: tool.capture()

    // ---- the dimmed screen with the selection cut out ----------------------
    Item {
        anchors.fill: parent
        visible: tool.active && !tool.capturing && tool.mode !== "screen"
        readonly property var r: tool.mode === "window" && tool.hovered ? tool.target()
                               : tool.mode === "selection" ? tool.selection : null
        // four dark bands around the clear part (all dark if there's none)
        Rectangle { color: "#80000000"; x: 0; y: 0; width: parent.width; height: parent.r ? parent.r.y : parent.height }
        Rectangle { visible: !!parent.r; color: "#80000000"; x: 0; y: parent.r ? parent.r.y : 0; width: parent.r ? parent.r.x : 0; height: parent.r ? parent.r.height : 0 }
        Rectangle { visible: !!parent.r; color: "#80000000"; x: parent.r ? parent.r.x + parent.r.width : 0; y: parent.r ? parent.r.y : 0
                    width: parent.r ? parent.width - x : 0; height: parent.r ? parent.r.height : 0 }
        Rectangle { visible: !!parent.r; color: "#80000000"; x: 0; y: parent.r ? parent.r.y + parent.r.height : 0
                    width: parent.width; height: parent.r ? parent.height - y : 0 }
        // the outline
        Rectangle {
            visible: !!parent.r
            x: parent.r ? parent.r.x - 1 : 0
            y: parent.r ? parent.r.y - 1 : 0
            width: parent.r ? parent.r.width + 2 : 0
            height: parent.r ? parent.r.height + 2 : 0
            color: tool.mode === "window" ? "#300000ff" : "transparent"
            border.color: "#ffffff"
            border.width: 1
        }
    }

    // ---- Selection: drag out, move and resize the rectangle ----------------
    MouseArea {
        id: selector
        anchors.fill: parent
        enabled: tool.active && tool.mode === "selection"
        cursorShape: Qt.CrossCursor
        property point start
        property rect startRect
        property string dragging: "" // "new", "move", or the handle ("tl", "br"...)
        function handleAt(x, y) {
            var r = tool.selection, h = 8;
            var left = Math.abs(x - r.x) < h, right = Math.abs(x - (r.x + r.width)) < h;
            var top = Math.abs(y - r.y) < h, bottom = Math.abs(y - (r.y + r.height)) < h;
            var inX = x > r.x - h && x < r.x + r.width + h, inY = y > r.y - h && y < r.y + r.height + h;
            if (!inX || !inY) return "";
            var v = top ? "t" : bottom ? "b" : "", s = left ? "l" : right ? "r" : "";
            if (v || s) return v + s;
            return "move";
        }
        onPressed: (mouse) => {
            start = Qt.point(mouse.x, mouse.y);
            startRect = tool.selection;
            dragging = handleAt(mouse.x, mouse.y) || "new";
            if (dragging === "new")
                tool.selection = Qt.rect(mouse.x, mouse.y, 0, 0);
        }
        onPositionChanged: (mouse) => {
            if (!pressed)
                return;
            var dx = mouse.x - start.x, dy = mouse.y - start.y, r = startRect;
            var x1 = r.x, y1 = r.y, x2 = r.x + r.width, y2 = r.y + r.height;
            if (dragging === "new") {
                x1 = start.x; y1 = start.y; x2 = mouse.x; y2 = mouse.y;
            } else if (dragging === "move") {
                var nx = Math.max(0, Math.min(tool.width - r.width, r.x + dx));
                var ny = Math.max(0, Math.min(tool.height - r.height, r.y + dy));
                tool.selection = Qt.rect(nx, ny, r.width, r.height);
                return;
            } else {
                if (dragging.indexOf("l") >= 0) x1 += dx;
                if (dragging.indexOf("r") >= 0) x2 += dx;
                if (dragging.indexOf("t") >= 0) y1 += dy;
                if (dragging.indexOf("b") >= 0) y2 += dy;
            }
            tool.selection = Qt.rect(Math.max(0, Math.min(x1, x2)), Math.max(0, Math.min(y1, y2)),
                                     Math.min(tool.width, Math.abs(x2 - x1)), Math.min(tool.height, Math.abs(y2 - y1)));
        }
        onDoubleClicked: tool.capture()
    }
    // the corner and edge handles, and the size
    Repeater {
        model: tool.active && tool.mode === "selection" && tool.selection.width > 0
               ? [[0, 0], [0.5, 0], [1, 0], [0, 0.5], [1, 0.5], [0, 1], [0.5, 1], [1, 1]] : []
        Rectangle {
            required property var modelData
            x: tool.selection.x + tool.selection.width * modelData[0] - 4
            y: tool.selection.y + tool.selection.height * modelData[1] - 4
            width: 8
            height: 8
            color: "#ffffff"
            border.color: "#000000"
        }
    }
    Rectangle {
        visible: tool.active && tool.mode === "selection" && tool.selection.width > 0
        x: Math.min(tool.width - width - 4, tool.selection.x + tool.selection.width + 6)
        y: Math.min(tool.height - height - 4, tool.selection.y + tool.selection.height + 6)
        width: sizeText.implicitWidth + 10
        height: sizeText.implicitHeight + 4
        color: "#ffffe1"
        border.color: "#000000"
        Text {
            id: sizeText
            anchors.centerIn: parent
            text: Math.round(tool.selection.width) + " × " + Math.round(tool.selection.height)
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontSize
        }
    }

    // ---- Window: point at one, click it ------------------------------------
    MouseArea {
        anchors.fill: parent
        enabled: tool.active && tool.mode === "window"
        hoverEnabled: true
        cursorShape: Qt.PointingHandCursor
        function windowAt(x, y) {
            var best = null;
            if (!tool.windows)
                return null;
            for (var i = 0; i < tool.windows.children.length; ++i) {
                var w = tool.windows.children[i];
                if (!w.visible || w.width <= 0)
                    continue;
                var p = w.mapToItem(tool, 0, 0);
                if (x >= p.x && x < p.x + w.width && y >= p.y && y < p.y + w.height && (!best || w.z > best.z))
                    best = w;
            }
            return best;
        }
        onPositionChanged: (mouse) => tool.hovered = windowAt(mouse.x, mouse.y)
        onClicked: (mouse) => {
            tool.hovered = windowAt(mouse.x, mouse.y);
            tool.capture();
        }
    }

    // ---- Entire Screen: a click anywhere -----------------------------------
    MouseArea {
        anchors.fill: parent
        enabled: tool.active && tool.mode === "screen"
        cursorShape: Qt.PointingHandCursor
        onClicked: tool.capture()
        Rectangle { anchors.fill: parent; color: "#20ffffff"; visible: parent.enabled }
    }

    // ---- the countdown -----------------------------------------------------
    Text {
        anchors.centerIn: parent
        visible: tool.countdown > 0 && !tool.capturing
        text: tool.countdown
        color: "#ffffff"
        style: Text.Outline
        styleColor: "#000000"
        font.family: Theme.fontFamily
        font.pixelSize: 96
        font.bold: true
    }

    // ---- the toolbar -------------------------------------------------------
    Item {
        id: toolbar
        visible: tool.active && !tool.capturing
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottom: parent.bottom
        anchors.bottomMargin: 60
        width: row.width + 12
        height: row.height + 12

        Bevel { anchors.fill: parent; raised: true }
        // the toolbar's own clicks stay on it
        MouseArea { anchors.fill: parent }

        Row {
            id: row
            x: 6
            y: 6
            spacing: 4

            component ModeButton: Item {
                id: mb
                property string modeName
                property string label
                width: 74
                height: 52
                readonly property bool on: tool.mode === modeName
                Bevel { anchors.fill: parent; raised: !mb.on; face: mb.on ? Theme.light : Theme.face }
                Canvas {
                    id: art
                    anchors.horizontalCenter: parent.horizontalCenter
                    y: 6
                    width: 26
                    height: 20
                    onPaint: {
                        var ctx = getContext("2d");
                        ctx.reset();
                        ctx.strokeStyle = "#000000";
                        ctx.fillStyle = "#ffffff";
                        if (mb.modeName === "screen") {
                            ctx.fillRect(1, 1, 24, 16); ctx.strokeRect(1.5, 1.5, 23, 15);
                            ctx.fillStyle = "#008080"; ctx.fillRect(3, 3, 20, 12);
                        } else if (mb.modeName === "window") {
                            ctx.fillRect(3, 3, 20, 15); ctx.strokeRect(3.5, 3.5, 19, 14);
                            ctx.fillStyle = "#000080"; ctx.fillRect(4, 4, 18, 3);
                        } else {
                            ctx.setLineDash([2, 2]);
                            ctx.strokeRect(2.5, 2.5, 21, 14);
                            ctx.setLineDash([]);
                            ctx.fillStyle = "#000000";
                            [[2, 2], [23, 2], [2, 16], [23, 16]].forEach(function (c) { ctx.fillRect(c[0] - 1, c[1] - 1, 3, 3); });
                        }
                    }
                }
                Text {
                    anchors.horizontalCenter: parent.horizontalCenter
                    y: art.y + art.height + 4
                    text: mb.label
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontSize - 1
                }
                MouseArea {
                    anchors.fill: parent
                    onClicked: { tool.mode = mb.modeName; tool.hovered = null; tool.forceActiveFocus(); }
                }
            }

            ModeButton { modeName: "screen"; label: "Entire Screen" }
            ModeButton { modeName: "window"; label: "Window" }
            ModeButton { modeName: "selection"; label: "Selection" }

            Rectangle { width: 2; height: 52; color: Theme.shadow; Rectangle { x: 1; width: 1; height: parent.height; color: Theme.highlight } }

            Ui.Button95 {
                anchors.verticalCenter: parent.verticalCenter
                width: 82
                text: "<u>O</u>ptions ▾"
                onClicked: {
                    var p = mapToItem(tool, 0, 0);
                    optionsMenu.popup(p.x, p.y, [
                        { text: "Save to", enabled: false },
                        { text: "    Pictures", checked: tool.destination === "Pictures", action: function () { tool.destination = "Pictures"; } },
                        { text: "    Desktop", checked: tool.destination === "Desktop", action: function () { tool.destination = "Desktop"; } },
                        { text: "    Documents", checked: tool.destination === "Documents", action: function () { tool.destination = "Documents"; } },
                        { text: "    Clipboard", checked: tool.destination === "Clipboard", action: function () { tool.destination = "Clipboard"; } },
                        { separator: true },
                        { text: "Timer", enabled: false },
                        { text: "    None", checked: tool.timer === 0, action: function () { tool.timer = 0; } },
                        { text: "    5 Seconds", checked: tool.timer === 5, action: function () { tool.timer = 5; } },
                        { text: "    10 Seconds", checked: tool.timer === 10, action: function () { tool.timer = 10; } },
                        { separator: true },
                        { text: "Show Floating Thumbnail", checked: tool.thumbnail, action: function () { tool.thumbnail = !tool.thumbnail; } },
                    ]);
                }
            }
            Ui.Button95 {
                anchors.verticalCenter: parent.verticalCenter
                width: 70
                isDefault: true
                text: "<u>C</u>apture"
                enabled: tool.mode !== "window" || !!tool.hovered
                onClicked: tool.capture()
            }
            Ui.Button95 {
                anchors.verticalCenter: parent.verticalCenter
                width: 24
                text: "✕"
                onClicked: tool.close()
            }
        }
    }

    // where it'll go, under the toolbar
    Text {
        visible: toolbar.visible
        anchors.horizontalCenter: toolbar.horizontalCenter
        anchors.top: toolbar.bottom
        anchors.topMargin: 4
        text: (tool.mode === "window" ? "Click a window. " : tool.mode === "selection" ? "Drag to select. " : "")
              + "Saves to " + tool.destination + (tool.timer ? ", after " + tool.timer + " seconds" : "") + ". Esc cancels."
        color: "#ffffff"
        style: Text.Outline
        styleColor: "#000000"
        font.family: Theme.fontFamily
        font.pixelSize: Theme.fontSize
    }

    // ---- the floating thumbnail, bottom right, for a few seconds ----------
    Item {
        id: thumb
        property string path
        property bool clipboard: false
        property bool showing: false
        width: 200
        height: pic.paintedHeight > 0 ? pic.paintedHeight + 26 : 140
        x: tool.width - width - 16
        y: tool.height - height - 50
        visible: showing
        opacity: hideTimer.running || area.containsMouse ? 1 : 0
        Behavior on opacity { NumberAnimation { duration: 250 } }
        onOpacityChanged: if (opacity === 0) showing = false

        function show(p, clip) {
            path = "";
            path = p;
            clipboard = clip;
            showing = true;
            hideTimer.restart();
        }
        Timer { id: hideTimer; interval: 5000 }

        Rectangle { anchors.fill: parent; color: Theme.face; border.color: Theme.shadowDarker }
        Bevel { anchors.fill: parent; raised: true }
        Image {
            id: pic
            x: 4; y: 4
            width: parent.width - 8
            height: 160
            fillMode: Image.PreserveAspectFit
            verticalAlignment: Image.AlignTop
            source: thumb.path ? "file://" + thumb.path : ""
            cache: false
            asynchronous: true
        }
        Text {
            x: 6
            anchors.bottom: parent.bottom
            anchors.bottomMargin: 4
            width: parent.width - 12
            elide: Text.ElideMiddle
            text: thumb.clipboard ? "Copied to the Clipboard" : thumb.path.substring(thumb.path.lastIndexOf("/") + 1)
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontSize
        }
        MouseArea {
            id: area
            anchors.fill: parent
            hoverEnabled: true
            cursorShape: Qt.PointingHandCursor
            onClicked: {
                hideTimer.stop();
                thumb.showing = false;
                if (!thumb.clipboard)
                    Launcher.run(thumb.path);
            }
        }
    }

    Ui.Menu95 { id: optionsMenu; z: 10 }
}
