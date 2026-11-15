import QtQuick

// A vertical Win95 trackbar, as in the volume controls: the maximum is at
// the top, tick marks run down the right side, and the raised thumb points
// right at them. Drag the thumb, click the groove, or use the arrow keys.
FocusScope {
    id: slider

    property real from: 0
    property real to: 100
    property real value: 0
    property real stepSize: 1
    property int tickCount: 11
    readonly property bool pressed: mouse.pressed
    signal moved()
    // the mouse button went up, or a key changed the value
    signal released()

    implicitWidth: 30
    implicitHeight: 120
    activeFocusOnTab: true

    readonly property real span: Math.max(1e-9, to - from)
    readonly property real travel: height - thumb.height
    readonly property real ratio: (value - from) / span

    function setValue(v) {
        var stepped = stepSize > 0 ? Math.round((v - from) / stepSize) * stepSize + from : v;
        var clamped = Math.max(Math.min(from, to), Math.min(Math.max(from, to), stepped));
        if (clamped !== value) {
            value = clamped;
            moved();
        }
    }

    // y measured from the top; the top is the maximum
    function valueAt(y) {
        return to - (y - thumb.height / 2) / Math.max(1, travel) * span;
    }

    Bevel {
        // the groove
        x: 8
        y: thumb.height / 2 - 2
        width: 4
        height: parent.height - thumb.height + 4
        style: "sunken"
        face: Theme.face
    }

    // tick marks
    Repeater {
        model: slider.tickCount
        delegate: Rectangle {
            required property int index
            x: 24
            y: Math.round(thumb.height / 2 + slider.travel * index / Math.max(1, slider.tickCount - 1))
            width: 4
            height: 1
            color: Theme.dark
        }
    }

    Canvas {
        id: thumb
        property color faceColor: Theme.face // repaint on scheme change
        onFaceColorChanged: requestPaint()
        x: 0
        y: Math.round(slider.travel * (1 - Math.max(0, Math.min(1, slider.ratio))))
        width: 21
        height: 11
        // a raised pentagon pointing right
        onPaint: {
            var ctx = getContext("2d");
            ctx.reset();
            var w = width, h = height, tip = 5;
            ctx.fillStyle = Theme.face;
            ctx.beginPath();
            ctx.moveTo(0, 0); ctx.lineTo(w - tip, 0); ctx.lineTo(w, h / 2); ctx.lineTo(w - tip, h); ctx.lineTo(0, h);
            ctx.closePath();
            ctx.fill();
            ctx.lineWidth = 1;
            ctx.strokeStyle = Theme.highlight;
            ctx.beginPath(); ctx.moveTo(0.5, h - 1); ctx.lineTo(0.5, 0.5); ctx.lineTo(w - tip, 0.5); ctx.lineTo(w - 0.5, h / 2); ctx.stroke();
            ctx.strokeStyle = Theme.dark;
            ctx.beginPath(); ctx.moveTo(0, h - 0.5); ctx.lineTo(w - tip, h - 0.5); ctx.lineTo(w - 0.5, h / 2); ctx.stroke();
            ctx.strokeStyle = Theme.shadow;
            ctx.beginPath(); ctx.moveTo(1, h - 1.5); ctx.lineTo(w - tip - 0.5, h - 1.5); ctx.lineTo(w - 1.5, h / 2); ctx.stroke();
        }
    }

    Loader {
        active: slider.activeFocus
        anchors.fill: parent
        anchors.margins: -1
        sourceComponent: FocusRect95 {}
    }

    MouseArea {
        id: mouse
        anchors.fill: parent
        onPressed: (event) => {
            slider.forceActiveFocus();
            slider.setValue(slider.valueAt(event.y));
        }
        onPositionChanged: (event) => {
            if (pressed)
                slider.setValue(slider.valueAt(event.y));
        }
        onReleased: slider.released()
    }

    Keys.onPressed: (event) => {
        var big = (to - from) / Math.max(1, tickCount - 1);
        if (event.key === Qt.Key_Down || event.key === Qt.Key_Left)
            setValue(value - (stepSize || big));
        else if (event.key === Qt.Key_Up || event.key === Qt.Key_Right)
            setValue(value + (stepSize || big));
        else if (event.key === Qt.Key_PageDown)
            setValue(value - big);
        else if (event.key === Qt.Key_PageUp)
            setValue(value + big);
        else if (event.key === Qt.Key_End)
            setValue(from);
        else if (event.key === Qt.Key_Home)
            setValue(to);
        else
            return;
        event.accepted = true;
        released();
    }
}
