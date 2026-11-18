import QtQuick
import Win95Ui

// ToolBar: a row of buttons across the top of the form (under its Menu).
// `buttons` is what the Studio's Menu Editor makes:
//
//   [{ text: "Save", name: "saveButton", glyph: "✔", picture: "image://app/save.png",
//      toolTip: "Save the record", action: "save", form: "", enabled: true, checked: false },
//    { separator: true }]
//
// Pressing one runs its Action (form.runAction), then itemClicked with
// `item` set to its name. Code can change a button:
//
//   toolBar1.setEnabled("saveButton", false)    toolBar1.setChecked("boldButton", true)
AppElement {
    id: bar

    property var buttons: []
    property bool showCaptions: true
    property string item: ""
    signal itemClicked()

    function choose(b) {
        if (!b || b.separator || b.enabled === false || !bar.enabled || bar.designMode)
            return;
        bar.item = b.name || "";
        if (bar.form && b.action)
            bar.form.runAction(b.action, b.form);
        bar.itemClicked();
    }

    function change(name, how) {
        var copy = JSON.parse(JSON.stringify(buttons)), found = false;
        copy.forEach(function (b) {
            if (b.name === name) {
                how(b);
                found = true;
            }
        });
        if (found)
            buttons = copy;
        return found;
    }
    function setEnabled(name, on) { return change(name, function (b) { b.enabled = !!on; }); }
    function setChecked(name, on) { return change(name, function (b) { b.checked = !!on; }); }
    function setText(name, text) { return change(name, function (b) { b.text = String(text); }); }
    function itemNamed(name) {
        for (var i = 0; i < buttons.length; ++i) {
            if (buttons[i].name === name)
                return buttons[i];
        }
        return null;
    }

    Rectangle { anchors.fill: parent; color: bar.fillColor }
    // (an etched line under it)
    Rectangle { anchors.bottom: parent.bottom; anchors.bottomMargin: 1; width: parent.width; height: 1; color: Theme.shadow }
    Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1; color: Theme.highlight }

    Row {
        x: 2
        y: 2
        height: parent.height - 5
        spacing: 1
        Repeater {
            model: bar.buttons
            delegate: Item {
                id: button
                required property var modelData
                readonly property bool separator: !!modelData.separator
                readonly property bool usable: !separator && modelData.enabled !== false && bar.enabled
                readonly property bool down: (mouse.pressed && mouse.containsMouse) || !!modelData.checked
                readonly property string picture: modelData.picture || ""
                readonly property string glyph: modelData.glyph || ""
                readonly property string caption: bar.showCaptions || (picture === "" && glyph === "") ? (modelData.text || "") : ""
                width: separator ? 8 : Math.max(content.implicitWidth + 12, parent.height)
                height: parent.height

                // a separator: an etched line
                Rectangle { visible: button.separator; x: 3; y: 2; width: 1; height: parent.height - 4; color: Theme.shadow }
                Rectangle { visible: button.separator; x: 4; y: 2; width: 1; height: parent.height - 4; color: Theme.highlight }

                Bevel {
                    visible: !button.separator
                    anchors.fill: parent
                    style: button.down ? "pressed" : "raised"
                    face: button.modelData.checked && !mouse.pressed ? Theme.light : bar.fillColor
                }
                Row {
                    id: content
                    visible: !button.separator
                    anchors.centerIn: parent
                    anchors.horizontalCenterOffset: button.down ? 1 : 0
                    anchors.verticalCenterOffset: button.down ? 1 : 0
                    spacing: 4
                    Image {
                        visible: button.picture !== ""
                        anchors.verticalCenter: parent.verticalCenter
                        width: visible ? Math.min(16, button.height - 6) : 0
                        height: width
                        source: button.picture
                        sourceSize: Qt.size(32, 32)
                        fillMode: Image.PreserveAspectFit
                        opacity: button.usable ? 1 : 0.4
                    }
                    EtchedText {
                        visible: button.picture === "" && button.glyph !== ""
                        anchors.verticalCenter: parent.verticalCenter
                        text: button.glyph
                        enabled: button.usable
                        pixelSize: 14
                        bold: true
                    }
                    EtchedText {
                        visible: button.caption !== ""
                        anchors.verticalCenter: parent.verticalCenter
                        text: button.caption
                        enabled: button.usable
                    }
                }
                MouseArea {
                    id: mouse
                    anchors.fill: parent
                    enabled: button.usable && !bar.designMode
                    hoverEnabled: true
                    onClicked: bar.choose(button.modelData)
                    onContainsMouseChanged: {
                        if (!containsMouse && bar.form)
                            bar.form.hideTip(button);
                    }
                }
                Timer {
                    interval: 500
                    running: mouse.containsMouse && !mouse.pressed && (button.modelData.toolTip || "") !== ""
                    onTriggered: {
                        if (bar.form)
                            bar.form.showTip(button, button.modelData.toolTip);
                    }
                }
            }
        }
    }
}
