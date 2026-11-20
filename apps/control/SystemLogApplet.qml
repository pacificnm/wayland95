import QtQuick
import QtQuick.Window
import Win95Ui
import Win95Control

// System Log: what the computer and your programs wrote in the system
// journal, like NT's Event Viewer. One start-up at a time, newest first,
// as bad as "Show" or worse, narrowed further by Find. The selected
// message, and everything journald kept with it, shows underneath.
Window {
    id: win

    width: 680
    height: 460
    minimumWidth: 420
    minimumHeight: 260
    visible: true
    color: Theme.face

    // system, kernel or user
    property string log: "system"
    property int boot: 0
    // 0-7: this bad or worse
    property int priority: 6
    property string current: ""   // the selected entry's key

    readonly property var logNames: ({ system: "System", kernel: "Kernel", user: "Your Programs" })
    readonly property var levels: [
        { name: "Errors", priority: 3 },
        { name: "Warnings and errors", priority: 4 },
        { name: "Notices, warnings and errors", priority: 5 },
        { name: "Information and above", priority: 6 },
        { name: "Everything (with debug)", priority: 7 },
    ]

    title: "System Log - " + logNames[log]

    readonly property string query: findField.text.trim().toLowerCase()
    readonly property var shown: {
        var all = SystemLog.entries;
        if (!query)
            return all;
        var q = query;
        return all.filter(function (e) {
            return e.message.toLowerCase().indexOf(q) >= 0 || e.source.toLowerCase().indexOf(q) >= 0
                || e.unit.toLowerCase().indexOf(q) >= 0;
        });
    }
    readonly property int currentIndex: {
        for (var i = 0; i < shown.length; ++i)
            if (shown[i].key === current)
                return i;
        return -1;
    }
    readonly property var entry: currentIndex >= 0 ? shown[currentIndex] : null

    function reload() {
        SystemLog.load(log, boot, priority);
    }
    function setLog(l) {
        if (log === l)
            return;
        log = l;
        boot = 0;
        SystemLog.loadBoots(l);
        reload();
    }
    function select(i) {
        if (shown.length === 0)
            return;
        i = Math.max(0, Math.min(shown.length - 1, i));
        current = shown[i].key;
        list.positionViewAtIndex(i, ListView.Contain);
    }
    function showAll() {
        SystemLog.setAdmin(true);
        SystemLog.loadBoots(log);
        reload();
    }
    function bootName(b) {
        if (b.index === 0)
            return "This start-up (" + when(b.first) + ")";
        return "Start-up of " + when(b.first);
    }
    function when(ms) {
        var d = new Date(ms);
        return d.toLocaleDateString(Qt.locale(), "M/d/yyyy") + " " + d.toLocaleTimeString(Qt.locale(), "h:mm AP");
    }
    function icon(p) { return p <= 3 ? "error" : p === 4 ? "warning" : "info"; }
    function typeName(p) { return p <= 3 ? "Error" : p === 4 ? "Warning" : p === 7 ? "Debug" : "Information"; }

    Component.onCompleted: {
        // win95control --applet systemlog --log kernel --find TEXT
        if (initialApplet === "systemlog") {
            if (initialLog === "system" || initialLog === "kernel" || initialLog === "user")
                log = initialLog;
            if (initialFind)
                findField.text = initialFind;
        }
        SystemLog.loadBoots(log);
        reload();
        list.forceActiveFocus();
    }
    Connections {
        target: SystemLog
        function onEntriesChanged() {
            // keep the selection if it's still there, else the newest
            if (win.currentIndex < 0 && win.shown.length)
                win.current = win.shown[0].key;
            if (SystemLog.error && win.active)
                msgBox.show("System Log", SystemLog.error, "error", ["OK"]);
        }
    }
    onShownChanged: if (currentIndex < 0 && shown.length) current = shown[0].key

    readonly property var logMenu: [
        { text: "&System", checked: log === "system", action: function () { win.setLog("system"); } },
        { text: "&Kernel", checked: log === "kernel", action: function () { win.setLog("kernel"); } },
        { text: "&Your Programs", checked: log === "user", action: function () { win.setLog("user"); } },
        { separator: true },
        { text: "Show &All Messages...", enabled: !SystemLog.canReadAll && !SystemLog.admin && log !== "user",
          action: win.showAll },
        { separator: true },
        { text: "Save Log &As...", enabled: shown.length > 0, action: function () {
            saveDialog.openAt(Win95Fs.homePath(), (log === "user" ? "my-programs" : log) + "-log.txt");
        } },
        { separator: true },
        { text: "&Close", shortcut: "Ctrl+W", action: function () { win.close(); } },
    ]
    readonly property var viewMenu: {
        var items = levels.map(function (l) {
            return { text: l.name, checked: win.priority === l.priority,
                     action: function () { win.priority = l.priority; win.reload(); } };
        });
        items.push({ separator: true });
        items.push({ text: "&Find...", shortcut: "Ctrl+F", action: function () { findField.forceActiveFocus(); findField.input.selectAll(); } });
        items.push({ text: "&Refresh", shortcut: "F5", action: win.reload });
        return items;
    }
    readonly property var entryMenu: [
        { text: "&Copy", shortcut: "Ctrl+C", enabled: !!entry, action: function () { SystemLog.copy(SystemLog.line(win.current)); } },
        { text: "Copy &Details", enabled: !!entry, action: function () {
            SystemLog.copy(SystemLog.line(win.current) + "\n\n" + SystemLog.fields(win.current));
        } },
        { separator: true },
        { text: "Find &More From " + (entry ? entry.source || "Here" : "Here"), enabled: !!entry && !!entry.source,
          action: function () { findField.text = win.entry.source; } },
    ]

    Shortcut { sequence: "F5"; enabled: !msgBox.visible; onActivated: win.reload() }
    Shortcut { sequence: "Ctrl+F"; onActivated: { findField.forceActiveFocus(); findField.input.selectAll(); } }
    Shortcut { sequence: "Ctrl+W"; onActivated: win.close() }
    Shortcut {
        sequence: "Esc"
        enabled: !msgBox.visible && !popup.visible && !saveDialog.visible
        onActivated: findField.activeFocus && findField.text ? findField.text = "" : win.close()
    }

    MenuBar95 {
        id: menuBar
        x: 2
        y: 2
        width: parent.width - 4
        height: 20
        popupMenu: popup
        menus: [
            { title: "Log", items: win.logMenu },
            { title: "View", items: win.viewMenu },
            { title: "Event", items: win.entryMenu },
            { title: "Help", items: [
                { text: "About System Log", action: function () {
                    msgBox.show("About System Log",
                                "System Log\n\nShows the messages the computer and your programs write in the "
                                + "system journal (journalctl). The newest 2000 of each start-up are shown.",
                                "info", ["OK"]);
                } },
            ] },
        ]
    }

    // --- the choices: which start-up, how bad, and Find
    Row {
        id: toolbar
        x: 6
        anchors.top: menuBar.bottom
        anchors.topMargin: 5
        spacing: 6
        height: 21

        component Label: Text {
            anchors.verticalCenter: parent.verticalCenter
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontSize
            color: Theme.text
        }

        Label { text: "Start-up:" }
        ComboBox95 {
            id: bootCombo
            width: Math.max(150, Math.min(220, (win.width - 420) / 2 + 150))
            popupMenu: popup
            model: SystemLog.boots.length ? SystemLog.boots.map(win.bootName) : ["This start-up"]
            currentIndex: {
                for (var i = 0; i < SystemLog.boots.length; ++i)
                    if (SystemLog.boots[i].index === win.boot)
                        return i;
                return 0;
            }
            onActivated: (i) => {
                win.boot = SystemLog.boots.length ? SystemLog.boots[i].index : 0;
                win.reload();
            }
        }
        Label { text: "Show:" }
        ComboBox95 {
            width: 150
            popupMenu: popup
            model: win.levels.map(function (l) { return l.name; })
            currentIndex: {
                for (var i = 0; i < win.levels.length; ++i)
                    if (win.levels[i].priority === win.priority)
                        return i;
                return 3;
            }
            onActivated: (i) => {
                win.priority = win.levels[i].priority;
                win.reload();
            }
        }
    }
    Row {
        anchors.right: parent.right
        anchors.rightMargin: 6
        y: toolbar.y
        spacing: 6
        Text {
            anchors.verticalCenter: parent.verticalCenter
            text: "<u>F</u>ind:"
            textFormat: Text.StyledText
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontSize
            color: Theme.text
        }
        TextField95 {
            id: findField
            width: Math.max(80, win.width - toolbar.width - 150)
            onAccepted: list.forceActiveFocus()
            Keys.onDownPressed: { list.forceActiveFocus(); win.select(win.currentIndex + 1); }
        }
        Button95 {
            width: 64
            text: "<u>R</u>efresh"
            enabled: !SystemLog.loading
            onClicked: win.reload()
        }
    }

    // --- only your own messages: offer the rest
    Rectangle {
        id: notice
        anchors.top: toolbar.bottom
        anchors.topMargin: 5
        x: 2
        width: parent.width - 4
        height: visible ? 26 : 0
        visible: SystemLog.partial && win.log !== "user"
        color: "#ffffe1"
        border.color: Theme.dark
        Icon95 { x: 5; anchors.verticalCenter: parent.verticalCenter; name: "info" }
        Text {
            x: 26
            width: parent.width - x - showAllButton.width - 12
            anchors.verticalCenter: parent.verticalCenter
            elide: Text.ElideRight
            text: win.log === "kernel" ? "The kernel's messages can only be seen by an administrator."
                                       : "Only your own messages are shown. The rest need an administrator's password."
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontSize
            color: "#000000"
        }
        Button95 {
            id: showAllButton
            anchors.right: parent.right
            anchors.rightMargin: 3
            anchors.verticalCenter: parent.verticalCenter
            width: 110
            height: 20
            text: "Show <u>A</u>ll Messages..."
            enabled: !SystemLog.loading
            onClicked: win.showAll()
        }
    }

    // --- the messages
    readonly property var columns: [["Type", 92], ["Date", 74], ["Time", 72], ["Source", 130], ["Message", Math.max(200, well.width - 4 - 368)]]

    Bevel {
        id: well
        anchors.top: notice.visible ? notice.bottom : toolbar.bottom
        anchors.topMargin: notice.visible ? 3 : 5
        x: 2
        width: parent.width - 4
        height: Math.max(60, splitter.y - y)
        style: "sunken"
        face: Theme.window

        Row {
            id: header
            x: 2
            y: 2
            Repeater {
                model: win.columns
                Item {
                    required property var modelData
                    required property int index
                    width: index === win.columns.length - 1 ? well.width - 4 - header.x - 368 : modelData[1]
                    height: 18
                    Bevel { anchors.fill: parent; style: "raised" }
                    Text {
                        x: 5
                        width: parent.width - 8
                        elide: Text.ElideRight
                        anchors.verticalCenter: parent.verticalCenter
                        text: parent.modelData[0]
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.fontSize
                        color: Theme.text
                    }
                }
            }
        }

        ListView {
            id: list
            x: 2
            y: header.y + 18
            width: parent.width - 4 - (listBar.visible ? listBar.width : 0)
            height: parent.height - y - 2
            clip: true
            interactive: false
            focus: true
            activeFocusOnTab: true
            model: win.shown
            reuseItems: true
            boundsBehavior: Flickable.StopAtBounds

            readonly property int page: Math.max(1, Math.floor(height / 18) - 1)
            Keys.onUpPressed: win.select(win.currentIndex - 1)
            Keys.onDownPressed: win.select(win.currentIndex + 1)
            Keys.onPressed: (event) => {
                switch (event.key) {
                case Qt.Key_PageUp: win.select(win.currentIndex - page); break;
                case Qt.Key_PageDown: win.select(win.currentIndex + page); break;
                case Qt.Key_Home: win.select(0); break;
                case Qt.Key_End: win.select(win.shown.length - 1); break;
                case Qt.Key_C:
                    if (!(event.modifiers & Qt.ControlModifier) || !win.entry)
                        return;
                    SystemLog.copy(SystemLog.line(win.current));
                    break;
                case Qt.Key_Menu:
                    if (win.entry) {
                        var p = list.mapToItem(win.contentItem, 20, (win.currentIndex + 1) * 18 - list.contentY);
                        popup.popup(p.x, p.y, win.entryMenu);
                    }
                    break;
                default:
                    return;
                }
                event.accepted = true;
            }

            delegate: Item {
                id: row
                required property var modelData
                required property int index
                readonly property bool isSelected: modelData.key === win.current
                readonly property bool lit: isSelected && list.activeFocus
                width: list.width
                height: 18

                Rectangle {
                    anchors.fill: parent
                    color: row.lit ? Theme.selection : row.isSelected ? Theme.face : "transparent"
                }
                Icon95 {
                    x: 4
                    y: 1
                    name: win.icon(row.modelData.priority)
                }
                Row {
                    Repeater {
                        model: [
                            win.typeName(row.modelData.priority),
                            Qt.formatDate(new Date(row.modelData.time), "M/d/yyyy"),
                            Qt.formatTime(new Date(row.modelData.time), "h:mm:ss AP"),
                            row.modelData.source || row.modelData.unit,
                            row.modelData.message.replace(/\s*\n\s*/g, " "),
                        ]
                        Text {
                            required property var modelData
                            required property int index
                            width: index === 4 ? Math.max(0, row.width - 368) : win.columns[index][1]
                            height: 18
                            leftPadding: index === 0 ? 24 : 4
                            rightPadding: 4
                            verticalAlignment: Text.AlignVCenter
                            elide: Text.ElideRight
                            text: modelData
                            textFormat: Text.PlainText
                            color: row.lit ? Theme.selectionText : Theme.text
                            font.family: Theme.fontFamily
                            font.pixelSize: Theme.fontSize
                        }
                    }
                }
                MouseArea {
                    anchors.fill: parent
                    acceptedButtons: Qt.LeftButton | Qt.RightButton
                    onPressed: (mouse) => {
                        list.forceActiveFocus();
                        win.current = row.modelData.key;
                        if (mouse.button === Qt.RightButton) {
                            var p = mapToItem(win.contentItem, mouse.x, mouse.y);
                            popup.popup(p.x, p.y, win.entryMenu);
                        }
                    }
                }
            }

            WheelHandler {
                acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad
                onWheel: (event) => listBar.scrollBy(-event.angleDelta.y / 120 * 54)
            }
        }
        ScrollBar95 {
            id: listBar
            anchors.right: parent.right
            anchors.rightMargin: 2
            y: list.y
            height: list.height
            flickable: list
            stepSize: 18
        }

        Text {
            anchors.centerIn: list
            visible: win.shown.length === 0
            text: SystemLog.loading ? "Reading the log..."
                  : win.query ? "No messages match “" + findField.text.trim() + "”."
                  : "There are no messages to show."
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontSize
            color: Theme.disabledText
        }
    }

    // --- drag to share the room between the list and the details
    Item {
        id: splitter
        x: 0
        width: parent.width
        height: 6
        // the share of the window above it, so resizing keeps the split
        property real share: 0.62
        y: Math.round(Math.max(well.y + 80, Math.min(statusBar.y - 70, win.height * share)))
        MouseArea {
            anchors.fill: parent
            cursorShape: Qt.SizeVerCursor
            onPositionChanged: (mouse) => {
                if (pressed)
                    splitter.share = mapToItem(win.contentItem, 0, mouse.y).y / win.height;
            }
        }
    }

    // --- the selected message
    Bevel {
        id: details
        anchors.top: splitter.bottom
        anchors.bottom: statusBar.top
        anchors.bottomMargin: 2
        x: 2
        width: parent.width - 4
        style: "sunken"
        face: Theme.window

        Flickable {
            id: detailFlick
            x: 3
            y: 3
            width: parent.width - 6 - detailBar.width
            height: parent.height - 6
            clip: true
            contentWidth: width
            contentHeight: detailCol.height
            interactive: false
            boundsBehavior: Flickable.StopAtBounds

            Column {
                id: detailCol
                width: detailFlick.width - 4
                x: 2
                spacing: 6
                TextEdit {
                    width: parent.width
                    readOnly: true
                    selectByMouse: true
                    wrapMode: TextEdit.Wrap
                    textFormat: TextEdit.PlainText
                    text: win.entry ? win.typeName(win.entry.priority) + "  •  "
                                      + Qt.formatDateTime(new Date(win.entry.time), "dddd, MMMM d, yyyy h:mm:ss AP")
                                      + "  •  " + (win.entry.source || "?")
                                      + (win.entry.pid ? " (process " + win.entry.pid + ")" : "")
                                      + (win.entry.unit ? "  •  " + win.entry.unit : "")
                                    : ""
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontSize
                    font.bold: true
                    color: Theme.text
                    selectionColor: Theme.selection
                    selectedTextColor: Theme.selectionText
                }
                TextEdit {
                    width: parent.width
                    readOnly: true
                    selectByMouse: true
                    wrapMode: TextEdit.WrapAtWordBoundaryOrAnywhere
                    textFormat: TextEdit.PlainText
                    text: win.entry ? win.entry.message : ""
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontSize
                    color: Theme.text
                    selectionColor: Theme.selection
                    selectedTextColor: Theme.selectionText
                }
                TextEdit {
                    width: parent.width
                    readOnly: true
                    selectByMouse: true
                    wrapMode: TextEdit.WrapAnywhere
                    textFormat: TextEdit.PlainText
                    text: win.entry ? SystemLog.fields(win.current) : ""
                    font.family: Theme.monoFamily
                    font.pixelSize: Theme.fontSize - 1
                    color: Theme.shadow
                    selectionColor: Theme.selection
                    selectedTextColor: Theme.selectionText
                }
            }
            WheelHandler {
                acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad
                onWheel: (event) => detailBar.scrollBy(-event.angleDelta.y / 120 * 48)
            }
        }
        onVisibleChanged: detailBar.scrollTo(0)
        ScrollBar95 {
            id: detailBar
            anchors.right: parent.right
            anchors.rightMargin: 2
            y: 2
            height: parent.height - 4
            flickable: detailFlick
            stepSize: 16
        }
    }
    onCurrentChanged: detailBar.scrollTo(0)

    Bevel {
        id: statusBar
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        height: 20
        style: "thinSunken"
        Text {
            x: 4
            width: parent.width - 24
            anchors.verticalCenter: parent.verticalCenter
            elide: Text.ElideRight
            text: {
                if (SystemLog.loading)
                    return SystemLog.admin && win.log !== "user" ? "Waiting for the administrator's password..." : "Reading the log...";
                var n = SystemLog.entries.length;
                var s = win.query ? win.shown.length + " of " + n + " event(s)" : n + " event(s)";
                if (n >= 2000)
                    s += " (the newest 2000)";
                if (SystemLog.admin && win.log !== "user")
                    s += ", all users";
                return s;
            }
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontSize
            color: Theme.text
        }
        SizeGrip95 {
            anchors.right: parent.right
            anchors.rightMargin: 1
            anchors.bottom: parent.bottom
            anchors.bottomMargin: 1
        }
    }

    FileDialog95 {
        id: saveDialog
        mode: "save"
        messageBox: msgBox
        popupMenu: popup
        onAccepted: (path) => {
            var err = SystemLog.save(path, win.shown.map(function (e) { return e.key; }));
            if (err)
                msgBox.show("System Log", "Couldn't save " + path + ":\n" + err, "error", ["OK"]);
        }
    }

    Menu95 { id: popup }
    MessageBox95 { id: msgBox }
}
