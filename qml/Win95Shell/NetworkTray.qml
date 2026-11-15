import QtQuick
import Win95Ui as Ui
import Win95Shell

// Network: a cable or Wi-Fi signal bars, a red cross when disconnected and
// a yellow "!" when there's no internet. Click for status and the Wi-Fi
// switch, double-click for Control Panel > Network.
TrayIcon {
    id: network

    readonly property string netState: Ui.NetworkStatus.state
    readonly property string kind: Ui.NetworkStatus.type
    readonly property string statusText: {
        if (netState === "off")
            return "Networking is turned off";
        if (netState === "connecting")
            return "Connecting...";
        if (netState === "disconnected" || kind === "")
            return "Not connected";
        var name = Ui.NetworkStatus.connectionName;
        var how = kind === "wifi" ? name + " (" + Ui.NetworkStatus.signal + "% signal)" : name;
        return (netState === "limited" ? "Connected to " + how + ", but no internet access" : "Connected to " + how);
    }

    toolTip: statusText
    popupWidth: 230
    popupHeight: Ui.NetworkStatus.hasWifi ? 120 : 96

    icon: Component {
        Canvas {
            property string netState: network.netState
            property string kind: network.kind
            property int signal: Ui.NetworkStatus.signal
            onNetStateChanged: requestPaint()
            onKindChanged: requestPaint()
            onSignalChanged: requestPaint()
            onPaint: {
                var ctx = getContext("2d");
                ctx.reset();
                function rect(x, y, w, h, c) { ctx.fillStyle = c; ctx.fillRect(x, y, w, h); }
                if (kind === "wifi") {
                    // four bars, filled up to the signal strength
                    for (var i = 0; i < 4; ++i) {
                        var h = 4 + i * 3;
                        rect(1 + i * 4, 15 - h, 3, h, "#000000");
                        rect(2 + i * 4, 16 - h, 1, h - 2, signal > i * 25 ? "#008000" : "#c0c0c0");
                    }
                } else {
                    // two little computers on a cable
                    rect(0, 1, 9, 7, "#000000"); rect(1, 2, 7, 5, "#008080");
                    rect(3, 8, 3, 2, "#000000");
                    rect(7, 7, 9, 7, "#000000"); rect(8, 8, 7, 5, "#008080");
                    rect(10, 14, 3, 2, "#000000");
                    rect(4, 10, 1, 4, "#000000"); rect(4, 13, 3, 1, "#000000");
                }
                if (netState === "disconnected" || netState === "off" || (netState !== "connecting" && kind === "")) {
                    ctx.strokeStyle = "#ff0000";
                    ctx.lineWidth = 2;
                    ctx.beginPath(); ctx.moveTo(8, 8); ctx.lineTo(15, 15); ctx.moveTo(15, 8); ctx.lineTo(8, 15); ctx.stroke();
                } else if (netState === "limited") {
                    rect(10, 8, 6, 8, "#000000"); rect(11, 9, 4, 6, "#ffff00");
                    rect(12, 10, 2, 3, "#000000"); rect(12, 14, 2, 1, "#000000");
                } else if (netState === "connecting") {
                    rect(9, 12, 2, 2, "#000080"); rect(12, 12, 2, 2, "#000080"); rect(15, 12, 1, 2, "#000080");
                }
            }
        }
    }

    onDoubleClicked: Launcher.launch("win95control", ["--applet", "network"])
    onPopupOpened: Ui.NetworkStatus.refresh()

    Text {
        x: 10
        y: 10
        width: parent.width - 20
        wrapMode: Text.Wrap
        text: network.statusText
        font.family: Theme.fontFamily
        font.pixelSize: Theme.fontSize
    }
    Ui.CheckBox95 {
        id: wifiBox
        x: 10
        y: 48
        visible: Ui.NetworkStatus.hasWifi
        text: "<u>W</u>ireless networking"
        onToggled: (on) => Ui.NetworkStatus.setWifiEnabled(on)
    }
    Binding { target: wifiBox; property: "checked"; value: Ui.NetworkStatus.wifiEnabled }
    Ui.Button95 {
        anchors.right: parent.right
        anchors.rightMargin: 10
        anchors.bottom: parent.bottom
        anchors.bottomMargin: 10
        text: "<u>N</u>etwork Settings..."
        onClicked: {
            network.closePopup();
            Launcher.launch("win95control", ["--applet", "network"]);
        }
    }
}
