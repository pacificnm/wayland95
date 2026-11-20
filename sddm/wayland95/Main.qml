import QtQuick

// The Wayland 95 login screen (an SDDM theme): the desktop color, and a
// Win95-style log-on dialog - user name, password, which desktop to start
// - with OK and Shut Down... on the right, like Win95's own logon box.
//
// SDDM gives every theme: `sddm` (login, power, hostName and the
// loginSucceeded / loginFailed / informationMessage signals), `userModel`,
// `sessionModel`, `keyboard`, `config` (theme.conf). SDDM shows this file
// once per screen, each sized to its screen.
Rectangle {
    id: root

    width: 1024
    height: 768
    color: config.stringValue("color") || "#008080"

    // logical pixels: the dialog is laid out at Win95 sizes, then the
    // whole screen scaled by a whole number on big screens
    readonly property int uiScale: {
        var s = parseInt(config.stringValue("scale"));
        if (s >= 1 && s <= 4)
            return s;
        return Math.max(1, Math.floor(Math.min(height / 1000, width / 1600)));
    }

    // --- what SDDM knows -------------------------------------------------
    property var users: []      // { name, realName, needsPassword }
    property var sessions: []   // { name, file }

    function readModels() {
        var u = [];
        for (var i = 0; i < userRepeater.count; ++i) {
            var it = userRepeater.itemAt(i);
            if (it)
                u.push({ name: it.userName, realName: it.realName, needsPassword: it.needsPassword });
        }
        users = u;
        var s = [];
        for (var j = 0; j < sessionRepeater.count; ++j) {
            var st = sessionRepeater.itemAt(j);
            if (st)
                s.push({ name: st.sessionName, file: st.sessionFile });
        }
        sessions = s;
    }

    Repeater {
        id: userRepeater
        model: userModel
        delegate: Item {
            visible: false
            readonly property string userName: model.name || ""
            readonly property string realName: model.realName || ""
            readonly property bool needsPassword: model.needsPassword !== false
        }
        onItemAdded: root.readModels()
        onItemRemoved: root.readModels()
    }
    Repeater {
        id: sessionRepeater
        model: sessionModel
        delegate: Item {
            visible: false
            readonly property string sessionName: model.name || ""
            readonly property string sessionFile: model.file || ""
        }
        onItemAdded: root.readModels()
        onItemRemoved: root.readModels()
    }

    // the session to start: the last one used, else Wayland 95
    function defaultSession() {
        var last = sessionModel.lastIndex;
        if (last >= 0 && last < sessions.length)
            return last;
        for (var i = 0; i < sessions.length; ++i)
            if (sessions[i].file.indexOf("wayland95") >= 0)
                return i;
        return 0;
    }

    // --- logging on ------------------------------------------------------
    property bool busy: false
    property string message: ""
    property bool messageIsError: false

    function logOn() {
        if (busy)
            return;
        var name = userField.text.trim();
        if (!name) {
            showMessage("Type your user name.", true);
            userField.forceActiveFocus();
            return;
        }
        busy = true;
        showMessage("", false);
        sddm.login(name, passwordField.text, sessionBox.currentIndex);
    }
    function showMessage(text, isError) {
        message = text;
        messageIsError = isError;
    }

    Connections {
        target: sddm
        function onLoginFailed() {
            root.busy = false;
            passwordField.text = "";
            root.showMessage("The user name or password is incorrect. Try again.", true);
            passwordField.forceActiveFocus();
        }
        function onLoginSucceeded() {
            root.showMessage("Starting Wayland 95...", false);
        }
        function onInformationMessage(text) {
            root.showMessage(text, false);
        }
    }

    // --- the desktop -----------------------------------------------------
    Image {
        id: wallpaper
        anchors.fill: parent
        visible: status === Image.Ready
        source: {
            var b = config.stringValue("background");
            return b ? (b.charAt(0) === "/" ? "file://" + b : Qt.resolvedUrl(b)) : "";
        }
        asynchronous: true
        fillMode: {
            switch (config.stringValue("backgroundMode")) {
            case "stretch": return Image.Stretch;
            case "tile":    return Image.Tile;
            default:        return Image.Pad;   // centered
            }
        }
    }

    // busy: the hourglass everywhere
    MouseArea {
        anchors.fill: parent
        z: 3000
        visible: root.busy
        cursorShape: Qt.BusyCursor
        acceptedButtons: Qt.AllButtons
    }

    Item {
        id: ui
        width: root.width / root.uiScale
        height: root.height / root.uiScale
        scale: root.uiScale
        transformOrigin: Item.TopLeft

        // --- Log On -------------------------------------------------------
        Dialog95 {
            id: logon
            title: "Welcome to Wayland 95"
            width: 420
            height: body.y + 3 + content.y + content.height + 14
            x: Math.round((ui.width - width) / 2)
            y: Math.round((ui.height - height) / 2)
            enabled: !shutDown.visible

            // banner: the logo and name, as on the Start menu's sidebar
            Rectangle {
                id: banner
                width: parent.width
                height: 54
                gradient: Gradient {
                    orientation: Gradient.Horizontal
                    GradientStop { position: 0; color: "#000000" }
                    GradientStop { position: 1; color: "#808080" }
                }
                Image {
                    id: logo
                    x: 14
                    anchors.verticalCenter: parent.verticalCenter
                    width: 32
                    height: 32
                    source: "logo.png"
                    smooth: false
                }
                Row {
                    x: logo.x + logo.width + 10
                    anchors.verticalCenter: parent.verticalCenter
                    anchors.verticalCenterOffset: -1
                    spacing: 4
                    Text {
                        text: "Wayland"
                        color: "#c0c0c0"
                        font.family: "Liberation Sans"
                        font.pixelSize: 26
                        font.bold: true
                    }
                    Text {
                        text: "95"
                        color: "#ffffff"
                        font.family: "Liberation Sans"
                        font.pixelSize: 26
                    }
                }
            }
            // etched line under the banner
            Column {
                y: banner.height
                Rectangle { width: logon.body.width; height: 1; color: W95.shadow }
                Rectangle { width: logon.body.width; height: 1; color: W95.highlight }
            }

            Icon95 {
                x: 14
                y: content.y + 2
                kind: "key"
            }

            Column {
                id: content
                x: 60
                y: banner.height + 16
                width: buttons.x - x - 14
                spacing: 8

                Text {
                    width: parent.width
                    wrapMode: Text.Wrap
                    text: "Type a user name and password to log on to Wayland 95."
                    color: W95.text
                    font.family: W95.fontFamily
                    font.pixelSize: W95.fontSize
                }

                Grid {
                    columns: 2
                    columnSpacing: 8
                    rowSpacing: 7
                    verticalItemAlignment: Grid.AlignVCenter

                    readonly property int fieldWidth: content.width - 72

                    Label95 { text: "<u>U</u>ser name:" }
                    TextField95 {
                        id: userField
                        width: parent.fieldWidth
                        dropDown: root.users.length > 0
                        enabled: !root.busy
                        onAccepted: {
                            if (passwordField.text)
                                root.logOn();
                            else
                                passwordField.forceActiveFocus();
                        }
                        onEscaped: passwordField.text = ""
                        onDroppedDown: {
                            var names = root.users.map(function (u) {
                                return u.realName && u.realName !== u.name ? u.realName + " (" + u.name + ")" : u.name;
                            });
                            var current = -1;
                            for (var i = 0; i < root.users.length; ++i)
                                if (root.users[i].name === userField.text)
                                    current = i;
                            listPopup.open(userField, names, current, function (i) {
                                userField.text = root.users[i].name;
                                passwordField.forceActiveFocus();
                            });
                        }
                    }

                    Label95 { text: "<u>P</u>assword:" }
                    TextField95 {
                        id: passwordField
                        width: parent.fieldWidth
                        password: true
                        enabled: !root.busy
                        onAccepted: root.logOn()
                        onEscaped: text = ""
                    }

                    Label95 { text: "<u>S</u>tart:" }
                    ComboBox95 {
                        id: sessionBox
                        width: parent.fieldWidth
                        enabled: !root.busy
                        popup: listPopup
                        model: root.sessions.map(function (s) { return s.name; })
                    }

                    Label95 {
                        visible: keyboardBox.visible
                        text: "<u>K</u>eyboard:"
                    }
                    ComboBox95 {
                        id: keyboardBox
                        visible: keyboard.enabled && keyboard.layouts.length > 1
                        width: parent.fieldWidth
                        enabled: !root.busy
                        popup: listPopup
                        model: {
                            var out = [];
                            for (var i = 0; i < keyboard.layouts.length; ++i)
                                out.push(keyboard.layouts[i].longName || keyboard.layouts[i].shortName);
                            return out;
                        }
                        currentIndex: keyboard.currentLayout
                        onActivated: (i) => { keyboard.currentLayout = i; }
                    }
                }

                // what went wrong, what PAM says, or Caps Lock; the line is
                // always there, so the dialog doesn't jump when it fills
                Item {
                    width: parent.width
                    height: Math.max(14, messageText.implicitHeight)
                    Text {
                        id: messageText
                        width: parent.width
                        wrapMode: Text.Wrap
                        text: root.message
                            || (keyboard.capsLock && passwordField.activeFocus ? "Caps Lock is on." : "")
                        color: root.messageIsError && root.message ? W95.error : W95.text
                        font.family: W95.fontFamily
                        font.pixelSize: W95.fontSize
                    }
                }
            }

            Column {
                id: buttons
                x: logon.body.width - width - 12
                y: content.y
                spacing: 6
                Button95 {
                    id: okButton
                    width: shutDownButton.implicitWidth
                    text: "OK"
                    isDefault: true
                    enabled: !root.busy
                    onClicked: root.logOn()
                }
                Button95 {
                    id: shutDownButton
                    text: "Shut <u>D</u>own..."
                    enabled: !root.busy && shutDown.options.length > 0
                    onClicked: shutDown.open()
                }
            }

            // Alt + the underlined letter (Shortcuts: a text field would
            // otherwise take the key as typing)
            readonly property bool shortcutsOn: !root.busy && !shutDown.visible && !listPopup.visible
            Shortcut { sequence: "Alt+U"; enabled: logon.shortcutsOn; onActivated: userField.forceActiveFocus() }
            Shortcut { sequence: "Alt+P"; enabled: logon.shortcutsOn; onActivated: passwordField.forceActiveFocus() }
            Shortcut { sequence: "Alt+S"; enabled: logon.shortcutsOn; onActivated: sessionBox.forceActiveFocus() }
            Shortcut {
                sequence: "Alt+K"
                enabled: logon.shortcutsOn && keyboardBox.visible
                onActivated: keyboardBox.forceActiveFocus()
            }
            Shortcut {
                sequence: "Alt+D"
                enabled: logon.shortcutsOn && shutDown.options.length > 0
                onActivated: shutDown.open()
            }
        }

        // --- Shut Down ----------------------------------------------------
        Item {
            id: shutDown
            anchors.fill: parent
            visible: false
            z: 1000

            readonly property var options: {
                var list = [];
                if (sddm.canPowerOff)
                    list.push({ id: "shutdown", key: Qt.Key_S, text: "<u>S</u>hut down the computer?" });
                if (sddm.canReboot)
                    list.push({ id: "restart", key: Qt.Key_R, text: "<u>R</u>estart the computer?" });
                if (sddm.canSuspend)
                    list.push({ id: "standby", key: Qt.Key_T, text: "S<u>t</u>and by?" });
                if (sddm.canHibernate)
                    list.push({ id: "hibernate", key: Qt.Key_H, text: "<u>H</u>ibernate?" });
                return list;
            }
            property int option: 0

            function open() {
                option = 0;
                visible = true;
                radios.itemAt(0).forceActiveFocus();
            }
            function close() {
                visible = false;
                passwordField.forceActiveFocus();
            }
            function accept() {
                var chosen = options[option].id;
                visible = false;
                if (chosen === "shutdown")
                    sddm.powerOff();
                else if (chosen === "restart")
                    sddm.reboot();
                else if (chosen === "standby")
                    sddm.suspend();
                else if (chosen === "hibernate")
                    sddm.hibernate();
                passwordField.forceActiveFocus();
            }

            // Win95 darkens everything behind its Shut Down box
            Rectangle {
                anchors.fill: parent
                color: "#000000"
                opacity: 0.45
            }
            MouseArea {
                anchors.fill: parent
                acceptedButtons: Qt.AllButtons
            }

            Dialog95 {
                title: "Shut Down"
                width: 340
                height: 21 + 3 + 88 + shutDown.options.length * 22
                x: Math.round((ui.width - width) / 2)
                y: Math.round((ui.height - height) / 2)

                Icon95 {
                    x: 14
                    y: 14
                    kind: "shutdown"
                }
                Text {
                    x: 60
                    y: 14
                    text: "What do you want the computer to do?"
                    color: W95.text
                    font.family: W95.fontFamily
                    font.pixelSize: W95.fontSize
                }
                Column {
                    x: 60
                    y: 36
                    spacing: 4
                    Repeater {
                        id: radios
                        model: shutDown.options
                        delegate: RadioButton95 {
                            required property var modelData
                            required property int index
                            text: modelData.text
                            checked: shutDown.option === index
                            onClicked: shutDown.option = index
                        }
                    }
                }
                Row {
                    anchors.horizontalCenter: parent.horizontalCenter
                    anchors.bottom: parent.bottom
                    anchors.bottomMargin: 10
                    spacing: 6
                    Button95 {
                        text: "Yes"
                        isDefault: true
                        onClicked: shutDown.accept()
                    }
                    Button95 {
                        text: "No"
                        onClicked: shutDown.close()
                    }
                }
            }

            Keys.onPressed: (event) => {
                var n = options.length;
                if (event.key === Qt.Key_Up || event.key === Qt.Key_Down) {
                    option = (option + (event.key === Qt.Key_Down ? 1 : n - 1)) % n;
                    radios.itemAt(option).forceActiveFocus();
                } else if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter || event.key === Qt.Key_Y) {
                    accept();
                } else if (event.key === Qt.Key_Escape || event.key === Qt.Key_N) {
                    close();
                } else {
                    for (var i = 0; i < n; ++i) {
                        if (options[i].key === event.key) {
                            option = i;
                            radios.itemAt(option).forceActiveFocus();
                            event.accepted = true;
                            return;
                        }
                    }
                    return;
                }
                event.accepted = true;
            }
        }

        ListPopup95 {
            id: listPopup
            anchors.fill: parent
        }
    }

    // --- starting up -----------------------------------------------------
    function start() {
        readModels();
        sessionBox.currentIndex = defaultSession();
        var last = userModel.lastUser || "";
        if (!last && users.length === 1)
            last = users[0].name;
        userField.text = last;
        if (last)
            passwordField.forceActiveFocus();
        else
            userField.forceActiveFocus();
    }
    Component.onCompleted: start()
    // the window gets keyboard focus a moment after it's shown
    Timer {
        interval: 200
        running: true
        onTriggered: {
            if (!userField.activeFocus && !passwordField.activeFocus)
                (userField.text ? passwordField : userField).forceActiveFocus();
        }
    }
}
