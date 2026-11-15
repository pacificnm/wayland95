import QtQuick

Item {
    id: startButton
    width: 70

    signal clicked()
    property bool down: false
    // held in while the Start menu is open, as in Win95
    property bool active: false
    readonly property bool pushed: down || active

    Bevel { anchors.fill: parent; raised: !startButton.pushed }

    Row {
        anchors.centerIn: parent
        anchors.horizontalCenterOffset: startButton.pushed ? 1 : 0
        anchors.verticalCenterOffset: startButton.pushed ? 1 : 0
        spacing: 4

        // generic four-color square, standing in for the real Windows logo
        Grid {
            columns: 2
            rows: 2
            width: 14
            height: 14
            Rectangle { width: 6; height: 6; color: "#ff0000" }
            Rectangle { width: 6; height: 6; color: "#00a000" }
            Rectangle { width: 6; height: 6; color: "#0000ff" }
            Rectangle { width: 6; height: 6; color: "#ffcc00" }
        }

        Text {
            text: "Start"
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontSize
            font.bold: true
        }
    }

    // the dotted focus rectangle Win95 draws while the menu is open
    Loader {
        active: startButton.active
        anchors.fill: parent
        anchors.margins: 4
        sourceComponent: Canvas {
            onPaint: {
                var ctx = getContext("2d");
                ctx.fillStyle = "#000000";
                var w = Math.floor(width), h = Math.floor(height);
                for (var x = 0; x < w; x += 2) { ctx.fillRect(x, 0, 1, 1); ctx.fillRect(x, h - 1, 1, 1); }
                for (var y = 0; y < h; y += 2) { ctx.fillRect(0, y, 1, 1); ctx.fillRect(w - 1, y, 1, 1); }
            }
        }
    }

    MouseArea {
        anchors.fill: parent
        // react on press, like Win95, so the menu opens immediately
        onPressed: { startButton.down = true; startButton.clicked(); }
        onReleased: startButton.down = false
        onCanceled: startButton.down = false
    }
}
