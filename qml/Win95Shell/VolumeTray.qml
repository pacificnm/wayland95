import QtQuick
import Win95Ui as Ui
import Win95Shell

// Volume: click for a slider and Mute, double-click for Volume Control,
// scroll to change the volume. Muted shows a red "no" sign, as in Win95.
// While a program says it's playing (MPRIS: MediaPlayers), the popup has
// what it's playing beside the slider: its picture, title and artist, and
// ⏮ ⏯ ⏭ (the keyboard's media keys go to the same one).
TrayIcon {
    id: volume

    toolTip: Ui.Audio.muted ? "Volume: muted" : "Volume: " + Math.round(Ui.Audio.volume * 100) + "%"
    readonly property bool playing: Ui.MediaPlayers.available
    popupWidth: playing ? 252 : 72
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

    // (the left 72 pixels: the volume, as it's always been)
    Text {
        x: Math.round((72 - width) / 2)
        y: 8
        text: "Volume"
        font.family: Theme.fontFamily
        font.pixelSize: Theme.fontSize
    }
    Ui.VSlider95 {
        id: slider
        x: Math.round((72 - width) / 2)
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
    // what's playing, and the buttons for it
    Item {
        id: nowPlaying
        visible: volume.playing
        x: 72
        width: 180
        height: 178
        // (an etched line between it and the slider)
        Rectangle { x: 0; y: 8; width: 1; height: parent.height - 16; color: Ui.Theme.dark }
        Rectangle { x: 1; y: 8; width: 1; height: parent.height - 16; color: Ui.Theme.highlight }
        Text {
            x: 10
            y: 8
            text: "Now Playing"
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontSize
        }
        Rectangle {
            id: art
            x: 10
            y: 28
            width: 56
            height: 56
            color: "#000000"
            border.color: Ui.Theme.dark
            Image {
                anchors.fill: parent
                anchors.margins: 1
                source: Ui.MediaPlayers.artUrl
                fillMode: Image.PreserveAspectFit
                asynchronous: true
                visible: status === Image.Ready
            }
            Text {
                anchors.centerIn: parent
                visible: Ui.MediaPlayers.artUrl === ""
                text: "♪"
                color: "#c0c0c0"
                font.pixelSize: 26
            }
        }
        Column {
            x: 74
            y: 28
            width: 98
            spacing: 2
            Text {
                id: songTitle
                width: parent.width
                text: Ui.MediaPlayers.title !== "" ? Ui.MediaPlayers.title : Ui.MediaPlayers.identity
                wrapMode: Text.Wrap
                maximumLineCount: 3
                elide: Text.ElideRight
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontSize
                font.bold: true
            }
            Text {
                width: parent.width
                visible: text !== ""
                text: Ui.MediaPlayers.artist
                elide: Text.ElideRight
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontSize
            }
        }
        Text {
            x: 10
            y: 92
            width: 162
            text: Ui.MediaPlayers.identity + (Ui.MediaPlayers.status === "Paused" ? " (paused)" : "")
            color: Ui.Theme.dark
            elide: Text.ElideRight
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontSize - 1
        }
        Row {
            x: 10
            y: 118
            spacing: 4
            Repeater {
                model: [{ icon: "previous", tip: "Previous" }, { icon: "", tip: "Play or pause" }, { icon: "next", tip: "Next" }]
                delegate: Item {
                    id: mediaButton
                    required property var modelData
                    required property int index
                    width: 36
                    height: 26
                    readonly property string iconName: modelData.icon !== "" ? modelData.icon
                                                       : (Ui.MediaPlayers.status === "Playing" ? "pause" : "play")
                    readonly property bool can: index === 0 ? Ui.MediaPlayers.canGoPrevious : index === 2 ? Ui.MediaPlayers.canGoNext : true
                    Ui.Bevel { anchors.fill: parent; style: buttonMouse.pressed && buttonMouse.containsMouse ? "pressed" : "raised" }
                    Ui.Icon95 {
                        anchors.centerIn: parent
                        anchors.horizontalCenterOffset: buttonMouse.pressed && buttonMouse.containsMouse ? 1 : 0
                        anchors.verticalCenterOffset: buttonMouse.pressed && buttonMouse.containsMouse ? 1 : 0
                        name: mediaButton.iconName
                        enabled: mediaButton.can
                    }
                    MouseArea {
                        id: buttonMouse
                        anchors.fill: parent
                        enabled: mediaButton.can
                        onClicked: {
                            if (mediaButton.index === 0)
                                Ui.MediaPlayers.previous();
                            else if (mediaButton.index === 2)
                                Ui.MediaPlayers.next();
                            else
                                Ui.MediaPlayers.playPause();
                        }
                    }
                }
            }
        }
        // (the player's window to the front)
        Ui.Button95 {
            x: 10
            y: 150
            width: 116
            text: "Show the Player"
            // (the shell can bring a minimized window back; a program can't itself)
            onClicked: {
                if (!WindowManager.showApp(Ui.MediaPlayers.desktopEntry))
                    Ui.MediaPlayers.raise();
                volume.closePopup();
            }
        }
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
