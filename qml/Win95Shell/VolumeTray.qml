import QtQuick
import Win95Ui as Ui
import Win95Shell

// Volume: click for a slider and Mute, double-click for Volume Control,
// scroll to change the volume. Muted shows a red "no" sign, as in Win95.
TrayIcon {
    id: volume

    toolTip: Ui.Audio.muted ? "Volume: muted" : "Volume: " + Math.round(Ui.Audio.volume * 100) + "%"
    popupWidth: 72
    popupHeight: 178

    icon: Component {
        Canvas {
            property bool muted: Ui.Audio.muted
            onMutedChanged: requestPaint()
            onPaint: {
                var ctx = getContext("2d");
                ctx.reset();
                ctx.fillStyle = "#c0c0c0";
                ctx.strokeStyle = "#000000";
                ctx.lineWidth = 1;
                ctx.fillRect(2, 6, 3, 5); ctx.strokeRect(2.5, 6.5, 3, 4);
                ctx.beginPath(); ctx.moveTo(5.5, 6.5); ctx.lineTo(9.5, 2.5); ctx.lineTo(9.5, 14.5); ctx.lineTo(5.5, 10.5);
                ctx.closePath(); ctx.fillStyle = "#808080"; ctx.fill(); ctx.stroke();
                if (muted) {
                    ctx.strokeStyle = "#ff0000";
                    ctx.lineWidth = 1.5;
                    ctx.beginPath(); ctx.arc(11, 11, 4, 0, Math.PI * 2); ctx.stroke();
                    ctx.beginPath(); ctx.moveTo(8.2, 13.8); ctx.lineTo(13.8, 8.2); ctx.stroke();
                } else {
                    ctx.beginPath(); ctx.arc(9, 8.5, 3, -0.8, 0.8); ctx.stroke();
                    ctx.beginPath(); ctx.arc(9, 8.5, 5.5, -0.8, 0.8); ctx.stroke();
                }
            }
        }
    }

    onDoubleClicked: Launcher.launch("win95volume")
    onWheel: (delta) => Ui.Audio.setVolume(Ui.Audio.outputId,
                                           Math.max(0, Math.min(1, Ui.Audio.volume + (delta > 0 ? 0.05 : -0.05))))
    onPopupOpened: slider.forceActiveFocus()

    Text {
        anchors.horizontalCenter: parent.horizontalCenter
        y: 8
        text: "Volume"
        font.family: Theme.fontFamily
        font.pixelSize: Theme.fontSize
    }
    Ui.VSlider95 {
        id: slider
        anchors.horizontalCenter: parent.horizontalCenter
        y: 28
        height: 110
        from: 0
        to: 100
        onMoved: Ui.Audio.setVolume(Ui.Audio.outputId, value / 100)
        onReleased: Ui.Wayland95Settings.playSound("default")
    }
    Binding {
        target: slider
        property: "value"
        value: Math.round(Math.min(1, Ui.Audio.volume) * 100)
        when: !slider.pressed
    }
    Ui.CheckBox95 {
        id: muteBox
        x: 10
        y: slider.y + slider.height + 10
        text: "<u>M</u>ute"
        onToggled: (on) => Ui.Audio.setMuted(Ui.Audio.outputId, on)
    }
    Binding {
        target: muteBox
        property: "checked"
        value: Ui.Audio.muted
    }
}
