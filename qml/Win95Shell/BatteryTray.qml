import QtQuick
import Win95Ui as Ui
import Win95Shell

// Battery: how full it is (red when low), with a plug when on mains power.
// Click for details.
TrayIcon {
    id: battery

    function timeText(minutes) {
        var h = Math.floor(minutes / 60), m = minutes % 60;
        return (h > 0 ? h + " hr " : "") + m + " min";
    }
    readonly property string statusText: {
        var s = Ui.Battery.percent + "% remaining";
        if (Ui.Battery.charging)
            s = Ui.Battery.percent + "%, charging"
                + (Ui.Battery.minutesLeft >= 0 ? " (" + timeText(Ui.Battery.minutesLeft) + " until full)" : "");
        else if (Ui.Battery.pluggedIn)
            s = Ui.Battery.percent + "%, plugged in";
        else if (Ui.Battery.minutesLeft >= 0)
            s += " (" + timeText(Ui.Battery.minutesLeft) + ")";
        return s;
    }

    toolTip: statusText
    popupWidth: 230
    popupHeight: 74

    icon: Component {
        Canvas {
            property int percent: Ui.Battery.percent
            property bool plugged: Ui.Battery.pluggedIn
            onPercentChanged: requestPaint()
            onPluggedChanged: requestPaint()
            onPaint: {
                var ctx = getContext("2d");
                ctx.reset();
                function rect(x, y, w, h, c) { ctx.fillStyle = c; ctx.fillRect(x, y, w, h); }
                // an upright battery
                rect(5, 1, 6, 2, "#000000");
                rect(3, 2, 10, 14, "#000000");
                rect(4, 3, 8, 12, "#ffffff");
                var h = Math.round(12 * Math.max(0, Math.min(100, percent)) / 100);
                rect(4, 15 - h, 8, h, percent <= 20 && !plugged ? "#ff0000" : "#00a000");
                if (plugged) {
                    // a lightning bolt
                    ctx.fillStyle = "#ffff00";
                    ctx.strokeStyle = "#000000";
                    ctx.beginPath();
                    ctx.moveTo(9, 4); ctx.lineTo(5.5, 10); ctx.lineTo(8, 10); ctx.lineTo(7, 14); ctx.lineTo(10.5, 8);
                    ctx.lineTo(8, 8); ctx.closePath(); ctx.fill(); ctx.stroke();
                }
            }
        }
    }

    onPopupOpened: Ui.Battery.refresh()

    Text {
        x: 10
        y: 10
        text: "Power status"
        font.family: Theme.fontFamily
        font.pixelSize: Theme.fontSize
        font.bold: true
    }
    Text {
        x: 10
        y: 30
        width: parent.width - 20
        wrapMode: Text.Wrap
        text: (Ui.Battery.pluggedIn ? "Power source: AC power\n" : "Power source: Battery\n") + battery.statusText
        font.family: Theme.fontFamily
        font.pixelSize: Theme.fontSize
    }
}
