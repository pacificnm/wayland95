/* The installer's buttons, as in a Win95 wizard: an etched line, then
 * "< Back", "Next >" and, a little apart, "Cancel".
 */
import io.calamares.ui 1.0
import io.calamares.core 1.0
import QtQuick 2.15

Rectangle {
    id: navigationBar
    color: "#c0c0c0"
    height: 44

    readonly property string fontFamily: "Liberation Sans"

    // the labels are rich text (for the underlined Alt+ letter)
    function escaped(t) {
        return t.replace(/&/g, "&amp;").replace(/</g, "&lt;").replace(/>/g, "&gt;");
    }

    // a Win95 push button; "&" in the text marks its Alt+ key
    component Button95: Item {
        id: button
        property string text: ""
        property bool isDefault: false
        signal clicked()

        readonly property int mnemonic: text.indexOf("&")
        readonly property string plain: text.replace("&", "")
        readonly property bool down: mouse.pressed && mouse.containsMouse

        implicitWidth: Math.max(75, label.implicitWidth + 20)
        implicitHeight: 23
        opacity: 1

        // the default button's black frame
        Rectangle {
            anchors.fill: parent
            color: "transparent"
            border.color: "#000000"
            visible: button.isDefault && button.enabled
        }
        Item {
            id: face
            anchors.fill: parent
            anchors.margins: button.isDefault && button.enabled ? 1 : 0
            Rectangle { anchors.fill: parent; color: "#c0c0c0" }
            // raised: white/light at top-left, black/gray at bottom-right;
            // pushed: gray all round inside black
            Rectangle { width: parent.width; height: 1; color: button.down ? "#000000" : "#ffffff" }
            Rectangle { width: 1; height: parent.height; color: button.down ? "#000000" : "#ffffff" }
            Rectangle { y: parent.height - 1; width: parent.width; height: 1; color: "#000000" }
            Rectangle { x: parent.width - 1; width: 1; height: parent.height; color: "#000000" }
            Rectangle { x: 1; y: 1; width: parent.width - 2; height: 1; color: button.down ? "#808080" : "#dfdfdf" }
            Rectangle { x: 1; y: 1; width: 1; height: parent.height - 2; color: button.down ? "#808080" : "#dfdfdf" }
            Rectangle { x: 1; y: parent.height - 2; width: parent.width - 2; height: 1; color: "#808080"; visible: !button.down }
            Rectangle { x: parent.width - 2; y: 1; width: 1; height: parent.height - 2; color: "#808080"; visible: !button.down }
        }
        // grayed text is etched: white under gray
        Text {
            anchors.centerIn: label
            anchors.horizontalCenterOffset: 1
            anchors.verticalCenterOffset: 1
            text: label.text
            textFormat: Text.StyledText
            font: label.font
            color: "#ffffff"
            visible: !button.enabled
        }
        Text {
            id: label
            anchors.centerIn: parent
            anchors.horizontalCenterOffset: button.down ? 1 : 0
            anchors.verticalCenterOffset: button.down ? 1 : 0
            text: button.mnemonic < 0 ? navigationBar.escaped(button.plain)
                : navigationBar.escaped(button.plain.slice(0, button.mnemonic))
                  + "<u>" + navigationBar.escaped(button.plain.charAt(button.mnemonic)) + "</u>"
                  + navigationBar.escaped(button.plain.slice(button.mnemonic + 1))
            textFormat: Text.StyledText
            color: button.enabled ? "#000000" : "#808080"
            font.family: navigationBar.fontFamily
            font.pixelSize: 11
        }
        MouseArea {
            id: mouse
            anchors.fill: parent
            enabled: button.enabled
            onClicked: button.clicked()
        }
        Shortcut {
            enabled: button.enabled && button.visible && button.mnemonic >= 0
            sequence: "Alt+" + button.plain.charAt(button.mnemonic)
            onActivated: button.clicked()
        }
    }

    // the etched line above the buttons
    Rectangle { x: 8; y: 0; width: parent.width - 16; height: 1; color: "#808080" }
    Rectangle { x: 8; y: 1; width: parent.width - 16; height: 1; color: "#ffffff" }

    Row {
        anchors.right: parent.right
        anchors.rightMargin: 10
        anchors.verticalCenter: parent.verticalCenter
        anchors.verticalCenterOffset: 1
        spacing: 0

        Button95 {
            // "< Back", like a Win95 wizard
            text: "< " + ViewManager.backLabel
            enabled: ViewManager.backEnabled
            visible: ViewManager.backAndNextVisible
            onClicked: ViewManager.back()
        }
        Button95 {
            // "Next >" while paging; "Install" or "Done" as it is
            text: ViewManager.nextIcon === "go-next" ? ViewManager.nextLabel + " >" : ViewManager.nextLabel
            isDefault: true
            enabled: ViewManager.nextEnabled
            visible: ViewManager.backAndNextVisible
            onClicked: ViewManager.next()
        }
        Item { width: 8; height: 1; visible: ViewManager.backAndNextVisible }
        Button95 {
            text: ViewManager.quitLabel
            enabled: ViewManager.quitEnabled
            visible: ViewManager.quitVisible
            onClicked: ViewManager.quit()
        }
    }
}
