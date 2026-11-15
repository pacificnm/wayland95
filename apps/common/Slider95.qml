import QtQuick

// A Win95 trackbar: sunken groove, a raised pointer-shaped thumb and tick
// marks underneath. Drag the thumb, click the groove, or use the arrow keys.
FocusScope {
    id: slider

    property real from: 0
    property real to: 100
    property real value: 0
    property real stepSize: 1
    property int tickCount: 11
    signal moved()

    implicitWidth: 160
    implicitHeight: 30
    activeFocusOnTab: true

    readonly property real span: Math.max(1e-9, to - from)
    readonly property real travel: width - thumb.width
    readonly property real ratio: (value - from) / span

    function setValue(v) {
        var stepped = stepSize > 0 ? Math.round((v - from) / stepSize) * stepSize + from : v;
        var clamped = Math.max(Math.min(from, to), Math.min(Math.max(from, to), stepped));
        if (clamped !== value) {
            value = clamped;
            moved();
        }
    }

    function valueAt(x) {
        return from + (x - thumb.width / 2) / Math.max(1, travel) * span;
    }

    Bevel {
        // the groove
        x: thumb.width / 2 - 2
        y: 8
        width: parent.width - thumb.width + 4
        height: 4
        style: "sunken"
        face: Theme.face
    }

    // tick marks
    Repeater {
        model: slider.tickCount
        delegate: Rectangle {
            required property int index
            x: Math.round(thumb.width / 2 + slider.travel * index / Math.max(1, slider.tickCount - 1))
            y: 24
            width: 1
            height: 4
            color: Theme.dark
        }
    }

    Canvas {
        id: thumb
        property color faceColor: Theme.face // repaint on scheme change
        onFaceColorChanged: requestPaint()
        width: 11
        height: 21
        x: Math.round(slider.travel * Math.max(0, Math.min(1, slider.ratio)))
        y: 0
        // a raised pentagon pointing down, like the Win95 trackbar thumb
        onPaint: {
            var ctx = getContext("2d");
            ctx.reset();
            var w = width, h = height, tip = 5;
            ctx.fillStyle = Theme.face;
            ctx.beginPath();
            ctx.moveTo(0, 0); ctx.lineTo(w, 0); ctx.lineTo(w, h - tip); ctx.lineTo(w / 2, h); ctx.lineTo(0, h - tip);
            ctx.closePath();
            ctx.fill();
            ctx.lineWidth = 1;
            ctx.strokeStyle = Theme.highlight;
            ctx.beginPath(); ctx.moveTo(0.5, h - tip); ctx.lineTo(0.5, 0.5); ctx.lineTo(w - 1, 0.5); ctx.stroke();
            ctx.strokeStyle = Theme.dark;
            ctx.beginPath(); ctx.moveTo(w - 0.5, 0); ctx.lineTo(w - 0.5, h - tip); ctx.lineTo(w / 2, h - 0.5); ctx.stroke();
            ctx.beginPath(); ctx.moveTo(w / 2, h - 0.5); ctx.lineTo(0.5, h - tip); ctx.stroke();
            ctx.strokeStyle = Theme.shadow;
            ctx.beginPath(); ctx.moveTo(w - 1.5, 1); ctx.lineTo(w - 1.5, h - tip - 0.5); ctx.lineTo(w / 2, h - 1.5); ctx.stroke();
        }
    }

    Loader {
        active: slider.activeFocus
        anchors.fill: parent
        anchors.margins: -1
        sourceComponent: FocusRect95 {}
    }

    MouseArea {
        anchors.fill: parent
        onPressed: (mouse) => {
            slider.forceActiveFocus();
            slider.setValue(slider.valueAt(mouse.x));
        }
        onPositionChanged: (mouse) => {
            if (pressed)
                slider.setValue(slider.valueAt(mouse.x));
        }
    }

    Keys.onPressed: (event) => {
        var big = (to - from) / Math.max(1, tickCount - 1);
        if (event.key === Qt.Key_Left || event.key === Qt.Key_Down)
            setValue(value - (stepSize || big));
        else if (event.key === Qt.Key_Right || event.key === Qt.Key_Up)
            setValue(value + (stepSize || big));
        else if (event.key === Qt.Key_PageDown)
            setValue(value - big);
        else if (event.key === Qt.Key_PageUp)
            setValue(value + big);
        else if (event.key === Qt.Key_Home)
            setValue(from);
        else if (event.key === Qt.Key_End)
            setValue(to);
        else
            return;
        event.accepted = true;
    }
}
