import QtQuick
import QtQuick.Window
import Win95Ui
import Win95Terminal

// The terminal window: a Win95 toolbar over a sunken, black text area. The
// window title and frame come from the compositor (server-side decoration),
// so this is just the client area.
Window {
    id: window

    readonly property int startColumns: 80
    readonly property int startRows: 25
    readonly property int sunkenBorder: 2

    title: term.title.length > 0 ? term.title : "Terminal"
    color: "#c0c0c0"
    visible: true
    minimumWidth: 200
    minimumHeight: 120

    Component.onCompleted: {
        // size the window for an 80x25 grid at the current font
        width = Math.ceil(term.cellWidth * startColumns) + 2 * term.padding + 2 * sunkenBorder + 8;
        height = Math.ceil(term.cellHeight * startRows) + 2 * term.padding + 2 * sunkenBorder
                 + toolbar.y + toolbar.height + 4 /* gap */ + 4 /* bottom margin */;
        term.forceActiveFocus();
    }

    Row {
        id: toolbar
        x: 4
        y: 4
        height: 22
        spacing: 2

        ToolButton95 {
            text: "A-"
            enabled: term.fontPixelSize > 8
            onClicked: term.fontPixelSize -= 1
        }
        Rectangle {
            // sunken "font size" readout, like the DOS box's font combo
            width: 44
            height: 22
            color: "#ffffff"
            Bevel { anchors.fill: parent; style: "sunken"; face: "#ffffff" }
            Text {
                anchors.centerIn: parent
                text: term.fontPixelSize + " px"
                font.family: "Liberation Sans"
                font.pixelSize: 11
            }
        }
        ToolButton95 {
            text: "A+"
            enabled: term.fontPixelSize < 40
            onClicked: term.fontPixelSize += 1
        }

        Item { width: 8; height: 1 }

        ToolButton95 {
            text: "Copy"
            enabled: term.hasSelection
            onClicked: { term.copy(); term.forceActiveFocus(); }
        }
        ToolButton95 {
            text: "Paste"
            onClicked: { term.paste(); term.forceActiveFocus(); }
        }
        ToolButton95 {
            text: "Select All"
            onClicked: { term.selectAll(); term.forceActiveFocus(); }
        }

        Item { width: 8; height: 1 }

        Text {
            anchors.verticalCenter: parent.verticalCenter
            text: term.columns + " x " + term.rows
            color: "#000000"
            font.family: "Liberation Sans"
            font.pixelSize: 11
        }
    }

    Bevel {
        id: well
        style: "sunken"
        face: "#000000"
        anchors.top: toolbar.bottom
        anchors.topMargin: 4
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.margins: 4
    }

    TerminalItem {
        id: term
        anchors.fill: well
        anchors.margins: window.sunkenBorder
        focus: true
        workingDirectory: initialWorkingDirectory
        command: initialCommand
        onFinished: Qt.quit()
        onBell: {
            bellFlash.restart();
            Wayland95Settings.playSound("default");
        }
    }

    // visual bell
    Rectangle {
        id: bellOverlay
        anchors.fill: term
        color: "#ffffff"
        opacity: 0
        SequentialAnimation on opacity {
            id: bellFlash
            running: false
            NumberAnimation { to: 0.35; duration: 40 }
            NumberAnimation { to: 0; duration: 120 }
        }
    }
}
