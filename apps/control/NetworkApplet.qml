import QtQuick
import Win95Ui
import Win95Control

// Network: the computer's network adapters, Wi-Fi networks, and its name
// on the network - through NetworkManager. Changes happen right away, so
// there's nothing to Apply.
PropertySheet {
    id: sheet

    title: "Network"
    titles: ["Configuration", "Wireless", "Identification"]
    canApply: false

    property var devices: Network.devices()
    property int selectedDevice: devices.length > 0 ? 0 : -1
    readonly property var device: selectedDevice >= 0 && selectedDevice < devices.length ? devices[selectedDevice] : null
    property var details: device ? Network.deviceDetails(device.name) : ({})
    property int selectedNetwork: -1
    readonly property var network: selectedNetwork >= 0 && selectedNetwork < Network.wifiNetworks.length
                                   ? Network.wifiNetworks[selectedNetwork] : null
    readonly property var ident: Network.identification()

    function reloadDevices() {
        var name = device ? device.name : "";
        devices = Network.devices();
        selectedDevice = 0;
        for (var i = 0; i < devices.length; ++i)
            if (devices[i].name === name)
                selectedDevice = i;
        details = device ? Network.deviceDetails(device.name) : {};
    }

    function confirmDisconnect() {
        if (!device)
            return;
        if (device.isDefault) {
            messageBox.show("Network",
                            "Disconnecting " + device.name + " will cut this computer's network connection, "
                            + "including any downloads or remote sessions using it.\n\nDisconnect anyway?",
                            "warning", ["Yes", "No"], function (i) {
                                if (i === 0)
                                    Network.disconnectDevice(sheet.device.name);
                            });
        } else {
            Network.disconnectDevice(device.name);
        }
    }

    function connectToNetwork() {
        if (!network)
            return;
        if (network.security && !network.known) {
            keyDialog.open(network.ssid);
            return;
        }
        lastWifi = network.ssid;
        Network.connectWifi(network.ssid, "");
    }
    // the network tried last, to ask for its key if it turns out to need one
    property string lastWifi: ""

    Connections {
        target: Network
        function onDevicesChanged() { sheet.reloadDevices(); }
        function onOperationFinished(ok, message) {
            // a saved network whose key another desktop kept (KDE's KWallet,
            // say): ask for it, and it's saved for every desktop from then on
            if (!ok && sheet.lastWifi && /secrets were required|passwd-file|no secrets/i.test(message)) {
                var ssid = sheet.lastWifi;
                sheet.lastWifi = "";
                keyDialog.open(ssid, true);
                return;
            }
            sheet.lastWifi = "";
            if (!ok && message.length > 0)
                sheet.messageBox.show("Network", message, "error", ["OK"], null);
            if (sheet.currentTab === 1)
                Network.refreshWifi(false);
        }
    }

    onCurrentTabChanged: if (currentTab === 1 && Network.wifiNetworks.length === 0) Network.refreshWifi(false)

    component Label: Text {
        width: 110
        height: 21
        verticalAlignment: Text.AlignVCenter
        font.family: Theme.fontFamily
        font.pixelSize: Theme.fontSize
    }
    component Field: Bevel {
        property string value
        width: 230
        height: 21
        style: "sunken"
        face: Theme.face // read-only: gray, like Win95's disabled fields
        Text {
            x: 4
            width: parent.width - 8
            anchors.verticalCenter: parent.verticalCenter
            text: parent.value
            elide: Text.ElideRight
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontSize
        }
    }

    // a small icon per adapter type
    component AdapterIcon: Canvas {
        property string type: ""
        width: 16
        height: 16
        onTypeChanged: requestPaint()
        onPaint: {
            var ctx = getContext("2d");
            ctx.reset();
            function rect(x, y, w, h, c) { ctx.fillStyle = c; ctx.fillRect(x, y, w, h); }
            if (type === "wifi") {
                ctx.strokeStyle = "#000080";
                ctx.lineWidth = 1.5;
                for (var i = 0; i < 3; ++i) {
                    ctx.beginPath(); ctx.arc(8, 13, 3 + i * 3.5, Math.PI * 1.2, Math.PI * 1.8); ctx.stroke();
                }
                rect(7, 12, 2, 2, "#000080");
            } else if (type === "wireguard" || type === "tun" || type === "vpn") {
                rect(3, 7, 10, 8, "#c0a000");              // padlock
                ctx.strokeStyle = "#000000";
                ctx.beginPath(); ctx.arc(8, 7, 3, Math.PI, 0); ctx.stroke();
                rect(7, 10, 2, 3, "#000000");
            } else {
                // a network card: green board, gold contacts, a jack
                rect(1, 4, 14, 8, "#008000");
                for (var k = 0; k < 6; ++k)
                    rect(3 + k * 2, 12, 1, 2, "#c0a000");
                rect(10, 6, 4, 4, "#c0c0c0");
                rect(3, 6, 5, 3, "#000000");
            }
        }
    }

    // ------------------------------------------------------- Configuration
    Item {
        Text {
            id: installedLabel
            text: Network.available ? "The following network components are installed:"
                                    : "NetworkManager isn't running, so network settings aren't available."
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontSize
        }

        Bevel {
            id: deviceBox
            y: installedLabel.height + 4
            width: parent.width
            height: 120
            style: "sunken"
            face: Theme.window

            ListView {
                id: deviceList
                x: 2; y: 2
                width: parent.width - 4
                height: parent.height - 4
                clip: true
                interactive: false
                model: sheet.devices
                currentIndex: sheet.selectedDevice
                activeFocusOnTab: true
                keyNavigationEnabled: false
                Keys.onUpPressed: sheet.selectedDevice = Math.max(0, sheet.selectedDevice - 1)
                Keys.onDownPressed: sheet.selectedDevice = Math.min(sheet.devices.length - 1, sheet.selectedDevice + 1)
                onCurrentIndexChanged: sheet.details = sheet.device ? Network.deviceDetails(sheet.device.name) : {}

                delegate: Item {
                    id: devRow
                    required property var modelData
                    required property int index
                    readonly property bool isCurrent: sheet.selectedDevice === index
                    width: deviceList.width
                    height: 18

                    AdapterIcon {
                        x: 3
                        anchors.verticalCenter: parent.verticalCenter
                        type: devRow.modelData.type
                    }
                    Rectangle {
                        x: 22
                        anchors.verticalCenter: parent.verticalCenter
                        width: devText.implicitWidth + 4
                        height: 15
                        color: devRow.isCurrent ? Theme.selection : "transparent"
                        Text {
                            id: devText
                            x: 2
                            anchors.verticalCenter: parent.verticalCenter
                            text: devRow.modelData.typeLabel + " (" + devRow.modelData.name + ")"
                            color: devRow.isCurrent ? Theme.selectionText : Theme.text
                            font.family: Theme.fontFamily
                            font.pixelSize: Theme.fontSize
                        }
                    }
                    Text {
                        anchors.right: parent.right
                        anchors.rightMargin: 4
                        anchors.verticalCenter: parent.verticalCenter
                        text: devRow.modelData.connected ? "Connected" : devRow.modelData.state
                        color: devRow.modelData.connected ? Theme.text : Theme.shadow
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.fontSize
                    }
                    MouseArea {
                        anchors.fill: parent
                        onPressed: {
                            deviceList.forceActiveFocus();
                            sheet.selectedDevice = devRow.index;
                        }
                        onDoubleClicked: detailsDialog.show()
                    }
                }
            }
        }

        Row {
            y: deviceBox.y + deviceBox.height + 6
            spacing: 6
            Button95 {
                text: "Connect"
                enabled: !Network.busy && !!sheet.device && sheet.device.controllable && !sheet.device.connected
                onClicked: Network.connectDevice(sheet.device.name)
            }
            Button95 {
                text: "Disconnect"
                enabled: !Network.busy && !!sheet.device && sheet.device.controllable && sheet.device.connected
                onClicked: sheet.confirmDisconnect()
            }
            Button95 {
                text: "Properties"
                enabled: !!sheet.device
                onClicked: detailsDialog.show()
            }
        }

        GroupBox95 {
            y: deviceBox.y + deviceBox.height + 38
            width: parent.width
            height: parent.height - y - 18
            title: "Description"

            Column {
                spacing: 3
                width: parent.width
                Repeater {
                    model: !sheet.device ? [] : [
                        sheet.device.connection ? "Connection: " + sheet.device.connection : "Not connected",
                        "IP address: " + (sheet.details.ipv4 && sheet.details.ipv4.length ? sheet.details.ipv4.join(", ") : "none"),
                        sheet.details.gateway4 ? "Gateway: " + sheet.details.gateway4 : "",
                        sheet.details.hwaddr ? "Adapter address: " + sheet.details.hwaddr : "",
                        sheet.device.isDefault ? "This adapter carries the computer's internet connection." : "",
                        !sheet.device.controllable ? "Managed outside NetworkManager (for example by Docker or a VPN tool)." : "",
                    ].filter(function (s) { return s.length > 0; })
                    Text {
                        required property string modelData
                        width: parent.width
                        elide: Text.ElideRight
                        text: modelData
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.fontSize
                    }
                }
            }
        }

        Text {
            anchors.bottom: parent.bottom
            visible: Network.busy
            text: Network.busyText
            color: Theme.shadow
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontSize
        }
    }

    // ------------------------------------------------------------- Wireless
    Item {
        CheckBox95 {
            id: wifiToggle
            text: "<u>E</u>nable wireless networking"
            checked: Network.wifiEnabled
            enabled: Network.available && !Network.busy
            onToggled: (on) => Network.setWifiEnabled(on)
        }

        Text {
            id: availableLabel
            y: wifiToggle.height + 8
            text: "Available wireless networks:"
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontSize
        }

        Bevel {
            id: wifiBox
            y: availableLabel.y + availableLabel.height + 4
            width: parent.width
            height: 176
            style: "sunken"
            face: Theme.window

            ListView {
                id: wifiList
                x: 2; y: 2
                width: parent.width - 4 - (wifiBar.visible ? wifiBar.width : 0)
                height: parent.height - 4
                clip: true
                interactive: false
                model: Network.wifiNetworks
                currentIndex: sheet.selectedNetwork
                activeFocusOnTab: true
                keyNavigationEnabled: false
                Keys.onUpPressed: sheet.selectedNetwork = Math.max(0, sheet.selectedNetwork - 1)
                Keys.onDownPressed: sheet.selectedNetwork = Math.min(Network.wifiNetworks.length - 1, sheet.selectedNetwork + 1)
                Keys.onReturnPressed: sheet.connectToNetwork()
                onCurrentIndexChanged: positionViewAtIndex(currentIndex, ListView.Contain)

                delegate: Item {
                    id: netRow
                    required property var modelData
                    required property int index
                    readonly property bool isCurrent: sheet.selectedNetwork === index
                    width: wifiList.width
                    height: 18

                    // signal strength: four bars
                    Row {
                        x: 3
                        anchors.bottom: parent.bottom
                        anchors.bottomMargin: 3
                        spacing: 1
                        Repeater {
                            model: 4
                            Rectangle {
                                required property int index
                                anchors.bottom: parent.bottom
                                width: 3
                                height: 3 + index * 3
                                color: netRow.modelData.signal > index * 25 ? Theme.text : Theme.light
                            }
                        }
                    }
                    Rectangle {
                        x: 22
                        anchors.verticalCenter: parent.verticalCenter
                        width: ssidText.implicitWidth + 4
                        height: 15
                        color: netRow.isCurrent ? Theme.selection : "transparent"
                        Text {
                            id: ssidText
                            x: 2
                            anchors.verticalCenter: parent.verticalCenter
                            text: netRow.modelData.ssid
                            font.bold: netRow.modelData.inUse
                            color: netRow.isCurrent ? Theme.selectionText : Theme.text
                            font.family: Theme.fontFamily
                            font.pixelSize: Theme.fontSize
                        }
                    }
                    Text {
                        anchors.right: parent.right
                        anchors.rightMargin: 4
                        anchors.verticalCenter: parent.verticalCenter
                        text: (netRow.modelData.inUse ? "Connected   " : netRow.modelData.known ? "Saved   " : "")
                              + (netRow.modelData.security ? netRow.modelData.security : "Open")
                        color: Theme.shadow
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.fontSize
                    }
                    MouseArea {
                        anchors.fill: parent
                        onPressed: {
                            wifiList.forceActiveFocus();
                            sheet.selectedNetwork = netRow.index;
                        }
                        onDoubleClicked: sheet.connectToNetwork()
                    }
                }

                WheelHandler { acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad; onWheel: (event) => wifiBar.scrollBy(-event.angleDelta.y / 120 * 54) }
            }
            ScrollBar95 {
                id: wifiBar
                anchors.right: parent.right
                anchors.rightMargin: 2
                y: 2
                height: parent.height - 4
                flickable: wifiList
                stepSize: 18
            }
            Text {
                anchors.centerIn: parent
                visible: Network.wifiNetworks.length === 0
                text: !Network.wifiEnabled ? "Wireless networking is off." : Network.busy ? "" : "No networks found."
                color: Theme.shadow
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontSize
            }
        }

        Row {
            y: wifiBox.y + wifiBox.height + 6
            spacing: 6
            Button95 {
                text: "Refresh"
                enabled: Network.wifiEnabled && !Network.busy
                onClicked: Network.refreshWifi(true)
            }
            Button95 {
                text: "Connect..."
                enabled: !Network.busy && !!sheet.network && !sheet.network.inUse
                onClicked: sheet.connectToNetwork()
            }
            Button95 {
                text: "Disconnect"
                enabled: !Network.busy && !!sheet.network && sheet.network.inUse
                onClicked: {
                    for (var i = 0; i < sheet.devices.length; ++i) {
                        if (sheet.devices[i].type === "wifi" && sheet.devices[i].connected) {
                            sheet.selectedDevice = i;
                            sheet.confirmDisconnect();
                            return;
                        }
                    }
                }
            }
        }

        Text {
            anchors.bottom: parent.bottom
            text: Network.busy ? Network.busyText : ""
            color: Theme.shadow
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontSize
        }
    }

    // -------------------------------------------------------- Identification
    Item {
        Text {
            id: identIntro
            width: parent.width
            wrapMode: Text.Wrap
            text: "This is how your computer identifies itself on the network."
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontSize
        }

        Grid {
            y: identIntro.height + 16
            columns: 2
            columnSpacing: 10
            rowSpacing: 10

            Label { text: "Computer name:" }
            Field { value: sheet.ident.hostname }
            Label { text: "Description:" }
            Field { value: sheet.ident.prettyHostname || "(none)" }
            Label { text: "DNS domain:" }
            Field { value: sheet.ident.domain || "(none)" }
        }

        Text {
            anchors.bottom: parent.bottom
            width: parent.width
            wrapMode: Text.Wrap
            text: "Changing the computer name needs administrator rights. In a terminal, use:\n"
                  + "sudo hostnamectl set-hostname NEW-NAME"
            color: Theme.shadow
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontSize
        }
    }

    extras: [
        // adapter Properties
        Dialog95 {
            id: detailsDialog
            title: sheet.device ? sheet.device.typeLabel + " Properties" : "Properties"
            dialogWidth: 360
            dialogHeight: 260

            Column {
                x: 12
                y: 12
                width: parent.width - 24
                spacing: 4
                Repeater {
                    model: !sheet.device ? [] : [
                        ["Adapter", sheet.device.name],
                        ["Type", sheet.device.typeLabel],
                        ["State", sheet.details.state || sheet.device.state],
                        ["Connection", sheet.details.connection || "(none)"],
                        ["Adapter address", sheet.details.hwaddr || "(none)"],
                        ["IPv4 address", (sheet.details.ipv4 || []).join("\n") || "(none)"],
                        ["Gateway", sheet.details.gateway4 || "(none)"],
                        ["DNS servers", (sheet.details.dns || []).join("\n") || "(none)"],
                        ["IPv6 address", (sheet.details.ipv6 || []).join("\n") || "(none)"],
                        ["MTU", sheet.details.mtu || "(unknown)"],
                    ]
                    Row {
                        required property var modelData
                        spacing: 8
                        Text {
                            width: 100
                            text: parent.modelData[0] + ":"
                            font.family: Theme.fontFamily
                            font.pixelSize: Theme.fontSize
                        }
                        Text {
                            width: 230
                            wrapMode: Text.WrapAnywhere
                            text: parent.modelData[1]
                            font.family: Theme.fontFamily
                            font.pixelSize: Theme.fontSize
                        }
                    }
                }
            }
            Button95 {
                anchors.right: parent.right
                anchors.rightMargin: 10
                anchors.bottom: parent.bottom
                anchors.bottomMargin: 10
                text: "OK"
                isDefault: true
                onClicked: detailsDialog.close()
            }
        },

        // network key prompt
        Dialog95 {
            id: keyDialog
            title: "Enter Network Key"
            dialogWidth: 330
            dialogHeight: 120 + keyIntro.height
            property string ssid: ""
            property bool savedElsewhere: false   // its saved key isn't reachable here

            function open(name, elsewhere) {
                ssid = name;
                savedElsewhere = !!elsewhere;
                keyField.text = "";
                showKey.checked = false;
                show();
                keyField.forceActiveFocus();
            }
            function accept() {
                if (keyField.text.length === 0)
                    return;
                var key = keyField.text;
                keyField.text = ""; // don't keep the key around in the UI
                close();
                Network.connectWifi(ssid, key);
            }

            Text {
                id: keyIntro
                x: 12
                y: 12
                width: parent.width - 24
                wrapMode: Text.Wrap
                text: keyDialog.savedElsewhere
                      ? "The saved key for \"" + keyDialog.ssid + "\" isn't available here (another desktop kept it). "
                        + "Type it once and it's saved for every desktop."
                      : "\"" + keyDialog.ssid + "\" needs a network key (password)."
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontSize
            }
            Text {
                x: 12
                y: keyIntro.y + keyIntro.height + 12
                height: 21
                verticalAlignment: Text.AlignVCenter
                text: "Network key:"
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontSize
            }
            TextField95 {
                id: keyField
                x: 92
                y: keyIntro.y + keyIntro.height + 12
                width: parent.width - x - 12
                input.echoMode: showKey.checked ? TextInput.Normal : TextInput.Password
                onAccepted: keyDialog.accept()
                onEscaped: keyDialog.close()
            }
            CheckBox95 {
                id: showKey
                x: 92
                y: keyField.y + 26
                text: "Show characters"
            }
            Row {
                anchors.right: parent.right
                anchors.rightMargin: 10
                anchors.bottom: parent.bottom
                anchors.bottomMargin: 10
                spacing: 6
                Button95 { text: "OK"; isDefault: true; onClicked: keyDialog.accept() }
                Button95 { text: "Cancel"; onClicked: keyDialog.close() }
            }
        }
    ]
}
