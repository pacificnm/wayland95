import QtQuick
import QtMultimedia
import Win95Ui

// MediaPlayer: sound or video: MP3, WAV, FLAC, OGG, M4A/AAC, Opus; MP4,
// WebM, MKV, AVI, MOV... (what this computer's FFmpeg plays). A file of the
// project (put inside the program when it's made, unless Embed is off), a
// path, or a web address. A video shows in it; a sound shows ♪ and its
// title. ShowControls puts Win95's ▶ ❚❚ ■, a position bar, the time and a
// speaker under it. Invisible, it still plays: a sound for the form.
//
// With a DataField it plays the file (a path or a web address) that field
// holds.
//
//   player1.play()  pause()  stop()  seek(ms)     player1.source = "/home/me/song.mp3"
//   player1.isPlaying  position  duration (ms)    player1.hasVideo  title  errorString
AppElement {
    id: player

    property string source: ""
    property bool embed: true       // (the compiler's: the file goes inside the program)
    property bool autoPlay: false
    property bool loop: false
    property int volume: 100
    property bool muted: false
    property bool showControls: true
    property string fillMode: "Fit"
    signal started()
    signal paused()
    signal stopped()
    signal finished()
    signal failed()

    defaultBack: "#000000"
    activeFocusOnTab: !designMode && showControls

    // what plays: its DataField's file, or Source (a path becomes a file:// URL)
    function urlOf(s) {
        s = s === undefined || s === null ? "" : String(s).trim();
        if (s === "" || /^[a-z][a-z0-9+.-]*:/i.test(s))
            return s;
        if (s.charAt(0) === "~")
            s = Win95Fs.homePath() + s.substring(1);
        return "file://" + encodeURI(s);
    }
    readonly property string playing: bound ? urlOf(fieldValue) : urlOf(source)
    readonly property bool isPlaying: media.playbackState === MediaPlayer.PlayingState
    readonly property bool isPaused: media.playbackState === MediaPlayer.PausedState
    readonly property int position: media.position
    readonly property int duration: media.duration
    readonly property bool hasVideo: media.hasVideo
    readonly property bool hasAudio: media.hasAudio
    // what went wrong last ("" while all is well)
    property string errorString: ""
    // the file's own title, else its name
    readonly property string fileName: decodeURIComponent(String(playing).replace(/[?#].*$/, "").replace(/^.*\//, ""))
    readonly property string title: {
        var t = media.metaData.stringValue(MediaMetaData.Title);
        return t ? t : (designMode ? String(source).replace(/^.*\//, "") : fileName);
    }

    function play() { if (!designMode && playing !== "") media.play(); }
    function pause() { media.pause(); }
    function stop() { media.stop(); }
    function seek(ms) { media.position = Math.max(0, Math.min(Number(ms) || 0, media.duration > 0 ? media.duration : Number(ms) || 0)); }
    function toggle() { if (isPlaying) pause(); else play(); }

    // "1:05", "1:02:09"
    function clock(ms) {
        var s = Math.max(0, Math.floor((ms || 0) / 1000)), h = Math.floor(s / 3600), m = Math.floor(s / 60) % 60;
        s = s % 60;
        return (h > 0 ? h + ":" + (m < 10 ? "0" : "") : "") + m + ":" + (s < 10 ? "0" : "") + s;
    }

    MediaPlayer {
        id: media
        source: player.designMode ? "" : player.playing
        loops: player.loop ? MediaPlayer.Infinite : 1
        audioOutput: AudioOutput {
            volume: Math.max(0, Math.min(100, player.volume)) / 100
            muted: player.muted
        }
        videoOutput: video
        property bool wasPlaying: false
        onPlaybackStateChanged: {
            if (playbackState === MediaPlayer.PlayingState) {
                wasPlaying = true;
                player.errorString = "";
                player.started();
            } else if (playbackState === MediaPlayer.PausedState) {
                player.paused();
            } else if (wasPlaying) {
                wasPlaying = false;
                // (at the end the state is Stopped before the status is EndOfMedia)
                Qt.callLater(function () {
                    if (media.mediaStatus === MediaPlayer.EndOfMedia)
                        player.finished();
                    else
                        player.stopped();
                });
            }
        }
        onErrorOccurred: (error, errorString) => {
            player.errorString = errorString !== "" ? errorString : "It can't be played.";
            player.failed();
        }
        onSourceChanged: {
            player.errorString = "";
            if (player.autoPlay && source !== "" && !player.designMode)
                play();
        }
    }
    Component.onCompleted: {
        if (autoPlay && playing !== "" && !designMode)
            media.play();
    }

    // the picture: a video, or ♪ and its title
    Rectangle {
        id: screen
        width: parent.width
        height: parent.height - (player.showControls ? bar.height : 0)
        visible: height > 4
        color: player.fillColor
        clip: true
        VideoOutput {
            id: video
            anchors.fill: parent
            fillMode: player.fillMode === "Stretch" ? VideoOutput.Stretch
                    : player.fillMode === "Crop" ? VideoOutput.PreserveAspectCrop : VideoOutput.PreserveAspectFit
        }
        Column {
            visible: !player.hasVideo
            anchors.centerIn: parent
            width: parent.width - 8
            spacing: 2
            Text {
                width: parent.width
                horizontalAlignment: Text.AlignHCenter
                visible: screen.height >= 40
                text: player.designMode ? "▶" : "♪"
                color: player.foreColor !== "" ? player.foreColor : "#c0c0c0"
                font.pixelSize: Math.min(32, screen.height / 2)
            }
            Text {
                width: parent.width
                horizontalAlignment: Text.AlignHCenter
                elide: Text.ElideMiddle
                text: player.errorString !== "" ? player.errorString
                    : player.title !== "" ? player.title : (player.designMode ? "MediaPlayer" : "")
                color: player.errorString !== "" ? "#ff6060" : player.foreColor !== "" ? player.foreColor : "#c0c0c0"
                font.family: player.family
                font.pixelSize: player.pixelSize
                font.bold: player.fontBold
            }
        }
        MouseArea {
            anchors.fill: parent
            enabled: !player.designMode
            onClicked: {
                player.forceActiveFocus();
                player.toggle();
            }
        }
    }

    // ▶ ❚❚ ■, where it is, the time, the speaker
    Item {
        id: bar
        visible: player.showControls
        y: parent.height - height
        width: parent.width
        height: 26
        Bevel { anchors.fill: parent; style: "raised"; face: Theme.face }
        Row {
            id: buttons
            x: 3
            y: 3
            spacing: 1
            Repeater {
                model: [{ glyph: "▶", what: "play" }, { glyph: "❚❚", what: "pause" }, { glyph: "■", what: "stop" }]
                delegate: Item {
                    id: control
                    required property var modelData
                    readonly property bool on: (modelData.what === "play" && player.isPlaying) || (modelData.what === "pause" && player.isPaused)
                    width: 22
                    height: 20
                    Bevel { anchors.fill: parent; style: controlMouse.pressed || control.on ? "pressed" : "raised" }
                    Text {
                        anchors.centerIn: parent
                        text: control.modelData.glyph
                        font.pixelSize: control.modelData.what === "pause" ? 8 : 10
                        color: player.enabled && !player.designMode ? Theme.text : Theme.disabledText
                    }
                    MouseArea {
                        id: controlMouse
                        anchors.fill: parent
                        enabled: player.enabled && !player.designMode
                        onClicked: {
                            player.forceActiveFocus();
                            if (control.modelData.what === "play")
                                player.play();
                            else if (control.modelData.what === "pause")
                                player.pause();
                            else
                                player.stop();
                        }
                    }
                }
            }
        }
        // where it is: a groove and a thumb, dragged or clicked to go there
        Item {
            id: track
            x: buttons.x + buttons.width + 6
            width: time.x - x - 6
            height: parent.height
            readonly property real ratio: player.duration > 0 ? Math.min(1, (seeking.pressed ? seeking.at : player.position) / player.duration) : 0
            Bevel { x: 0; y: 11; width: parent.width; height: 4; style: "sunken"; face: Theme.face }
            Bevel {
                x: Math.round(track.ratio * (track.width - width))
                y: 5
                width: 10
                height: 16
                style: "raised"
                visible: player.duration > 0 || player.designMode
            }
            MouseArea {
                id: seeking
                anchors.fill: parent
                enabled: player.enabled && !player.designMode && player.duration > 0
                property real at: 0
                function place(x) { at = Math.max(0, Math.min(1, x / Math.max(1, width))) * player.duration; }
                onPressed: (mouse) => place(mouse.x)
                onPositionChanged: (mouse) => place(mouse.x)
                onReleased: player.seek(at)
            }
        }
        Text {
            id: time
            anchors.right: speaker.left
            anchors.rightMargin: 6
            anchors.verticalCenter: parent.verticalCenter
            text: player.clock(player.position) + " / " + player.clock(player.duration)
            color: Theme.text
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontSize - 1
        }
        Item {
            id: speaker
            anchors.right: parent.right
            anchors.rightMargin: 3
            y: 3
            width: 22
            height: 20
            Bevel { anchors.fill: parent; style: muteMouse.pressed || player.muted ? "pressed" : "raised" }
            Text {
                anchors.centerIn: parent
                text: player.muted ? "🔇" : "🔊"
                font.pixelSize: 11
                color: Theme.text
            }
            MouseArea {
                id: muteMouse
                anchors.fill: parent
                enabled: player.enabled && !player.designMode
                onClicked: player.muted = !player.muted
            }
        }
    }

    // Space plays and pauses; Left and Right go back and on five seconds
    Keys.onPressed: (event) => {
        if (designMode)
            return;
        if (event.key === Qt.Key_Space)
            toggle();
        else if (event.key === Qt.Key_Left)
            seek(position - 5000);
        else if (event.key === Qt.Key_Right)
            seek(position + 5000);
        else
            return;
        event.accepted = true;
    }
}
