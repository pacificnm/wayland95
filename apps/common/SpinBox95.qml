import QtQuick

// A Win95 spin box: a number field with small up/down arrow buttons.
// Type a number, click the arrows, or use the Up/Down keys.
FocusScope {
    id: spin

    property int value: 0
    property int from: 0
    property int to: 99
    signal valueModified(int value)

    implicitWidth: 60
    implicitHeight: 21

    function setValue(v) {
        v = Math.max(from, Math.min(to, Math.round(v)));
        if (v !== value) {
            value = v;
            valueModified(v);
        }
        field.text = String(value);
    }
    onValueChanged: if (!field.input.activeFocus) field.text = String(value)

    TextField95 {
        id: field
        width: parent.width - arrows.width
        height: parent.height
        text: String(spin.value)
        focus: true
        input.validator: IntValidator { bottom: spin.from; top: spin.to }
        input.horizontalAlignment: TextInput.AlignRight
        onAccepted: spin.setValue(parseInt(text) || spin.from)
        Connections {
            target: field.input
            function onActiveFocusChanged() {
                if (!field.input.activeFocus)
                    spin.setValue(parseInt(field.text) || spin.from);
            }
        }
        Keys.onUpPressed: spin.setValue(spin.value + 1)
        Keys.onDownPressed: spin.setValue(spin.value - 1)
    }

    Column {
        id: arrows
        anchors.right: parent.right
        width: 16
        height: parent.height
        Repeater {
            model: [1, -1]
            Item {
                required property int modelData
                width: 16
                height: spin.height / 2
                Bevel {
                    anchors.fill: parent
                    style: arrowMouse.pressed ? "pressed" : "raised"
                }
                Canvas {
                    anchors.centerIn: parent
                    width: 7
                    height: 4
                    property bool up: parent.modelData > 0
                    onPaint: {
                        var ctx = getContext("2d");
                        ctx.reset();
                        ctx.fillStyle = "#000000";
                        ctx.beginPath();
                        if (up) { ctx.moveTo(0, 4); ctx.lineTo(7, 4); ctx.lineTo(3.5, 0.5); }
                        else { ctx.moveTo(0, 0); ctx.lineTo(7, 0); ctx.lineTo(3.5, 3.5); }
                        ctx.closePath();
                        ctx.fill();
                    }
                }
                MouseArea {
                    id: arrowMouse
                    anchors.fill: parent
                    onPressed: {
                        spin.setValue(spin.value + parent.modelData);
                        repeat.start();
                    }
                    onReleased: repeat.stop()
                    Timer {
                        id: repeat
                        interval: 400
                        repeat: true
                        onTriggered: { interval = 80; spin.setValue(spin.value + parent.parent.modelData); }
                        onRunningChanged: if (!running) interval = 400
                    }
                }
            }
        }
    }
}
