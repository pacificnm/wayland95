import QtQuick
import Win95Shell

// The desktop's icons: My Computer and the rest, then the user's shortcuts
// (DesktopShortcuts: the .desktop files in ~/Desktop), each with the little
// shortcut arrow. The built-in icons are drawn as plain generic shapes (not
// Microsoft's actual icon artwork - that's trademarked/copyrighted).
//
// Arranged as in Win95: at first in columns down the left; drag one and it
// stays where it's dropped (DesktopShortcuts.layout keeps where, across
// logins). The desktop's menu (desktopMenuRequested) has Arrange Icons (by
// Name, Type or Date, and Auto Arrange, which keeps them in columns in the
// order they're dragged to) and Line up Icons (each to the nearest place on
// the grid). New ones go in the first free place in the columns.
Item {
    id: grid
    readonly property int cellWidth: 80
    readonly property int cellHeight: 80

    // an icon was right-clicked: its entry (label, kind, program, arg; a
    // shortcut's kind is "shortcut", with its path), and where, in this
    // item's coordinates (the desktop shows its menu)
    signal menuRequested(var entry, real x, real y)
    // the desktop itself was right-clicked (between the icons)
    signal desktopMenuRequested(real x, real y)

    // --- the layout -------------------------------------------------------
    readonly property var layout: DesktopShortcuts.layout
    readonly property bool autoArrange: layout.autoArrange === true
    property string selectedKey: ""

    readonly property var entries: builtIn.concat(DesktopShortcuts.shortcuts.map(function (s) {
        return { label: s.label, kind: "shortcut", iconSource: s.iconSource, icon: s.icon, path: s.path,
                 key: s.key, modified: s.modified };
    }))
    // the grid: how many icons fit down a column
    readonly property int rows: Math.max(1, Math.floor(height / cellHeight))
    function cellPos(i) {
        return { x: Math.floor(i / rows) * cellWidth, y: (i % rows) * cellHeight };
    }
    // the icons' keys in their order: the saved order first, then any new ones
    function orderedKeys() {
        var keys = entries.map(function (e) { return e.key; });
        var saved = (layout.order || []).filter(function (k) { return keys.indexOf(k) >= 0; });
        return saved.concat(keys.filter(function (k) { return saved.indexOf(k) < 0; }));
    }
    // where every icon goes: key -> { x, y }
    readonly property var placement: {
        var out = {};
        var keys = orderedKeys();
        if (autoArrange) {
            for (var i = 0; i < keys.length; ++i)
                out[keys[i]] = cellPos(i);
            return out;
        }
        // where each was put (kept on the screen, if it's smaller now)...
        var positions = layout.positions || {};
        var maxX = Math.max(0, width - cellWidth), maxY = Math.max(0, height - cellHeight);
        var placed = [];
        for (var k = 0; k < keys.length; ++k) {
            var p = positions[keys[k]];
            if (!p)
                continue;
            out[keys[k]] = { x: width > 0 ? Math.min(Math.max(0, p.x), maxX) : p.x,
                             y: height > 0 ? Math.min(Math.max(0, p.y), maxY) : p.y };
            placed.push(out[keys[k]]);
        }
        // ...and the rest in the first free places down the columns
        var cell = 0;
        for (var n = 0; n < keys.length; ++n) {
            if (out[keys[n]])
                continue;
            while (cell < 10000 && overlaps(cellPos(cell), placed))
                ++cell;
            out[keys[n]] = cellPos(cell);
            placed.push(out[keys[n]]);
            ++cell;
        }
        return out;
    }
    function overlaps(p, placed) {
        for (var i = 0; i < placed.length; ++i) {
            if (Math.abs(placed[i].x - p.x) < cellWidth && Math.abs(placed[i].y - p.y) < cellHeight)
                return true;
        }
        return false;
    }

    function save(order, positions, autoArrange) {
        DesktopShortcuts.saveLayout({ autoArrange: autoArrange, order: order, positions: positions });
    }
    // every icon where it is now
    function currentPositions() {
        var out = {};
        for (var k in placement)
            out[k] = { x: Math.round(placement[k].x), y: Math.round(placement[k].y) };
        return out;
    }

    // an icon dropped at (x, y)
    function dropped(key, x, y) {
        var order = orderedKeys();
        if (autoArrange) {
            // into the place it was dropped on, the others moving along
            var col = Math.max(0, Math.round(x / cellWidth)), row = Math.min(rows - 1, Math.max(0, Math.round(y / cellHeight)));
            var to = Math.min(order.length - 1, col * rows + row);
            order.splice(order.indexOf(key), 1);
            order.splice(to, 0, key);
            save(order, currentPositions(), true);
            return;
        }
        var positions = currentPositions();
        positions[key] = { x: Math.round(x), y: Math.round(y) };
        save(order, positions, false);
    }

    // Arrange Icons > by Name, Type or Date: My Computer and the others
    // first, as in Win95, then the shortcuts sorted; into columns
    function arrangeBy(how) {
        var shortcuts = entries.filter(function (e) { return e.kind === "shortcut"; });
        shortcuts.sort(function (a, b) {
            if (how === "date" && a.modified !== b.modified)
                return b.modified - a.modified; // newest first
            if (how === "type") {
                // what they start: Wayland 95's own programs, then the rest
                var ta = a.icon ? 0 : 1, tb = b.icon ? 0 : 1;
                if (ta !== tb)
                    return ta - tb;
            }
            return a.label.localeCompare(b.label, undefined, { sensitivity: "base" });
        });
        var order = builtIn.map(function (e) { return e.key; })
                           .concat(shortcuts.map(function (e) { return e.key; }));
        var positions = {};
        for (var i = 0; i < order.length; ++i)
            positions[order[i]] = cellPos(i);
        save(order, positions, autoArrange);
    }
    function setAutoArrange(on) {
        if (on) {
            // in the order they're in now: down each column, left to right
            var keys = orderedKeys().slice();
            keys.sort(function (a, b) {
                var pa = placement[a], pb = placement[b];
                var ca = Math.round(pa.x / cellWidth), cb = Math.round(pb.x / cellWidth);
                return ca !== cb ? ca - cb : pa.y - pb.y;
            });
            var positions = {};
            for (var i = 0; i < keys.length; ++i)
                positions[keys[i]] = cellPos(i);
            save(keys, positions, true);
        } else {
            save(orderedKeys(), currentPositions(), false);
        }
    }
    // Line up Icons: each to the nearest free place on the grid
    function lineUp() {
        var keys = orderedKeys().slice();
        keys.sort(function (a, b) {
            var pa = placement[a], pb = placement[b];
            return pa.x !== pb.x ? pa.x - pb.x : pa.y - pb.y;
        });
        var cols = Math.max(1, Math.floor(width / cellWidth));
        var taken = {};
        var positions = {};
        for (var i = 0; i < keys.length; ++i) {
            var p = placement[keys[i]];
            var best = null, bestD = Infinity;
            for (var c = 0; c < cols + keys.length / rows + 1; ++c) {
                for (var r = 0; r < rows; ++r) {
                    if (taken[c + "," + r])
                        continue;
                    var dx = c * cellWidth - p.x, dy = r * cellHeight - p.y;
                    var d = dx * dx + dy * dy;
                    if (d < bestD) {
                        bestD = d;
                        best = { c: c, r: r };
                    }
                }
            }
            taken[best.c + "," + best.r] = true;
            positions[keys[i]] = { x: best.c * cellWidth, y: best.r * cellHeight };
        }
        save(orderedKeys(), positions, autoArrange);
    }

    // a click between the icons: nothing selected; a right-click: the
    // desktop's menu
    MouseArea {
        anchors.fill: parent
        z: -1
        acceptedButtons: Qt.LeftButton | Qt.RightButton
        onPressed: (mouse) => {
            grid.selectedKey = "";
            if (mouse.button === Qt.RightButton)
                grid.desktopMenuRequested(mouse.x, mouse.y);
        }
    }
    function open(entry) {
        if (entry.kind === "shortcut")
            DesktopShortcuts.open(entry.path);
        else if (entry.program)
            Launcher.launch(entry.program, entry.program === "win95control" ? ["--applet", entry.arg]
                                           : entry.arg ? [entry.arg] : []);
    }

    // Draws the generic computer (monitor + case) on a 32x32 grid, at
    // (ox, oy) scaled by s. Shared by the My Computer and Network icons.
    function drawComputer(ctx, ox, oy, s) {
        function rect(x, y, w, h, color) {
            ctx.fillStyle = color;
            ctx.fillRect(ox + x * s, oy + y * s, Math.max(1, w * s), Math.max(1, h * s));
        }
        function frame(x, y, w, h, fill) {
            rect(x, y, w, h, "#000000");
            rect(x + 1, y + 1, w - 2, h - 2, fill);
        }
        // monitor
        frame(4, 1, 24, 20, "#c0c0c0");
        rect(5, 2, 22, 1, "#ffffff");
        rect(5, 2, 1, 18, "#ffffff");
        frame(7, 4, 18, 13, "#008080");   // screen
        rect(8, 5, 6, 1, "#40c0c0");      // glare
        rect(13, 21, 6, 2, "#808080");    // neck
        // case
        frame(2, 23, 28, 8, "#c0c0c0");
        rect(3, 24, 26, 1, "#ffffff");
        rect(5, 26, 12, 2, "#808080");    // drive slot
        rect(24, 26, 3, 2, "#00c000");    // power light
    }

    readonly property var builtIn: [
        { label: "My Computer", kind: "computer", key: "computer", program: "win95files",    arg: "computer:" },
        { label: "Network",     kind: "network",  key: "network",  program: "win95control",  arg: "network" },
        { label: "Recycle Bin", kind: "bin",      key: "bin",      program: "win95files",    arg: "trash:" },
        { label: "Terminal",    kind: "terminal", key: "terminal", program: "win95terminal", arg: "" },
    ]

    Repeater {
    model: grid.entries
    delegate: Item {
        id: cell
        required property var modelData
        readonly property string kind: modelData.kind
        readonly property string key: modelData.key
        width: grid.cellWidth
        height: grid.cellHeight
        // where the layout puts it (until it's dragged; then again after)
        x: { var p = grid.placement[cell.key]; return p ? p.x : 0; }
        y: { var p = grid.placement[cell.key]; return p ? p.y : 0; }
        z: iconMouse.drag.active ? 1 : 0

        readonly property bool selected: grid.selectedKey === key

        Column {
            anchors.horizontalCenter: parent.horizontalCenter
            y: 8
            spacing: 4

            Item {
                width: 32
                height: 32
                anchors.horizontalCenter: parent.horizontalCenter

                Canvas {
                    // a monitor on a desktop case
                    visible: cell.kind === "computer"
                    anchors.fill: parent
                    onPaint: grid.drawComputer(getContext("2d"), 0, 0, 1)
                }
                Canvas {
                    // three small computers wired to each other
                    visible: cell.kind === "network"
                    anchors.fill: parent
                    onPaint: {
                        var ctx = getContext("2d");
                        // cabling first, so the machines sit on top of it
                        ctx.strokeStyle = "#000000";
                        ctx.lineWidth = 1;
                        ctx.beginPath();
                        ctx.moveTo(8.5, 13); ctx.lineTo(8.5, 20.5); ctx.lineTo(24.5, 20.5); ctx.lineTo(24.5, 13);
                        ctx.moveTo(16.5, 20.5); ctx.lineTo(16.5, 24);
                        ctx.stroke();
                        grid.drawComputer(ctx, 0, 0, 0.5);
                        grid.drawComputer(ctx, 16, 0, 0.5);
                        grid.drawComputer(ctx, 8, 17, 0.5);
                    }
                }
                // a shortcut: the program's own icon, or a Wayland 95
                // program's drawn one, with the shortcut arrow over its corner
                Image {
                    id: shortcutImage
                    visible: cell.kind === "shortcut" && !!cell.modelData.iconSource && status === Image.Ready
                    anchors.fill: parent
                    sourceSize: Qt.size(32, 32)
                    source: cell.kind === "shortcut" ? (cell.modelData.iconSource || "") : ""
                    asynchronous: true
                    fillMode: Image.PreserveAspectFit
                }
                MenuIcon {
                    visible: cell.kind === "shortcut" && !shortcutImage.visible
                    size: 32
                    kind: cell.modelData.icon || "app"
                }
                Canvas {
                    visible: cell.kind === "shortcut"
                    x: 0; y: 23
                    width: 9; height: 9
                    onPaint: {
                        // white box, black frame, a black arrow curving up-right
                        var ctx = getContext("2d");
                        ctx.fillStyle = "#000000";
                        ctx.fillRect(0, 0, 9, 9);
                        ctx.fillStyle = "#ffffff";
                        ctx.fillRect(1, 1, 7, 7);
                        ctx.fillStyle = "#000000";
                        ctx.fillRect(4, 2, 3, 1);
                        ctx.fillRect(6, 2, 1, 3);
                        ctx.fillRect(5, 3, 1, 1);
                        ctx.fillRect(4, 4, 1, 1);
                        ctx.fillRect(3, 5, 1, 2);
                        ctx.fillRect(2, 6, 1, 1);
                    }
                }

                Rectangle {
                    // a little black console window with a prompt
                    visible: cell.kind === "terminal"
                    anchors.fill: parent
                    anchors.margins: 3
                    color: "#c0c0c0"
                    border.color: "black"
                    Rectangle { x: 1; y: 1; width: parent.width - 2; height: 5; color: "#000080" }
                    Rectangle {
                        x: 3; y: 8; width: parent.width - 6; height: parent.height - 11
                        color: "black"
                        Text {
                            x: 2; y: 1
                            text: ">_"
                            color: "#c0c0c0"
                            font.family: "DejaVu Sans Mono"
                            font.pixelSize: 9
                            font.bold: true
                        }
                    }
                }
                Canvas {
                    // a wire-mesh wastebasket; crumpled paper pokes out of
                    // the top while the Recycle Bin has anything in it
                    id: binIcon
                    visible: cell.kind === "bin"
                    anchors.fill: parent
                    readonly property bool full: TrashWatcher.full
                    onFullChanged: requestPaint()

                    onPaint: {
                        var ctx = getContext("2d");
                        ctx.reset();
                        ctx.lineWidth = 1;

                        // tapered body
                        ctx.beginPath();
                        ctx.moveTo(5.5, 9.5);
                        ctx.lineTo(26.5, 9.5);
                        ctx.lineTo(23.5, 30.5);
                        ctx.lineTo(8.5, 30.5);
                        ctx.closePath();
                        ctx.fillStyle = "#e8e8e8";
                        ctx.fill();

                        // mesh: ribs converging toward the base, plus bands
                        ctx.strokeStyle = "#808080";
                        for (var i = 1; i < 6; ++i) {
                            var t = i / 6;
                            ctx.beginPath();
                            ctx.moveTo(5.5 + 21 * t, 9.5);
                            ctx.lineTo(8.5 + 15 * t, 30.5);
                            ctx.stroke();
                        }
                        for (var y = 14.5; y < 30; y += 5) {
                            var inset = (y - 9.5) / 21 * 3;
                            ctx.beginPath();
                            ctx.moveTo(5.5 + inset, y);
                            ctx.lineTo(26.5 - inset, y);
                            ctx.stroke();
                        }

                        ctx.strokeStyle = "#000000";
                        ctx.beginPath();
                        ctx.moveTo(5.5, 9.5);
                        ctx.lineTo(26.5, 9.5);
                        ctx.lineTo(23.5, 30.5);
                        ctx.lineTo(8.5, 30.5);
                        ctx.closePath();
                        ctx.stroke();

                        // crumpled paper
                        if (binIcon.full) {
                            ctx.fillStyle = "#ffffff";
                            var balls = [[11, 7, 4], [17, 5, 4.5], [22, 7.5, 3.5]];
                            for (var b = 0; b < balls.length; ++b) {
                                ctx.beginPath();
                                ctx.arc(balls[b][0], balls[b][1], balls[b][2], 0, Math.PI * 2);
                                ctx.fill();
                                ctx.stroke();
                            }
                            ctx.strokeStyle = "#808080";
                            ctx.beginPath();
                            ctx.moveTo(15, 4); ctx.lineTo(18, 7);
                            ctx.moveTo(10, 6); ctx.lineTo(12, 8);
                            ctx.stroke();
                            ctx.strokeStyle = "#000000";
                        }

                        // rim, drawn over the paper
                        ctx.fillStyle = "#c0c0c0";
                        ctx.fillRect(3.5, 8.5, 25, 3);
                        ctx.strokeRect(3.5, 8.5, 25, 3);
                        ctx.fillStyle = "#ffffff";
                        ctx.fillRect(4.5, 9.5, 23, 1);
                    }
                }
            }

            Rectangle {
                anchors.horizontalCenter: parent.horizontalCenter
                color: cell.selected ? Theme.selectionBlue : "transparent"
                width: label.width + 4
                height: label.height + 2

                // long names wrap onto a second line, then end in "..."
                Text {
                    id: label
                    anchors.centerIn: parent
                    width: Math.min(implicitWidth, grid.cellWidth - 6)
                    text: cell.modelData.label
                    color: "white"
                    horizontalAlignment: Text.AlignHCenter
                    wrapMode: Text.Wrap
                    maximumLineCount: 2
                    elide: Text.ElideRight
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontSize
                }
            }
        }

        MouseArea {
            id: iconMouse
            anchors.fill: parent
            acceptedButtons: Qt.LeftButton | Qt.RightButton
            // dragged with the left button, kept on the desktop
            drag.target: (pressedButtons & Qt.LeftButton) ? cell : null
            drag.axis: Drag.XAndYAxis
            drag.minimumX: 0
            drag.minimumY: 0
            drag.maximumX: Math.max(0, grid.width - grid.cellWidth)
            drag.maximumY: Math.max(0, grid.height - grid.cellHeight)
            property bool dragged: false
            drag.onActiveChanged: if (drag.active) dragged = true
            onPressed: (mouse) => {
                grid.selectedKey = cell.key;
                dragged = false;
                if (mouse.button === Qt.RightButton) {
                    var p = mapToItem(grid, mouse.x, mouse.y);
                    grid.menuRequested(cell.modelData, p.x, p.y);
                }
            }
            onReleased: (mouse) => {
                if (!dragged)
                    return;
                dragged = false;
                // the layout says where it went now; it follows that again
                grid.dropped(cell.key, cell.x, cell.y);
                cell.x = Qt.binding(function () { var p = grid.placement[cell.key]; return p ? p.x : 0; });
                cell.y = Qt.binding(function () { var p = grid.placement[cell.key]; return p ? p.y : 0; });
            }
            onDoubleClicked: (mouse) => {
                if (mouse.button === Qt.LeftButton)
                    grid.open(cell.modelData);
            }
        }
    }
    }
}
