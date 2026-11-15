import QtQuick
import Win95Ui as Ui
import Win95Shell

// The little panel that shows the brightness or volume level when a
// laptop key changes it, then fades away.
Item {
    id: osd

    property string kind: "brightness" // or "volume"
    property int level: 0
    property bool muted: false

    function show(newKind, newLevel, isMuted) {
        kind = newKind;
        level = newLevel;
        muted = !!isMuted;
        icon.requestPaint();
        opacity = 1;
        hideTimer.restart();
    }

    width: 240
    height: 54
    opacity: 0
    visible: opacity > 0
    Behavior on opacity { NumberAnimation { duration: 200 } }
    Timer { id: hideTimer; interval: 1500; onTriggered: osd.opacity = 0 }

    Bevel { anchors.fill: parent; raised: true }

    Canvas {
        id: icon
        x: 12
        anchors.verticalCenter: parent.verticalCenter
        width: 28
        height: 28
        onPaint: {
            var ctx = getContext("2d");
            ctx.reset();
            ctx.strokeStyle = "#000000";
            ctx.fillStyle = "#000000";
            ctx.lineWidth = 2;
            if (osd.kind === "brightness") {
                // a sun
                ctx.fillStyle = "#ffd000";
                ctx.beginPath(); ctx.arc(14, 14, 6, 0, Math.PI * 2); ctx.fill(); ctx.stroke();
                for (var r = 0; r < 8; ++r) {
                    var a = r * Math.PI / 4;
                    ctx.beginPath();
                    ctx.moveTo(14 + Math.cos(a) * 9, 14 + Math.sin(a) * 9);
                    ctx.lineTo(14 + Math.cos(a) * 13, 14 + Math.sin(a) * 13);
                    ctx.stroke();
                }
            } else {
                // a speaker, crossed out when muted
                ctx.fillStyle = "#808080";
                ctx.lineWidth = 1.5;
                ctx.beginPath(); ctx.moveTo(3, 10); ctx.lineTo(9, 10); ctx.lineTo(16, 3); ctx.lineTo(16, 25);
                ctx.lineTo(9, 18); ctx.lineTo(3, 18); ctx.closePath(); ctx.fill(); ctx.stroke();
                if (osd.muted) {
                    ctx.strokeStyle = "#ff0000"; ctx.lineWidth = 2.5;
                    ctx.beginPath(); ctx.moveTo(19, 9); ctx.lineTo(27, 19); ctx.moveTo(27, 9); ctx.lineTo(19, 19); ctx.stroke();
                } else {
                    ctx.beginPath(); ctx.arc(16, 14, 5, -0.8, 0.8); ctx.stroke();
                    ctx.beginPath(); ctx.arc(16, 14, 9, -0.8, 0.8); ctx.stroke();
                }
            }
        }
    }

    // a Win95 progress bar of blue blocks
    Ui.Bevel {
        id: bar
        x: 52
        anchors.verticalCenter: parent.verticalCenter
        width: parent.width - x - 14
        height: 18
        style: "sunken"
        face: Ui.Theme.window
        Row {
            x: 3
            y: 3
            spacing: 2
            Repeater {
                model: 20
                Rectangle {
                    required property int index
                    width: (bar.width - 6 - 19 * 2) / 20
                    height: bar.height - 6
                    color: osd.muted ? "#808080" : Ui.Theme.selection
                    visible: index < Math.round(osd.level / 5)
                }
            }
        }
    }
}
