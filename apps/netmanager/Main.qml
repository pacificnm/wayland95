import QtQuick
import QtQuick.Window
import QtWebEngine
import QtWebChannel
import Win95Ui
import Win95NetManager

// Network Manager: the network's devices (routers, switches, access points,
// printers, storage, cameras...) listed on the left; Connect opens the
// chosen one's web page on the right, in Chromium, and fills in its login.
// Devices are saved with New Device or Properties (Devices.qml keeps them;
// their passwords are in the keyring).
Window {
    id: window

    title: connected ? connectedDevice.name + " - Network Manager" : "Network Manager"
    width: 1000
    height: 680
    minimumWidth: 640
    minimumHeight: 420
    visible: true
    color: Theme.face

    property string selectedId: ""
    readonly property var selected: { Devices.devices; return selectedId ? Devices.device(selectedId) : null; }
    property string connectedId: ""
    readonly property var connectedDevice: { Devices.devices; return connectedId ? Devices.device(connectedId) : ({}); }
    readonly property bool connected: connectedId !== "" && connectedDevice.id !== undefined
    readonly property string gateway: Devices.gateway()
    readonly property bool modalOpen: msgBox.visible || deviceDialog.visible || loginDialog.visible || promptDialog.visible
                                      || fileDialog.visible || aboutDialog.visible
    property string statusText: ""
    property string hoverText: ""
    // per connection: the login was sent by itself once (a wrong password mustn't loop),
    // and the keyring's password answered a page's own login box once
    property bool autoTried: false
    property bool basicTried: false
    property bool loginRequested: false   // Log In was clicked: fill and send

    function kindName(k) {
        return { router: "Router", "switch": "Switch", accesspoint: "Wireless access point", printer: "Printer",
                 storage: "Network storage", camera: "Camera", server: "Server" }[k] || "Device";
    }
    function notify(text) {
        statusText = text;
        statusTimer.restart();
    }
    Timer { id: statusTimer; interval: 8000; onTriggered: window.statusText = "" }

    // ---- devices
    function newDevice(kind, host) { deviceDialog.create(kind || "other", host || ""); }
    function addRouter() { deviceDialog.create("router", gateway); }
    function editDevice() { if (selected) deviceDialog.edit(selectedId); }
    function deleteDevice() {
        if (!selected)
            return;
        var id = selectedId, name = selected.name;
        msgBox.show("Delete Device", "Delete “" + name + "” from Network Manager?\n\nIts saved password is removed from the keyring. "
                    + "The device itself isn't changed.", "question", ["Yes", "No"], function (b) {
            if (b !== 0)
                return;
            if (window.connectedId === id)
                window.disconnect();
            Devices.remove(id);
            window.selectedId = Devices.devices.length ? Devices.devices[0].id : "";
        });
    }
    function connectTo(id) {
        var d = Devices.device(id || selectedId);
        if (!d.id)
            return;
        selectedId = d.id;
        connectedId = d.id;
        autoTried = false;
        basicTried = false;
        loginRequested = false;
        Devices.touch(d.id);
        view.url = d.url;
        view.forceActiveFocus();
    }
    function disconnect() {
        connectedId = "";
        view.url = "about:blank";
    }
    // fills the device's page's login (and sends it if `send`): only on its own pages
    function fillLogin(send) {
        var d = connectedDevice;
        if (!connected || !Devices.isDevicePage(d.id, view.url.toString()))
            return;
        Devices.password(d.id, function (found, pw) {
            if (!found && !d.username)
                return;
            if (connectedId === d.id && Devices.isDevicePage(d.id, view.url.toString()))
                device.fillRequested(d.username, found ? pw : "", send);
        });
    }
    function logIn() {
        loginRequested = true;
        fillLogin(true);
    }

    // ---- menus
    function deviceMenu() {
        return [
            { text: "&New Device...", shortcut: "Ctrl+N", action: function () { window.newDevice(); } },
            { text: "Add Your &Router" + (gateway ? " (" + gateway + ")" : "") + "...", enabled: gateway !== "", action: addRouter },
            { separator: true },
            { text: "&Connect", shortcut: "Enter", enabled: !!selected, action: function () { window.connectTo(); } },
            { text: "&Log In", shortcut: "Ctrl+L", enabled: connected, action: logIn },
            { text: "&Disconnect", enabled: connected, action: disconnect },
            { separator: true },
            { text: "&Properties...", shortcut: "Alt+Enter", enabled: !!selected, action: editDevice },
            { text: "De&lete", shortcut: "Del", enabled: !!selected, action: deleteDevice },
            { separator: true },
            { text: "E&xit", action: function () { window.close(); } },
        ];
    }
    function viewMenu() {
        return [
            { text: "&Back", shortcut: "Alt+Left", enabled: connected && view.canGoBack, action: function () { view.goBack(); } },
            { text: "&Forward", shortcut: "Alt+Right", enabled: connected && view.canGoForward, action: function () { view.goForward(); } },
            { text: "&Home Page", enabled: connected, action: function () { view.url = connectedDevice.url; } },
            { separator: true },
            { text: "&Reload", shortcut: "F5", enabled: connected, action: function () { view.reload(); } },
            { text: "&Stop", shortcut: "Esc", enabled: connected && view.loading, action: function () { view.stop(); } },
            { separator: true },
            { text: "Zoom &In", shortcut: "Ctrl++", enabled: connected, action: function () { view.zoomFactor = Math.min(3, view.zoomFactor + 0.1); } },
            { text: "Zoom &Out", shortcut: "Ctrl+-", enabled: connected, action: function () { view.zoomFactor = Math.max(0.3, view.zoomFactor - 0.1); } },
            { text: "&Actual Size", shortcut: "Ctrl+0", enabled: connected, action: function () { view.zoomFactor = 1; } },
            { separator: true },
            { text: "Open in &Web Browser", enabled: connected, action: function () { Qt.openUrlExternally(view.url); } },
        ];
    }
    function helpMenu() {
        return [{ text: "&About Network Manager", action: function () { aboutDialog.show(); } }];
    }
    Shortcut { sequence: "Ctrl+N"; enabled: !window.modalOpen; onActivated: window.newDevice() }
    Shortcut { sequence: "Ctrl+L"; enabled: !window.modalOpen && window.connected; onActivated: window.logIn() }
    Shortcut { sequence: "F5"; enabled: !window.modalOpen && window.connected; onActivated: view.reload() }
    Shortcut { sequence: "Alt+Left"; enabled: !window.modalOpen && window.connected; onActivated: view.goBack() }
    Shortcut { sequence: "Alt+Right"; enabled: !window.modalOpen && window.connected; onActivated: view.goForward() }
    Shortcut { sequences: ["Ctrl++", "Ctrl+="]; enabled: !window.modalOpen && window.connected; onActivated: view.zoomFactor = Math.min(3, view.zoomFactor + 0.1) }
    Shortcut { sequence: "Ctrl+-"; enabled: !window.modalOpen && window.connected; onActivated: view.zoomFactor = Math.max(0.3, view.zoomFactor - 0.1) }
    Shortcut { sequence: "Ctrl+0"; enabled: !window.modalOpen && window.connected; onActivated: view.zoomFactor = 1 }

    MenuBar95 {
        id: menuBar
        x: 2
        y: 2
        width: parent.width - 4
        height: 20
        popupMenu: popup
        menus: [
            { title: "Device", items: window.deviceMenu },
            { title: "View", items: window.viewMenu },
            { title: "Help", items: window.helpMenu },
        ]
    }

    // ---- the toolbar
    Item {
        id: toolBar
        x: 2
        y: menuBar.y + menuBar.height + 2
        width: parent.width - 4
        height: 30
        Rectangle { width: parent.width; height: 1; color: Theme.shadow }
        Rectangle { y: 1; width: parent.width; height: 1; color: Theme.highlight }
        Row {
            x: 2
            y: 4
            spacing: 2
            ToolButton95 { height: 24; text: "New..."; onClicked: window.newDevice() }
            ToolButton95 { height: 24; text: window.connected && window.connectedId === window.selectedId ? "Disconnect" : "Connect"
                           enabled: !!window.selected
                           onClicked: window.connected && window.connectedId === window.selectedId ? window.disconnect() : window.connectTo() }
            ToolButton95 { height: 24; text: "Properties"; enabled: !!window.selected; onClicked: window.editDevice() }
            ToolButton95 { height: 24; text: "Delete"; enabled: !!window.selected; onClicked: window.deleteDevice() }
            Item { width: 10; height: 1 }
            ToolButton95 { width: 26; height: 24; glyph: "◄"; enabled: window.connected && view.canGoBack; onClicked: view.goBack() }
            ToolButton95 { width: 26; height: 24; glyph: "►"; enabled: window.connected && view.canGoForward; onClicked: view.goForward() }
            ToolButton95 { height: 24; text: view.loading ? "Stop" : "Reload"; enabled: window.connected
                           onClicked: view.loading ? view.stop() : view.reload() }
            ToolButton95 { height: 24; text: "Home"; enabled: window.connected; onClicked: view.url = window.connectedDevice.url }
            Item { width: 10; height: 1 }
            ToolButton95 { height: 24; text: "Log In"; enabled: window.connected; onClicked: window.logIn() }
        }
    }

    // ---- the devices
    Bevel {
        id: listWell
        x: 4
        y: toolBar.y + toolBar.height + 2
        width: 220
        height: statusBar.y - y - 4
        style: "sunken"
        face: Theme.window
        activeFocusOnTab: true
        onActiveFocusChanged: if (activeFocus) list.forceActiveFocus()
        ListView {
            id: list
            x: 2
            y: 2
            width: parent.width - 4 - (listBar.visible ? listBar.width : 0)
            height: parent.height - 4
            clip: true
            interactive: false
            boundsBehavior: Flickable.StopAtBounds
            model: Devices.devices
            focus: true
            Keys.onPressed: (event) => {
                var i = Devices.devices.findIndex(function (d) { return d.id === window.selectedId; });
                if (event.key === Qt.Key_Down && i < Devices.devices.length - 1) window.selectedId = Devices.devices[i + 1].id;
                else if (event.key === Qt.Key_Up && i > 0) window.selectedId = Devices.devices[i - 1].id;
                else if ((event.key === Qt.Key_Return || event.key === Qt.Key_Enter) && (event.modifiers & Qt.AltModifier)) window.editDevice();
                else if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter) window.connectTo();
                else if (event.key === Qt.Key_Delete) window.deleteDevice();
                else return;
                event.accepted = true;
            }
            delegate: Item {
                id: row
                required property var modelData
                readonly property bool isSelected: modelData.id === window.selectedId
                width: list.width
                height: 38
                Rectangle {
                    x: 36
                    y: 2
                    width: parent.width - 38
                    height: parent.height - 4
                    color: row.isSelected ? Theme.selection : "transparent"
                }
                Loader {
                    x: 36
                    y: 2
                    width: parent.width - 38
                    height: parent.height - 4
                    active: row.isSelected && list.activeFocus
                    sourceComponent: FocusRect95 { color: "#ffff80" }
                }
                DeviceIcon { x: 3; y: 3; kind: row.modelData.kind }
                Text {
                    x: 40
                    y: 4
                    width: parent.width - 44
                    elide: Text.ElideRight
                    textFormat: Text.PlainText
                    text: row.modelData.name + (row.modelData.id === window.connectedId ? "  ●" : "")
                    color: row.isSelected ? Theme.selectionText : Theme.text
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontSize
                    font.bold: row.modelData.id === window.connectedId
                }
                Text {
                    x: 40
                    y: 19
                    width: parent.width - 44
                    elide: Text.ElideRight
                    textFormat: Text.PlainText
                    text: row.modelData.host + (row.modelData.port > 0 ? ":" + row.modelData.port : "")
                    color: row.isSelected ? Theme.selectionText : Theme.shadow
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontSize
                }
                MouseArea {
                    anchors.fill: parent
                    acceptedButtons: Qt.LeftButton | Qt.RightButton
                    onPressed: { list.forceActiveFocus(); window.selectedId = row.modelData.id; }
                    onDoubleClicked: (mouse) => { if (mouse.button === Qt.LeftButton) window.connectTo(); }
                    onClicked: (mouse) => {
                        if (mouse.button !== Qt.RightButton)
                            return;
                        var p = mapToItem(popup, mouse.x, mouse.y);
                        popup.popup(p.x, p.y, [
                            { text: "&Connect", action: function () { window.connectTo(); } },
                            { separator: true },
                            { text: "&Properties...", action: window.editDevice },
                            { text: "De&lete", action: window.deleteDevice },
                        ]);
                    }
                }
            }
            WheelHandler {
                onWheel: (event) => {
                    list.contentY = Math.max(0, Math.min(list.contentHeight - list.height, list.contentY - event.angleDelta.y / 120 * 76));
                }
            }
        }
        ScrollBar95 {
            id: listBar
            anchors.right: parent.right
            anchors.rightMargin: 2
            y: 2
            height: parent.height - 4
            flickable: list
            stepSize: 38
        }
        Column {
            visible: Devices.devices.length === 0
            x: 10
            y: 14
            width: parent.width - 20
            spacing: 8
            Text {
                width: parent.width
                wrapMode: Text.Wrap
                text: "No devices yet."
                color: Theme.shadow
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontSize
            }
            Button95 { width: parent.width; text: "New Device..."; onClicked: window.newDevice() }
            Button95 { width: parent.width; visible: window.gateway !== ""; text: "Add Your Router..."; onClicked: window.addRouter() }
        }
    }

    // ---- the page (connected), or the chosen device's details
    Item {
        id: pageArea
        x: listWell.x + listWell.width + 4
        y: listWell.y
        width: parent.width - x - 4
        height: listWell.height

        // the address it's at, and whether the connection is private
        Item {
            id: addressBar
            visible: window.connected
            width: parent.width
            height: visible ? 26 : 0
            Text {
                id: addressLabel
                height: 22
                verticalAlignment: Text.AlignVCenter
                text: "Address:"
                color: Theme.text
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontSize
            }
            Bevel {
                x: addressLabel.width + 6
                width: parent.width - x - lockLabel.width - 8
                height: 22
                style: "sunken"
                face: Theme.window
                Text {
                    x: 4
                    width: parent.width - 8
                    height: parent.height
                    verticalAlignment: Text.AlignVCenter
                    elide: Text.ElideMiddle
                    textFormat: Text.PlainText
                    text: view.url.toString() === "about:blank" ? "" : view.url.toString()
                    color: Theme.text
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontSize
                }
            }
            Text {
                id: lockLabel
                anchors.right: parent.right
                height: 22
                verticalAlignment: Text.AlignVCenter
                text: view.url.toString().indexOf("https:") === 0
                      ? (window.connectedDevice.fingerprint ? "🔒 Encrypted (certificate you trusted)" : "🔒 Encrypted")
                      : "Not encrypted"
                color: view.url.toString().indexOf("https:") === 0 ? Theme.text : "#800000"
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontSize
            }
        }
        Bevel {
            id: viewFrame
            visible: window.connected
            y: addressBar.height
            width: parent.width
            height: parent.height - y
            style: "sunken"
            WebEngineView {
                id: view
                x: 2
                y: 2
                width: parent.width - 4
                height: parent.height - 4
                profile: profile
                url: "about:blank"
                settings.javascriptCanOpenWindows: false
                settings.localContentCanAccessFileUrls: false
                settings.pluginsEnabled: false
                webChannel: channel
                webChannelWorld: WebEngineScript.ApplicationWorld

                onLoadingChanged: (info) => {
                    if (info.status === WebEngineView.LoadFailedStatus && info.errorDomain !== WebEngineView.NoErrorDomain
                            && info.errorCode !== -3 /* aborted */)
                        window.notify("Couldn't open " + info.url + ": " + info.errorString);
                }
                onLinkHovered: (url) => window.hoverText = url.toString()
                // a page's new window opens here
                onNewWindowRequested: (request) => { view.url = request.requestedUrl; }

                // a device that signs its own certificate (most do): trusted
                // for this device when you say so, and then only that certificate
                Component.onCompleted: Devices.watch(view)
                onCertificateError: (error) => {
                    error.defer();
                    // (Devices hears of the certificate after this handler)
                    Qt.callLater(view.certificateProblem, error);
                }
                function certificateProblem(error) {
                    var d = window.connectedDevice;
                    var fp = Devices.fingerprint(error.url);
                    var ours = window.connected && Devices.isDevicePage(d.id, error.url.toString());
                    if (ours && d.fingerprint && fp && d.fingerprint === fp) {
                        error.acceptCertificate();
                        return;
                    }
                    if (!error.overridable) {
                        error.rejectCertificate();
                        msgBox.show("Network Manager", "The page at " + error.url + " can't be opened safely: " + error.description, "error", ["OK"], null);
                        return;
                    }
                    var changed = ours && d.fingerprint && d.fingerprint !== fp;
                    var who = Devices.certificateSubject(error.url);
                    msgBox.show(changed ? "Certificate Changed" : "Certificate Not Trusted",
                                (changed ? "“" + d.name + "” now shows a DIFFERENT certificate from the one you trusted. "
                                           + "It could have been reset or updated, or something else could be pretending to be it.\n\n"
                                         : "No one vouches for the certificate of " + error.url.toString().replace(/^https:\/\//, "").replace(/\/.*/, "")
                                           + " (" + error.description + "). Network devices usually make their own, so this is normal for a device "
                                           + "you know, but check it's the one you mean.\n\n")
                                + "Certificate: " + who + "\nFingerprint (SHA-256):\n" + fp
                                + (ours ? "\n\nTrust this certificate for “" + d.name + "”?" : "\n\nOpen it this once?"),
                                changed ? "warning" : "question", [ours ? "Trust It" : "Open It", "Cancel"], function (b) {
                                    if (b === 0) {
                                        if (ours)
                                            Devices.setFingerprint(d.id, fp);
                                        error.acceptCertificate();
                                    } else {
                                        error.rejectCertificate();
                                    }
                                });
                }

                // a device that asks for its login in a box of the browser's
                // own (HTTP authentication): the saved login, once; then asked
                onAuthenticationDialogRequested: (request) => {
                    request.accepted = true;
                    var d = window.connectedDevice;
                    if (request.type === AuthenticationDialogRequest.AuthenticationTypeHTTP
                            && window.connected && !window.basicTried && Devices.isDevicePage(d.id, request.url.toString())) {
                        window.basicTried = true;
                        Devices.password(d.id, function (found, pw) {
                            if (found && d.username)
                                request.dialogAccept(d.username, pw);
                            else
                                loginDialog.ask(request, d);
                        });
                        return;
                    }
                    loginDialog.ask(request, Devices.isDevicePage(d.id, request.url.toString()) ? d : null);
                }

                onJavaScriptDialogRequested: (r) => {
                    r.accepted = true;
                    var host = r.securityOrigin.toString().replace(/^\w+:\/\//, "").replace(/\/.*$/, "");
                    if (r.type === JavaScriptDialogRequest.DialogTypeAlert) {
                        msgBox.show(host + " says", r.message, "info", ["OK"], function () { r.dialogAccept(); });
                    } else if (r.type === JavaScriptDialogRequest.DialogTypePrompt) {
                        promptDialog.ask(r, host);
                    } else {
                        msgBox.show(host + " asks", r.type === JavaScriptDialogRequest.DialogTypeBeforeUnload
                                    ? "Leave this page? Changes you made may not be saved." : r.message,
                                    "question", ["OK", "Cancel"], function (b) { if (b === 0) r.dialogAccept(); else r.dialogReject(); });
                    }
                }
                // a page asking for a file (firmware, a settings backup to restore)
                onFileDialogRequested: (r) => {
                    r.accepted = true;
                    fileDialog.request = r;
                    fileDialog.openAt(Win95Fs.homePath() + "/Downloads", "");
                }
                onContextMenuRequested: (request) => {
                    request.accepted = true;
                    var p = view.mapToItem(popup, request.position.x, request.position.y);
                    var items = [];
                    if (request.selectedText !== "")
                        items.push({ text: "&Copy", action: function () { view.triggerWebAction(WebEngineView.Copy); } }, { separator: true });
                    if (request.isContentEditable)
                        items.push({ text: "&Paste", action: function () { view.triggerWebAction(WebEngineView.Paste); } }, { separator: true });
                    if (request.linkUrl.toString() !== "")
                        items.push({ text: "Copy &Link Address", action: function () { view.triggerWebAction(WebEngineView.CopyLinkToClipboard); } }, { separator: true });
                    popup.popup(p.x, p.y, items.concat(window.viewMenu().slice(0, 5)));
                }
            }
        }

        // ---- not connected: the chosen device
        Bevel {
            visible: !window.connected
            anchors.fill: parent
            style: "sunken"
            face: Theme.window
            Column {
                visible: !!window.selected
                x: 24
                y: 24
                width: parent.width - 48
                spacing: 10
                Row {
                    spacing: 14
                    DeviceIcon { size: 48; kind: window.selected ? window.selected.kind : "other" }
                    Column {
                        spacing: 2
                        Text {
                            textFormat: Text.PlainText
                            text: window.selected ? window.selected.name : ""
                            color: Theme.text
                            font.family: Theme.fontFamily
                            font.pixelSize: 18
                            font.bold: true
                        }
                        Text {
                            text: window.selected ? window.kindName(window.selected.kind) : ""
                            color: Theme.shadow
                            font.family: Theme.fontFamily
                            font.pixelSize: Theme.fontSize
                        }
                    }
                }
                Column {
                    spacing: 6
                    width: parent.width
                    Repeater {
                        model: !window.selected ? [] : [
                            ["Web page:", window.selected.url],
                            ["User name:", window.selected.username || "(none)"],
                            ["Login:", window.selected.fillLogin ? (window.selected.autoLogin ? "Filled in and sent by itself" : "Filled in for you") : "Typed by you"],
                            ["Certificate:", window.selected.protocol !== "https" ? "(HTTP: not encrypted)"
                                             : window.selected.fingerprint ? "Its own, trusted by you: " + window.selected.fingerprint.slice(0, 23) + "..." : "Checked as usual"],
                            ["Last used:", window.selected.lastUsed ? new Date(window.selected.lastUsed).toLocaleString(Qt.locale(), Locale.ShortFormat) : "Never"],
                            ["Notes:", window.selected.notes || ""],
                        ]
                        Row {
                            required property var modelData
                            spacing: 8
                            Text {
                                width: 80
                                textFormat: Text.PlainText
                                text: parent.modelData[0]
                                color: Theme.text
                                font.family: Theme.fontFamily
                                font.pixelSize: Theme.fontSize
                            }
                            Text {
                                width: Math.max(100, pageArea.width - 140)
                                wrapMode: Text.Wrap
                                textFormat: Text.PlainText
                                text: parent.modelData[1]
                                color: Theme.text
                                font.family: Theme.fontFamily
                                font.pixelSize: Theme.fontSize
                            }
                        }
                    }
                }
                Row {
                    spacing: 6
                    Button95 { width: 100; text: "Connect"; isDefault: true; onClicked: window.connectTo() }
                    Button95 { width: 100; text: "Properties..."; onClicked: window.editDevice() }
                }
                Text {
                    width: parent.width
                    wrapMode: Text.Wrap
                    text: "Connect opens the device's settings page here and fills in its login. Press Log In (Ctrl+L) to fill it in and send it."
                    color: Theme.shadow
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontSize
                }
            }
            Column {
                visible: !window.selected
                anchors.centerIn: parent
                width: Math.min(parent.width - 48, 420)
                spacing: 10
                DeviceIcon { anchors.horizontalCenter: parent.horizontalCenter; size: 48; kind: "netmanager" }
                Text {
                    width: parent.width
                    horizontalAlignment: Text.AlignHCenter
                    wrapMode: Text.Wrap
                    text: "Network Manager logs in to the settings pages of your network's devices: your router, switches, "
                          + "access points, printers, network storage and cameras.\n\nSave a device with its address and login, then Connect."
                    color: Theme.text
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontSize
                }
                Row {
                    anchors.horizontalCenter: parent.horizontalCenter
                    spacing: 6
                    Button95 { width: 120; text: "New Device..."; onClicked: window.newDevice() }
                    Button95 {
                        width: 150
                        visible: window.gateway !== ""
                        text: "Add Your Router (" + window.gateway + ")..."
                        onClicked: window.addRouter()
                    }
                }
            }
        }
    }

    // ---- the status bar
    Item {
        id: statusBar
        x: 2
        anchors.bottom: parent.bottom
        anchors.bottomMargin: 2
        width: parent.width - 4
        height: 20
        Bevel { width: parent.width - progressCell.width - 2; height: parent.height; style: "thinSunken" }
        Text {
            x: 4
            width: parent.width - progressCell.width - 10
            height: parent.height
            verticalAlignment: Text.AlignVCenter
            elide: Text.ElideRight
            textFormat: Text.PlainText
            color: Theme.text
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontSize
            text: window.hoverText !== "" ? window.hoverText
                : window.statusText !== "" ? window.statusText
                : window.connected ? (view.loading ? "Opening " + window.connectedDevice.name + "..." : "Connected to " + window.connectedDevice.name)
                : Devices.devices.length === 1 ? "1 device" : Devices.devices.length + " devices"
        }
        Item {
            id: progressCell
            anchors.right: parent.right
            width: 160
            height: parent.height
            Bevel { anchors.fill: parent; style: "thinSunken" }
            ProgressBar95 {
                x: 2
                y: 2
                width: parent.width - 4
                height: parent.height - 4
                visible: window.connected && view.loading
                value: view.loadProgress
            }
        }
    }

    // ---- the page's login filler (login.js), in a world of its own
    WebChannel {
        id: channel
        registeredObjects: [device]
    }
    QtObject {
        id: device
        WebChannel.id: "w95device"
        signal fillRequested(string username, string password, bool send)
        // a page of the device has loaded: fill its login if the device wants it
        function ready() {
            var d = window.connectedDevice;
            if (!window.connected || !Devices.isDevicePage(d.id, view.url.toString()))
                return;
            if (window.loginRequested) {
                window.loginRequested = false;
                return;
            }
            if (d.fillLogin) {
                var send = d.autoLogin && !window.autoTried;
                if (send)
                    window.autoTried = true;
                window.fillLogin(send);
            }
        }
        function result(what) {
            if (what === "submitted")
                window.notify("Logging in to " + window.connectedDevice.name + "...");
            else if (what === "filled")
                window.notify("The login is filled in: press the page's log-in button, or Log In.");
            else if (what === "noform" && window.loginRequested)
                window.notify("This page has no login box.");
        }
    }
    WebEngineProfile {
        id: profile
        // devices' sessions (cookies) kept between runs, apart from the Web Browser's
        storageName: "win95netmanager"
        httpCacheType: WebEngineProfile.DiskHttpCache
        persistentCookiesPolicy: WebEngineProfile.AllowPersistentCookies
        Component.onCompleted: userScripts.collection = [
            { name: "webchannel", sourceUrl: "qrc:///qtwebchannel/qwebchannel.js",
              injectionPoint: WebEngineScript.DocumentCreation, worldId: WebEngineScript.ApplicationWorld },
            { name: "login", sourceUrl: Qt.resolvedUrl("login.js"),
              injectionPoint: WebEngineScript.DocumentReady, worldId: WebEngineScript.ApplicationWorld },
        ]
        // a settings backup or log the device gives: to Downloads
        onDownloadRequested: (download) => {
            download.downloadDirectory = Win95Fs.homePath() + "/Downloads";
            download.accept();
            window.notify("Saving " + download.downloadFileName + " to Downloads...");
        }
        onDownloadFinished: (download) => {
            window.notify(download.state === WebEngineDownloadRequest.DownloadCompleted
                          ? "Saved " + download.downloadFileName + " in Downloads."
                          : "Couldn't save " + download.downloadFileName + ".");
        }
    }

    // ---- dialogs
    DeviceDialog {
        id: deviceDialog
        popupMenu: popup
        messageBox: msgBox
        onSaved: (id) => {
            window.selectedId = id;
            list.forceActiveFocus();
        }
    }

    // a login box of the browser's own (HTTP authentication), asked
    Dialog95 {
        id: loginDialog
        property var request: null
        property var forDevice: null
        title: "Log In"
        dialogWidth: 360
        dialogHeight: 196
        function ask(r, d) {
            request = r;
            forDevice = d;
            userBox.text = d ? d.username : "";
            passBox.text = "";
            rememberBox.checked = !!d;
            show();
            (userBox.text ? passBox : userBox).input.forceActiveFocus();
        }
        function finish(ok) {
            var r = request;
            request = null;
            close();
            if (!r)
                return;
            if (!ok) {
                r.dialogReject();
                return;
            }
            if (forDevice && rememberBox.checked) {
                if (forDevice.username !== userBox.text) {
                    var changed = Object.assign({}, forDevice);
                    changed.username = userBox.text;
                    Devices.save(changed);
                }
                Devices.setPassword(forDevice.id, passBox.text, function () {});
            }
            r.dialogAccept(userBox.text, passBox.text);
        }
        onClosed: if (request) { request.dialogReject(); request = null; }
        Keys.onEscapePressed: finish(false)
        Text {
            x: 12
            y: 10
            width: parent.width - 24
            wrapMode: Text.Wrap
            textFormat: Text.PlainText
            text: loginDialog.request ? loginDialog.request.url.toString().replace(/^\w+:\/\//, "").replace(/\/.*$/, "")
                                        + " asks for a user name and password" + (loginDialog.request.realm ? " (“" + loginDialog.request.realm + "”)." : ".") : ""
            color: Theme.text
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontSize
        }
        Text { x: 12; y: 46; text: "User name:"; color: Theme.text; font.family: Theme.fontFamily; font.pixelSize: Theme.fontSize }
        TextField95 { id: userBox; x: 90; y: 42; width: parent.width - 102; onAccepted: loginDialog.finish(true) }
        Text { x: 12; y: 74; text: "Password:"; color: Theme.text; font.family: Theme.fontFamily; font.pixelSize: Theme.fontSize }
        TextField95 { id: passBox; x: 90; y: 70; width: parent.width - 102; input.echoMode: TextInput.Password; onAccepted: loginDialog.finish(true) }
        CheckBox95 { id: rememberBox; x: 90; y: 98; visible: !!loginDialog.forDevice; text: "<u>R</u>emember it for this device" }
        Row {
            anchors.right: parent.right
            anchors.rightMargin: 12
            anchors.bottom: parent.bottom
            anchors.bottomMargin: 10
            spacing: 6
            Button95 { text: "OK"; isDefault: true; onClicked: loginDialog.finish(true) }
            Button95 { text: "Cancel"; onClicked: loginDialog.finish(false) }
        }
    }

    // a page's prompt()
    Dialog95 {
        id: promptDialog
        property var request: null
        dialogWidth: 360
        dialogHeight: 140
        function ask(r, host) {
            request = r;
            title = host + " asks";
            promptText.text = r.message;
            promptField.text = r.defaultText;
            show();
            promptField.input.forceActiveFocus();
        }
        function finish(ok) {
            var r = request;
            request = null;
            close();
            if (r) {
                if (ok) r.dialogAccept(promptField.text);
                else r.dialogReject();
            }
        }
        onClosed: if (request) { request.dialogReject(); request = null; }
        Keys.onEscapePressed: finish(false)
        Text {
            id: promptText
            x: 12
            y: 10
            width: parent.width - 24
            wrapMode: Text.Wrap
            textFormat: Text.PlainText
            color: Theme.text
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontSize
        }
        TextField95 { id: promptField; x: 12; y: 48; width: parent.width - 24; onAccepted: promptDialog.finish(true) }
        Row {
            anchors.right: parent.right
            anchors.rightMargin: 12
            anchors.bottom: parent.bottom
            anchors.bottomMargin: 10
            spacing: 6
            Button95 { text: "OK"; isDefault: true; onClicked: promptDialog.finish(true) }
            Button95 { text: "Cancel"; onClicked: promptDialog.finish(false) }
        }
    }

    FileDialog95 {
        id: fileDialog
        property var request: null
        mode: "open"
        filters: [{ label: "All Files (*.*)", patterns: ["*"] }]
        messageBox: msgBox
        popupMenu: popup
        onAccepted: (path) => {
            if (request)
                request.dialogAccept(["file://" + path]);
            request = null;
        }
        onVisibleChanged: if (!visible && request) { request.dialogReject(); request = null; }
    }

    Dialog95 {
        id: aboutDialog
        title: "About Network Manager"
        dialogWidth: 360
        dialogHeight: 170
        DeviceIcon { x: 14; y: 14; kind: "netmanager" }
        Text {
            x: 60
            y: 14
            width: parent.width - 74
            wrapMode: Text.Wrap
            color: Theme.text
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontSize
            text: "<b>Network Manager</b><br>Wayland 95<br><br>Logs in to the settings pages of your network's devices, "
                  + "with Chromium (Qt WebEngine). Passwords are kept in your keyring."
        }
        Button95 {
            anchors.right: parent.right
            anchors.rightMargin: 12
            anchors.bottom: parent.bottom
            anchors.bottomMargin: 10
            text: "OK"
            isDefault: true
            onClicked: aboutDialog.close()
        }
    }

    Menu95 { id: popup }
    MessageBox95 { id: msgBox }

    Component.onCompleted: {
        if (Devices.devices.length)
            selectedId = Devices.devices[0].id;
        // win95netmanager NAME: that device, connected
        if (initialDevice !== "") {
            var d = Devices.devices.filter(function (x) { return x.name.toLowerCase() === initialDevice.toLowerCase() || x.id === initialDevice; })[0];
            if (d)
                connectTo(d.id);
        }
        if (testDialog === "new")
            addRouter();
        else if (testDialog === "edit" && selectedId)
            editDevice();
        list.forceActiveFocus();
    }
}
