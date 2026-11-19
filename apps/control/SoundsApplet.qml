import QtQuick
import Win95Ui
import Win95Control

// Sounds Properties: which sound plays for each desktop event, with
// Browse, None and Preview, and two schemes; and Audio: which speakers and
// microphone to use, their levels, and the sound card's profile. Audio's
// changes happen right away (through PipeWire), like the volume control.
PropertySheet {
    id: sheet

    title: "Sounds Properties"
    titles: ["Sounds", "Audio"]

    readonly property var events: Wayland95Settings.soundEvents()
    readonly property var schemes: Wayland95Settings.soundSchemes()

    // local copy of event id -> file
    property var sounds: Wayland95Settings.sounds
    property string scheme: Wayland95Settings.soundScheme
    property int current: 0

    readonly property string currentId: events[current] ? events[current].id : ""
    readonly property string currentFile: sounds[currentId] || ""

    function setSound(id, path) {
        var copy = Object.assign({}, sounds);
        copy[id] = path;
        sounds = copy;
        scheme = "(Custom)";
        dirty = true;
    }

    function schemeNames() {
        var names = schemes.map(function (s) { return s.name; });
        if (names.indexOf(scheme) < 0)
            names.push(scheme);
        return names;
    }

    onApplyRequested: Wayland95Settings.setSounds(sounds, scheme)

    Item {
        Text {
            id: eventsLabel
            textFormat: Text.StyledText
            text: "<u>E</u>vents:"
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontSize
        }

        Bevel {
            id: eventBox
            y: eventsLabel.height + 4
            width: parent.width
            height: 156
            style: "sunken"
            face: Theme.window

            ListView {
                id: eventList
                x: 2; y: 2
                width: parent.width - 4 - (eventBar.visible ? eventBar.width : 0)
                height: parent.height - 4
                clip: true
                interactive: false
                model: sheet.events
                currentIndex: sheet.current
                keyNavigationEnabled: false
                activeFocusOnTab: true
                onCurrentIndexChanged: positionViewAtIndex(currentIndex, ListView.Contain)
                Keys.onUpPressed: sheet.current = Math.max(0, sheet.current - 1)
                Keys.onDownPressed: sheet.current = Math.min(sheet.events.length - 1, sheet.current + 1)

                delegate: Item {
                    id: eventRow
                    required property var modelData
                    required property int index
                    readonly property bool isCurrent: sheet.current === index
                    readonly property bool hasSound: !!sheet.sounds[modelData.id]
                    width: eventList.width
                    height: 18

                    // a little speaker for events that have a sound
                    Item {
                        visible: eventRow.hasSound
                        x: 3; y: 3
                        width: 12; height: 12
                        Rectangle { x: 1; y: 4; width: 3; height: 4; color: "#000000" }
                        Canvas {
                            anchors.fill: parent
                            onPaint: {
                                var ctx = getContext("2d");
                                ctx.fillStyle = "#000000";
                                ctx.beginPath(); ctx.moveTo(4, 4); ctx.lineTo(8, 1); ctx.lineTo(8, 11); ctx.lineTo(4, 8);
                                ctx.closePath(); ctx.fill();
                                ctx.strokeStyle = "#000000";
                                ctx.beginPath(); ctx.arc(8, 6, 3, -0.8, 0.8); ctx.stroke();
                            }
                        }
                    }
                    Rectangle {
                        x: 18
                        anchors.verticalCenter: parent.verticalCenter
                        width: eventText.implicitWidth + 4
                        height: 15
                        color: eventRow.isCurrent ? Theme.selection : "transparent"
                        Text {
                            id: eventText
                            x: 2
                            anchors.verticalCenter: parent.verticalCenter
                            text: eventRow.modelData.label
                            color: eventRow.isCurrent ? Theme.selectionText : Theme.text
                            font.family: Theme.fontFamily
                            font.pixelSize: Theme.fontSize
                        }
                    }
                    MouseArea {
                        anchors.fill: parent
                        onPressed: {
                            eventList.forceActiveFocus();
                            sheet.current = eventRow.index;
                        }
                        onDoubleClicked: Wayland95Settings.playFile(sheet.sounds[eventRow.modelData.id] || "")
                    }
                }

                WheelHandler { acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad; onWheel: (event) => eventBar.scrollBy(-event.angleDelta.y / 120 * 54) }
            }
            ScrollBar95 {
                id: eventBar
                anchors.right: parent.right
                anchors.rightMargin: 2
                y: 2
                height: parent.height - 4
                flickable: eventList
                stepSize: 18
            }
        }

        GroupBox95 {
            id: soundGroup
            y: eventBox.y + eventBox.height + 8
            width: parent.width
            height: 76
            title: "Sound"

            Text {
                id: nameLabel
                y: 2
                height: 21
                verticalAlignment: Text.AlignVCenter
                textFormat: Text.StyledText
                text: "<u>N</u>ame:"
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontSize
            }
            Bevel {
                id: nameBox
                x: 46
                y: 2
                width: parent.width - x
                height: 21
                style: "sunken"
                face: Theme.window
                Text {
                    x: 4
                    width: parent.width - 8
                    anchors.verticalCenter: parent.verticalCenter
                    text: sheet.currentFile ? Win95Fs.fileName(sheet.currentFile) : "(None)"
                    elide: Text.ElideMiddle
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontSize
                }
            }
            Row {
                y: 28
                x: 46
                spacing: 6
                Button95 {
                    text: "Browse..."
                    onClicked: browseDialog.openAt(sheet.currentFile ? Win95Fs.parentDir(sheet.currentFile)
                                                                     : "/usr/share/sounds", "")
                }
                Button95 {
                    text: "None"
                    enabled: sheet.currentFile.length > 0
                    onClicked: sheet.setSound(sheet.currentId, "")
                }
                Button95 {
                    text: "▶ Preview"
                    enabled: sheet.currentFile.length > 0
                    onClicked: {
                        if (!Wayland95Settings.playFile(sheet.currentFile))
                            sheet.messageBox.show("Sounds", "Cannot play this sound. Wayland 95 uses pw-play, paplay or aplay; none of them could play it.",
                                                  "warning", ["OK"], null);
                    }
                }
            }
        }

        GroupBox95 {
            y: soundGroup.y + soundGroup.height + 8
            width: parent.width
            height: 52
            title: "<u>S</u>chemes"

            ComboBox95 {
                y: 2
                width: parent.width
                popupMenu: sheet.popupMenu
                model: sheet.schemeNames()
                currentIndex: Math.max(0, model.indexOf(sheet.scheme))
                onActivated: (i) => {
                    var name = model[i];
                    for (var k = 0; k < sheet.schemes.length; ++k) {
                        if (sheet.schemes[k].name === name) {
                            sheet.sounds = sheet.schemes[k].sounds;
                            sheet.scheme = name;
                            sheet.dirty = true;
                        }
                    }
                }
            }
        }
    }

    // ----------------------------------------------------------------- Audio
    Item {
        id: audioPage
        readonly property var outputs: Audio.outputs
        readonly property var inputs: Audio.inputs
        function defaultIndex(list) {
            for (var i = 0; i < list.length; ++i)
                if (list[i].isDefault)
                    return i;
            return 0;
        }
        component Label: Text {
            height: 21
            verticalAlignment: Text.AlignVCenter
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontSize
        }

        Text {
            visible: !Audio.available
            width: parent.width
            wrapMode: Text.Wrap
            text: "No sound devices were found. Is PipeWire running?"
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontSize
        }

        GroupBox95 {
            id: playback
            visible: Audio.available
            width: parent.width
            height: 108
            title: "Playback"
            Grid {
                columns: 2
                columnSpacing: 8
                rowSpacing: 8
                verticalItemAlignment: Grid.AlignVCenter
                Label { text: "<u>P</u>lay through:" }
                ComboBox95 {
                    width: 260
                    popupMenu: sheet.popupMenu
                    model: audioPage.outputs.map(function (o) { return o.description; })
                    currentIndex: audioPage.defaultIndex(audioPage.outputs)
                    onActivated: (i) => Audio.setDefault(audioPage.outputs[i].id)
                }
                Label { text: "<u>V</u>olume:" }
                Row {
                    spacing: 8
                    Slider95 {
                        width: 170
                        from: 0; to: 100; stepSize: 1; tickCount: 11
                        value: Math.round(Audio.volume * 100)
                        onMoved: Audio.setVolume(Audio.outputId, value / 100)
                    }
                    CheckBox95 {
                        anchors.verticalCenter: parent.verticalCenter
                        text: "<u>M</u>ute"
                        checked: Audio.muted
                        onToggled: (on) => Audio.setMuted(Audio.outputId, on)
                    }
                }
                Item { width: 1; height: 1 }
                Button95 {
                    width: 120
                    text: "Play a <u>t</u>est sound"
                    onClicked: Wayland95Settings.playFile("/usr/share/sounds/freedesktop/stereo/audio-test-signal.oga")
                               || Wayland95Settings.playFile("/usr/share/sounds/freedesktop/stereo/bell.oga")
                }
            }
        }

        GroupBox95 {
            id: recording
            visible: Audio.available
            y: playback.height + 8
            width: parent.width
            height: 80
            title: "Recording"
            Grid {
                columns: 2
                columnSpacing: 8
                rowSpacing: 8
                verticalItemAlignment: Grid.AlignVCenter
                Label { text: "<u>R</u>ecord from:" }
                ComboBox95 {
                    width: 260
                    popupMenu: sheet.popupMenu
                    model: audioPage.inputs.length ? audioPage.inputs.map(function (o) { return o.description; }) : ["(none)"]
                    currentIndex: audioPage.defaultIndex(audioPage.inputs)
                    enabled: audioPage.inputs.length > 0
                    onActivated: (i) => Audio.setDefault(audioPage.inputs[i].id)
                }
                Label { text: "<u>L</u>evel:" }
                Row {
                    spacing: 8
                    Slider95 {
                        width: 170
                        from: 0; to: 100; stepSize: 1; tickCount: 11
                        enabled: audioPage.inputs.length > 0
                        value: Math.round(Audio.inputVolume * 100)
                        onMoved: Audio.setVolume(Audio.inputId, value / 100)
                    }
                    CheckBox95 {
                        anchors.verticalCenter: parent.verticalCenter
                        text: "M<u>u</u>te"
                        enabled: audioPage.inputs.length > 0
                        checked: Audio.inputMuted
                        onToggled: (on) => Audio.setMuted(Audio.inputId, on)
                    }
                }
            }
        }

        GroupBox95 {
            id: cardGroup
            visible: Audio.cards.length > 0
            y: recording.y + recording.height + 8
            width: parent.width
            height: 80
            title: "Sound card"
            property int card: 0
            readonly property var current: Audio.cards[Math.min(card, Audio.cards.length - 1)] || ({ profiles: [] })
            readonly property var choices: current.profiles.filter(function (p) { return p.available || p.index === current.profile; })
            Grid {
                columns: 2
                columnSpacing: 8
                rowSpacing: 8
                verticalItemAlignment: Grid.AlignVCenter
                Label { text: "<u>C</u>ard:" }
                ComboBox95 {
                    width: 260
                    popupMenu: sheet.popupMenu
                    model: Audio.cards.map(function (c) { return c.description; })
                    currentIndex: cardGroup.card
                    onActivated: (i) => cardGroup.card = i
                }
                Label { text: "Pr<u>o</u>file:" }
                ComboBox95 {
                    width: 260
                    popupMenu: sheet.popupMenu
                    model: cardGroup.choices.map(function (p) { return p.description; })
                    currentIndex: Math.max(0, cardGroup.choices.findIndex(function (p) { return p.index === cardGroup.current.profile; }))
                    onActivated: (i) => Audio.setProfile(cardGroup.current.id, cardGroup.choices[i].index)
                }
            }
        }

        Text {
            anchors.bottom: parent.bottom
            width: parent.width
            wrapMode: Text.Wrap
            color: Theme.shadow
            text: "Changes here happen right away. A card's profile chooses which of its outputs and inputs "
                  + "are on (speakers, headphones, HDMI)."
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontSize
        }
    }


    extras: [
        FileDialog95 {
            id: browseDialog
            title: "Browse for " + (sheet.events[sheet.current] ? sheet.events[sheet.current].label : "sound")
            messageBox: sheet.messageBox
            popupMenu: sheet.popupMenu
            defaultSuffix: ""
            filters: [
                { label: "Sounds (*.wav;*.ogg;*.oga;*.flac)", patterns: ["*.wav", "*.ogg", "*.oga", "*.flac", "*.mp3"] },
                { label: "All Files (*.*)", patterns: ["*"] },
            ]
            onAccepted: (path) => sheet.setSound(sheet.currentId, path)
        }
    ]
}
