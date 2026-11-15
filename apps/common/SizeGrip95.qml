import QtQuick
import QtQuick.Window

// The size grip in the bottom-right corner of a status bar: three pairs
// of diagonal ridges. Dragging it resizes the window (the compositor does
// the resizing). Hidden while the window is maximized, like Win95's.
Item {
    id: grip

    width: 12
    height: 12
    visible: !!Window.window && Window.window.visibility !== Window.Maximized
             && Window.window.visibility !== Window.FullScreen

    Canvas {
        anchors.fill: parent
        onPaint: {
            var ctx = getContext("2d");
            ctx.reset();
            // each ridge: one light diagonal, then two shadow ones
            for (var r = 0; r < 3; ++r) {
                for (var k = 0; k < 3; ++k) {
                    var d = r * 4 + k; // distance of this diagonal from the corner's far edge
                    ctx.fillStyle = k === 0 ? Theme.highlight : Theme.shadow;
                    for (var x = d; x < 12; ++x)
                        ctx.fillRect(x, 11 - (x - d), 1, 1);
                }
            }
        }
    }

    MouseArea {
        anchors.fill: parent
        anchors.margins: -2
        cursorShape: Qt.SizeFDiagCursor
        onPressed: {
            if (grip.Window.window)
                grip.Window.window.startSystemResize(Qt.RightEdge | Qt.BottomEdge);
        }
    }
}
