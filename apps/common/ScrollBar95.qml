import QtQuick

// A Win95 scrollbar driving a Flickable (ListView, GridView, a text
// area...). Vertical by default; set `orientation: Qt.Horizontal` for the
// bottom bar. Arrow buttons and the trough auto-repeat while held; the
// thumb drags. Hides itself when everything fits.
Item {
    id: bar

    property Flickable flickable
    property int orientation: Qt.Vertical
    property real stepSize: 20

    readonly property bool vertical: orientation === Qt.Vertical
    readonly property int buttonSize: 16

    implicitWidth: vertical ? 16 : 100
    implicitHeight: vertical ? 100 : 16
    visible: needed

    readonly property real contentSize: !flickable ? 0 : (vertical ? flickable.contentHeight : flickable.contentWidth)
    readonly property real viewSize: !flickable ? 0 : (vertical ? flickable.height : flickable.width)
    readonly property real maxPos: Math.max(0, contentSize - viewSize)
    readonly property bool needed: contentSize > viewSize + 0.5
    readonly property real position: !flickable ? 0
        : (vertical ? flickable.contentY - flickable.originY : flickable.contentX - flickable.originX)

    function scrollTo(pos) {
        if (!flickable)
            return;
        var p = Math.max(0, Math.min(maxPos, pos));
        if (vertical)
            flickable.contentY = flickable.originY + p;
        else
            flickable.contentX = flickable.originX + p;
    }
    function scrollBy(delta) { scrollTo(position + delta); }

    // position along the bar's axis of a point in `item` coordinates
    function along(item, x, y) {
        var p = item.mapToItem(bar, x, y);
        return vertical ? p.y : p.x;
    }

    // trough: the classic white/gray checkerboard
    Canvas {
        id: trough
        x: bar.vertical ? 0 : bar.buttonSize
        y: bar.vertical ? bar.buttonSize : 0
        width: bar.vertical ? bar.width : bar.width - 2 * bar.buttonSize
        height: bar.vertical ? bar.height - 2 * bar.buttonSize : bar.height
        readonly property real length: bar.vertical ? height : width
        onPaint: {
            var ctx = getContext("2d");
            ctx.fillStyle = Theme.face;
            ctx.fillRect(0, 0, width, height);
            ctx.fillStyle = Theme.highlight;
            for (var yy = 0; yy < height; ++yy)
                for (var xx = yy % 2; xx < width; xx += 2)
                    ctx.fillRect(xx, yy, 1, 1);
        }
        onWidthChanged: requestPaint()
        onHeightChanged: requestPaint()
        // repaint when the color scheme changes
        property color faceColor: Theme.face
        onFaceColorChanged: requestPaint()

        MouseArea {
            id: troughMouse
            anchors.fill: parent
            property int direction: 0
            onPressed: (mouse) => {
                var p = bar.along(troughMouse, mouse.x, mouse.y);
                direction = p < bar.along(thumb, 0, 0) ? -1 : 1;
                bar.scrollBy(direction * bar.viewSize);
                repeat.start(direction * bar.viewSize, function () {
                    // stop once the thumb reaches the pointer
                    var q = bar.along(troughMouse, troughMouse.mouseX, troughMouse.mouseY);
                    var start = bar.along(thumb, 0, 0);
                    if ((troughMouse.direction < 0 && q >= start)
                            || (troughMouse.direction > 0 && q <= start + thumb.length))
                        return false;
                    return true;
                });
            }
            onReleased: repeat.stop()
        }
    }

    Item {
        id: thumb
        readonly property real length: Math.max(8, trough.length * (bar.contentSize > 0 ? bar.viewSize / bar.contentSize : 1))
        readonly property real offset: bar.maxPos > 0 ? (trough.length - length) * bar.position / bar.maxPos : 0
        x: bar.vertical ? 0 : trough.x + offset
        y: bar.vertical ? trough.y + offset : 0
        width: bar.vertical ? bar.width : length
        height: bar.vertical ? length : bar.height

        Bevel { anchors.fill: parent; style: "raised" }

        MouseArea {
            id: thumbMouse
            anchors.fill: parent
            property real startMouse: 0
            property real startPos: 0
            onPressed: (mouse) => {
                startMouse = bar.along(thumbMouse, mouse.x, mouse.y);
                startPos = bar.position;
            }
            onPositionChanged: (mouse) => {
                if (!pressed)
                    return;
                var travel = trough.length - thumb.length;
                if (travel <= 0)
                    return;
                var d = bar.along(thumbMouse, mouse.x, mouse.y) - startMouse;
                bar.scrollTo(startPos + d * bar.maxPos / travel);
            }
        }
    }

    component ArrowButton: Item {
        id: arrowButton
        property string glyph
        signal stepped()
        width: bar.buttonSize
        height: bar.buttonSize
        readonly property bool down: arrowMouse.pressed && arrowMouse.containsMouse
        Bevel { anchors.fill: parent; style: arrowButton.down ? "thinSunken" : "raised" }
        Text {
            anchors.centerIn: parent
            anchors.horizontalCenterOffset: arrowButton.down ? 1 : 0
            anchors.verticalCenterOffset: arrowButton.down ? 1 : 0
            text: arrowButton.glyph
            font.pixelSize: 7
            color: Theme.text
        }
        MouseArea {
            id: arrowMouse
            anchors.fill: parent
            onPressed: {
                arrowButton.stepped();
                repeat.start(0, null, arrowButton);
            }
            onReleased: repeat.stop()
        }
    }

    ArrowButton {
        x: 0
        y: 0
        glyph: bar.vertical ? "▲" : "◀"
        onStepped: bar.scrollBy(-bar.stepSize)
    }
    ArrowButton {
        x: bar.vertical ? 0 : bar.width - bar.buttonSize
        y: bar.vertical ? bar.height - bar.buttonSize : 0
        glyph: bar.vertical ? "▼" : "▶"
        onStepped: bar.scrollBy(bar.stepSize)
    }

    // shared auto-repeat: initial delay, then fast steps
    Timer {
        id: repeat
        property real amount: 0
        property var keepGoing: null
        property var button: null
        interval: 350
        repeat: true
        function start(amt, check, btn) {
            amount = amt;
            keepGoing = check;
            button = btn || null;
            interval = 350;
            restart();
        }
        onTriggered: {
            interval = 50;
            if (button) {
                button.stepped();
            } else if (!keepGoing || keepGoing()) {
                bar.scrollBy(amount);
            } else {
                stop();
            }
        }
    }
}
