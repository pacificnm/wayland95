import QtQuick
import Win95Ui
import Win95Control

// Bluetooth: turn Bluetooth on or off, pair and connect devices, and set how
// this computer appears to others - through BlueZ. Device actions happen
// right away; the Options tab's settings wait for Apply.
PropertySheet {
    id: sheet

    title: "Bluetooth"
    titles: ["Devices", "Options"]

    property int selectedIndex: -1
    property string selectedPath: ""
    readonly property var device: selectedIndex >= 0 && selectedIndex < Bluetooth.devices.length
                                  ? Bluetooth.devices[selectedIndex] : null

    // Options tab: edited copies, written on Apply
    property string nameEdit: Bluetooth.adapterName
    property bool discoverableEdit: Bluetooth.discoverable

    function kindLabel(kind) {
        return ({ audio: "Headphones or speaker", keyboard: "Keyboard", mouse: "Mouse or touchpad",
                  gamepad: "Game controller", phone: "Phone", computer: "Computer" })[kind] || "Device";
    }
    function statusText(d) {
        if (d.connected)
            return d.battery >= 0 ? "Connected, " + d.battery + "%" : "Connected";
        return d.paired ? "Paired" : "Not paired";
    }

    // keep the same device selected as the list re-sorts
    function reselect() {
        var list = Bluetooth.devices;
        for (var i = 0; i < list.length; ++i) {
            if (list[i].path === selectedPath) {
                selectedIndex = i;
                return;
            }
        }
        selectedIndex = list.length > 0 ? 0 : -1;
        selectedPath = selectedIndex >= 0 ? list[0].path : "";
    }
    function select(i) {
        selectedIndex = i;
        selectedPath = Bluetooth.devices[i].path;
    }

    function primaryAction() {
        if (!device || Bluetooth.busy)
            return;
        if (!device.paired)
            Bluetooth.pair(device.path);
        else if (device.connected)
            Bluetooth.disconnectDevice(device.path);
        else
            Bluetooth.connectDevice(device.path);
    }
    function confirmRemove() {
        if (!device)
            return;
        var d = device;
        messageBox.show("Remove Device",
                        "Are you sure you want to remove '" + d.name + "'?\n\n"
                        + (d.paired ? "You'll have to pair it again to use it with this computer." : ""),
                        "question", ["Yes", "No"], function (i) {
                            if (i === 0)
                                Bluetooth.removeDevice(d.path);
                        });
    }

    Component.onCompleted: reselect()
    // stop searching when the window closes; BlueZ would also stop it when
    // the Control Panel quits, but it may stay open on other applets
    onClosing: if (Bluetooth.discovering) Bluetooth.stopDiscovery()

    Connections {
        target: Bluetooth
        function onDevicesChanged() { sheet.reselect(); }
        function onAdapterChanged() {
            if (!sheet.dirty) {
                sheet.nameEdit = Bluetooth.adapterName;
                sheet.discoverableEdit = Bluetooth.discoverable;
            }
        }
        function onOperationFinished(ok, message) {
            if (!ok && message.length > 0)
                sheet.messageBox.show("Bluetooth", message, "error", ["OK"], null);
        }
        function onPairingRequest(kind, deviceName, code) {
            if (kind === "cancel")
                pairDialog.close();
            else
                pairDialog.ask(kind, deviceName, code);
        }
    }

    onApplyRequested: {
        Bluetooth.setAdapterName(nameEdit);
        if (discoverableEdit !== Bluetooth.discoverable)
            Bluetooth.setDiscoverable(discoverableEdit);
    }

    component Label: Text {
        font.family: Theme.fontFamily
        font.pixelSize: Theme.fontSize
    }

    // ------------------------------------------------------------- Devices
    Item {
        CheckBox95 {
            id: powerBox
            text: "Turn on <u>B</u>luetooth"
            checked: Bluetooth.powered
            enabled: Bluetooth.available && !Bluetooth.blocked
            onToggled: (on) => {
                Bluetooth.setPowered(on);
                // follow the adapter again (the change may fail)
                powerBox.checked = Qt.binding(function () { return Bluetooth.powered; });
            }
        }

        Label {
            id: listLabel
            y: powerBox.height + 8
            text: "Bluetooth devices:"
        }

        Bevel {
            id: listBox
            y: listLabel.y + listLabel.height + 4
            width: parent.width
            height: 182
            style: "sunken"
            face: Theme.window

            ListView {
                id: list
                x: 2; y: 2
                width: parent.width - 4 - (listBar.visible ? listBar.width : 0)
                height: parent.height - 4
                clip: true
                interactive: false
                visible: Bluetooth.powered
                model: Bluetooth.devices
                currentIndex: sheet.selectedIndex
                activeFocusOnTab: true
                keyNavigationEnabled: false
                Keys.onUpPressed: if (sheet.selectedIndex > 0) sheet.select(sheet.selectedIndex - 1)
                Keys.onDownPressed: if (sheet.selectedIndex < Bluetooth.devices.length - 1) sheet.select(sheet.selectedIndex + 1)
                Keys.onReturnPressed: sheet.primaryAction()
                Keys.onDeletePressed: sheet.confirmRemove()
                onCurrentIndexChanged: positionViewAtIndex(currentIndex, ListView.Contain)

                delegate: Item {
                    id: row
                    required property var modelData
                    required property int index
                    readonly property bool isCurrent: sheet.selectedIndex === index
                    width: list.width
                    height: 20

                    BluetoothDeviceIcon {
                        x: 3
                        anchors.verticalCenter: parent.verticalCenter
                        kind: row.modelData.kind
                    }
                    Rectangle {
                        x: 23
                        anchors.verticalCenter: parent.verticalCenter
                        width: Math.min(nameText.implicitWidth + 4, row.width - x - statusText.width - 10)
                        height: 15
                        color: row.isCurrent ? Theme.selection : "transparent"
                        Text {
                            id: nameText
                            x: 2
                            width: parent.width - 4
                            anchors.verticalCenter: parent.verticalCenter
                            text: row.modelData.name
                            elide: Text.ElideRight
                            font.bold: row.modelData.connected
                            color: row.isCurrent ? Theme.selectionText : Theme.text
                            font.family: Theme.fontFamily
                            font.pixelSize: Theme.fontSize
                        }
                    }
                    Text {
                        id: statusText
                        anchors.right: parent.right
                        anchors.rightMargin: 4
                        anchors.verticalCenter: parent.verticalCenter
                        text: sheet.statusText(row.modelData)
                        color: row.modelData.connected ? Theme.text : Theme.shadow
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.fontSize
                    }
                    MouseArea {
                        anchors.fill: parent
                        onPressed: {
                            list.forceActiveFocus();
                            sheet.select(row.index);
                        }
                        onDoubleClicked: sheet.primaryAction()
                    }
                }

                WheelHandler { acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad; onWheel: (event) => listBar.scrollBy(-event.angleDelta.y / 120 * 60) }
            }
            ScrollBar95 {
                id: listBar
                anchors.right: parent.right
                anchors.rightMargin: 2
                y: 2
                height: parent.height - 4
                flickable: list
                stepSize: 20
            }
            Label {
                anchors.centerIn: parent
                width: parent.width - 40
                horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.Wrap
                visible: !Bluetooth.powered || Bluetooth.devices.length === 0
                color: Theme.shadow
                text: !Bluetooth.available ? "No Bluetooth adapter was found on this computer."
                    : Bluetooth.blocked ? "Bluetooth is switched off by airplane mode or a hardware switch."
                    : !Bluetooth.powered ? "Bluetooth is turned off."
                    : Bluetooth.discovering ? "Searching for devices..."
                    : "No devices yet. Click Search to look for nearby devices."
            }
        }

        Row {
            id: buttons
            y: listBox.y + listBox.height + 6
            spacing: 6
            Button95 {
                text: Bluetooth.discovering ? "Stop" : "<u>S</u>earch"
                enabled: Bluetooth.powered && !Bluetooth.busy
                onClicked: Bluetooth.discovering ? Bluetooth.stopDiscovery() : Bluetooth.startDiscovery()
            }
            Button95 {
                text: !sheet.device || !sheet.device.paired ? "<u>P</u>air"
                      : sheet.device.connected ? "<u>D</u>isconnect" : "<u>C</u>onnect"
                enabled: Bluetooth.powered && !Bluetooth.busy && !!sheet.device
                onClicked: sheet.primaryAction()
            }
            Button95 {
                text: "<u>R</u>emove"
                enabled: !Bluetooth.busy && !!sheet.device && (sheet.device.paired || sheet.device.trusted)
                onClicked: sheet.confirmRemove()
            }
            Button95 {
                text: "Pr<u>o</u>perties"
                enabled: !!sheet.device
                onClicked: detailsDialog.show()
            }
        }

        Label {
            y: buttons.y + buttons.height + 10
            width: parent.width
            wrapMode: Text.Wrap
            color: Theme.shadow
            text: Bluetooth.busy ? Bluetooth.busyText
                : Bluetooth.discovering ? "Searching for devices. To add one, put it in pairing mode "
                                          + "(see its manual), then select it and click Pair."
                : ""
        }
    }

    // ------------------------------------------------------------- Options
    Item {
        GroupBox95 {
            id: computerGroup
            width: parent.width
            height: 96
            title: "This computer"

            Grid {
                columns: 2
                columnSpacing: 8
                rowSpacing: 8
                verticalItemAlignment: Grid.AlignVCenter

                Label { text: "Bluetooth <u>n</u>ame:" }
                TextField95 {
                    id: nameField
                    width: 200
                    enabled: Bluetooth.available
                    text: sheet.nameEdit
                    input.maximumLength: 248
                    onAccepted: sheet.apply()
                    Connections {
                        target: nameField.input
                        function onTextEdited() {
                            sheet.nameEdit = nameField.input.text;
                            sheet.dirty = true;
                        }
                    }
                }
                Label { text: "Address:" }
                Bevel {
                    width: 200
                    height: 21
                    style: "sunken"
                    face: Theme.face // read-only
                    Label {
                        x: 4
                        anchors.verticalCenter: parent.verticalCenter
                        text: Bluetooth.adapterAddress || "(none)"
                    }
                }
            }
        }

        CheckBox95 {
            id: findBox
            y: computerGroup.height + 14
            text: "<u>L</u>et other Bluetooth devices find this computer"
            checked: sheet.discoverableEdit
            enabled: Bluetooth.powered
            onToggled: (on) => {
                sheet.discoverableEdit = on;
                sheet.dirty = true;
            }
        }
        Label {
            x: 20
            y: findBox.y + findBox.height + 4
            width: parent.width - x
            wrapMode: Text.Wrap
            color: Theme.shadow
            text: Bluetooth.discoverableTimeout > 0
                  ? "For " + Math.round(Bluetooth.discoverableTimeout / 60) + " minutes after you click Apply. "
                    + "You only need this when pairing from the other device; pairing from this "
                    + "computer works without it."
                  : "You only need this when pairing from the other device."
        }
    }

    extras: [
        // device Properties
        Dialog95 {
            id: detailsDialog
            title: sheet.device ? sheet.device.name + " Properties" : "Properties"
            dialogWidth: 330
            dialogHeight: 220

            Row {
                x: 12
                y: 12
                spacing: 10
                BluetoothDeviceIcon {
                    kind: sheet.device ? sheet.device.kind : "other"
                    size: 32
                }
                Label {
                    anchors.verticalCenter: parent.verticalCenter
                    text: sheet.device ? sheet.device.name : ""
                    font.bold: true
                }
            }
            Column {
                x: 12
                y: 56
                spacing: 4
                Repeater {
                    model: !sheet.device ? [] : [
                        ["Type", sheet.kindLabel(sheet.device.kind)],
                        ["Address", sheet.device.address],
                        ["Status", sheet.device.connected ? "Connected" : "Not connected"],
                        ["Paired", sheet.device.paired ? "Yes" : "No"],
                        ["Reconnects by itself", sheet.device.trusted ? "Yes" : "No"],
                        ["Battery", sheet.device.battery >= 0 ? sheet.device.battery + "%" : "(not reported)"],
                    ]
                    Row {
                        required property var modelData
                        spacing: 8
                        Label { width: 120; text: parent.modelData[0] + ":" }
                        Label { text: parent.modelData[1] }
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

        // questions from the pairing agent
        Dialog95 {
            id: pairDialog
            title: "Pair with " + deviceName
            dialogWidth: 340
            dialogHeight: 170
            property string kind: ""
            property string deviceName: ""
            property string code: ""
            readonly property bool asksForCode: kind === "pin" || kind === "passkey"

            function ask(k, name, c) {
                kind = k;
                deviceName = name;
                code = c;
                codeField.text = "";
                show();
                if (asksForCode)
                    codeField.forceActiveFocus();
            }
            function answer(accept) {
                if (kind !== "display")
                    Bluetooth.answerPairing(accept, codeField.text);
                codeField.text = "";
                close();
            }

            Label {
                x: 12
                y: 14
                width: parent.width - 24
                wrapMode: Text.Wrap
                text: pairDialog.kind === "confirm"
                      ? "Make sure this number is shown on '" + pairDialog.deviceName + "', then click Yes:"
                    : pairDialog.kind === "display"
                      ? "Type this number on '" + pairDialog.deviceName + "', then press Enter on it:"
                    : pairDialog.kind === "pin"
                      ? "Type the PIN for '" + pairDialog.deviceName + "'. Many devices use 0000 or 1234; "
                        + "see its manual."
                    : pairDialog.kind === "passkey"
                      ? "Type the passkey shown on '" + pairDialog.deviceName + "':"
                    : "'" + pairDialog.deviceName + "' wants to pair with this computer. Allow it?"
            }
            Text {
                x: 12
                y: 62
                width: parent.width - 24
                horizontalAlignment: Text.AlignHCenter
                visible: pairDialog.kind === "confirm" || pairDialog.kind === "display"
                text: pairDialog.code.length === 6 ? pairDialog.code.substring(0, 3) + " " + pairDialog.code.substring(3)
                                                   : pairDialog.code
                font.family: Theme.fontFamily
                font.pixelSize: 24
                font.bold: true
                font.letterSpacing: 2
            }
            TextField95 {
                id: codeField
                x: 12
                y: 66
                width: parent.width - 24
                visible: pairDialog.asksForCode
                input.inputMethodHints: pairDialog.kind === "passkey" ? Qt.ImhDigitsOnly : Qt.ImhNone
                input.maximumLength: pairDialog.kind === "passkey" ? 6 : 16
                onAccepted: pairDialog.answer(true)
                onEscaped: pairDialog.answer(false)
            }
            Row {
                anchors.right: parent.right
                anchors.rightMargin: 10
                anchors.bottom: parent.bottom
                anchors.bottomMargin: 10
                spacing: 6
                Button95 {
                    visible: pairDialog.kind !== "display"
                    text: pairDialog.asksForCode ? "OK" : "Yes"
                    isDefault: true
                    onClicked: pairDialog.answer(true)
                }
                Button95 {
                    text: pairDialog.kind === "display" ? "Close" : pairDialog.asksForCode ? "Cancel" : "No"
                    onClicked: pairDialog.answer(false)
                }
            }
        }
    ]
}
