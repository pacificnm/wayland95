import QtQuick
import Win95Ui

// One column of the Volume Control: a name, a vertical volume slider and a
// Mute check box. Shows the volume it's given and reports what the user
// does; the slider follows the real volume except while it's being dragged.
Item {
    id: column

    property string title: ""
    property string subtitle: ""
    property real volume: 0     // 0..1
    property bool muted: false
    property string muteText: "<u>M</u>ute"
    signal volumeMoved(real volume)
    signal muteToggled(bool muted)
    // the slider was let go (Win95 dings so you can hear the new volume)
    signal volumeReleased()

    width: 96
    height: 250

    Text {
        id: titleText
        x: 4
        y: 6
        width: parent.width - 8
        height: 28
        horizontalAlignment: Text.AlignHCenter
        wrapMode: Text.Wrap
        maximumLineCount: 2
        elide: Text.ElideRight
        text: column.title
        font.family: Theme.fontFamily
        font.pixelSize: Theme.fontSize
    }
    Text {
        x: 4
        y: titleText.y + titleText.height
        width: parent.width - 8
        horizontalAlignment: Text.AlignHCenter
        elide: Text.ElideRight
        text: column.subtitle
        color: Theme.shadow
        font.family: Theme.fontFamily
        font.pixelSize: Theme.fontSize
    }

    Text {
        x: 10
        y: 58
        text: "Volume:"
        font.family: Theme.fontFamily
        font.pixelSize: Theme.fontSize
    }

    VSlider95 {
        id: slider
        anchors.horizontalCenter: parent.horizontalCenter
        y: 78
        height: 128
        from: 0
        to: 100
        stepSize: 1
        tickCount: 11
        onMoved: column.volumeMoved(value / 100)
        onReleased: column.volumeReleased()
    }
    // follow the real volume, except mid-drag
    Binding {
        target: slider
        property: "value"
        value: Math.round(Math.min(1, column.volume) * 100)
        when: !slider.pressed
    }

    CheckBox95 {
        id: muteBox
        x: 10
        y: slider.y + slider.height + 10
        text: column.muteText
        onToggled: (on) => column.muteToggled(on)
    }
    Binding {
        target: muteBox
        property: "checked"
        value: column.muted
    }
}
