import QtQuick
import Win95Ui
import Win95Control

// WireGuard VPN's settings: this computer's tunnels (/etc/wireguard/NAME.conf,
// run by wg-quick@NAME), each with its own keys, addresses and peers.
// Everything goes through wayland95-services (root, by password): the files
// are root's. Private and preshared keys stay there: this window shows a
// tunnel's public key, and asks for a key to be kept, replaced or made.
PropertySheet {
    id: sheet

    title: "WireGuard VPN Properties"
    titles: ["Tunnels", "Interface", "Peers"]

    // [{ name, enabled, active }]
    property var tunnels: []
    property string current: ""
    readonly property var tunnel: {
        for (var i = 0; i < tunnels.length; ++i)
            if (tunnels[i].name === current)
                return tunnels[i];
        return null;
    }
    // the chosen tunnel, as read: its interface and peers
    property var iface: ({})
    property var peers: []      // [{ PublicKey, PresharedKey ("yes"/""), newPreshared, AllowedIPs, Endpoint, PersistentKeepalive, handshake, rx, tx }]
    property int peerIndex: -1
    readonly property var peer: peerIndex >= 0 && peerIndex < peers.length ? peers[peerIndex] : null
    property bool newKey: false // Apply makes the tunnel a new private key
    property bool busy: false
    property string loadError: ""
    property bool filling: false // the fields are being filled in, not edited

    property bool closeWhenDone: false
    onClosing: (close) => {
        if (busy) {
            close.accepted = false;
            closeWhenDone = true;
        }
    }

    // ------------------------------------------------------------ the helper
    function admin(args, input, done) {
        busy = true;
        Services.admin(args, input, function (ok, output) {
            sheet.busy = false;
            done(ok, output);
            if (sheet.closeWhenDone && !sheet.busy)
                sheet.close();
        });
    }
    function failed(what, output) {
        closeWhenDone = false;
        messageBox.show("WireGuard VPN", output || what, "error", ["OK"]);
    }

    function loadList(select) {
        admin(["wg-list"], "", function (ok, output) {
            if (!ok) {
                sheet.loadError = output || "The tunnels couldn't be read.";
                sheet.tunnels = [];
                return;
            }
            sheet.loadError = "";
            var list = [];
            var lines = output.split("\n");
            for (var i = 0; i < lines.length; ++i) {
                var f = lines[i].trim().split(/\s+/);
                if (f.length === 3)
                    list.push({ name: f[0], enabled: f[1] === "yes", active: f[2] === "yes" });
            }
            sheet.tunnels = list;
            var want = select || sheet.current;
            var there = list.some(function (t) { return t.name === want; });
            var next = there ? want : list.length ? list[0].name : "";
            // (choosing another reads it: onCurrentChanged)
            if (next !== sheet.current)
                sheet.current = next;
            else if (next)
                sheet.loadTunnel();
            if (!next)
                sheet.clearTunnel();
            Services.refresh();
        });
    }
    function clearTunnel() {
        iface = {};
        peers = [];
        peerIndex = -1;
        newKey = false;
        dirty = false;
        showInterface();
        showPeer();
    }
    function loadTunnel() {
        admin(["wg-show", current], "", function (ok, output) {
            if (!ok) {
                sheet.failed("The tunnel couldn't be read.", output);
                return;
            }
            var section = null, i2 = {}, ps = [];
            var lines = output.split("\n");
            for (var i = 0; i < lines.length; ++i) {
                var line = lines[i];
                if (line === "[interface]") { section = i2; continue; }
                if (line === "[peer]") { section = { PublicKey: "", PresharedKey: "", newPreshared: "", AllowedIPs: "", Endpoint: "", PersistentKeepalive: "" }; ps.push(section); continue; }
                var eq = line.indexOf("=");
                if (eq < 0 || !section)
                    continue;
                var key = line.substring(0, eq), value = line.substring(eq + 1);
                if (key === "other")
                    section.other = (section.other ? section.other + "\n" : "") + value;
                else
                    section[key] = value;
            }
            sheet.iface = i2;
            sheet.peers = ps;
            sheet.peerIndex = ps.length ? Math.max(0, Math.min(sheet.peerIndex, ps.length - 1)) : -1;
            sheet.newKey = false;
            sheet.showInterface();
            sheet.showPeer();
            sheet.dirty = false;
        });
    }
    function showInterface() {
        filling = true;
        addressField.text = iface.Address || "";
        portField.text = iface.ListenPort || "";
        dnsField.text = iface.DNS || "";
        mtuField.text = iface.MTU || "";
        newKeyBox.checked = newKey;
        filling = false;
    }
    function showPeer() {
        var p = peer;
        filling = true;
        peerKeyField.text = p ? p.PublicKey : "";
        endpointField.text = p ? p.Endpoint : "";
        allowedField.text = p ? p.AllowedIPs : "";
        keepaliveField.text = p ? p.PersistentKeepalive : "";
        presharedField.text = p ? p.newPreshared : "";
        peerKeyField.input.cursorPosition = 0; // (a key is longer than its field: show its start)
        filling = false;
    }
    // the fields into the chosen peer (before another is chosen, or Apply)
    function storePeer() {
        if (!peer)
            return;
        var copy = peers.slice();
        copy[peerIndex] = Object.assign({}, peer, {
            PublicKey: peerKeyField.text.trim(), Endpoint: endpointField.text.trim(),
            AllowedIPs: allowedField.text.trim(), PersistentKeepalive: keepaliveField.text.trim(),
            newPreshared: presharedField.text.trim(),
        });
        peers = copy;
    }
    function choosePeer(index) {
        storePeer();
        peerIndex = index;
        showPeer();
    }

    onCurrentChanged: if (current && !busy) loadTunnel()
    Component.onCompleted: loadList("")

    onApplyRequested: {
        if (!current)
            return;
        storePeer();
        var lines = ["[interface]", "PrivateKey=" + (newKey ? "generate" : "keep"),
                     "Address=" + addressField.text.trim(), "ListenPort=" + portField.text.trim(),
                     "DNS=" + dnsField.text.trim(), "MTU=" + mtuField.text.trim()];
        for (var i = 0; i < peers.length; ++i) {
            var p = peers[i];
            lines.push("[peer]");
            lines.push("PublicKey=" + p.PublicKey);
            lines.push("PresharedKey=" + (p.newPreshared === "none" ? "none" : p.newPreshared ? p.newPreshared
                                          : p.PresharedKey === "yes" ? "keep" : "none"));
            lines.push("AllowedIPs=" + p.AllowedIPs);
            lines.push("Endpoint=" + p.Endpoint);
            lines.push("PersistentKeepalive=" + p.PersistentKeepalive);
        }
        admin(["wg-save", current], lines.join("\n"), function (ok, output) {
            if (!ok) {
                sheet.failed("The settings couldn't be changed.", output);
                sheet.dirty = true;
                return;
            }
            sheet.loadList(sheet.current);
        });
    }

    function unit(action) {
        admin(["unit", action, "wireguard:" + current], "", function (ok, output) {
            if (!ok)
                sheet.failed("That didn't work.", output);
            sheet.loadList(sheet.current);
        });
    }
    function ago(epoch) {
        var s = Math.max(0, Math.round(Date.now() / 1000 - epoch));
        return s < 90 ? s + " seconds ago" : s < 5400 ? Math.round(s / 60) + " minutes ago"
             : s < 129600 ? Math.round(s / 3600) + " hours ago" : Math.round(s / 86400) + " days ago";
    }
    function bytes(n) {
        n = Number(n) || 0;
        return n < 1024 ? n + " bytes" : n < 1048576 ? (n / 1024).toFixed(1) + " KB"
             : n < 1073741824 ? (n / 1048576).toFixed(1) + " MB" : (n / 1073741824).toFixed(2) + " GB";
    }

    component Label: Text {
        wrapMode: Text.Wrap
        textFormat: Text.StyledText
        color: Theme.text
        font.family: Theme.fontFamily
        font.pixelSize: Theme.fontSize
    }

    // ------------------------------------------------------------- Tunnels
    Item {
        AppletIcon { x: 4; y: 2; kind: "wireguard" }
        Label {
            x: 46; y: 2
            width: parent.width - x
            text: sheet.loadError ? sheet.loadError
                : sheet.busy && !sheet.tunnels.length ? "Looking..."
                : sheet.tunnels.length ? "This computer's tunnels. A connected tunnel carries the traffic for its peers' networks."
                : "This computer has no tunnels yet. Add one, or import a .conf file you were given."
        }

        Rectangle {
            id: tunnelList
            y: 44
            width: parent.width - 100
            height: 150
            color: Theme.window
            Bevel { anchors.fill: parent; style: "sunken"; filled: false }
            Column {
                x: 3; y: 3
                width: parent.width - 6
                Repeater {
                    model: sheet.tunnels
                    Rectangle {
                        required property var modelData
                        width: parent.width
                        height: 20
                        color: modelData.name === sheet.current ? Theme.selection : "transparent"
                        Text {
                            x: 4
                            anchors.verticalCenter: parent.verticalCenter
                            text: modelData.name
                            color: modelData.name === sheet.current ? Theme.selectionText : Theme.text
                            font.family: Theme.fontFamily
                            font.pixelSize: Theme.fontSize
                        }
                        Text {
                            x: 110
                            anchors.verticalCenter: parent.verticalCenter
                            text: (modelData.active ? "Connected" : "Not connected")
                                  + (modelData.enabled ? ", starts at boot" : "")
                            color: modelData.name === sheet.current ? Theme.selectionText : Theme.text
                            font.family: Theme.fontFamily
                            font.pixelSize: Theme.fontSize
                        }
                        MouseArea {
                            anchors.fill: parent
                            onClicked: {
                                if (sheet.dirty) {
                                    sheet.messageBox.show("WireGuard VPN", "Apply or cancel the changes to " + sheet.current + " first.",
                                                          "info", ["OK"]);
                                    return;
                                }
                                sheet.current = modelData.name;
                            }
                        }
                    }
                }
            }
        }
        Column {
            x: tunnelList.width + 8
            y: tunnelList.y
            spacing: 6
            Button95 {
                width: 92
                text: sheet.tunnel && sheet.tunnel.active ? "<u>D</u>isconnect" : "<u>C</u>onnect"
                enabled: !sheet.busy && !!sheet.tunnel
                onClicked: sheet.unit(sheet.tunnel.active ? "stop" : "start")
            }
            Button95 {
                width: 92
                text: "<u>A</u>dd..."
                enabled: !sheet.busy && !sheet.loadError
                onClicked: {
                    var n = 0;
                    while (sheet.tunnels.some(function (t) { return t.name === "wg" + n; }))
                        n++;
                    nameField.text = "wg" + n;
                    addDialog.importText = "";
                    addDialog.show();
                    nameField.forceActiveFocus();
                }
            }
            Button95 {
                width: 92
                text: "<u>I</u>mport..."
                enabled: !sheet.busy && !sheet.loadError
                onClicked: importDialog.openAt(Services.homePath, "")
            }
            Button95 {
                width: 92
                text: "<u>R</u>emove"
                enabled: !sheet.busy && !!sheet.tunnel
                onClicked: sheet.messageBox.show("WireGuard VPN",
                    "Remove the tunnel " + sheet.current + "?\n\nIt's disconnected, and its settings and private key are deleted. "
                    + "Its peers won't know this computer any more unless you set it up again with a new key.",
                    "warning", ["Yes", "No"], function (b) {
                        if (b !== 0)
                            return;
                        sheet.admin(["wg-delete", sheet.current], "", function (ok, output) {
                            if (!ok)
                                sheet.failed("The tunnel couldn't be removed.", output);
                            sheet.loadList("");
                        });
                    })
            }
        }
        CheckBox95 {
            id: bootBox
            y: tunnelList.y + tunnelList.height + 10
            text: "Connect when the computer <u>s</u>tarts"
            enabled: !sheet.busy && !!sheet.tunnel
            onToggled: (on) => sheet.unit(on ? "enable" : "disable")
        }
        Binding { target: bootBox; property: "checked"; value: sheet.tunnel ? sheet.tunnel.enabled : false }
        Label {
            y: bootBox.y + 28
            width: parent.width
            color: Theme.shadow
            text: sheet.busy ? "Working..." : "Changes here need an administrator's password."
        }
    }

    // ----------------------------------------------------------- Interface
    Item {
        enabled: !!sheet.tunnel && !sheet.busy
        Label {
            width: parent.width
            text: sheet.tunnel ? "This computer's end of <b>" + sheet.current + "</b>." : "Choose a tunnel on the Tunnels tab."
        }
        GroupBox95 {
            y: 24
            width: parent.width
            height: 96
            title: "Key"
            Label { text: "Public key (give this to the other end):" }
            TextField95 {
                y: 18
                width: parent.width
                text: sheet.iface.PublicKey || ""
                Component.onCompleted: input.readOnly = true
            }
            CheckBox95 {
                id: newKeyBox
                y: 46
                text: "Make a <u>n</u>ew private key when applied (peers need the new public key)"
                onToggled: (on) => { sheet.newKey = on; sheet.dirty = true; }
            }
        }
        GroupBox95 {
            y: 126
            width: parent.width
            height: 130
            title: "Addresses"
            Label { y: 3; text: "<u>A</u>ddress:" }
            TextField95 { id: addressField; x: 110; width: parent.width - 110; onTextChanged: if (!sheet.filling) sheet.dirty = true }
            Label { y: 29; text: "<u>D</u>NS servers:" }
            TextField95 { id: dnsField; x: 110; y: 26; width: parent.width - 110; onTextChanged: if (!sheet.filling) sheet.dirty = true }
            Label { y: 55; text: "Listen <u>p</u>ort:" }
            TextField95 { id: portField; x: 110; y: 52; width: 70; onTextChanged: if (!sheet.filling) sheet.dirty = true }
            Label { x: 190; y: 55; color: Theme.shadow; text: "(empty: any)" }
            Label { y: 81; text: "<u>M</u>TU:" }
            TextField95 { id: mtuField; x: 110; y: 78; width: 70; onTextChanged: if (!sheet.filling) sheet.dirty = true }
            Label { x: 190; y: 81; color: Theme.shadow; text: "(empty: automatic)" }
        }
        Label {
            y: 264
            width: parent.width
            color: Theme.shadow
            visible: !!sheet.iface.other
            text: "This tunnel's file has more settings, kept as they are:<br>" + (sheet.iface.other || "").replace(/</g, "&lt;").replace(/\n/g, "<br>")
        }
    }

    // --------------------------------------------------------------- Peers
    Item {
        enabled: !!sheet.tunnel && !sheet.busy
        Label {
            width: parent.width
            text: sheet.tunnel ? "The other ends of <b>" + sheet.current + "</b>: computers or servers it talks to."
                               : "Choose a tunnel on the Tunnels tab."
        }
        Rectangle {
            id: peerList
            y: 22
            width: parent.width - 100
            height: 66
            color: Theme.window
            clip: true
            Bevel { anchors.fill: parent; style: "sunken"; filled: false }
            Column {
                x: 3; y: 3
                width: parent.width - 6
                Repeater {
                    model: sheet.peers
                    Rectangle {
                        required property var modelData
                        required property int index
                        width: parent.width
                        height: 20
                        color: index === sheet.peerIndex ? Theme.selection : "transparent"
                        Text {
                            x: 4
                            width: parent.width - 8
                            anchors.verticalCenter: parent.verticalCenter
                            elide: Text.ElideRight
                            text: (modelData.Endpoint || "(no endpoint)") + "   " + (modelData.PublicKey ? modelData.PublicKey.substring(0, 12) + "..." : "(new)")
                            color: index === sheet.peerIndex ? Theme.selectionText : Theme.text
                            font.family: Theme.fontFamily
                            font.pixelSize: Theme.fontSize
                        }
                        MouseArea { anchors.fill: parent; onClicked: sheet.choosePeer(index) }
                    }
                }
            }
        }
        Column {
            x: peerList.width + 8
            y: peerList.y
            spacing: 6
            Button95 {
                width: 92
                text: "A<u>d</u>d"
                onClicked: {
                    sheet.storePeer();
                    var copy = sheet.peers.slice();
                    copy.push({ PublicKey: "", PresharedKey: "", newPreshared: "", AllowedIPs: "", Endpoint: "", PersistentKeepalive: "" });
                    sheet.peers = copy;
                    sheet.peerIndex = copy.length - 1;
                    sheet.showPeer();
                    sheet.dirty = true;
                    peerKeyField.forceActiveFocus();
                }
            }
            Button95 {
                width: 92
                text: "Re<u>m</u>ove"
                enabled: !!sheet.peer
                onClicked: {
                    var copy = sheet.peers.slice();
                    copy.splice(sheet.peerIndex, 1);
                    sheet.peers = copy;
                    sheet.peerIndex = copy.length ? Math.min(sheet.peerIndex, copy.length - 1) : -1;
                    sheet.showPeer();
                    sheet.dirty = true;
                }
            }
        }
        GroupBox95 {
            y: 94
            width: parent.width
            height: 178
            title: "Peer"
            enabled: !!sheet.peer
            Label { y: 3; text: "Public <u>k</u>ey:" }
            TextField95 { id: peerKeyField; x: 110; width: parent.width - 110; onTextChanged: if (!sheet.filling) sheet.dirty = true }
            Label { y: 29; text: "<u>E</u>ndpoint:" }
            TextField95 { id: endpointField; x: 110; y: 26; width: parent.width - 110; onTextChanged: if (!sheet.filling) sheet.dirty = true }
            Label { y: 55; text: "A<u>l</u>lowed IPs:" }
            TextField95 { id: allowedField; x: 110; y: 52; width: parent.width - 110; onTextChanged: if (!sheet.filling) sheet.dirty = true }
            Label { y: 81; text: "Keepali<u>v</u>e:" }
            TextField95 { id: keepaliveField; x: 110; y: 78; width: 50; onTextChanged: if (!sheet.filling) sheet.dirty = true }
            Label { x: 168; y: 81; color: Theme.shadow; text: "seconds (empty: off; 25 behind a router)" }
            Label { y: 107; text: "Preshared key:" }
            TextField95 { id: presharedField; x: 110; y: 104; width: parent.width - 110; onTextChanged: if (!sheet.filling) sheet.dirty = true }
            Label {
                x: 110; y: 128
                width: parent.width - 110
                color: Theme.shadow
                text: sheet.peer && sheet.peer.PresharedKey === "yes" ? "One is set. Empty keeps it; a new key replaces it; \"none\" removes it."
                                                                       : "None is set. Enter one if the other end uses one."
            }
        }
        Label {
            y: 278
            width: parent.width
            visible: !!sheet.peer && !!sheet.tunnel
            text: !sheet.peer ? "" : !sheet.tunnel || !sheet.tunnel.active ? "The tunnel isn't connected."
                : !(Number(sheet.peer.handshake) > 0) ? "Connected, but this peer hasn't answered yet."
                : "Last heard from " + sheet.ago(Number(sheet.peer.handshake)) + ". Received " + sheet.bytes(sheet.peer.rx)
                  + ", sent " + sheet.bytes(sheet.peer.tx) + "."
        }
    }

    extras: [
        // Add...: a new tunnel, with a key of its own
        Dialog95 {
            id: addDialog
            property string importText: "" // importing a file: its contents
            title: importText ? "Import Tunnel" : "Add Tunnel"
            dialogWidth: 300
            dialogHeight: 132
            Text {
                x: 12; y: 12
                width: 270
                wrapMode: Text.Wrap
                text: "The tunnel's name (its network interface: letters, digits, - and _, 15 at most):"
                color: Theme.text
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontSize
            }
            TextField95 {
                id: nameField
                x: 12; y: 46
                width: 150
                onAccepted: addOk.clicked()
            }
            Row {
                x: 300 - width - 20; y: 78
                spacing: 6
                Button95 {
                    id: addOk
                    text: "OK"
                    isDefault: true
                    onClicked: {
                        var name = nameField.text.trim();
                        if (!/^[A-Za-z0-9_=+.-]{1,15}$/.test(name)) {
                            sheet.messageBox.show("WireGuard VPN", "That isn't a usable name.", "warning", ["OK"]);
                            return;
                        }
                        addDialog.close();
                        var importing = addDialog.importText;
                        sheet.admin(importing ? ["wg-import", name] : ["wg-save", name],
                                    importing ? importing : "[interface]\nPrivateKey=generate", function (ok, output) {
                            if (!ok) {
                                sheet.failed("The tunnel couldn't be made.", output);
                                return;
                            }
                            if (importing && output)
                                sheet.messageBox.show("WireGuard VPN", output, "info", ["OK"]);
                            sheet.loadList(name);
                            if (!importing)
                                sheet.currentTab = 1;
                        });
                    }
                }
                Button95 { text: "Cancel"; onClicked: addDialog.close() }
            }
        },
        FileDialog95 {
            id: importDialog
            title: "Import Tunnel"
            dialogWidth: 390 // (inside the property sheet)
            messageBox: sheet.messageBox
            popupMenu: sheet.popupMenu
            defaultSuffix: ""
            filters: [
                { label: "WireGuard tunnels (*.conf)", patterns: ["*.conf"] },
                { label: "All Files (*.*)", patterns: ["*"] },
            ]
            onAccepted: (path) => {
                var text = Services.readFile(path);
                if (!text || text.indexOf("[Interface]") < 0 && text.toLowerCase().indexOf("[interface]") < 0) {
                    sheet.messageBox.show("WireGuard VPN", "That isn't a WireGuard tunnel file (it has no [Interface] section).",
                                          "error", ["OK"]);
                    return;
                }
                var base = path.substring(path.lastIndexOf("/") + 1).replace(/\.conf$/i, "").replace(/[^A-Za-z0-9_=+.-]/g, "").substring(0, 15);
                nameField.text = base || "wg0";
                addDialog.importText = text;
                addDialog.show();
                nameField.forceActiveFocus();
            }
        }
    ]
}
