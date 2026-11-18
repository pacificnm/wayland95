import QtQuick
import QtQuick.Window
import Win95Ui

// A message box in a window of its own (a form may be too small to hold
// one): App.msgBox() and form.msgBox() make these.
Window {
    id: box

    property string message: ""
    property string icon: "info"    // info, warning, error, question
    property var buttons: ["OK"]
    signal finished(int index)

    flags: Qt.Dialog
    modality: Qt.ApplicationModal
    color: Theme.face
    // (its size from what it holds, never from its own width and height)
    readonly property int boxWidth: Math.max(260, Math.min(520, measure.implicitWidth + 90), buttonRow.width + 40)
    readonly property int boxHeight: Math.max(32, text.implicitHeight) + 72
    width: boxWidth
    height: boxHeight
    minimumWidth: boxWidth
    maximumWidth: boxWidth
    minimumHeight: boxHeight
    maximumHeight: boxHeight

    property bool answered: false
    function finish(index) {
        if (answered)
            return;
        answered = true;
        visible = false;
        finished(index);
    }
    // closed by its title bar: the last button (Cancel, No)
    onClosing: finish(buttons.length - 1)
    onVisibleChanged: {
        if (visible) {
            answered = false;
            requestActivate();
            Wayland95Settings.playSound(icon === "error" ? "critical" : icon === "warning" ? "exclamation"
                                        : icon === "question" ? "question" : "asterisk");
            if (buttonRepeater.count > 0)
                buttonRepeater.itemAt(0).forceActiveFocus();
        }
    }

    Shortcut { sequence: "Escape"; onActivated: box.finish(box.buttons.length - 1) }

    Item {
        id: iconItem
        x: 14
        y: 14
        width: 32
        height: 32
        Rectangle {
            visible: box.icon !== "warning"
            anchors.fill: parent
            radius: 16
            color: box.icon === "error" ? "#ff0000" : "#ffffff"
            border.color: box.icon === "error" ? "#800000" : "#000080"
            border.width: 1
        }
        Text {
            visible: box.icon === "warning"
            anchors.centerIn: parent
            text: "⚠"
            color: "#e0c000"
            font.pixelSize: 32
        }
        Text {
            visible: box.icon !== "warning"
            anchors.centerIn: parent
            text: box.icon === "error" ? "✕" : (box.icon === "question" ? "?" : "i")
            color: box.icon === "error" ? "white" : "#000080"
            font.family: "Liberation Serif"
            font.pixelSize: 22
            font.bold: true
        }
    }

    // (its widest line, unwrapped: a wrapped Text's own implicitWidth
    // follows its width, and the window's follows both)
    Text {
        id: measure
        visible: false
        text: box.message
        textFormat: Text.PlainText
        font: text.font
    }
    Text {
        id: text
        x: 60
        y: 14 + Math.max(0, Math.round((32 - implicitHeight) / 2))
        width: Math.min(measure.implicitWidth, 430)
        text: box.message
        textFormat: Text.PlainText
        wrapMode: Text.Wrap
        color: Theme.text
        font.family: Theme.fontFamily
        font.pixelSize: Theme.fontSize
    }

    Row {
        id: buttonRow
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottom: parent.bottom
        anchors.bottomMargin: 10
        spacing: 6
        Repeater {
            id: buttonRepeater
            model: box.buttons
            delegate: Button95 {
                required property string modelData
                required property int index
                text: modelData
                isDefault: index === 0
                onClicked: box.finish(index)
                KeyNavigation.right: buttonRepeater.itemAt(index + 1)
                KeyNavigation.left: index > 0 ? buttonRepeater.itemAt(index - 1) : null
            }
        }
    }
}
