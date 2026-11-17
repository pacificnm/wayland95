import QtQuick
import QtQuick.Window
import Win95Ui
import Win95Mines

// Mines: clear a minefield without setting one off. A number says how many
// of the eight cells around it hold mines. Left click opens a cell (the
// first is never a mine), right click puts a flag on one (and a "?" after
// it, if Marks is on). Both buttons together, the middle button, or a
// click on a number opens the cells around it, once it has as many flags
// as its number. Arrow keys and Space/F play from the keyboard.
Window {
    id: window

    // ------------------------------------------------------------- the game
    property int cols: 9
    property int rows: 9
    property int mineCount: 10
    property string level: "beginner" // beginner | intermediate | expert | custom
    property bool marks: true

    // ready: nothing opened yet (the mines are laid on the first click)
    property string phase: "ready" // ready | playing | won | lost
    property int flags: 0
    property int seconds: 0
    property int opened: 0
    property int cursor: -1          // the keyboard's cell (-1: none shown)
    property var pressedCells: ({})  // cells drawn held down
    property bool pressing: false    // a cell is held (the button shows it)

    readonly property var levels: ({
        beginner: { cols: 9, rows: 9, mines: 10, name: "Beginner" },
        intermediate: { cols: 16, rows: 16, mines: 40, name: "Intermediate" },
        expert: { cols: 30, rows: 16, mines: 99, name: "Expert" },
    })
    readonly property bool debug: gameDebug

    // a small, seedable random number generator (mulberry32): --seed deals
    // the same minefields every run
    property double rngState: gameSeed >= 0 ? gameSeed : Date.now() % 4294967296
    function random() {
        var t = (rngState += 0x6D2B79F5) >>> 0;
        rngState = t;
        t = Math.imul(t ^ (t >>> 15), t | 1);
        t ^= t + Math.imul(t ^ (t >>> 7), t | 61);
        return ((t ^ (t >>> 14)) >>> 0) / 4294967296;
    }

    function log(s) { if (debug) console.log("mines: " + s); }

    function neighbours(i) {
        var r = Math.floor(i / cols), c = i % cols, out = [];
        for (var dr = -1; dr <= 1; ++dr)
            for (var dc = -1; dc <= 1; ++dc) {
                if (!dr && !dc)
                    continue;
                var rr = r + dr, cc = c + dc;
                if (rr >= 0 && rr < rows && cc >= 0 && cc < cols)
                    out.push(rr * cols + cc);
            }
        return out;
    }

    function newGame() {
        clock.stop();
        phase = "ready";
        flags = 0;
        seconds = 0;
        opened = 0;
        pressedCells = {};
        pressing = false;
        if (cursor >= cols * rows)
            cursor = -1;
        var n = cols * rows;
        if (cells.count !== n) {
            cells.clear();
            for (var i = 0; i < n; ++i)
                cells.append({ mine: false, adj: 0, open: false, mark: 0, hit: false });
        } else {
            for (var j = 0; j < n; ++j)
                cells.set(j, { mine: false, adj: 0, open: false, mark: 0, hit: false });
        }
        log("new " + cols + "x" + rows + " " + mineCount);
    }

    // the mines go anywhere but the first cell opened and (room permitting)
    // its neighbours
    function layMines(first) {
        var keep = {};
        keep[first] = true;
        if (cols * rows - 9 >= mineCount)
            neighbours(first).forEach(function (k) { keep[k] = true; });
        var free = [];
        for (var i = 0; i < cols * rows; ++i)
            if (!keep[i])
                free.push(i);
        var placed = [];
        for (var m = 0; m < mineCount && free.length; ++m) {
            var k = Math.floor(random() * free.length);
            placed.push(free[k]);
            cells.setProperty(free[k], "mine", true);
            free.splice(k, 1);
        }
        for (var c = 0; c < cols * rows; ++c) {
            var adj = 0;
            neighbours(c).forEach(function (k) { if (cells.get(k).mine) ++adj; });
            cells.setProperty(c, "adj", adj);
        }
        log("mines " + placed.sort(function (a, b) { return a - b; }).join(","));
    }

    function open(i) {
        if (phase === "won" || phase === "lost")
            return;
        var cell = cells.get(i);
        if (cell.open || cell.mark === 1)
            return;
        if (phase === "ready") {
            layMines(i);
            phase = "playing";
            seconds = 1;
            clock.start();
        }
        if (cell.mine) {
            cells.setProperty(i, "hit", true);
            lose();
            return;
        }
        // open it, and keep going outwards from cells with no mines around
        var todo = [i];
        while (todo.length) {
            var k = todo.pop();
            var c = cells.get(k);
            if (c.open || c.mark === 1 || c.mine)
                continue;
            if (c.mark === 2)
                cells.setProperty(k, "mark", 0);
            cells.setProperty(k, "open", true);
            ++opened;
            if (c.adj === 0)
                todo = todo.concat(neighbours(k));
        }
        if (opened === cols * rows - mineCount)
            win();
        else
            log("opened " + opened);
    }

    // a number with as many flags around it as it says: open the rest
    function chord(i) {
        var cell = cells.get(i);
        if (!cell.open || cell.adj === 0 || phase !== "playing")
            return;
        var around = neighbours(i), flagged = 0;
        around.forEach(function (k) { if (cells.get(k).mark === 1) ++flagged; });
        if (flagged !== cell.adj)
            return;
        around.forEach(function (k) { if (phase === "playing") open(k); });
    }

    function toggleMark(i) {
        if (phase === "won" || phase === "lost")
            return;
        var cell = cells.get(i);
        if (cell.open)
            return;
        var next = cell.mark === 0 ? 1 : (cell.mark === 1 && marks ? 2 : 0);
        if (cell.mark === 1) --flags;
        if (next === 1) ++flags;
        cells.setProperty(i, "mark", next);
        log("mark " + i + " " + next);
    }

    function lose() {
        clock.stop();
        phase = "lost";
        log("lost");
    }

    function win() {
        clock.stop();
        phase = "won";
        for (var i = 0; i < cols * rows; ++i)
            if (cells.get(i).mine)
                cells.setProperty(i, "mark", 1);
        flags = mineCount;
        log("won " + seconds);
        if (level !== "custom" && seconds < bestTime(level))
            nameDialog.ask();
    }

    function setLevel(name, c, r, m) {
        level = name;
        if (name !== "custom") {
            c = levels[name].cols; r = levels[name].rows; m = levels[name].mines;
        }
        cols = c;
        rows = r;
        mineCount = Math.max(1, Math.min(m, (c - 1) * (r - 1)));
        GameStore.setValue("level", name);
        if (name === "custom") {
            GameStore.setValue("customCols", cols);
            GameStore.setValue("customRows", rows);
            GameStore.setValue("customMines", mineCount);
        }
        newGame();
    }

    function bestTime(lv) { return Number(GameStore.value("best/" + lv + "/time", 999)); }
    function bestName(lv) { return String(GameStore.value("best/" + lv + "/name", "Anonymous")); }

    Component.onCompleted: {
        marks = GameStore.value("marks", "true") === "true" || GameStore.value("marks", true) === true;
        var lv = String(GameStore.value("level", "beginner"));
        if (lv === "custom")
            setLevel("custom", Number(GameStore.value("customCols", 9)), Number(GameStore.value("customRows", 9)),
                     Number(GameStore.value("customMines", 10)));
        else
            setLevel(levels[lv] ? lv : "beginner");
        board.forceActiveFocus();
    }

    Timer {
        id: clock
        interval: 1000
        repeat: true
        onTriggered: if (window.seconds < 999) ++window.seconds
    }

    ListModel { id: cells }

    // --------------------------------------------------------------- window
    readonly property int cellSize: 16
    readonly property int pad: 6
    readonly property int boardWidth: cols * cellSize + 6
    readonly property int boardHeight: rows * cellSize + 6
    readonly property int statusHeight: 37

    width: boardWidth + 2 * pad + 4
    height: menuBar.height + statusHeight + boardHeight + 3 * pad + 4
    minimumWidth: width
    maximumWidth: width
    minimumHeight: height
    maximumHeight: height
    visible: true
    color: Theme.face
    title: "Mines"

    readonly property bool modalOpen: popup.visible || msgBox.visible || customDialog.visible
                                      || bestDialog.visible || nameDialog.visible

    // ----------------------------------------------------------------- menus
    function gameMenu() {
        return [
            { text: "New", shortcut: "F2", action: window.newGame },
            { separator: true },
            { text: "Beginner", checked: level === "beginner", action: function () { window.setLevel("beginner"); } },
            { text: "Intermediate", checked: level === "intermediate", action: function () { window.setLevel("intermediate"); } },
            { text: "Expert", checked: level === "expert", action: function () { window.setLevel("expert"); } },
            { text: "Custom...", checked: level === "custom", action: function () { customDialog.open(); } },
            { separator: true },
            { text: "Marks (?)", checked: marks, action: function () {
                window.marks = !window.marks;
                GameStore.setValue("marks", window.marks ? "true" : "false");
            } },
            { separator: true },
            { text: "Best Times...", action: function () { bestDialog.show(); } },
            { separator: true },
            { text: "Exit", action: window.close },
        ];
    }
    function helpMenu() {
        return [
            { text: "How to Play", action: function () {
                msgBox.show("How to Play",
                            "Open every cell that has no mine in it.\n\n"
                            + "Left click: open a cell. The first one is always safe.\n"
                            + "Right click: put a flag on a cell you think is a mine.\n"
                            + "A number tells how many mines touch that cell. Click a\n"
                            + "number that has all its flags to open the cells around it.\n\n"
                            + "Keyboard: the arrow keys, Space to open, F to flag, F2 for a new game.",
                            "info", ["OK"], function () { board.forceActiveFocus(); });
            } },
            { separator: true },
            { text: "About Mines", action: function () {
                msgBox.show("About Mines", "Mines\n\nA minefield game for the Wayland 95 desktop.\nVersion "
                            + Wayland95Settings.version, "info", ["OK"], function () { board.forceActiveFocus(); });
            } },
        ];
    }

    Shortcut { sequence: "F2"; enabled: !window.modalOpen; onActivated: window.newGame() }

    MenuBar95 {
        id: menuBar
        x: 0
        y: 0
        width: parent.width
        height: 20
        popupMenu: popup
        menus: [
            { title: "Game", items: window.gameMenu },
            { title: "Help", items: window.helpMenu },
        ]
    }

    // the raised area the game sits in
    Bevel {
        anchors.fill: parent
        anchors.topMargin: menuBar.height
        style: "raised"

        // mines left, the new-game button, the clock
        Bevel {
            id: status
            x: window.pad + 2
            y: window.pad + 2
            width: window.boardWidth
            height: window.statusHeight
            style: "sunken"

            Counter {
                id: minesLeft
                x: 6
                anchors.verticalCenter: parent.verticalCenter
                value: window.mineCount - window.flags
            }
            Item {
                id: faceButton
                width: 26
                height: 26
                anchors.centerIn: parent
                readonly property bool down: faceMouse.pressed && faceMouse.containsMouse
                Bevel {
                    anchors.fill: parent
                    style: faceButton.down ? "pressed" : "raised"
                }
                Glyph {
                    x: faceButton.down ? 6 : 5
                    y: faceButton.down ? 6 : 5
                    kind: window.phase === "won" ? "won"
                        : window.phase === "lost" ? "lost"
                        : window.pressing ? "pressed" : "ready"
                }
                MouseArea {
                    id: faceMouse
                    anchors.fill: parent
                    onClicked: { window.newGame(); board.forceActiveFocus(); }
                }
            }
            Counter {
                id: timeCounter
                anchors.right: parent.right
                anchors.rightMargin: 6
                anchors.verticalCenter: parent.verticalCenter
                value: window.seconds
            }
        }

        // the minefield
        Bevel {
            id: field
            x: window.pad + 2
            y: status.y + status.height + window.pad
            width: window.boardWidth
            height: window.boardHeight
            style: "sunken"

            FocusScope {
                id: board
                x: 3
                y: 3
                width: window.cols * window.cellSize
                height: window.rows * window.cellSize
                focus: true

                Grid {
                    columns: window.cols
                    Repeater {
                        model: cells
                        delegate: Item {
                            id: cell
                            required property int index
                            required property bool mine
                            required property int adj
                            required property bool open
                            required property int mark
                            required property bool hit
                            width: window.cellSize
                            height: window.cellSize

                            readonly property bool over: window.phase === "lost" || window.phase === "won"
                            // a lost game shows where the mines were, and wrong flags
                            readonly property bool showMine: (window.phase === "lost" && mine && mark !== 1) || hit
                            readonly property bool wrongFlag: window.phase === "lost" && mark === 1 && !mine
                            readonly property bool flat: open || showMine || wrongFlag || window.pressedCells[index] === true

                            Rectangle {
                                anchors.fill: parent
                                visible: cell.flat
                                color: cell.hit ? "#ff0000" : Theme.face
                                Rectangle { width: parent.width; height: 1; color: Theme.shadow }
                                Rectangle { width: 1; height: parent.height; color: Theme.shadow }
                            }
                            Bevel {
                                anchors.fill: parent
                                visible: !cell.flat
                                style: "raised"
                            }
                            Text {
                                anchors.fill: parent
                                anchors.leftMargin: 1
                                anchors.topMargin: 1
                                visible: cell.open && cell.adj > 0
                                text: cell.adj
                                horizontalAlignment: Text.AlignHCenter
                                verticalAlignment: Text.AlignVCenter
                                font.family: Theme.fontFamily
                                font.pixelSize: 13
                                font.bold: true
                                color: ["#000000", "#0000ff", "#008000", "#ff0000", "#000080",
                                        "#800000", "#008080", "#000000", "#808080"][cell.adj]
                            }
                            Glyph {
                                x: cell.flat ? 1 : 0
                                y: cell.flat ? 1 : 0
                                visible: kind !== ""
                                kind: cell.wrongFlag ? "wrong" : cell.showMine ? "mine"
                                    : (cell.mark === 1 && !cell.open) ? "flag" : ""
                            }
                            Text {
                                anchors.fill: parent
                                visible: cell.mark === 2 && !cell.open && !cell.showMine
                                text: "?"
                                horizontalAlignment: Text.AlignHCenter
                                verticalAlignment: Text.AlignVCenter
                                font.family: Theme.fontFamily
                                font.pixelSize: 12
                                font.bold: true
                            }
                            // the keyboard's cell
                            Rectangle {
                                anchors.fill: parent
                                anchors.margins: 2
                                visible: window.cursor === cell.index && board.activeFocus
                                color: "transparent"
                                border.color: "#000000"
                                border.width: 1
                            }
                        }
                    }
                }

                // ------------------------------------------------- the mouse
                MouseArea {
                    id: boardMouse
                    anchors.fill: parent
                    acceptedButtons: Qt.LeftButton | Qt.RightButton | Qt.MiddleButton
                    property bool chording: false
                    property bool spent: false   // a chord ended: ignore the rest until all buttons are up

                    function cellAt(x, y) {
                        var c = Math.floor(x / window.cellSize), r = Math.floor(y / window.cellSize);
                        return (c < 0 || r < 0 || c >= window.cols || r >= window.rows) ? -1 : r * window.cols + c;
                    }
                    function preview(x, y) {
                        var i = cellAt(x, y), set = {};
                        if (i >= 0 && containsMouse) {
                            var list = chording ? window.neighbours(i).concat([i]) : [i];
                            list.forEach(function (k) {
                                var c = cells.get(k);
                                if (!c.open && c.mark !== 1)
                                    set[k] = true;
                            });
                        }
                        window.pressedCells = set;
                    }
                    function playing() { return window.phase === "ready" || window.phase === "playing"; }

                    onPressed: (mouse) => {
                        board.forceActiveFocus();
                        window.cursor = -1;
                        window.log("press " + Math.round(mapToItem(null, mouse.x, mouse.y).x) + " "
                                   + Math.round(mapToItem(null, mouse.x, mouse.y).y));
                        if (!playing())
                            return;
                        var both = (mouse.buttons & Qt.LeftButton) && (mouse.buttons & Qt.RightButton);
                        if (mouse.button === Qt.MiddleButton || both) {
                            chording = true;
                        } else if (mouse.button === Qt.RightButton) {
                            var i = cellAt(mouse.x, mouse.y);
                            if (i >= 0)
                                window.toggleMark(i);
                            return;
                        }
                        window.pressing = true;
                        preview(mouse.x, mouse.y);
                    }
                    onPositionChanged: (mouse) => {
                        if (window.pressing)
                            preview(mouse.x, mouse.y);
                    }
                    onReleased: (mouse) => {
                        var i = cellAt(mouse.x, mouse.y);
                        if (chording) {
                            chording = false;
                            spent = mouse.buttons !== 0;
                            if (i >= 0 && containsMouse)
                                window.chord(i);
                        } else if (spent) {
                            spent = mouse.buttons !== 0;
                        } else if (mouse.button === Qt.LeftButton && window.pressing && i >= 0 && containsMouse) {
                            if (cells.get(i).open)
                                window.chord(i);
                            else
                                window.open(i);
                        }
                        if (mouse.buttons === 0) {
                            window.pressing = false;
                            window.pressedCells = {};
                        }
                    }
                    onCanceled: { window.pressing = false; window.pressedCells = {}; chording = false; }
                }

                // ---------------------------------------------- the keyboard
                Keys.onPressed: (event) => {
                    var c = window.cursor < 0 ? 0 : window.cursor;
                    var r = Math.floor(c / window.cols), col = c % window.cols;
                    switch (event.key) {
                    case Qt.Key_Left:  col = (col + window.cols - 1) % window.cols; break;
                    case Qt.Key_Right: col = (col + 1) % window.cols; break;
                    case Qt.Key_Up:    r = (r + window.rows - 1) % window.rows; break;
                    case Qt.Key_Down:  r = (r + 1) % window.rows; break;
                    case Qt.Key_Space:
                    case Qt.Key_Return:
                    case Qt.Key_Enter:
                        if (window.cursor >= 0) {
                            if (cells.get(window.cursor).open)
                                window.chord(window.cursor);
                            else
                                window.open(window.cursor);
                        }
                        window.cursor = c;
                        event.accepted = true;
                        return;
                    case Qt.Key_F:
                        if (window.cursor >= 0)
                            window.toggleMark(window.cursor);
                        window.cursor = c;
                        event.accepted = true;
                        return;
                    default:
                        return;
                    }
                    window.cursor = window.cursor < 0 ? 0 : r * window.cols + col;
                    event.accepted = true;
                }
            }
        }
    }

    // ----------------------------------------------------------- dialogs
    DialogWindow95 {
        id: customDialog
        transientParent: window
        title: "Custom Field"
        width: 230
        height: 110
        function open() {
            heightBox.value = window.rows;
            widthBox.value = window.cols;
            minesBox.value = window.mineCount;
            show();
        }
        function done() {
            close();
            window.setLevel("custom", widthBox.value, heightBox.value, minesBox.value);
        }
        onAccepted: done()

        Column {
            x: 12
            y: 14
            spacing: 8
            Repeater {
                model: ["Height:", "Width:", "Mines:"]
                delegate: Text {
                    required property string modelData
                    height: 21
                    verticalAlignment: Text.AlignVCenter
                    text: modelData
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontSize
                }
            }
        }
        Column {
            x: 64
            y: 14
            spacing: 8
            SpinBox95 { id: heightBox; width: 60; from: 9; to: 24 }
            SpinBox95 { id: widthBox; width: 60; from: 9; to: 30 }
            SpinBox95 { id: minesBox; width: 60; from: 10; to: (widthBox.value - 1) * (heightBox.value - 1) }
        }
        Column {
            x: customDialog.width - width - 12
            y: 14
            spacing: 6
            Button95 {
                text: "OK"
                isDefault: true
                onClicked: customDialog.done()
            }
            Button95 { text: "Cancel"; onClicked: customDialog.close() }
        }
    }

    DialogWindow95 {
        id: bestDialog
        transientParent: window
        title: "Best Times"
        width: 300
        height: 110
        property int stamp: 0 // bumped on opening and by Reset Scores: the times read again
        onAccepted: close()
        onVisibleChanged: if (visible) {
            ++stamp;
            window.log("best " + window.bestName(window.level));
        }

        Column {
            x: 14
            y: 14
            spacing: 4
            Repeater {
                model: ["beginner", "intermediate", "expert"]
                delegate: Row {
                    required property string modelData
                    spacing: 0
                    readonly property int t: bestDialog.stamp >= 0 ? window.bestTime(modelData) : 0
                    Text {
                        width: 90
                        text: window.levels[parent.modelData].name + ":"
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.fontSize
                    }
                    Text {
                        width: 80
                        text: parent.t + " seconds"
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.fontSize
                    }
                    Text {
                        text: bestDialog.stamp >= 0 ? window.bestName(parent.modelData) : ""
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.fontSize
                    }
                }
            }
        }
        Row {
            x: (bestDialog.width - width) / 2
            y: 72
            spacing: 10
            Button95 {
                text: "Reset Scores"
                width: 90
                onClicked: {
                    ["beginner", "intermediate", "expert"].forEach(function (lv) {
                        GameStore.setValue("best/" + lv + "/time", 999);
                        GameStore.setValue("best/" + lv + "/name", "Anonymous");
                    });
                    ++bestDialog.stamp;
                }
            }
            Button95 { text: "OK"; isDefault: true; onClicked: bestDialog.close() }
        }
    }

    DialogWindow95 {
        id: nameDialog
        transientParent: window
        title: "New Best Time"
        width: 260
        height: 116
        function ask() {
            nameField.text = String(GameStore.value("lastName", ""));
            show();
            nameField.forceActiveFocus();
            nameField.input.selectAll();
        }
        function done() {
            var name = nameField.text.trim() || "Anonymous";
            GameStore.setValue("lastName", name);
            GameStore.setValue("best/" + window.level + "/time", window.seconds);
            GameStore.setValue("best/" + window.level + "/name", name);
            close();
            bestDialog.show();
        }
        onAccepted: done()

        Text {
            x: 14
            y: 12
            width: nameDialog.width - 28
            wrapMode: Text.WordWrap
            text: "You have the fastest time for the " + (window.levels[window.level] ? window.levels[window.level].name.toLowerCase() : "")
                  + " level. Please type your name."
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontSize
        }
        TextField95 {
            id: nameField
            x: 14
            y: 48
            width: nameDialog.width - 28
            onAccepted: nameDialog.done()
        }
        Button95 {
            x: (nameDialog.width - width) / 2
            y: 80
            text: "OK"
            isDefault: true
            onClicked: nameDialog.done()
        }
    }

    // a dialog closed: back to the minefield
    Connections {
        target: window
        function onActiveChanged() { if (window.active && !window.modalOpen) board.forceActiveFocus(); }
    }

    Menu95 {
        id: popup
        onClosed: if (!window.modalOpen) board.forceActiveFocus()
    }
    MessageBox95 { id: msgBox }
}
