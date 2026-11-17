import QtQuick
import Win95Ui as Ui
import Win95Shell

// Network: a cable or Wi-Fi signal bars, a red cross when disconnected, a
// yellow "!" when there's no internet, and a padlock while a WireGuard
// tunnel is connected. Click for the connection, its addresses and the
// tunnels, and the Wi-Fi switch; double-click for Control Panel > Network.
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

    readonly property bool vpn: Ui.NetworkStatus.vpnConnected
    readonly property string vpnText: Ui.NetworkStatus.vpns.map(function (v) { return v.name; }).join(", ")
    // the addresses, as lines of "label: value" (none while disconnected)
    readonly property var details: {
        var s = Ui.NetworkStatus, lines = [];
        if (netState === "disconnected" || netState === "off" || kind === "")
            return lines;
        if (s.ipv4.length)
            lines.push(["IP address", s.ipv4.join(", ")]);
        if (s.gateway)
            lines.push(["Gateway", s.gateway]);
        if (s.dns.length)
            lines.push(["DNS", s.dns.join(", ")]);
        if (s.ipv6.length)
            lines.push(["IPv6", s.ipv6.join(", ")]);
        if (s.hardwareAddress)
            lines.push(["Adapter", s.device + " (" + s.hardwareAddress + ")"]);
        return lines;
    }

    toolTip: statusText + (vpn ? "\nVPN connected: " + vpnText : "")
    popupWidth: 300
    popupHeight: body.y + body.height + (Ui.NetworkStatus.hasWifi ? 34 : 8) + 34

    icon: Component {
        Canvas {
            property string netState: network.netState
            property string kind: network.kind
            property int signal: Ui.NetworkStatus.signal
            property bool locked: network.vpn
            onNetStateChanged: requestPaint()
            onKindChanged: requestPaint()
            onSignalChanged: requestPaint()
            onLockedChanged: requestPaint()
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
                if (locked) {
                    // a gold padlock at the bottom left: a VPN tunnel is up
                    rect(1, 6, 5, 1, "#000000"); rect(0, 7, 1, 3, "#000000"); rect(6, 7, 1, 3, "#000000");
                    rect(1, 7, 1, 3, "#ffffff"); rect(5, 7, 1, 3, "#ffffff");
                    rect(0, 10, 7, 6, "#000000"); rect(1, 11, 5, 4, "#ffd800");
                    rect(3, 12, 1, 2, "#000000");
                }
            }
        }
    }

    onDoubleClicked: Launcher.launch("win95control", ["--applet", "network"])
    onPopupOpened: Ui.NetworkStatus.refresh()

    Column {
        id: body
        x: 10
        y: 10
        width: parent.width - 20
        spacing: 6

        Text {
            width: parent.width
            wrapMode: Text.Wrap
            text: network.statusText
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontSize
        }
        // the addresses
        Grid {
            visible: network.details.length > 0
            columns: 2
            columnSpacing: 8
            rowSpacing: 2
            Repeater {
                model: network.details.length * 2
                Text {
                    required property int index
                    readonly property var row: network.details[Math.floor(index / 2)]
                    width: index % 2 ? body.width - 80 : 72
                    wrapMode: Text.WrapAnywhere
                    text: index % 2 ? row[1] : row[0] + ":"
                    color: index % 2 ? Theme.text : Theme.shadow
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontSize
                }
            }
        }
        // the tunnels
        Repeater {
            model: Ui.NetworkStatus.vpns
            Row {
                required property var modelData
                spacing: 6
                Canvas {
                    width: 8; height: 11
                    onPaint: {
                        var ctx = getContext("2d");
                        function rect(x, y, w, h, c) { ctx.fillStyle = c; ctx.fillRect(x, y, w, h); }
                        rect(1, 0, 5, 1, "#000000"); rect(0, 1, 1, 4, "#000000"); rect(6, 1, 1, 4, "#000000");
                        rect(0, 5, 7, 6, "#000000"); rect(1, 6, 5, 4, "#ffd800"); rect(3, 7, 1, 2, "#000000");
                    }
                }
                Text {
                    width: body.width - 14
                    wrapMode: Text.WrapAnywhere
                    text: "VPN " + modelData.name + " connected"
                          + (modelData.addresses.length ? ": " + modelData.addresses.join(", ") : "")
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontSize
                }
            }
        }
    }
    Ui.CheckBox95 {
        id: wifiBox
        x: 10
        y: body.y + body.height + 8
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
