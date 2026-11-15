import QtQuick
import QtQuick.Window
import Win95Ui
import Win95Control

// Printers: a folder with Add Printer and the printers CUPS knows, like
// Win95's. Double-click a printer to see what it's printing; File has
// Pause Printing, Purge Print Documents, Set As Default, Delete and
// Properties.
Window {
    id: win

    width: 460
    height: 320
    minimumWidth: 280
    minimumHeight: 200
    visible: true
    color: Theme.face
    title: "Printers"

    property string view: "icons"   // "icons" or "details"
    property string current: ""      // the selected entry's name ("" none, "@add" Add Printer)

    readonly property var entries: [{ name: "@add", label: "Add Printer", kind: "add" }].concat(
        Printers.printers.map(function (p) { return Object.assign({ label: p.name, kind: "printer" }, p); }))
    readonly property var selectedPrinter: {
        for (var i = 0; i < Printers.printers.length; ++i)
            if (Printers.printers[i].name === current)
                return Printers.printers[i];
        return null;
    }

    function statusOf(p) {
        if (!p)
            return "";
        var s = p.state === "stopped" ? "Paused" : p.state === "printing" ? "Printing" : "Ready";
        if (!p.accepting)
            s += ", not accepting documents";
        var extra = p.reasons.filter(function (r) { return r !== "Paused"; });
        return extra.length ? s + " (" + extra.join(", ") + ")" : s;
    }
    function jobsFor(name) {
        return Printers.jobs.filter(function (j) { return j.printer === name; });
    }

    // one window of each kind per printer
    property var windows: ({})
    function openWindow(kind, printer, component, props) {
        var id = kind + ":" + printer;
        if (windows[id]) {
            windows[id].raise();
            windows[id].requestActivate();
            return;
        }
        var w = component.createObject(null, props);
        var copy = Object.assign({}, windows);
        copy[id] = w;
        windows = copy;
        w.closing.connect(function () {
            var c = Object.assign({}, win.windows);
            delete c[id];
            win.windows = c;
            Qt.callLater(w.destroy);
        });
    }
    function activate(name) {
        if (name === "@add")
            openWindow("wizard", "", wizardComponent, {});
        else if (name)
            openWindow("queue", name, queueComponent, { printerName: name });
    }
    function openProperties(name) {
        if (name && name !== "@add")
            openWindow("properties", name, propertiesComponent, { printerName: name });
    }
    function deletePrinter(name) {
        if (!name || name === "@add")
            return;
        var waiting = jobsFor(name).length;
        msgBox.show("Printers", "Are you sure you want to delete " + name + "?"
                    + (waiting ? "\n\nIts " + waiting + " waiting document(s) will be canceled." : ""),
                    "question", ["Yes", "No"], function (i) {
            if (i === 0) {
                Printers.deletePrinter(name);
                win.current = "";
            }
        });
    }

    Component { id: queueComponent; PrintQueueWindow {} }
    Component { id: wizardComponent; AddPrinterWizard {} }
    Component { id: propertiesComponent; PrinterProperties {} }

    Connections {
        target: Printers
        function onFailed(message) { msgBox.show("Printers", message, "error", ["OK"]); }
    }

    readonly property bool printerSelected: !!selectedPrinter
    readonly property var fileMenu: [
        { text: "&Open", enabled: current !== "", action: function () { win.activate(win.current); } },
        { separator: true },
        { text: "Pa&use Printing", enabled: printerSelected, checked: printerSelected && selectedPrinter.state === "stopped",
          action: function () { Printers.setPaused(win.current, win.selectedPrinter.state !== "stopped"); } },
        { text: "Purge Print Doc&uments", enabled: printerSelected && jobsFor(current).length > 0,
          action: function () { Printers.purge(win.current); } },
        { text: "Set As De&fault", enabled: printerSelected, checked: printerSelected && selectedPrinter.isDefault,
          action: function () { Printers.setDefault(win.current); } },
        { separator: true },
        { text: "&Delete", shortcut: "Del", enabled: printerSelected, action: function () { win.deletePrinter(win.current); } },
        { text: "P&roperties", enabled: printerSelected, action: function () { win.openProperties(win.current); } },
        { separator: true },
        { text: "&Close", action: function () { win.close(); } },
    ]
    readonly property var viewMenu: [
        { text: "Lar&ge Icons", checked: view === "icons", action: function () { win.view = "icons"; } },
        { text: "&Details", checked: view === "details", action: function () { win.view = "details"; } },
        { separator: true },
        { text: "&Refresh", shortcut: "F5", action: function () { Printers.refresh(); } },
    ]

    Shortcut { sequence: "F5"; onActivated: Printers.refresh() }
    Shortcut { sequence: "Del"; enabled: !msgBox.visible; onActivated: win.deletePrinter(win.current) }
    Shortcut { sequence: "Esc"; enabled: !msgBox.visible && !popup.visible; onActivated: win.close() }

    MenuBar95 {
        id: menuBar
        x: 2
        y: 2
        width: parent.width - 4
        height: 20
        popupMenu: popup
        menus: [
            { title: "File", items: win.fileMenu },
            { title: "View", items: win.viewMenu },
        ]
    }

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
            visible: win.view === "details"
            x: 2
            y: 2
            height: visible ? 18 : 0
            Repeater {
                model: [["Name", 150], ["Documents", 76], ["Status", 160], ["Comment", 200]]
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

        GridView {
            id: grid
            x: 2
            y: 2 + header.height
            width: parent.width - 4 - (vbar.visible ? vbar.width : 0)
            height: parent.height - 4 - header.height
            clip: true
            interactive: false
            focus: true
            model: win.entries
            cellWidth: win.view === "icons" ? 86 : width
            cellHeight: win.view === "icons" ? 76 : 20
            keyNavigationEnabled: false
            Keys.onPressed: (event) => {
                var i = -1;
                for (var k = 0; k < win.entries.length; ++k)
                    if (win.entries[k].name === win.current)
                        i = k;
                var step = win.view === "icons" ? Math.max(1, Math.floor(grid.width / grid.cellWidth)) : 1;
                var next = i;
                switch (event.key) {
                case Qt.Key_Right: next = i + 1; break;
                case Qt.Key_Left: next = i - 1; break;
                case Qt.Key_Down: next = i < 0 ? 0 : i + step; break;
                case Qt.Key_Up: next = i - step; break;
                case Qt.Key_Return: case Qt.Key_Enter: win.activate(win.current); event.accepted = true; return;
                default: return;
                }
                next = Math.max(0, Math.min(win.entries.length - 1, next));
                win.current = win.entries[next].name;
                event.accepted = true;
            }

            delegate: Item {
                id: cell
                required property var modelData
                required property int index
                readonly property bool isSelected: win.current === modelData.name
                width: grid.cellWidth
                height: grid.cellHeight

                Item {
                    visible: win.view === "icons"
                    anchors.fill: parent
                    PrinterIcon {
                        id: bigIcon
                        anchors.horizontalCenter: parent.horizontalCenter
                        y: 4
                        kind: cell.modelData.kind
                        isDefault: !!cell.modelData.isDefault
                        paused: cell.modelData.state === "stopped"
                        problem: !!cell.modelData.reasons && cell.modelData.reasons.some(function (r) { return r !== "Paused"; })
                    }
                    Rectangle {
                        anchors.horizontalCenter: parent.horizontalCenter
                        y: bigIcon.y + 36
                        width: Math.min(bigLabel.implicitWidth, cell.width - 6) + 4
                        height: bigLabel.height + 2
                        color: cell.isSelected ? Theme.selection : "transparent"
                        Text {
                            id: bigLabel
                            anchors.centerIn: parent
                            width: Math.min(implicitWidth, cell.width - 6)
                            horizontalAlignment: Text.AlignHCenter
                            wrapMode: Text.WrapAnywhere
                            maximumLineCount: 2
                            elide: Text.ElideRight
                            text: cell.modelData.label
                            color: cell.isSelected ? Theme.selectionText : Theme.text
                            font.family: Theme.fontFamily
                            font.pixelSize: Theme.fontSize
                        }
                    }
                }

                Row {
                    visible: win.view === "details"
                    x: 2
                    height: parent.height
                    spacing: 4
                    PrinterIcon {
                        anchors.verticalCenter: parent.verticalCenter
                        size: 16
                        kind: cell.modelData.kind
                        isDefault: !!cell.modelData.isDefault
                        paused: cell.modelData.state === "stopped"
                    }
                    Rectangle {
                        anchors.verticalCenter: parent.verticalCenter
                        width: Math.min(smallLabel.implicitWidth, 124) + 4
                        height: 15
                        color: cell.isSelected ? Theme.selection : "transparent"
                        Text {
                            id: smallLabel
                            x: 2
                            width: parent.width - 4
                            anchors.verticalCenter: parent.verticalCenter
                            elide: Text.ElideRight
                            text: cell.modelData.label
                            color: cell.isSelected ? Theme.selectionText : Theme.text
                            font.family: Theme.fontFamily
                            font.pixelSize: Theme.fontSize
                        }
                    }
                }
                Row {
                    visible: win.view === "details" && cell.modelData.kind === "printer"
                    x: 152
                    anchors.verticalCenter: parent.verticalCenter
                    Repeater {
                        model: cell.modelData.kind !== "printer" ? [] : [
                            [String(win.jobsFor(cell.modelData.name).length), 76],
                            [win.statusOf(cell.modelData), 160],
                            [cell.modelData.info || "", 200],
                        ]
                        Text {
                            required property var modelData
                            width: modelData[1]
                            leftPadding: 4
                            elide: Text.ElideRight
                            text: modelData[0]
                            color: Theme.text
                            font.family: Theme.fontFamily
                            font.pixelSize: Theme.fontSize
                        }
                    }
                }

                MouseArea {
                    anchors.fill: parent
                    acceptedButtons: Qt.LeftButton | Qt.RightButton
                    onPressed: (mouse) => {
                        grid.forceActiveFocus();
                        win.current = cell.modelData.name;
                        if (mouse.button === Qt.RightButton && cell.modelData.kind === "printer") {
                            var p = mapToItem(win.contentItem, mouse.x, mouse.y);
                            popup.popup(p.x, p.y, win.fileMenu.slice(0, 9));
                        }
                    }
                    onDoubleClicked: win.activate(cell.modelData.name)
                }
            }

            MouseArea {
                // a click on empty space clears the selection
                anchors.fill: parent
                z: -1
                onPressed: { grid.forceActiveFocus(); win.current = ""; }
            }
            WheelHandler { onWheel: (event) => vbar.scrollBy(-event.angleDelta.y / 120 * 54) }
        }

        ScrollBar95 {
            id: vbar
            visible: needed
            anchors.right: parent.right
            anchors.rightMargin: 2
            y: 2
            height: parent.height - 4
            flickable: grid
            stepSize: grid.cellHeight
        }

        Text {
            visible: !Printers.available
            anchors.centerIn: parent
            width: parent.width - 40
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.Wrap
            text: "The printing service (CUPS) isn't running, so no printers can be shown."
            color: Theme.shadow
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontSize
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
            text: win.selectedPrinter
                  ? win.jobsFor(win.current).length + " document(s) in queue. " + win.statusOf(win.selectedPrinter)
                  : win.current === "@add" ? "Adds a printer to your computer."
                  : win.entries.length + " object(s)"
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
