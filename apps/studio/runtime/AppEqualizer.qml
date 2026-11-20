import QtQuick
import Win95Ui

// Equalizer: a MediaPlayer's equalizer, the way a music player shows it:
// On, a list of presets, the preamp, and a slider for each of the ten
// bands (31 Hz to 16 kHz, -12 to +12 dB). Player is the MediaPlayer's
// Name; what it changes is the player's (player1.equalizer, eqGains,
// eqPreamp), so code can read and set them too. Its event is changed.
//
//   eq1.player = "player2"     eq1.target (the MediaPlayer)
AppElement {
    id: eq

    property string player: ""
    property bool showPresets: true
    property bool showPreamp: true
    signal changed()

    activeFocusOnTab: false

    // the MediaPlayer (found once the form has made all its elements)
    property var target: null
    function attach() {
        var p = !designMode && form && player !== "" ? form.element(player) : null;
        target = p && p.eqGains !== undefined ? p : null;
        if (player !== "" && !designMode && form && !target)
            console.warn(form.formName + ", " + objectName + ": there's no MediaPlayer named " + player + " on the form");
    }
    onPlayerChanged: Qt.callLater(attach)
    onFormChanged: Qt.callLater(attach)
    Component.onCompleted: Qt.callLater(attach)

    readonly property var gains: target ? target.eqGains : [0, 0, 0, 0, 0, 0, 0, 0, 0, 0]
    readonly property var names: ["31", "62", "125", "250", "500", "1K", "2K", "4K", "8K", "16K"]
    readonly property var presets: ["Flat", "Rock", "Pop", "Jazz", "Classical", "Dance", "Metal", "Bass Boost",
                                    "Treble Boost", "Vocal", "Loudness", "Headphones"]
    readonly property string presetNow: target ? target.eqPresetNow : "Flat"
    readonly property bool usable: target !== null && enabled

    // the band sliders' left edge: after the scale, and the preamp
    readonly property int bandsX: showPreamp ? 76 : 36
    readonly property int slidersTop: 32
    readonly property int sliderHeight: Math.max(40, height - slidersTop - 22)

    // On, and the presets
    CheckBox95 {
        id: onBox
        x: 4
        y: 6
        text: "On"
        enabled: eq.usable
        checked: eq.target ? eq.target.equalizer : false
        onToggled: (on) => {
            if (eq.target) {
                eq.target.equalizer = on;
                eq.changed();
            }
        }
    }
    Text {
        visible: eq.showPresets
        x: 56
        y: 8
        text: "Preset:"
        color: eq.enabled ? Theme.text : Theme.disabledText
        font.family: Theme.fontFamily
        font.pixelSize: Theme.fontSize
    }
    ComboBox95 {
        id: presetBox
        visible: eq.showPresets
        x: 104
        y: 4
        width: 132
        enabled: eq.usable
        popupMenu: eq.form && !eq.designMode ? eq.form.popupMenu : null
        model: eq.presets.concat(eq.presetNow === "Custom" ? ["Custom"] : [])
        currentIndex: Math.max(0, model.indexOf(eq.presetNow))
        onActivated: (i) => {
            if (eq.target && i < eq.presets.length) {
                eq.target.applyEqPreset(eq.presets[i]);
                eq.changed();
            }
        }
    }
    Button95 {
        visible: eq.showPresets
        x: 244
        y: 3
        width: 56
        text: "Flat"
        enabled: eq.usable && eq.presetNow !== "Flat"
        onClicked: {
            eq.target.applyEqPreset("Flat");
            if (eq.target.eqPreamp !== 0)
                eq.target.eqPreamp = 0;
            eq.changed();
        }
    }

    // the scale
    Repeater {
        model: [{ t: "+12", at: 0 }, { t: "0", at: 0.5 }, { t: "-12", at: 1 }]
        delegate: Text {
            required property var modelData
            x: 0
            width: 26
            horizontalAlignment: Text.AlignRight
            y: eq.slidersTop + 5 + Math.round((eq.sliderHeight - 10) * modelData.at) - height / 2
            text: modelData.t
            color: eq.enabled ? Theme.text : Theme.disabledText
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontSize - 2
        }
    }

    // the preamp
    VSlider95 {
        id: preamp
        visible: eq.showPreamp
        x: 32
        y: eq.slidersTop
        height: eq.sliderHeight
        from: -12
        to: 12
        stepSize: 1
        tickCount: 5
        enabled: eq.usable
        value: eq.target ? eq.target.eqPreamp : 0
        onMoved: {
            if (eq.target) {
                eq.target.eqPreamp = Math.round(value);
                eq.changed();
            }
            value = Qt.binding(function () { return eq.target ? eq.target.eqPreamp : 0; });
        }
    }
    Text {
        visible: eq.showPreamp
        x: 30
        width: 34
        y: eq.slidersTop + eq.sliderHeight + 4
        horizontalAlignment: Text.AlignHCenter
        text: "Pre"
        color: eq.enabled ? Theme.text : Theme.disabledText
        font.family: Theme.fontFamily
        font.pixelSize: Theme.fontSize - 2
    }

    // the bands
    Repeater {
        model: 10
        delegate: Item {
            id: band
            required property int index
            x: eq.bandsX + index * Math.max(30, Math.floor((eq.width - eq.bandsX) / 10))
            y: eq.slidersTop
            width: 30
            height: eq.sliderHeight + 22
            VSlider95 {
                id: s
                height: eq.sliderHeight
                from: -12
                to: 12
                stepSize: 1
                tickCount: 5
                enabled: eq.usable
                value: eq.gains[band.index] || 0
                onMoved: {
                    if (eq.target) {
                        eq.target.setEqGain(band.index, Math.round(value));
                        eq.changed();
                    }
                    value = Qt.binding(function () { return eq.gains[band.index] || 0; });
                }
            }
            Text {
                y: eq.sliderHeight + 4
                width: 24
                horizontalAlignment: Text.AlignHCenter
                text: eq.names[band.index]
                color: eq.enabled ? Theme.text : Theme.disabledText
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontSize - 2
            }
        }
    }
}
