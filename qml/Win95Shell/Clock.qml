import QtQuick
import Win95Ui as Ui

// The taskbar clock (Control Panel > Taskbar: Show Clock, 24-hour clock).
Item {
    id: clock
    width: label.implicitWidth + 12
    height: 20

    // drawn with its own sunken box, unless it sits in the taskbar's tray
    property bool framed: true

    readonly property string format: Ui.Wayland95Settings.clock24Hour ? "HH:mm" : "h:mm ap"
    function refresh() { label.text = Qt.formatTime(new Date(), format); }
    onFormatChanged: refresh()

    Bevel { anchors.fill: parent; raised: false; visible: clock.framed }

    Text {
        id: label
        anchors.centerIn: parent
        font.family: Theme.fontFamily
        font.pixelSize: Theme.fontSize
        Component.onCompleted: clock.refresh()
    }

    Timer {
        // tick on the second so the minute flips on time
        interval: 1000
        running: true
        repeat: true
        onTriggered: clock.refresh()
    }
}
