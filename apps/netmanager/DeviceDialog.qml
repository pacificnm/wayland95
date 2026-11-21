import QtQuick
import Win95Ui
import Win95NetManager

// A device's connection details: its name and kind, how its web page is
// reached (HTTP or HTTPS, address, port, page), its login (the password
// goes in the keyring), whether the login is filled in on its page (and
// sent), and notes. New Device and Properties both use it.
Dialog95 {
    id: dialog

    property Menu95 popupMenu
    property MessageBox95 messageBox
    property string deviceId: ""       // "" for a new device
    property bool passwordChanged: false
    signal saved(string id)

    readonly property var kinds: [
        { key: "router", name: "Router" }, { key: "switch", name: "Switch" },
        { key: "accesspoint", name: "Wireless access point" }, { key: "printer", name: "Printer" },
        { key: "storage", name: "Network storage (NAS)" }, { key: "camera", name: "Camera" },
        { key: "server", name: "Server" }, { key: "other", name: "Other device" },
    ]
    property int kindIndex: 0
    property bool https: false

    title: deviceId ? "Device Properties" : "New Device"
    dialogWidth: 420
    dialogHeight: 452

    function details() {
        return {
            id: deviceId, name: nameField.text, kind: kinds[kindIndex].key, protocol: https ? "https" : "http",
            host: hostField.text.trim().replace(/^https?:\/\//i, "").replace(/\/.*$/, ""),
            port: parseInt(portField.text) || 0, path: pathField.text.trim(),
            username: userField.text, fillLogin: fillBox.checked, autoLogin: autoBox.checked && fillBox.checked,
            notes: notesField.text,
        };
    }
    readonly property string preview: { nameField.text; hostField.text; portField.text; pathField.text; https; return Devices.urlOf(details()); }

    // a new device: its kind and address to start with (the router: the gateway)
    function create(kind, host) {
        deviceId = "";
        nameField.text = kind === "router" && host ? "Router" : "";
        kindIndex = Math.max(0, kinds.findIndex(function (k) { return k.key === kind; }));
        https = false;
        hostField.text = host || "";
        portField.text = "";
        pathField.text = "";
        userField.text = kind === "router" ? "admin" : "";
        passwordField.text = "";
        passwordChanged = false;
        fillBox.checked = true;
        autoBox.checked = false;
        notesField.text = "";
        show();
        nameField.input.forceActiveFocus();
    }
    function edit(id) {
        var d = Devices.device(id);
        deviceId = id;
        nameField.text = d.name;
        kindIndex = Math.max(0, kinds.findIndex(function (k) { return k.key === d.kind; }));
        https = d.protocol === "https";
        hostField.text = d.host;
        portField.text = d.port > 0 ? String(d.port) : "";
        pathField.text = d.path;
        userField.text = d.username;
        passwordField.text = "";
        passwordChanged = false;
        fillBox.checked = d.fillLogin;
        autoBox.checked = d.autoLogin;
        notesField.text = d.notes;
        show();
        nameField.input.forceActiveFocus();
        // the saved password, shown as dots (the keyring may ask to be unlocked)
        Devices.password(id, function (found, pw) {
            if (found && dialog.deviceId === id && !dialog.passwordChanged) {
                passwordField.text = pw;
                dialog.passwordChanged = false;
            }
        });
    }
    function accept() {
        var d = details();
        var problem = Devices.check(d);
        if (problem !== "") {
            messageBox.show(title, problem, "warning", ["OK"], null);
            return;
        }
        var id = Devices.save(d);
        if (!id)
            return;
        var pw = passwordField.text;
        var changed = passwordChanged;
        close();
        if (changed || !deviceId) {
            Devices.setPassword(id, pw, function (ok, error) {
                if (!ok)
                    messageBox.show("Network Manager", "The password couldn't be kept in the keyring: " + error
                                    + "\n\nThe device is saved; you'll be asked for its password when it's needed.", "warning", ["OK"], null);
            });
        }
        saved(id);
    }
    Keys.onEscapePressed: close()

    component Label: Text {
        height: 21
        verticalAlignment: Text.AlignVCenter
        textFormat: Text.StyledText
        color: Theme.text
        font.family: Theme.fontFamily
        font.pixelSize: Theme.fontSize
    }

    DeviceIcon { x: 12; y: 10; kind: dialog.kinds[dialog.kindIndex].key }
    Label { x: 56; y: 6; text: "<u>N</u>ame:" }
    TextField95 { id: nameField; x: 130; y: 6; width: parent.width - 142; onAccepted: dialog.accept() }
    Label { x: 56; y: 32; text: "<u>K</u>ind:" }
    ComboBox95 {
        x: 130
        y: 32
        width: parent.width - 142
        popupMenu: dialog.popupMenu
        model: dialog.kinds.map(function (k) { return k.name; })
        currentIndex: dialog.kindIndex
        onActivated: (i) => dialog.kindIndex = i
    }

    GroupBox95 {
        id: connectionBox
        x: 12
        y: 62
        width: parent.width - 24
        height: 140
        title: "Web page"
        Row {
            spacing: 16
            RadioButton95 { text: "HTTP"; checked: !dialog.https; onClicked: dialog.https = false }
            RadioButton95 { text: "HTTPS (encrypted)"; checked: dialog.https; onClicked: dialog.https = true }
        }
        Label { y: 24; text: "<u>A</u>ddress:" }
        TextField95 {
            id: hostField
            x: 70
            y: 24
            width: parent.width - 70 - 104
            onAccepted: dialog.accept()
            Text {
                visible: hostField.text === ""
                x: 4
                anchors.verticalCenter: parent.verticalCenter
                text: "192.168.1.1 or router.lan"
                color: Theme.shadow
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontSize
            }
        }
        Label { x: parent.width - 96; y: 24; text: "<u>P</u>ort:" }
        TextField95 {
            id: portField
            x: parent.width - 56
            y: 24
            width: 56
            input.validator: IntValidator { bottom: 1; top: 65535 }
            onAccepted: dialog.accept()
        }
        Label { y: 50; text: "Pa<u>g</u>e:" }
        TextField95 {
            id: pathField
            x: 70
            y: 50
            width: parent.width - 70
            onAccepted: dialog.accept()
            Text {
                visible: pathField.text === ""
                x: 4
                anchors.verticalCenter: parent.verticalCenter
                text: "(the first page: /)"
                color: Theme.shadow
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontSize
            }
        }
        Text {
            y: 78
            width: parent.width
            elide: Text.ElideMiddle
            text: dialog.preview ? "Opens " + dialog.preview : "Type the address the device's settings page is at."
            color: Theme.shadow
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontSize
        }
    }

    GroupBox95 {
        id: loginBox
        x: 12
        y: connectionBox.y + connectionBox.height + 6
        width: parent.width - 24
        height: 118
        title: "Login"
        Label { text: "<u>U</u>ser name:" }
        TextField95 { id: userField; x: 76; width: parent.width - 76; onAccepted: dialog.accept() }
        Label { y: 26; text: "Pass<u>w</u>ord:" }
        TextField95 {
            id: passwordField
            x: 76
            y: 26
            width: parent.width - 76
            input.echoMode: TextInput.Password
            onTextChanged: if (input.activeFocus) dialog.passwordChanged = true
            onAccepted: dialog.accept()
        }
        CheckBox95 {
            id: fillBox
            y: 54
            text: "<u>F</u>ill in the login on the device's page"
        }
        CheckBox95 {
            id: autoBox
            x: 16
            y: 74
            enabled: fillBox.checked
            text: "and log <u>i</u>n by itself"
        }
    }
    Text {
        x: 12
        y: loginBox.y + loginBox.height + 2
        width: parent.width - 24
        wrapMode: Text.Wrap
        text: Devices.keyringAvailable ? "The password is kept in your keyring, not in a file."
                                       : "There's no keyring, so the password can't be kept: you'll be asked for it."
        color: Theme.shadow
        font.family: Theme.fontFamily
        font.pixelSize: Theme.fontSize
    }
    Label { x: 12; y: loginBox.y + loginBox.height + 22; text: "N<u>o</u>tes:" }
    TextField95 {
        id: notesField
        x: 70
        y: loginBox.y + loginBox.height + 22
        width: parent.width - 82
    }

    Row {
        anchors.right: parent.right
        anchors.rightMargin: 12
        anchors.bottom: parent.bottom
        anchors.bottomMargin: 10
        spacing: 6
        Button95 { text: "OK"; isDefault: true; onClicked: dialog.accept() }
        Button95 { text: "Cancel"; onClicked: dialog.close() }
    }
}
