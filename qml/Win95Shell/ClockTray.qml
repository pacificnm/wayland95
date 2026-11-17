import QtQuick
import Win95Ui as Ui
import Win95Shell

// The taskbar clock. Hover for the date; click for a month calendar;
// double-click for Control Panel > Date/Time.
TrayIcon {
    id: clockTray

    property date now: new Date()
    // the month the calendar shows (the first of it)
    property int viewYear: now.getFullYear()
    property int viewMonth: now.getMonth()
    readonly property string timeFormat: Ui.Wayland95Settings.clock24Hour ? "HH:mm" : "h:mm ap"

    function showMonth(year, month) {
        var d = new Date(year, month, 1);
        viewYear = d.getFullYear();
        viewMonth = d.getMonth();
    }
    function goToday() {
        now = new Date();
        showMonth(now.getFullYear(), now.getMonth());
    }

    width: timeMetrics.advanceWidth + 12
    toolTip: Qt.formatDate(now, "dddd, MMMM d, yyyy")
    popupWidth: 236
    popupHeight: 300

    TextMetrics {
        id: timeMetrics
        font.family: Theme.fontFamily
        font.pixelSize: Theme.fontSize
        // a wide time, so the clock doesn't change width every minute
        text: Qt.formatTime(new Date(2000, 0, 1, 12, 58), clockTray.timeFormat)
    }

    icon: Component {
        Text {
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
            text: Qt.formatTime(clockTray.now, clockTray.timeFormat)
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontSize
        }
    }

    // wakes when the minute turns; every second only while the pop-up (with
    // its seconds) is open
    function untilNextTick() {
        return popupOpen ? 1000 - (Date.now() % 1000) : 60000 - (Date.now() % 60000) + 10;
    }
    Timer {
        id: tick
        interval: clockTray.untilNextTick()
        running: true
        repeat: true
        onTriggered: { clockTray.now = new Date(); interval = clockTray.untilNextTick(); }
    }
    onPopupOpenChanged: { now = new Date(); tick.interval = untilNextTick(); tick.restart(); }
    // the time zone changed (Control Panel > Date/Time): the new time at once
    Connections {
        target: Ui.TimeZones
        function onCurrentChanged() { clockTray.now = new Date(); tick.interval = clockTray.untilNextTick(); tick.restart(); }
    }

    onPopupOpened: goToday()
    onDoubleClicked: Launcher.launch("win95control", ["--applet", "datetime"])

    // ------------------------------------------------------------ pop-up
    Text {
        x: 10
        y: 10
        text: Qt.formatDate(clockTray.now, "dddd, MMMM d, yyyy")
        font.family: Theme.fontFamily
        font.pixelSize: Theme.fontSize
        font.bold: true
    }
    Text {
        x: 10
        y: 28
        text: Qt.formatTime(clockTray.now, Ui.Wayland95Settings.clock24Hour ? "HH:mm:ss" : "h:mm:ss ap")
        font.family: Theme.fontFamily
        font.pixelSize: 20
    }

    // month navigation
    Item {
        id: nav
        x: 10
        y: 62
        width: parent.width - 20
        height: 22
        Ui.Button95 {
            width: 24
            height: 22
            text: "◄"
            onClicked: clockTray.showMonth(clockTray.viewYear, clockTray.viewMonth - 1)
        }
        Text {
            anchors.centerIn: parent
            text: Qt.formatDate(new Date(clockTray.viewYear, clockTray.viewMonth, 1), "MMMM yyyy")
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontSize
            font.bold: true
        }
        Ui.Button95 {
            anchors.right: parent.right
            width: 24
            height: 22
            text: "►"
            onClicked: clockTray.showMonth(clockTray.viewYear, clockTray.viewMonth + 1)
        }
    }

    // the month: a white sunken box with day names and 6 weeks of days
    Ui.Bevel {
        id: monthBox
        x: 10
        y: nav.y + nav.height + 6
        width: parent.width - 20
        height: 7 * 20 + 6
        style: "sunken"
        face: Ui.Theme.window

        // 0 = Sunday; Control Panel > Regional Settings > Date chooses
        readonly property int firstDay: {
            var r = Ui.Wayland95Settings.region;
            if (r.firstDayOfWeek >= 0)
                return r.firstDayOfWeek % 7;
            return Qt.locale(r.date || r.time || r.region || Qt.locale().name).firstDayOfWeek % 7;
        }
        readonly property real cellWidth: (width - 4) / 7

        Row {
            x: 2
            y: 3
            Repeater {
                model: 7
                Text {
                    required property int index
                    width: monthBox.cellWidth
                    horizontalAlignment: Text.AlignHCenter
                    // Sunday 2000-01-02, plus the weekday offset
                    text: Qt.locale().dayName((monthBox.firstDay + index) % 7, Locale.ShortFormat).substring(0, 2)
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontSize
                    font.bold: true
                }
            }
        }
        Rectangle { x: 4; y: 19; width: parent.width - 8; height: 1; color: Theme.shadow }

        Grid {
            x: 2
            y: 22
            columns: 7
            Repeater {
                model: 42
                Item {
                    id: cell
                    required property int index
                    readonly property date day: {
                        var first = new Date(clockTray.viewYear, clockTray.viewMonth, 1);
                        var offset = (first.getDay() - monthBox.firstDay + 7) % 7;
                        return new Date(clockTray.viewYear, clockTray.viewMonth, 1 - offset + index);
                    }
                    readonly property bool inMonth: day.getMonth() === clockTray.viewMonth
                    readonly property bool isToday: day.getFullYear() === clockTray.now.getFullYear()
                                                    && day.getMonth() === clockTray.now.getMonth()
                                                    && day.getDate() === clockTray.now.getDate()
                    width: monthBox.cellWidth
                    height: 20
                    Rectangle {
                        anchors.centerIn: parent
                        width: 24
                        height: 17
                        visible: cell.isToday
                        color: Ui.Theme.selection
                    }
                    Text {
                        anchors.centerIn: parent
                        text: cell.day.getDate()
                        color: cell.isToday ? Theme.selectionText : cell.inMonth ? Theme.text : Theme.shadow
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.fontSize
                    }
                }
            }
        }
    }

    Row {
        anchors.right: parent.right
        anchors.rightMargin: 10
        anchors.bottom: parent.bottom
        anchors.bottomMargin: 10
        spacing: 6
        Ui.Button95 {
            text: "<u>T</u>oday"
            onClicked: clockTray.goToday()
        }
        Ui.Button95 {
            text: "<u>D</u>ate/Time Settings..."
            onClicked: {
                clockTray.closePopup();
                Launcher.launch("win95control", ["--applet", "datetime"]);
            }
        }
    }
}
