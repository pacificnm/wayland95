import QtQuick
import Win95Ui
import Win95Control

// A printer's Properties: General (comment, location, test page) and
// Details (where it's connected, its driver, sharing).
PropertySheet {
    id: sheet

    required property string printerName

    title: printerName + " Properties"
    titles: ["General", "Details"]

    readonly property var printer: {
        for (var i = 0; i < Printers.printers.length; ++i)
            if (Printers.printers[i].name === printerName)
                return Printers.printers[i];
        return null;
    }

    // edited copies, written on Apply
    property string info: ""
    property string location: ""
    property bool shared: false
    property bool loaded: false
    onPrinterChanged: {
        if (printer && !loaded) {
            info = printer.info;
            location = printer.location;
            shared = printer.shared;
            loaded = true;
        }
    }
    Component.onCompleted: printerChanged()
    onApplyRequested: Printers.setProperties(printerName, info, location, shared)

    Connections {
        target: Printers
        function onFailed(message) { if (sheet.active) sheet.messageBox.show(sheet.title, message, "error", ["OK"]); }
    }

    component Label: Text {
        height: 21
        verticalAlignment: Text.AlignVCenter
        font.family: Theme.fontFamily
        font.pixelSize: Theme.fontSize
    }
    component Etched: Item {
        height: 2
        Rectangle { width: parent.width; height: 1; color: Theme.shadow }
        Rectangle { y: 1; width: parent.width; height: 1; color: Theme.highlight }
    }

    // --------------------------------------------------------------- General
    Item {
        PrinterIcon {
            x: 4
            y: 4
            isDefault: !!sheet.printer && sheet.printer.isDefault
        }
        Text {
            x: 50
            y: 12
            text: sheet.printerName
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontSize
        }
        Etched { y: 46; width: parent.width }

        Grid {
            y: 58
            columns: 2
            columnSpacing: 10
            rowSpacing: 8
            verticalItemAlignment: Grid.AlignVCenter
            Label { text: "<u>C</u>omment:" }
            TextField95 {
                id: commentField
                width: 270
                height: 21
                text: sheet.info
                Connections {
                    target: commentField.input
                    function onTextEdited() { sheet.info = commentField.text; sheet.dirty = true; }
                }
            }
            Label { text: "<u>L</u>ocation:" }
            TextField95 {
                id: locationField
                width: 270
                height: 21
                text: sheet.location
                Connections {
                    target: locationField.input
                    function onTextEdited() { sheet.location = locationField.text; sheet.dirty = true; }
                }
            }
        }

        Button95 {
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            width: 110
            text: "Print <u>T</u>est Page"
            onClicked: {
                Printers.printTestPage(sheet.printerName);
                sheet.messageBox.show(sheet.printerName, "A test page is now being sent to the printer. "
                                      + "It shows the printer's name, driver and settings.", "info", ["OK"]);
            }
        }
    }

    // --------------------------------------------------------------- Details
    Item {
        Grid {
            columns: 2
            columnSpacing: 10
            rowSpacing: 8
            verticalItemAlignment: Grid.AlignVCenter
            Label { text: "Print to:" }
            TextField95 {
                width: 270
                height: 21
                enabled: false
                text: sheet.printer ? sheet.printer.uri : ""
            }
            Label { text: "Driver:" }
            Label {
                width: 270
                elide: Text.ElideRight
                text: sheet.printer && sheet.printer.makeModel ? sheet.printer.makeModel : "(none)"
            }
            Label { text: "Status:" }
            Label {
                width: 270
                elide: Text.ElideRight
                text: !sheet.printer ? "" : (sheet.printer.state === "stopped" ? "Paused" : sheet.printer.state === "printing" ? "Printing" : "Ready")
                      + (sheet.printer.reasons.length ? " (" + sheet.printer.reasons.join(", ") + ")" : "")
            }
            Label { text: "Message:"; visible: !!sheet.printer && !!sheet.printer.stateMessage }
            Label {
                visible: !!sheet.printer && !!sheet.printer.stateMessage
                width: 270
                elide: Text.ElideRight
                text: sheet.printer ? sheet.printer.stateMessage : ""
            }
        }

        CheckBox95 {
            y: 140
            text: "<u>S</u>hare this printer with other computers on the network"
            checked: sheet.shared
            onToggled: (on) => { sheet.shared = on; sheet.dirty = true; }
        }
    }
}
