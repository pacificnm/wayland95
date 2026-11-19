import QtQuick
import QtQuick.Window
import Win95Ui

// System Monitor: live charts of the processor, memory, network and disks,
// and the running programs, with End Process. Everything comes from the
// kernel's /proc files through the Monitor singleton.
Window {
    id: window

    title: "System Monitor"
    width: 600
    height: 480
    minimumWidth: 440
    minimumHeight: 360
    visible: true
    color: Theme.face

    // ------------------------------------------------------------ helpers
    function bytes(n) {
        if (n >= 1073741824) return (n / 1073741824).toFixed(1) + " GB";
        if (n >= 1048576) return (n / 1048576).toFixed(1) + " MB";
        if (n >= 1024) return (n / 1024).toFixed(0) + " KB";
        return Math.round(n) + " bytes";
    }
    function rate(n) {
        if (n >= 1048576) return (n / 1048576).toFixed(1) + " MB/s";
        if (n >= 1024) return (n / 1024).toFixed(0) + " KB/s";
        return Math.round(n) + " B/s";
    }
    function duration(s) {
        var d = Math.floor(s / 86400), h = Math.floor(s % 86400 / 3600), m = Math.floor(s % 3600 / 60);
        return (d > 0 ? d + "d " : "") + h + "h " + m + "m";
    }
    function colored(color, text) { return "<font color=\"" + color + "\">" + text + "</font>"; }

    readonly property var now: Monitor.now
    readonly property var history: Monitor.history
    function values(key) { return history[key] || []; }

    readonly property color cpuColor: "#008000"
    readonly property color memColor: "#0000c0"
    readonly property color swapColor: "#a000a0"
    readonly property color inColor: "#008000"
    readonly property color outColor: "#c00000"

    // -------------------------------------------------------------- menus
    function setSpeed(ms) { Monitor.interval = ms; }
    function fileMenu() { return [{ text: "Exit", action: window.close }]; }
    function viewMenu() {
        return [
            { text: "Refresh Now", shortcut: "F5", action: Monitor.refresh },
            { separator: true },
            { text: "Update Speed: High (½ second)", checked: Monitor.interval === 500, action: function () { setSpeed(500); } },
            { text: "Update Speed: Normal (1 second)", checked: Monitor.interval === 1000, action: function () { setSpeed(1000); } },
            { text: "Update Speed: Low (5 seconds)", checked: Monitor.interval === 5000, action: function () { setSpeed(5000); } },
            { text: "Paused", checked: Monitor.interval === 0, action: function () { setSpeed(0); } },
        ];
    }
    function processMenu() {
        var p = processes.selected;
        return [
            { text: "End Process", shortcut: "Del", enabled: !!p, action: function () { processes.confirmEnd(false); } },
            { text: "End Process Now (force)", enabled: !!p, action: function () { processes.confirmEnd(true); } },
        ];
    }
    function helpMenu() {
        return [{ text: "About System Monitor", action: function () {
            msgBox.show("About System Monitor",
                        "System Monitor\n\nShows how hard your computer is working and which programs "
                        + "are running. Part of Wayland 95.", "info", ["OK"], null);
        } }];
    }

    Shortcut { sequence: "F5"; onActivated: Monitor.refresh() }

    MenuBar95 {
        id: menuBar
        x: 2
        y: 2
        width: parent.width - 4
        height: 20
        popupMenu: popup
        menus: [
            { title: "File", items: window.fileMenu },
            { title: "View", items: window.viewMenu },
            { title: "Process", items: window.processMenu },
            { title: "Help", items: window.helpMenu },
        ]
    }

    TabWidget95 {
        id: tabs
        x: 6
        y: menuBar.y + menuBar.height + 6
        width: parent.width - 12
        height: statusBar.y - y - 6
        titles: ["Performance", "Processes"]
        onCurrentIndexChanged: if (currentIndex === 1) processes.forceActiveFocus()

        // -------------------------------------------------- Performance
        Item {
            id: perf
            readonly property real chartHeight: (height - cores.height - 18) / 2
            readonly property real chartWidth: (width - 10) / 2

            Chart95 {
                id: cpuChart
                width: perf.chartWidth
                height: perf.chartHeight
                title: "Processor Usage"
                valueText: window.colored(window.cpuColor, Math.round(window.now.cpu || 0) + "%")
                series: [{ values: window.values("cpu"), color: window.cpuColor }]
            }
            Chart95 {
                x: perf.chartWidth + 10
                width: perf.chartWidth
                height: perf.chartHeight
                title: "Memory in Use"
                valueText: window.colored(window.memColor, window.bytes(window.now.memUsed || 0))
                           + " of " + window.bytes(window.now.memTotal || 0)
                           + ((window.now.swapTotal || 0) > 0
                              ? "  " + window.colored(window.swapColor, "swap " + window.bytes(window.now.swapUsed || 0)) : "")
                series: [{ values: window.values("swap"), color: window.swapColor },
                         { values: window.values("mem"), color: window.memColor }]
            }
            Chart95 {
                y: perf.chartHeight + 8
                width: perf.chartWidth
                height: perf.chartHeight
                title: "Network"
                maxValue: 0
                formatScale: window.rate
                valueText: window.colored(window.inColor, "In " + window.rate(window.now.netRx || 0))
                           + "  " + window.colored(window.outColor, "Out " + window.rate(window.now.netTx || 0))
                series: [{ values: window.values("netRx"), color: window.inColor },
                         { values: window.values("netTx"), color: window.outColor }]
            }
            Chart95 {
                x: perf.chartWidth + 10
                y: perf.chartHeight + 8
                width: perf.chartWidth
                height: perf.chartHeight
                title: "Disk"
                maxValue: 0
                formatScale: window.rate
                valueText: window.colored(window.inColor, "Read " + window.rate(window.now.diskRead || 0))
                           + "  " + window.colored(window.outColor, "Write " + window.rate(window.now.diskWrite || 0))
                series: [{ values: window.values("diskRead"), color: window.inColor },
                         { values: window.values("diskWrite"), color: window.outColor }]
            }

            // one small meter per processor core
            Item {
                id: cores
                anchors.bottom: parent.bottom
                width: parent.width
                height: 44
                Text {
                    id: coresLabel
                    text: "Each processor core:"
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontSize
                }
                Row {
                    y: coresLabel.height + 3
                    spacing: 3
                    Repeater {
                        model: window.now.cores || []
                        Bevel {
                            required property real modelData
                            width: Math.max(8, Math.min(30, (cores.width - Monitor.coreCount * 3) / Monitor.coreCount))
                            height: 22
                            style: "sunken"
                            face: "#000000"
                            Rectangle {
                                x: 2
                                width: parent.width - 4
                                anchors.bottom: parent.bottom
                                anchors.bottomMargin: 2
                                height: (parent.height - 4) * modelData / 100
                                color: "#00ff00"
                            }
                        }
                    }
                }
            }
        }

        // ---------------------------------------------------- Processes
        FocusScope {
            id: processes

            property bool allUsers: true
            property string sortKey: "cpu"
            property bool sortDescending: true
            property int selectedPid: -1
            readonly property var rows: {
                var list = Monitor.processes.filter(function (p) { return allUsers || p.mine; });
                var key = sortKey, desc = sortDescending;
                list.sort(function (a, b) {
                    var x = a[key], y = b[key];
                    var c = typeof x === "string" ? x.localeCompare(y, undefined, { sensitivity: "base" }) : x - y;
                    if (c === 0)
                        c = a.pid - b.pid;
                    return desc ? -c : c;
                });
                return list;
            }
            readonly property var selected: {
                for (var i = 0; i < rows.length; ++i)
                    if (rows[i].pid === selectedPid)
                        return rows[i];
                return null;
            }
            readonly property var columns: [
                { key: "name", title: "Image Name", width: 0.34 },
                { key: "pid", title: "PID", width: 0.11 },
                { key: "user", title: "User Name", width: 0.2 },
                { key: "cpu", title: "CPU", width: 0.12 },
                { key: "memory", title: "Memory", width: 0.23 },
            ]
            function sortBy(key) {
                if (sortKey === key) {
                    sortDescending = !sortDescending;
                } else {
                    sortKey = key;
                    // numbers biggest-first, names A-Z
                    sortDescending = key === "cpu" || key === "memory";
                }
            }
            function confirmEnd(force) {
                var p = selected;
                if (!p)
                    return;
                msgBox.show("System Monitor",
                            "WARNING: Ending a process can make it lose any unsaved work.\n\n"
                            + "Are you sure you want to end '" + p.name + "' (" + p.pid + ")"
                            + (force ? " immediately, without letting it close itself?" : "?"),
                            "exclamation", ["Yes", "No"], function (i) {
                                if (i !== 0)
                                    return;
                                var error = Monitor.endProcess(p.pid, force);
                                if (error.length > 0)
                                    msgBox.show("System Monitor", "Unable to end '" + p.name + "'.\n\n" + error,
                                                "error", ["OK"], null);
                            });
            }

            Keys.onDeletePressed: confirmEnd(false)
            Keys.onUpPressed: move(-1)
            Keys.onDownPressed: move(1)
            function move(d) {
                var i = -1;
                for (var k = 0; k < rows.length; ++k)
                    if (rows[k].pid === selectedPid)
                        i = k;
                i = Math.max(0, Math.min(rows.length - 1, i + d));
                if (rows.length > 0) {
                    selectedPid = rows[i].pid;
                    list.positionViewAtIndex(i, ListView.Contain);
                }
            }

            Bevel {
                id: listBox
                width: parent.width
                height: parent.height - buttons.height - 8
                style: "sunken"
                face: Theme.window

                // column headers: click to sort
                Row {
                    id: header
                    x: 2
                    y: 2
                    Repeater {
                        model: processes.columns
                        Item {
                            required property var modelData
                            width: (listBox.width - 4 - listBar.width) * modelData.width
                            height: 18
                            Bevel { anchors.fill: parent; style: headerMouse.pressed ? "pressed" : "raised" }
                            Text {
                                x: 4
                                width: parent.width - 8
                                anchors.verticalCenter: parent.verticalCenter
                                elide: Text.ElideRight
                                horizontalAlignment: modelData.key === "name" || modelData.key === "user"
                                                     ? Text.AlignLeft : Text.AlignRight
                                text: modelData.title + (processes.sortKey === modelData.key
                                                         ? (processes.sortDescending ? " ▼" : " ▲") : "")
                                font.family: Theme.fontFamily
                                font.pixelSize: Theme.fontSize
                            }
                            MouseArea {
                                id: headerMouse
                                anchors.fill: parent
                                onClicked: processes.sortBy(modelData.key)
                            }
                        }
                    }
                }

                ListView {
                    id: list
                    x: 2
                    y: header.y + header.height
                    width: parent.width - 4 - listBar.width
                    height: parent.height - y - 2
                    clip: true
                    interactive: false
                    model: processes.rows
                    delegate: Item {
                        id: row
                        required property var modelData
                        readonly property bool isSelected: modelData.pid === processes.selectedPid
                        width: list.width
                        height: 17
                        Rectangle {
                            anchors.fill: parent
                            color: row.isSelected ? Theme.selection : "transparent"
                        }
                        Row {
                            Repeater {
                                model: processes.columns
                                Text {
                                    required property var modelData
                                    width: list.width * modelData.width
                                    height: 17
                                    leftPadding: 4
                                    rightPadding: 4
                                    verticalAlignment: Text.AlignVCenter
                                    elide: Text.ElideRight
                                    horizontalAlignment: modelData.key === "name" || modelData.key === "user"
                                                         ? Text.AlignLeft : Text.AlignRight
                                    color: row.isSelected ? Theme.selectionText : Theme.text
                                    font.family: Theme.fontFamily
                                    font.pixelSize: Theme.fontSize
                                    text: {
                                        var p = row.modelData;
                                        switch (modelData.key) {
                                        case "cpu": return p.cpu < 0.5 ? (p.cpu > 0 ? "<1%" : "0%") : Math.round(p.cpu) + "%";
                                        case "memory": return window.bytes(p.memory);
                                        default: return String(p[modelData.key]);
                                        }
                                    }
                                }
                            }
                        }
                        MouseArea {
                            anchors.fill: parent
                            acceptedButtons: Qt.LeftButton | Qt.RightButton
                            onPressed: (mouse) => {
                                processes.forceActiveFocus();
                                processes.selectedPid = row.modelData.pid;
                                if (mouse.button === Qt.RightButton) {
                                    var p = mapToItem(popup, mouse.x, mouse.y);
                                    popup.popup(p.x, p.y, window.processMenu());
                                }
                            }
                        }
                    }
                    WheelHandler { acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad; onWheel: (event) => listBar.scrollBy(-event.angleDelta.y / 120 * 51) }
                }
                ScrollBar95 {
                    id: listBar
                    anchors.right: parent.right
                    anchors.rightMargin: 2
                    y: 2
                    height: parent.height - 4
                    flickable: list
                    stepSize: 17
                }
            }

            Item {
                id: buttons
                anchors.bottom: parent.bottom
                width: parent.width
                height: 23
                CheckBox95 {
                    anchors.verticalCenter: parent.verticalCenter
                    text: "<u>S</u>how processes from all users"
                    checked: processes.allUsers
                    onToggled: (on) => processes.allUsers = on
                }
                Button95 {
                    anchors.right: parent.right
                    text: "<u>E</u>nd Process"
                    enabled: !!processes.selected
                    onClicked: processes.confirmEnd(false)
                }
            }
        }
    }

    // only gather the process list while it's on screen
    Binding {
        target: Monitor
        property: "processesWanted"
        value: tabs.currentIndex === 1 && window.visible
    }

    // ---------------------------------------------------------- status bar
    Row {
        id: statusBar
        x: 2
        anchors.bottom: parent.bottom
        anchors.bottomMargin: 2
        width: parent.width - 4
        height: 20
        spacing: 2
        Repeater {
            model: [
                { text: "Processes: " + (window.now.processes || 0), share: 0.22 },
                { text: "CPU Usage: " + Math.round(window.now.cpu || 0) + "%", share: 0.2 },
                { text: "Load: " + (window.now.load || ""), share: 0.28 },
                { text: "Up: " + window.duration(window.now.uptime || 0), share: 0.3 },
            ]
            Bevel {
                required property var modelData
                width: (statusBar.width - 6) * modelData.share
                height: 20
                style: "thinSunken"
                Text {
                    x: 4
                    width: parent.width - 8
                    anchors.verticalCenter: parent.verticalCenter
                    elide: Text.ElideRight
                    text: parent.modelData.text
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontSize
                }
            }
        }
    }

    SizeGrip95 {
        anchors.right: statusBar.right
        anchors.rightMargin: 1
        anchors.bottom: statusBar.bottom
        anchors.bottomMargin: 1
    }

    // window-level overlays (last, so they stack on top)
    Menu95 { id: popup }
    MessageBox95 { id: msgBox }
}
