import QtQuick
import Win95Ui
import Win95Control

// Problem Reports: what was written when a Wayland 95 program (or the
// desktop) crashed. Each can be read, copied, reported on GitHub (a filled-in
// new issue to look over and submit; nothing is sent by itself) or deleted.
PropertySheet {
    id: sheet

    title: "Problem Reports"
    titles: ["Problem Reports"]
    canApply: false

    readonly property var current: list.selected
    property string body: current ? ProblemReports.text(current.file) : ""

    Component.onCompleted: {
        ProblemReports.reload();
        ProblemReports.markAllSeen();
    }
    Connections {
        target: ProblemReports
        function onReportsChanged() {
            sheet.body = sheet.current ? ProblemReports.text(sheet.current.file) : "";
        }
    }

    function when(ms) {
        var d = new Date(ms);
        return d.toLocaleDateString(Qt.locale(), "M/d/yyyy") + " " + d.toLocaleTimeString(Qt.locale(), "h:mm AP");
    }

    component Label: Text {
        wrapMode: Text.Wrap
        textFormat: Text.StyledText
        font.family: Theme.fontFamily
        font.pixelSize: Theme.fontSize
    }

    Item {
        AppletIcon { x: 4; y: 2; kind: "problems" }
        Label {
            x: 46
            y: 2
            width: parent.width - x
            text: "When a program crashes, Wayland 95 writes down what it was doing. Sending a report "
                  + "helps get the problem fixed."
        }

        ProgramList {
            id: list
            y: 40
            width: parent.width
            height: 96
            model: ProblemReports.reports
            iconComponent: AppletIcon { size: parent.size; kind: "problems" }
            noteFor: function (item) { return sheet.when(item.time); }
            emptyText: "No problems have been reported."
            onModelChanged: if (!selected && model.length) selectedKey = model[0].key
            Component.onCompleted: if (model.length) selectedKey = model[0].key
        }

        Label {
            id: summary
            y: list.y + list.height + 6
            width: parent.width
            text: sheet.current ? "<b>" + sheet.current.summary + "</b>"
                                  + (sheet.current.version ? " (Wayland 95 " + sheet.current.version + ")" : "") : ""
        }

        // the report itself
        Bevel {
            id: textBox
            y: summary.y + summary.height + 4
            width: parent.width
            height: buttons.y - y - 8
            style: "sunken"
            face: Theme.window
            Flickable {
                id: flick
                x: 3
                y: 3
                width: parent.width - 6 - bar.width
                height: parent.height - 6
                clip: true
                contentWidth: width
                contentHeight: reportText.height
                interactive: false
                TextEdit {
                    id: reportText
                    width: flick.width
                    readOnly: true
                    selectByMouse: true
                    wrapMode: TextEdit.WrapAnywhere
                    text: sheet.body
                    font.family: "monospace"
                    font.pixelSize: Theme.fontSize - 1
                    color: Theme.text
                }
                WheelHandler { onWheel: (event) => bar.scrollBy(-event.angleDelta.y / 120 * 48) }
            }
            ScrollBar95 {
                id: bar
                anchors.right: parent.right
                anchors.rightMargin: 2
                y: 2
                height: parent.height - 4
                flickable: flick
                stepSize: 16
            }
        }

        Row {
            id: buttons
            anchors.bottom: parent.bottom
            anchors.right: parent.right
            spacing: 6
            Button95 {
                width: 120
                text: "<u>R</u>eport on GitHub..."
                enabled: !!sheet.current
                onClicked: Qt.openUrlExternally(ProblemReports.issueUrl(sheet.current.file))
            }
            Button95 {
                width: 56
                text: "<u>C</u>opy"
                enabled: !!sheet.current
                onClicked: ProblemReports.copy(sheet.current.file)
            }
            Button95 {
                width: 60
                text: "<u>D</u>elete"
                enabled: !!sheet.current
                onClicked: ProblemReports.remove(sheet.current.file)
            }
            Button95 {
                width: 70
                text: "Delete <u>A</u>ll"
                enabled: ProblemReports.reports.length > 0
                onClicked: sheet.messageBox.show("Problem Reports", "Delete all " + ProblemReports.reports.length + " problem reports?",
                                                 "question", ["Yes", "No"], function (b) {
                    if (b === 0)
                        ProblemReports.removeAll();
                })
            }
        }
    }
}
