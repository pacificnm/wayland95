import QtQuick
import Win95Ui as Ui
import Win95Shell

// Bluetooth: the Bluetooth symbol (gray when off). Click to turn it on or off and see what's connected, double-click
// for Control Panel > Bluetooth.
TrayIcon {
    id: bluetooth

    readonly property var connected: Ui.Bluetooth.devices.filter(function (d) { return d.connected; })

    toolTip: !Ui.Bluetooth.powered ? "Bluetooth: off"
           : connected.length === 0 ? "Bluetooth: on, nothing connected"
           : connected.length === 1 ? "Bluetooth: " + connected[0].name
           : "Bluetooth: " + connected.length + " devices connected"
    popupWidth: 220
    popupHeight: 100 + Math.max(1, connected.length) * 16

    icon: Component {
        Canvas {
            property bool powered: Ui.Bluetooth.powered
            onPoweredChanged: requestPaint()
            onPaint: {
                var ctx = getContext("2d");
                ctx.reset();
                // the Bluetooth symbol: a white rune on a blue oval
                ctx.fillStyle = powered ? "#0060d0" : "#808080";
                ctx.strokeStyle = powered ? "#003080" : "#404040";
                ctx.lineWidth = 1;
                ctx.beginPath();
                ctx.roundedRect(3.5, 0.5, 9, 15, 4.5, 4.5);
                ctx.fill();
                ctx.stroke();
                ctx.strokeStyle = "#ffffff";
                ctx.lineWidth = 1.3;
                ctx.lineJoin = "miter";
                ctx.beginPath();
                ctx.moveTo(5.5, 5);
                ctx.lineTo(10.5, 10.25);
                ctx.lineTo(8, 12.75);
                ctx.lineTo(8, 3.25);
                ctx.lineTo(10.5, 5.75);
                ctx.lineTo(5.5, 11);
                ctx.stroke();
            }
        }
    }

    onDoubleClicked: Launcher.launch("win95control", ["--applet", "bluetooth"])

    Ui.CheckBox95 {
        id: powerBox
        x: 10
        y: 10
        enabled: !Ui.Bluetooth.blocked
        text: Ui.Bluetooth.blocked ? "Bluetooth (switched off by airplane mode)" : "<u>B</u>luetooth"
        onToggled: (on) => Ui.Bluetooth.setPowered(on)
    }
    Binding { target: powerBox; property: "checked"; value: Ui.Bluetooth.powered }

    Text {
        id: connectedLabel
        x: 10
        y: 36
        text: "Connected devices:"
        color: Ui.Bluetooth.powered ? Theme.text : Theme.shadow
        font.family: Theme.fontFamily
        font.pixelSize: Theme.fontSize
    }
    Column {
        x: 20
        y: connectedLabel.y + 18
        width: parent.width - 30
        Repeater {
            model: bluetooth.connected
            Text {
                required property var modelData
                width: parent.width
                elide: Text.ElideRight
                text: modelData.name + (modelData.battery >= 0 ? " (" + modelData.battery + "%)" : "")
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontSize
            }
        }
        Text {
            visible: bluetooth.connected.length === 0
            text: "(none)"
            color: Theme.shadow
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontSize
        }
    }
    Ui.Button95 {
        anchors.right: parent.right
        anchors.rightMargin: 10
        anchors.bottom: parent.bottom
        anchors.bottomMargin: 10
        text: "Bluetooth <u>S</u>ettings..."
        onClicked: {
            bluetooth.closePopup();
            Launcher.launch("win95control", ["--applet", "bluetooth"]);
        }
    }
}
