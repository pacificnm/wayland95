import QtQuick
import Win95Ui
import Win95Control

// Date/Time Properties: a month calendar you can browse, a live analog
// clock and the time zone. Read-only: the system clock and time zone belong
// to the administrator (timedatectl), not to a desktop applet.
PropertySheet {
    id: sheet

    title: "Date/Time Properties"
    titles: ["Date & Time", "Time Zone"]
    canApply: false

    property date now: new Date()
    property int viewMonth: now.getMonth()
    property int viewYear: now.getFullYear()

    readonly property var monthNames: ["January", "February", "March", "April", "May", "June", "July",
                                       "August", "September", "October", "November", "December"]

    Timer {
        interval: 1000
        running: sheet.visible
        repeat: true
        onTriggered: sheet.now = new Date()
    }

    // ------------------------------------------------------ Date & Time tab
    Item {
        GroupBox95 {
            id: dateGroup
            width: 196
            height: 236
            title: "<u>D</u>ate"

            Row {
                spacing: 6
                ComboBox95 {
                    width: 100
                    model: sheet.monthNames
                    currentIndex: sheet.viewMonth
                    popupMenu: sheet.popupMenu
                    onActivated: (i) => sheet.viewMonth = i
                }
                // year "spin box"
                Bevel {
                    width: 66
                    height: 21
                    style: "sunken"
                    face: Theme.window
                    Text {
                        x: 4
                        anchors.verticalCenter: parent.verticalCenter
                        text: sheet.viewYear
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.fontSize
                    }
                    Column {
                        anchors.right: parent.right
                        anchors.rightMargin: 2
                        anchors.verticalCenter: parent.verticalCenter
                        ToolButton95 { width: 15; height: 8; text: ""; glyph: ""; onClicked: sheet.viewYear++
                            Text { anchors.centerIn: parent; text: "▴"; font.pixelSize: 7 } }
                        ToolButton95 { width: 15; height: 8; text: ""; glyph: ""; onClicked: sheet.viewYear--
                            Text { anchors.centerIn: parent; text: "▾"; font.pixelSize: 7 } }
                    }
                }
            }

            // the month grid
            Bevel {
                y: 28
                width: parent.width
                height: parent.height - 28
                style: "sunken"
                face: Theme.window

                readonly property int firstDay: new Date(sheet.viewYear, sheet.viewMonth, 1).getDay()
                readonly property int daysInMonth: new Date(sheet.viewYear, sheet.viewMonth + 1, 0).getDate()
                readonly property bool viewingToday: sheet.viewYear === sheet.now.getFullYear()
                                                     && sheet.viewMonth === sheet.now.getMonth()

                Grid {
                    id: days
                    x: 4
                    y: 3
                    columns: 7
                    readonly property real cellW: (parent.width - 8) / 7

                    Repeater {
                        model: ["S", "M", "T", "W", "T", "F", "S"]
                        delegate: Rectangle {
                            required property string modelData
                            width: days.cellW
                            height: 18
                            color: Theme.shadow
                            Text {
                                anchors.centerIn: parent
                                text: parent.modelData
                                color: "white"
                                font.family: Theme.fontFamily
                                font.pixelSize: Theme.fontSize
                            }
                        }
                    }
                    Repeater {
                        model: 42
                        delegate: Item {
                            required property int index
                            readonly property int day: index - parent.parent.firstDay + 1
                            readonly property bool inMonth: day >= 1 && day <= parent.parent.daysInMonth
                            readonly property bool today: inMonth && parent.parent.viewingToday && day === sheet.now.getDate()
                            width: days.cellW
                            height: 22
                            Rectangle {
                                anchors.centerIn: parent
                                width: 20
                                height: 16
                                visible: parent.today
                                color: Theme.selection
                            }
                            Text {
                                anchors.centerIn: parent
                                visible: parent.inMonth
                                text: parent.day
                                color: parent.today ? Theme.selectionText : Theme.text
                                font.family: Theme.fontFamily
                                font.pixelSize: Theme.fontSize
                            }
                        }
                    }
                }
            }
        }

        GroupBox95 {
            x: dateGroup.width + 8
            width: parent.width - x
            height: dateGroup.height
            title: "<u>T</u>ime"

            Canvas {
                id: clockFace
                width: 140
                height: 140
                anchors.horizontalCenter: parent.horizontalCenter
                y: 6

                property date time: sheet.now
                onTimeChanged: requestPaint()

                onPaint: {
                    var ctx = getContext("2d");
                    ctx.reset();
                    var cx = width / 2, cy = height / 2, r = width / 2 - 6;

                    // hour marks: squares, bigger at 12/3/6/9
                    for (var m = 0; m < 60; ++m) {
                        var a = m / 60 * Math.PI * 2;
                        var x = cx + Math.sin(a) * r, y = cy - Math.cos(a) * r;
                        if (m % 15 === 0) {
                            ctx.fillStyle = "#008080";
                            ctx.fillRect(x - 3, y - 3, 6, 6);
                            ctx.fillStyle = "#000000";
                            ctx.fillRect(x - 3, y + 2, 6, 1);
                            ctx.fillRect(x + 2, y - 3, 1, 6);
                        } else if (m % 5 === 0) {
                            ctx.fillStyle = "#008080";
                            ctx.fillRect(x - 2, y - 2, 4, 4);
                        } else {
                            ctx.fillStyle = "#808080";
                            ctx.fillRect(x - 0.5, y - 0.5, 1, 1);
                        }
                    }

                    function hand(angle, length, width, color) {
                        var dx = Math.sin(angle), dy = -Math.cos(angle);
                        var px = -dy * width, py = dx * width;
                        ctx.fillStyle = color;
                        ctx.strokeStyle = "#000000";
                        ctx.lineWidth = 1;
                        ctx.beginPath();
                        ctx.moveTo(cx - dx * width * 2, cy - dy * width * 2);
                        ctx.lineTo(cx + px, cy + py);
                        ctx.lineTo(cx + dx * length, cy + dy * length);
                        ctx.lineTo(cx - px, cy - py);
                        ctx.closePath();
                        ctx.fill();
                        ctx.stroke();
                    }

                    var t = time;
                    var h = t.getHours() % 12, mi = t.getMinutes(), s = t.getSeconds();
                    hand((h + mi / 60) / 12 * Math.PI * 2, r * 0.55, 4, "#008080");
                    hand((mi + s / 60) / 60 * Math.PI * 2, r * 0.8, 3, "#008080");
                    // second hand: a plain line
                    var sa = s / 60 * Math.PI * 2;
                    ctx.strokeStyle = "#000000";
                    ctx.lineWidth = 1;
                    ctx.beginPath();
                    ctx.moveTo(cx, cy);
                    ctx.lineTo(cx + Math.sin(sa) * r * 0.85, cy - Math.cos(sa) * r * 0.85);
                    ctx.stroke();
                }
            }

            Bevel {
                anchors.horizontalCenter: parent.horizontalCenter
                y: clockFace.y + clockFace.height + 14
                width: 120
                height: 21
                style: "sunken"
                face: Theme.window
                Text {
                    anchors.centerIn: parent
                    text: Qt.formatTime(sheet.now, Wayland95Settings.clock24Hour ? "HH:mm:ss" : "h:mm:ss AP")
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontSize
                }
            }
        }

        Text {
            y: dateGroup.height + 10
            width: parent.width
            wrapMode: Text.Wrap
            text: "Current time zone: " + SystemInfo.timeZoneName
            color: Theme.text
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontSize
        }
        Text {
            anchors.bottom: parent.bottom
            width: parent.width
            wrapMode: Text.Wrap
            text: "The system date, time and time zone are managed by your system (for example with timedatectl), which needs administrator rights."
            color: Theme.shadow
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontSize
        }
    }

    // ---------------------------------------------------------- Time Zone tab
    Item {
        GroupBox95 {
            width: parent.width
            height: 120
            title: "Time zone"
            Column {
                spacing: 8
                Text {
                    text: SystemInfo.timeZoneName
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontSize
                    font.bold: true
                }
                Text {
                    text: "Zone: " + SystemInfo.timeZoneId
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontSize
                }
                Text {
                    text: "Local time: " + Qt.formatDateTime(sheet.now, "dddd, MMMM d, yyyy  "
                          + (Wayland95Settings.clock24Hour ? "HH:mm" : "h:mm AP"))
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontSize
                }
                Text {
                    text: "Universal time: " + Qt.formatDateTime(new Date(sheet.now.getTime()
                          + sheet.now.getTimezoneOffset() * 60000), "HH:mm")
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontSize
                }
            }
        }
    }
}
