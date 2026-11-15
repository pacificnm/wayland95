import QtQuick
import QtQuick.Window
import Win95Ui
import Win95Control

// One printer's queue, like Win95's: the documents waiting or printing,
// with Printer (Pause Printing, Purge Print Documents...) and Document
// (Pause Printing, Cancel Printing) menus.
Window {
    id: win

    required property string printerName

    width: 600
    height: 260
    minimumWidth: 360
    minimumHeight: 160
    visible: true
    color: Theme.face

    readonly property var printer: {
        for (var i = 0; i < Printers.printers.length; ++i)
            if (Printers.printers[i].name === printerName)
                return Printers.printers[i];
        return null;
    }
    readonly property bool paused: !!printer && printer.state === "stopped"
    readonly property var jobs: Printers.jobs.filter(function (j) { return j.printer === win.printerName; })
    property int current: -1   // the selected job's id
    readonly property var job: {
        for (var i = 0; i < jobs.length; ++i)
            if (jobs[i].id === current)
                return jobs[i];
        return null;
    }

    title: printerName + (paused ? " - Paused" : "")

    // the printer went away (deleted elsewhere)
    onPrinterChanged: if (!printer && Printers.available) win.close()


    readonly property var printerMenu: [
        { text: "Pa&use Printing", checked: paused, action: function () { Printers.setPaused(win.printerName, !win.paused); } },
        { text: "Purge Print Doc&uments", enabled: jobs.length > 0, action: function () {
            msgBox.show(win.printerName, "Are you sure you want to cancel all " + win.jobs.length + " document(s)?",
                        "question", ["Yes", "No"], function (i) { if (i === 0) Printers.purge(win.printerName); });
        } },
        { text: "Set As De&fault", checked: !!printer && printer.isDefault, action: function () { Printers.setDefault(win.printerName); } },
        { separator: true },
        { text: "&Print Test Page", action: function () { Printers.printTestPage(win.printerName); } },
        { separator: true },
        { text: "&Close", action: function () { win.close(); } },
    ]
    readonly property var documentMenu: [
        { text: "Pa&use Printing", enabled: !!job && (job.state === "waiting" || job.state === "held"), checked: !!job && job.state === "held",
          action: function () { Printers.setJobHeld(win.current, win.job.state !== "held"); } },
        { text: "&Cancel Printing", shortcut: "Del", enabled: !!job, action: function () { Printers.cancelJob(win.current); } },
    ]

    Shortcut { sequence: "Del"; enabled: !!win.job && !msgBox.visible; onActivated: Printers.cancelJob(win.current) }
    Shortcut { sequence: "Esc"; enabled: !msgBox.visible && !popup.visible; onActivated: win.close() }
    Connections {
        target: Printers
        function onFailed(message) { if (win.active) msgBox.show(win.printerName, message, "error", ["OK"]); }
    }

    MenuBar95 {
        id: menuBar
        x: 2
        y: 2
        width: parent.width - 4
        height: 20
        popupMenu: popup
        menus: [
            { title: "Printer", items: win.printerMenu },
            { title: "Document", items: win.documentMenu },
        ]
    }

    readonly property var columns: [["Document Name", 200], ["Status", 100], ["Owner", 80], ["Progress", 100], ["Started At", 130]]

    Bevel {
        id: well
        anchors.top: menuBar.bottom
        anchors.topMargin: 2
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: statusBar.top
        anchors.bottomMargin: 2
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
                    width: modelData[1]
                    height: 18
                    Bevel { anchors.fill: parent; style: "raised" }
                    Text {
                        x: 5
                        anchors.verticalCenter: parent.verticalCenter
                        text: parent.modelData[0]
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.fontSize
                    }
                }
            }
        }

        ListView {
            id: list
            x: 2
            y: header.y + 18
            width: parent.width - 4
            height: parent.height - y - 2
            clip: true
            interactive: false
            focus: true
            model: win.jobs
            keyNavigationEnabled: false
            Keys.onUpPressed: { var i = win.jobs.indexOf(win.job); if (i > 0) win.current = win.jobs[i - 1].id; }
            Keys.onDownPressed: { var i = win.jobs.indexOf(win.job); if (i + 1 < win.jobs.length) win.current = win.jobs[i + 1].id; }

            delegate: Item {
                id: row
                required property var modelData
                readonly property bool isSelected: modelData.id === win.current
                width: list.width
                height: 18

                Rectangle {
                    width: 200
                    height: parent.height
                    color: row.isSelected ? Theme.selection : "transparent"
                }
                Row {
                    Repeater {
                        model: [
                            row.modelData.name || "(untitled)",
                            win.paused && row.modelData.state !== "held" ? "Printer paused"
                              : row.modelData.state === "printing" ? "Printing"
                              : row.modelData.state === "held" ? "Paused"
                              : row.modelData.state === "stopped" ? "Error" : "Waiting",
                            row.modelData.owner,
                            (row.modelData.pages > 0 ? row.modelData.pages + " page(s), " : "") + Math.max(1, row.modelData.size) + "KB",
                            Qt.formatDateTime(row.modelData.created, "h:mm:ss AP M/d/yy"),
                        ]
                        Text {
                            required property var modelData
                            required property int index
                            width: win.columns[index][1]
                            height: 18
                            leftPadding: 4
                            verticalAlignment: Text.AlignVCenter
                            elide: Text.ElideRight
                            text: modelData
                            color: row.isSelected && index === 0 ? Theme.selectionText : Theme.text
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
                        win.current = row.modelData.id;
                        if (mouse.button === Qt.RightButton) {
                            var p = mapToItem(win.contentItem, mouse.x, mouse.y);
                            popup.popup(p.x, p.y, win.documentMenu);
                        }
                    }
                }
            }
        }
    }

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
            text: win.jobs.length + " job(s) in queue"
                  + (win.printer && win.printer.stateMessage ? ". " + win.printer.stateMessage : "")
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontSize
        }
        SizeGrip95 {
            anchors.right: parent.right
            anchors.rightMargin: 1
            anchors.bottom: parent.bottom
            anchors.bottomMargin: 1
        }
    }

    Menu95 { id: popup }
    MessageBox95 { id: msgBox }
}
