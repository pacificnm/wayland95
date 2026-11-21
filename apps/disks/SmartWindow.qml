import QtQuick
import QtQuick.Window
import Win95Ui

// A disk's S.M.A.R.T. report from smartctl (Disks.readSmart asks for the
// administrator password): its health in plain words, the disk's own
// measurements, and the self-tests it can run on itself.
Window {
    id: win

    property var drive: null
    property var report: ({})
    property bool loading: false
    readonly property bool ok: !!report.ok
    readonly property bool ata: report.protocol === "ATA"

    title: (drive ? drive.name : "Disk") + " - S.M.A.R.T. Status"
    width: 560
    height: 470
    minimumWidth: 460
    minimumHeight: 400
    color: Theme.face

    function open(d) {
        drive = d;
        report = ({});
        tabs.currentIndex = 0;
        visible = true;
        requestActivate();
        reload();
    }
    function reload() {
        if (!drive)
            return;
        loading = true;
        Disks.readSmart(drive.device);
    }

    Connections {
        target: Disks
        function onSmartReady(device, r) {
            if (!win.drive || device !== win.drive.device)
                return;
            win.loading = false;
            // the password box's Cancel, with a report already showing: keep it
            if (r.cancelled && win.ok)
                return;
            win.report = r;
        }
        function onSelfTestStarted(device, ok, message) {
            if (!win.drive || device !== win.drive.device)
                return;
            if (message)
                msgBox.show("S.M.A.R.T. Self-Test", message, ok ? "info" : "error", ["OK"], null);
            if (ok)
                win.reload();
        }
    }

    function bytes(n) {
        if (n >= 1e12) return (n / 1e12).toFixed(2) + " TB";
        if (n >= 1e9) return (n / 1e9).toFixed(1) + " GB";
        if (n >= 1e6) return (n / 1e6).toFixed(0) + " MB";
        return n + " bytes";
    }
    function hours(h) {
        if (h <= 0) return "";
        var text = Number(h).toLocaleString(Qt.locale(), "f", 0) + " hours";
        if (h >= 24 * 365) return text + " (" + (h / 24 / 365).toFixed(1) + " years)";
        if (h >= 48) return text + " (" + Math.round(h / 24) + " days)";
        return text;
    }
    function minutes(m) { return m > 0 ? "about " + m + (m === 1 ? " minute" : " minutes") : ""; }

    TabWidget95 {
        id: tabs
        x: 8
        y: 8
        width: parent.width - 16
        height: parent.height - 50
        titles: ["General", "Attributes", "Self-Tests"]

        // ---- General
        Item {
            // the verdict, big
            Canvas {
                id: verdictIcon
                x: 14
                y: 14
                width: 32
                height: 32
                readonly property string health: win.ok ? win.report.health : ""
                onHealthChanged: requestPaint()
                onPaint: {
                    var ctx = getContext("2d");
                    ctx.reset();
                    if (win.loading || !win.ok)
                        return;
                    if (health === "warning") {
                        ctx.fillStyle = "#000000";
                        ctx.beginPath(); ctx.moveTo(16, 1); ctx.lineTo(31, 30); ctx.lineTo(1, 30); ctx.closePath(); ctx.fill();
                        ctx.fillStyle = "#ffff00";
                        ctx.beginPath(); ctx.moveTo(16, 4); ctx.lineTo(28.5, 28.5); ctx.lineTo(3.5, 28.5); ctx.closePath(); ctx.fill();
                        ctx.fillStyle = "#000000";
                        ctx.fillRect(14.5, 11, 3, 10);
                        ctx.fillRect(14.5, 23, 3, 3);
                        return;
                    }
                    ctx.fillStyle = "#000000";
                    ctx.beginPath(); ctx.arc(16, 16, 15, 0, Math.PI * 2); ctx.fill();
                    ctx.fillStyle = health === "failing" ? "#ff0000" : health === "ok" ? "#00a000" : "#808080";
                    ctx.beginPath(); ctx.arc(16, 16, 14, 0, Math.PI * 2); ctx.fill();
                    ctx.strokeStyle = "#ffffff";
                    ctx.lineWidth = 3.5;
                    ctx.beginPath();
                    if (health === "failing") {
                        ctx.moveTo(10, 10); ctx.lineTo(22, 22); ctx.moveTo(22, 10); ctx.lineTo(10, 22);
                    } else if (health === "ok") {
                        ctx.moveTo(9, 16); ctx.lineTo(14, 21); ctx.lineTo(23, 11);
                    } else {
                        ctx.moveTo(16, 9); ctx.lineTo(16, 18); ctx.moveTo(16, 21); ctx.lineTo(16, 24);
                    }
                    ctx.stroke();
                }
                Connections {
                    target: win
                    function onLoadingChanged() { verdictIcon.requestPaint(); }
                    function onReportChanged() { verdictIcon.requestPaint(); }
                }
            }
            Text {
                id: verdict
                x: 58
                y: 14
                width: parent.width - x - 14
                wrapMode: Text.Wrap
                font.bold: true
                color: win.ok && win.report.health === "failing" ? "#c00000" : Theme.text
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontSize + 2
                text: win.loading ? "Reading the disk's health report..."
                    : !win.ok ? (win.report.error ? "The health report couldn't be read." : "")
                    : !win.report.smartAvailable ? "This disk doesn't report its health (no S.M.A.R.T.)."
                    : !win.report.smartEnabled ? "S.M.A.R.T. is switched off on this disk."
                    : win.report.health === "failing" ? "This disk is failing. Back up what's on it now!"
                    : win.report.health === "warning" ? "This disk is working, but has signs of wear."
                    : win.report.health === "ok" ? "This disk is healthy."
                    : "This disk's health isn't known."
            }
            Text {
                id: details
                x: 58
                y: verdict.y + verdict.height + 6
                width: parent.width - x - 14
                wrapMode: Text.Wrap
                color: Theme.text
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontSize
                text: win.loading ? "Disk Manager needs the administrator password to read it."
                    : !win.ok ? (win.report.error || "")
                    : (win.report.problems && win.report.problems.length ? "• " + win.report.problems.join("\n• ")
                       : win.report.health === "ok" ? "Nothing in its report is worrying." : "")
                      + (win.report.errorLogNote ? "\n\n" + win.report.errorLogNote : "")
            }

            GroupBox95 {
                id: infoBox
                visible: win.ok
                title: "Disk"
                x: 10
                y: Math.max(64, details.y + details.height + 10)
                width: parent.width - 20
                height: infoGrid.height + 30

                Grid {
                    id: infoGrid
                    columns: 2
                    columnSpacing: 12
                    rowSpacing: 3
                    width: parent.width
                    Repeater {
                        model: {
                            var r = win.report;
                            var rows = [
                                ["Model:", r.model || (win.drive ? win.drive.name : "")],
                                ["Family:", r.family || ""],
                                ["Serial number:", r.serial || ""],
                                ["Firmware:", r.firmware || ""],
                                ["Capacity:", r.capacity > 0 ? bytes(r.capacity) : ""],
                                ["Type:", r.protocol === "NVMe" ? "NVMe solid-state drive"
                                          : r.rpm === 0 ? "Solid-state drive (" + r.protocol + ")"
                                          : r.rpm > 0 ? "Hard disk, " + r.rpm + " rpm (" + r.protocol + ")" : (r.protocol || "")],
                                ["Connection:", r.interfaceSpeed || ""],
                                ["Temperature:", r.temperature > 0 ? r.temperature + " °C" : ""],
                                ["Powered on:", hours(r.powerOnHours || 0)],
                                ["Times started:", r.powerCycles > 0 ? Number(r.powerCycles).toLocaleString(Qt.locale(), "f", 0) : ""],
                            ];
                            var out = [];
                            for (var i = 0; i < rows.length; ++i)
                                if (rows[i][1])
                                    out.push(rows[i][0], rows[i][1]);
                            return out;
                        }
                        Text {
                            required property string modelData
                            required property int index
                            width: index % 2 === 0 ? 100 : infoGrid.width - 112
                            elide: Text.ElideRight
                            text: modelData
                            color: Theme.text
                            font.family: Theme.fontFamily
                            font.pixelSize: Theme.fontSize
                        }
                    }
                }
            }
        }

        // ---- Attributes
        Item {
            Bevel {
                id: attrWell
                x: 8
                y: 8
                width: parent.width - 16
                height: parent.height - 16
                style: "sunken"
                face: Theme.window

                readonly property var columns: win.ata
                    ? [{ title: "ID", w: 32, right: true }, { title: "Attribute", w: Math.max(120, attrList.width - 32 - 46 * 3 - 110 - 66 - 92) },
                       { title: "Value", w: 46, right: true }, { title: "Worst", w: 46, right: true }, { title: "Thresh", w: 46, right: true },
                       { title: "Raw", w: 110, right: true }, { title: "Type", w: 66 }, { title: "Status", w: 92 }]
                    : [{ title: "Attribute", w: Math.max(160, attrList.width - 180) }, { title: "Value", w: 180, right: true }]

                Row {
                    id: attrHeader
                    x: 2
                    y: 2
                    clip: true
                    width: parent.width - 4
                    height: 18
                    Repeater {
                        model: attrWell.columns
                        Item {
                            required property var modelData
                            width: modelData.w
                            height: 18
                            Bevel { anchors.fill: parent; style: "raised" }
                            Text {
                                x: 4
                                width: parent.width - 8
                                height: parent.height
                                verticalAlignment: Text.AlignVCenter
                                horizontalAlignment: parent.modelData.right ? Text.AlignRight : Text.AlignLeft
                                elide: Text.ElideRight
                                text: parent.modelData.title
                                color: Theme.text
                                font.family: Theme.fontFamily
                                font.pixelSize: Theme.fontSize
                            }
                        }
                    }
                }
                ListView {
                    id: attrList
                    x: 2
                    y: attrHeader.y + attrHeader.height
                    width: parent.width - 4 - (attrBar.visible ? attrBar.width : 0)
                    height: parent.height - y - 2
                    clip: true
                    interactive: false
                    boundsBehavior: Flickable.StopAtBounds
                    model: win.ok ? win.report.attributes : []
                    delegate: Row {
                        id: attrRow
                        required property var modelData
                        height: 18
                        readonly property var cells: win.ata
                            ? [modelData.id, modelData.name, modelData.value, modelData.worst, modelData.thresh, modelData.raw,
                               modelData.type, modelData.status]
                            : [modelData.name, modelData.raw]
                        Repeater {
                            model: attrWell.columns
                            Text {
                                required property var modelData
                                required property int index
                                width: modelData.w
                                height: 18
                                leftPadding: 4
                                rightPadding: 4
                                verticalAlignment: Text.AlignVCenter
                                horizontalAlignment: modelData.right ? Text.AlignRight : Text.AlignLeft
                                elide: Text.ElideRight
                                text: String(attrRow.cells[index])
                                color: attrRow.modelData.bad ? "#c00000" : Theme.text
                                font.bold: attrRow.modelData.bad
                                font.family: Theme.fontFamily
                                font.pixelSize: Theme.fontSize
                            }
                        }
                    }
                    WheelHandler {
                        onWheel: (event) => {
                            attrList.contentY = Math.max(0, Math.min(attrList.contentHeight - attrList.height,
                                                                     attrList.contentY - event.angleDelta.y / 120 * 54));
                        }
                    }
                }
                ScrollBar95 {
                    id: attrBar
                    anchors.right: parent.right
                    anchors.rightMargin: 2
                    y: attrList.y
                    height: attrList.height
                    flickable: attrList
                    stepSize: 18
                }
                Text {
                    visible: !win.loading && (!win.ok || !win.report.attributes || win.report.attributes.length === 0)
                    anchors.centerIn: parent
                    width: parent.width - 40
                    horizontalAlignment: Text.AlignHCenter
                    wrapMode: Text.Wrap
                    text: "This disk reports no measurements."
                    color: Theme.shadow
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontSize
                }
            }
        }

        // ---- Self-tests
        Item {
            Text {
                id: testIntro
                x: 12
                y: 12
                width: parent.width - 24
                wrapMode: Text.Wrap
                color: Theme.text
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontSize
                text: !win.ok ? ""
                    : !win.report.canSelfTest ? "This disk can't test itself."
                    : win.report.testRunning ? "A test is running: " + win.report.testRemaining + "% still to go. "
                                               + "Press Refresh to see how it's going."
                    : "The disk can check itself while you keep working. The short test checks the parts most likely "
                      + "to fail; the extended test reads the whole disk."
            }
            ProgressBar95 {
                id: testProgress
                visible: win.ok && win.report.testRunning
                x: 12
                y: testIntro.y + testIntro.height + 8
                width: parent.width - 24
                height: 20
                value: win.ok ? Math.max(0, 100 - win.report.testRemaining) : 0
            }
            Row {
                id: testButtons
                x: 12
                y: testIntro.y + testIntro.height + (testProgress.visible ? 36 : 10)
                spacing: 6
                Button95 {
                    width: 150
                    text: "<u>S</u>hort Test" + (win.ok && win.report.shortMinutes ? " (" + win.report.shortMinutes + " min)" : "")
                    enabled: win.ok && win.report.canSelfTest && !win.report.testRunning && !win.loading
                    onClicked: Disks.startSelfTest(win.drive.device, "short")
                }
                Button95 {
                    width: 150
                    text: "<u>E</u>xtended Test" + (win.ok && win.report.longMinutes ? " (" + win.report.longMinutes + " min)" : "")
                    enabled: win.ok && win.report.canSelfTest && !win.report.testRunning && !win.loading
                    onClicked: {
                        msgBox.show("S.M.A.R.T. Self-Test",
                                    "The extended test reads the whole disk" + (win.report.longMinutes ? ", which takes "
                                    + minutes(win.report.longMinutes) : "") + ". The computer can be used meanwhile, "
                                    + "but more slowly.\n\nStart it?", "question", ["Yes", "No"], function (i) {
                            if (i === 0)
                                Disks.startSelfTest(win.drive.device, "long");
                        });
                    }
                }
                Button95 {
                    width: 90
                    text: "S<u>t</u>op Test"
                    enabled: win.ok && win.report.testRunning && !win.loading
                    onClicked: Disks.startSelfTest(win.drive.device, "abort")
                }
            }
            Text {
                id: logTitle
                x: 12
                y: testButtons.y + testButtons.height + 12
                text: "Earlier tests:"
                color: Theme.text
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontSize
            }
            Bevel {
                id: logWell
                x: 12
                y: logTitle.y + logTitle.height + 4
                width: parent.width - 24
                height: parent.height - y - 12
                style: "sunken"
                face: Theme.window
                readonly property var columns: [
                    { title: "Test", w: 130 }, { title: "Result", w: Math.max(120, logList.width - 130 - 110) },
                    { title: "At (hours on)", w: 110, right: true },
                ]
                Row {
                    id: logHeader
                    x: 2
                    y: 2
                    height: 18
                    clip: true
                    width: parent.width - 4
                    Repeater {
                        model: logWell.columns
                        Item {
                            required property var modelData
                            width: modelData.w
                            height: 18
                            Bevel { anchors.fill: parent; style: "raised" }
                            Text {
                                x: 4
                                width: parent.width - 8
                                height: parent.height
                                verticalAlignment: Text.AlignVCenter
                                horizontalAlignment: parent.modelData.right ? Text.AlignRight : Text.AlignLeft
                                elide: Text.ElideRight
                                text: parent.modelData.title
                                color: Theme.text
                                font.family: Theme.fontFamily
                                font.pixelSize: Theme.fontSize
                            }
                        }
                    }
                }
                ListView {
                    id: logList
                    x: 2
                    y: logHeader.y + logHeader.height
                    width: parent.width - 4 - (logBar.visible ? logBar.width : 0)
                    height: parent.height - y - 2
                    clip: true
                    interactive: false
                    boundsBehavior: Flickable.StopAtBounds
                    model: win.ok ? win.report.selfTests : []
                    delegate: Row {
                        id: logRow
                        required property var modelData
                        height: 18
                        readonly property var cells: [modelData.type, modelData.status, Number(modelData.hours).toLocaleString(Qt.locale(), "f", 0)]
                        Repeater {
                            model: logWell.columns
                            Text {
                                required property var modelData
                                required property int index
                                width: modelData.w
                                height: 18
                                leftPadding: 4
                                rightPadding: 4
                                verticalAlignment: Text.AlignVCenter
                                horizontalAlignment: modelData.right ? Text.AlignRight : Text.AlignLeft
                                elide: Text.ElideRight
                                text: String(logRow.cells[index])
                                color: logRow.modelData.ok ? Theme.text : "#c00000"
                                font.family: Theme.fontFamily
                                font.pixelSize: Theme.fontSize
                            }
                        }
                    }
                    WheelHandler {
                        onWheel: (event) => {
                            logList.contentY = Math.max(0, Math.min(logList.contentHeight - logList.height,
                                                                    logList.contentY - event.angleDelta.y / 120 * 54));
                        }
                    }
                }
                ScrollBar95 {
                    id: logBar
                    anchors.right: parent.right
                    anchors.rightMargin: 2
                    y: logList.y
                    height: logList.height
                    flickable: logList
                    stepSize: 18
                }
                Text {
                    visible: win.ok && (!win.report.selfTests || win.report.selfTests.length === 0)
                    anchors.centerIn: parent
                    text: "The disk hasn't run any tests yet."
                    color: Theme.shadow
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontSize
                }
            }
        }
    }

    Row {
        anchors.right: parent.right
        anchors.rightMargin: 8
        anchors.bottom: parent.bottom
        anchors.bottomMargin: 10
        spacing: 6
        Button95 {
            width: 75
            text: "<u>R</u>efresh"
            enabled: !win.loading
            onClicked: win.reload()
        }
        Button95 {
            width: 75
            text: "Close"
            isDefault: true
            onClicked: win.close()
        }
    }
    Shortcut { sequence: "Escape"; onActivated: win.close() }
    Shortcut { sequence: "F5"; onActivated: if (!win.loading) win.reload() }

    Menu95 { id: popup }
    MessageBox95 { id: msgBox }

    // W95_DISKS_GRAB: a picture of this window too, once the report is in
    Timer {
        id: grabTimer
        running: testGrab !== "" && win.visible && !win.loading
        interval: 800
        onTriggered: {
            var f = testGrab.replace(/\.png$/, "") + "-smart-" + tabs.currentIndex + ".png";
            // the next tab only once this one is saved (grabbing takes a frame)
            win.contentItem.grabToImage(function (r) {
                r.saveToFile(f);
                console.log("grabbed " + f);
                if (tabs.currentIndex < 2) { tabs.currentIndex++; grabTimer.restart(); }
            });
        }
    }
}
