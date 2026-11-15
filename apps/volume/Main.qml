import QtQuick
import QtQuick.Window
import Win95Ui

// Volume Control: a column per sound source, like Win95's sndvol32.
// Playback shows the speakers ("Volume Control", whose Mute all silences
// everything) and every program playing sound; Recording shows the
// microphone. Options > Properties picks which device and which mode.
Window {
    id: window

    property bool recording: startRecording

    // the columns, rebuilt from Audio whenever something changes
    readonly property var columns: {
        if (!Audio.available)
            return [];
        if (recording) {
            if (Audio.inputId < 0)
                return [];
            return [{ id: Audio.inputId, title: "Recording Control", subtitle: "Microphone",
                      volume: Audio.inputVolume, muted: Audio.inputMuted, muteText: "<u>M</u>ute",
                      master: true }];
        }
        var list = [{ id: Audio.outputId, title: "Volume Control", subtitle: "",
                      volume: Audio.volume, muted: Audio.muted, muteText: "<u>M</u>ute all", master: true }];
        for (var i = 0; i < Audio.streams.length; ++i) {
            var s = Audio.streams[i];
            list.push({ id: s.id, title: s.name, subtitle: s.mediaName !== s.name ? s.mediaName : "",
                        volume: s.volume, muted: s.muted, muteText: "<u>M</u>ute", master: false });
        }
        return list;
    }
    readonly property int columnWidth: 96

    title: recording ? "Recording Control" : "Volume Control"
    width: Math.max(300, Math.max(1, columns.length) * (columnWidth + 2) + 12)
    height: 330
    minimumWidth: width
    maximumWidth: width
    minimumHeight: height
    maximumHeight: height
    visible: true
    color: Theme.face

    function optionsMenu() {
        return [
            { text: "Properties", action: function () { propertiesDialog.open(); } },
            { separator: true },
            { text: "Exit", action: window.close },
        ];
    }
    function helpMenu() {
        return [
            { text: "About Volume Control", action: function () {
                msgBox.show("About Volume Control",
                            "Volume Control\n\nAdjusts the volume of your speakers, of each program "
                            + "playing sound, and of your microphone (through PipeWire).",
                            "info", ["OK"], null);
            } },
        ];
    }

    MenuBar95 {
        id: menuBar
        x: 2
        y: 2
        width: parent.width - 4
        height: 20
        popupMenu: popup
        menus: [
            { title: "Options", items: window.optionsMenu },
            { title: "Help", items: window.helpMenu },
        ]
    }

    Row {
        x: 6
        y: menuBar.y + menuBar.height + 4
        Repeater {
            model: window.columns
            Row {
                id: cell
                required property var modelData
                required property int index

                // etched divider between columns
                Item {
                    visible: cell.index > 0
                    width: 2
                    height: 250
                    Rectangle { x: 0; y: 4; width: 1; height: parent.height - 8; color: Theme.shadow }
                    Rectangle { x: 1; y: 4; width: 1; height: parent.height - 8; color: Theme.highlight }
                }
                MixerColumn {
                    width: window.columnWidth
                    title: cell.modelData.title
                    subtitle: cell.modelData.subtitle
                    volume: cell.modelData.volume
                    muted: cell.modelData.muted
                    muteText: cell.modelData.muteText
                    onVolumeMoved: (v) => Audio.setVolume(cell.modelData.id, v)
                    onMuteToggled: (m) => Audio.setMuted(cell.modelData.id, m)
                    onVolumeReleased: if (cell.modelData.master && !window.recording) Wayland95Settings.playSound("default")
                }
            }
        }
    }

    Text {
        anchors.centerIn: parent
        width: parent.width - 40
        horizontalAlignment: Text.AlignHCenter
        wrapMode: Text.Wrap
        visible: window.columns.length === 0
        text: !Audio.available ? "No sound device was found.\n\nVolume Control needs PipeWire and WirePlumber."
                               : "No microphone was found."
        font.family: Theme.fontFamily
        font.pixelSize: Theme.fontSize
    }

    // status bar: which device these controls belong to
    Bevel {
        x: 2
        anchors.bottom: parent.bottom
        anchors.bottomMargin: 2
        width: parent.width - 4
        height: 20
        style: "thinSunken"
        Text {
            x: 4
            width: parent.width - 8
            anchors.verticalCenter: parent.verticalCenter
            elide: Text.ElideRight
            text: window.recording ? Audio.inputDescription : Audio.outputDescription
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontSize
        }
    }

    // Options > Properties: which device, playback or recording
    Dialog95 {
        id: propertiesDialog
        title: "Properties"
        dialogWidth: 300
        dialogHeight: 190
        property bool recordingChoice: false
        readonly property var devices: recordingChoice ? Audio.inputs : Audio.outputs

        function defaultIndex() {
            for (var i = 0; i < devices.length; ++i)
                if (devices[i].isDefault)
                    return i;
            return 0;
        }
        function open() {
            recordingChoice = window.recording;
            deviceBox.currentIndex = defaultIndex();
            show();
        }
        function accept() {
            var d = devices[deviceBox.currentIndex];
            if (d && !d.isDefault)
                Audio.setDefault(d.id);
            window.recording = recordingChoice;
            close();
        }

        Text {
            x: 12
            y: 16
            text: "Mi<u>x</u>er device:"
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontSize
        }
        ComboBox95 {
            id: deviceBox
            x: 96
            y: 12
            width: parent.width - x - 12
            popupMenu: popup
            model: propertiesDialog.devices.map(function (d) { return d.description; })
        }
        GroupBox95Local {
            x: 12
            y: 46
            width: parent.width - 24
            height: 70
        }
        Text {
            x: 20
            y: 46
            text: " Adjust volume for "
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontSize
            Rectangle { anchors.fill: parent; color: Theme.face; z: -1 }
        }
        RadioButton95 {
            x: 24
            y: 66
            text: "<u>P</u>layback"
            checked: !propertiesDialog.recordingChoice
            onClicked: {
                propertiesDialog.recordingChoice = false;
                deviceBox.currentIndex = propertiesDialog.defaultIndex();
            }
        }
        RadioButton95 {
            x: 24
            y: 88
            text: "<u>R</u>ecording"
            checked: propertiesDialog.recordingChoice
            onClicked: {
                propertiesDialog.recordingChoice = true;
                deviceBox.currentIndex = propertiesDialog.defaultIndex();
            }
        }
        Row {
            anchors.right: parent.right
            anchors.rightMargin: 10
            anchors.bottom: parent.bottom
            anchors.bottomMargin: 10
            spacing: 6
            Button95 { text: "OK"; isDefault: true; onClicked: propertiesDialog.accept() }
            Button95 { text: "Cancel"; onClicked: propertiesDialog.close() }
        }
    }

    // an etched frame (the Control Panel's GroupBox95 isn't in Win95Ui)
    component GroupBox95Local: Item {
        Rectangle { x: 0; y: 7; width: parent.width - 1; height: parent.height - 7
                    color: "transparent"; border.color: Theme.shadow }
        Rectangle { x: 1; y: 8; width: parent.width - 1; height: parent.height - 7
                    color: "transparent"; border.color: Theme.highlight; z: -1 }
    }

    // window-level overlays (last, so they stack on top)
    Menu95 { id: popup }
    MessageBox95 { id: msgBox }
}
