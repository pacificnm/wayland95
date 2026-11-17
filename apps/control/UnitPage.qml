import QtQuick
import Win95Ui
import Win95Control

// One Services tab of Start Up: systemd's services, the computer's or (user)
// your own. A column list (Start with the computer, Name, Description,
// Status) with what to show and a Find box above, and Start / Stop /
// Restart / Properties below. The check box is "starts with the computer"
// (systemctl enable/disable); the computer's ask for a password (polkit).
Item {
    id: page

    property bool user: false
    property PropertySheet sheet

    readonly property var all: user ? Startup.userUnits : Startup.systemUnits
    readonly property bool loading: user ? Startup.loadingUser : Startup.loadingSystem
    readonly property string error: user ? Startup.userError : Startup.systemError
    property int show: 0 // all, starting with the computer, running, failed
    property string selectedKey: ""
    property string message: ""

    readonly property var shown: {
        var f = findField.text.trim().toLowerCase();
        return all.filter(function (u) {
            if (show === 1 && !u.enabled) return false;
            if (show === 2 && u.active !== "active") return false;
            if (show === 3 && u.active !== "failed") return false;
            return !f || u.name.toLowerCase().indexOf(f) >= 0 || (u.description || "").toLowerCase().indexOf(f) >= 0;
        });
    }
    readonly property var selected: {
        for (var i = 0; i < all.length; ++i)
            if (all[i].key === selectedKey)
                return all[i];
        return null;
    }

    function load() {
        if (all.length === 0 && !loading)
            Startup.refreshUnits(user);
    }

    function statusText(u) {
        if (!u) return "";
        if (u.active === "active") return u.sub === "running" ? "Running" : u.sub === "exited" ? "Done" : "Active";
        if (u.active === "failed") return "Failed";
        if (u.active === "activating") return "Starting...";
        if (u.active === "deactivating") return "Stopping...";
        return "Stopped";
    }
    function startText(u) {
        if (!u) return "";
        switch (u.file) {
        case "enabled": case "enabled-runtime": case "alias": return "It starts with the " + (user ? "session" : "computer") + ".";
        case "disabled": return "It doesn't start with the " + (user ? "session" : "computer") + ".";
        case "masked": case "masked-runtime": return "It's turned off for good: it never starts, even when asked.";
        case "static": return "It starts when something else needs it.";
        case "indirect": return "It starts when something else asks for it.";
        case "generated": case "transient": return "The system makes it as it starts up.";
        }
        return "";
    }

    function say(text) { message = text; }

    // stopping, disabling or masking something the desktop needs: ask first
    function risky(u, verb, then) {
        if (!u.important) {
            then();
            return;
        }
        sheet.messageBox.show("Start Up",
                              verb + " " + u.name + " could stop the desktop, the network, the login screen or the "
                              + "computer itself from working properly.\n\nDo it anyway?",
                              "warning", ["Yes", "No"], function (b) {
                                  if (b === 0)
                                      then();
                              });
    }

    function act(u, action, done) {
        if (!u)
            return;
        say((action === "start" ? "Starting " : action === "stop" ? "Stopping " : action === "restart" ? "Restarting "
             : "Changing ") + u.name + "...");
        Startup.unitAction(user, u.key, action, function (ok, msg) {
            if (!ok) {
                say("");
                sheet.messageBox.show("Start Up", "Couldn't change " + u.name + ".\n\n" + msg, "error", ["OK"]);
                return;
            }
            say(done);
        });
    }

    function start(u) { act(u, "start", u.name + " started."); }
    function stop(u) { risky(u, "Stopping", function () { act(u, "stop", u.name + " stopped."); }); }
    function restart(u) { risky(u, "Restarting", function () { act(u, "restart", u.name + " restarted."); }); }
    function setEnabled(u, on) {
        if (on)
            act(u, "enable", u.name + " will start with the " + (user ? "session" : "computer")
                + (u.active === "active" ? "." : ". Click Start to start it now too."));
        else
            risky(u, "Not starting", function () {
                act(u, "disable", u.name + " won't start with the " + (user ? "session" : "computer")
                    + (u.active === "active" ? " (it's still running now)." : "."));
            });
    }
    function mask(u, on) {
        if (on)
            risky(u, "Turning off", function () { act(u, "mask", u.name + " is turned off for good: it never starts."); });
        else
            act(u, "unmask", u.name + " can start again.");
    }

    function properties(u) {
        if (!u)
            return;
        statusWindow.unit = u;
        statusWindow.text = "Loading...";
        statusWindow.show();
        Startup.unitStatus(user, u.key, function (text) { statusWindow.text = text; });
    }

    function menuFor(u) {
        var masked = u.file === "masked" || u.file === "masked-runtime";
        return [
            { text: "&Start", enabled: u.active !== "active" && !masked, action: function () { page.start(u); } },
            { text: "S&top", enabled: u.active === "active" || u.active === "activating", action: function () { page.stop(u); } },
            { text: "&Restart", enabled: !masked, action: function () { page.restart(u); } },
            { separator: true },
            { text: "Start &with the " + (user ? "Session" : "Computer"), enabled: u.canEnable, checked: u.enabled,
              action: function () { page.setEnabled(u, !u.enabled); } },
            masked ? { text: "&Allow It to Start", action: function () { page.mask(u, false); } }
                   : { text: "&Never Start (Mask)", action: function () { page.mask(u, true); } },
            { separator: true },
            { text: "&Properties", action: function () { page.properties(u); } },
        ];
    }

    component Label: Text {
        font.family: Theme.fontFamily
        font.pixelSize: Theme.fontSize
        color: Theme.text
    }

    // --- what to show
    Label { x: 0; y: 4; text: "Show:" }
    ComboBox95 {
        id: showBox
        x: 36
        y: 0
        width: 170
        model: ["All services", "Starting with the " + (page.user ? "session" : "computer"), "Running", "Failed"]
        popupMenu: page.sheet ? page.sheet.popupMenu : null
        currentIndex: page.show
        onActivated: (i) => page.show = i
    }
    Label { x: 220; y: 4; text: "Find:" }
    TextField95 {
        id: findField
        objectName: page.user ? "findUser" : "findSystem"
        x: 250
        y: 0
        width: parent.width - x - 70
    }
    Button95 {
        anchors.right: parent.right
        y: -1
        width: 64
        text: "Re<u>f</u>resh"
        onClicked: Startup.refreshUnits(page.user)
    }

    // --- the list
    Bevel {
        id: box
        y: 27
        width: parent.width
        height: parent.height - y - 76
        style: "sunken"
        face: Theme.window

        readonly property real nameWidth: 150
        readonly property real statusWidth: 74

        Row {
            x: 2
            y: 2
            Repeater {
                model: [{ t: "", w: 22 }, { t: "Name", w: box.nameWidth }, { t: "Description", w: box.width - 4 - 22 - box.nameWidth - box.statusWidth - bar.width },
                        { t: "Status", w: box.statusWidth }]
                delegate: Item {
                    required property var modelData
                    width: modelData.w
                    height: 18
                    Bevel { anchors.fill: parent; style: "raised" }
                    Label {
                        x: 5
                        anchors.verticalCenter: parent.verticalCenter
                        width: parent.width - 8
                        elide: Text.ElideRight
                        text: modelData.t
                    }
                }
            }
        }

        ListView {
            id: view
            x: 2
            y: 20
            width: parent.width - 4 - bar.width
            height: parent.height - 22
            clip: true
            interactive: false
            model: page.shown
            activeFocusOnTab: true
            keyNavigationEnabled: false
            function selectAt(i) {
                if (page.shown.length === 0)
                    return;
                i = Math.max(0, Math.min(page.shown.length - 1, i));
                page.selectedKey = page.shown[i].key;
                positionViewAtIndex(i, ListView.Contain);
            }
            function selectedIndex() {
                for (var i = 0; i < page.shown.length; ++i)
                    if (page.shown[i].key === page.selectedKey)
                        return i;
                return -1;
            }
            Keys.onUpPressed: selectAt(selectedIndex() - 1)
            Keys.onDownPressed: selectAt(selectedIndex() + 1)
            Keys.onPressed: (event) => {
                if (event.key === Qt.Key_Space && page.selected && page.selected.canEnable)
                    page.setEnabled(page.selected, !page.selected.enabled);
                else if ((event.key === Qt.Key_Return || event.key === Qt.Key_Enter) && page.selected)
                    page.properties(page.selected);
                else if (event.key === Qt.Key_Home) selectAt(0);
                else if (event.key === Qt.Key_End) selectAt(page.shown.length - 1);
                else return;
                event.accepted = true;
            }

            delegate: Item {
                id: row
                required property var modelData
                required property int index
                readonly property bool isSelected: modelData.key === page.selectedKey
                readonly property color ink: isSelected ? Theme.selectionText : Theme.text
                width: view.width
                height: 18

                Rectangle {
                    x: 22
                    width: parent.width - x
                    height: parent.height
                    color: row.isSelected ? Theme.selection : "transparent"
                }
                CheckBox95 {
                    x: 4
                    anchors.verticalCenter: parent.verticalCenter
                    enabled: row.modelData.canEnable && !Startup.busy
                    opacity: row.modelData.canEnable ? 1 : 0.4
                    checked: row.modelData.enabled
                    onToggled: (on) => {
                        page.selectedKey = row.modelData.key;
                        page.setEnabled(row.modelData, on);
                        checked = Qt.binding(function () { return row.modelData.enabled; });
                    }
                }
                Label {
                    x: 26
                    width: box.nameWidth - 6
                    anchors.verticalCenter: parent.verticalCenter
                    elide: Text.ElideRight
                    text: row.modelData.name
                    color: row.ink
                }
                Label {
                    x: 22 + box.nameWidth + 4
                    width: view.width - x - box.statusWidth - 6
                    anchors.verticalCenter: parent.verticalCenter
                    elide: Text.ElideRight
                    text: row.modelData.description || ""
                    color: row.ink
                }
                Label {
                    x: view.width - box.statusWidth + 4
                    anchors.verticalCenter: parent.verticalCenter
                    text: page.statusText(row.modelData)
                    color: row.isSelected ? Theme.selectionText : row.modelData.active === "failed" ? "#c00000" : Theme.text
                    font.bold: row.modelData.active === "failed"
                }
                MouseArea {
                    x: 22
                    width: parent.width - x
                    height: parent.height
                    acceptedButtons: Qt.LeftButton | Qt.RightButton
                    onPressed: (mouse) => {
                        view.forceActiveFocus();
                        page.selectedKey = row.modelData.key;
                        if (mouse.button === Qt.RightButton && page.sheet) {
                            var p = mapToItem(page.sheet.popupMenu, mouse.x, mouse.y);
                            page.sheet.popupMenu.popup(p.x, p.y, page.menuFor(row.modelData));
                        }
                    }
                    onDoubleClicked: page.properties(row.modelData)
                }
            }
            WheelHandler { onWheel: (event) => bar.scrollBy(-event.angleDelta.y / 120 * 54) }
        }
        ScrollBar95 {
            id: bar
            anchors.right: parent.right
            anchors.rightMargin: 2
            y: 20
            height: parent.height - 22
            flickable: view
            stepSize: 18
        }
        Label {
            anchors.centerIn: parent
            width: parent.width - 40
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.Wrap
            visible: page.shown.length === 0
            color: Theme.shadow
            text: page.loading ? "Looking at the services..."
                  : page.error ? page.error
                  : page.all.length === 0 ? "No services." : "None to show."
        }
    }

    // --- the one chosen, and what to do with it
    Label {
        id: about
        y: box.y + box.height + 6
        width: parent.width
        height: 30
        wrapMode: Text.Wrap
        textFormat: Text.StyledText
        text: page.message ? page.message
              : page.selected ? "<b>" + page.selected.name + "</b>: " + page.statusText(page.selected).toLowerCase() + ". "
                                + page.startText(page.selected)
              : (page.all.length + " services, "
                 + page.all.filter(function (u) { return u.active === "active" && u.sub === "running"; }).length + " running."
                 + (page.user ? "" : " Changing them asks for an administrator's password."))
    }
    Row {
        anchors.bottom: parent.bottom
        spacing: 6
        Button95 {
            width: 64
            text: "<u>S</u>tart"
            enabled: !!page.selected && page.selected.active !== "active" && page.selected.file !== "masked" && !Startup.busy
            onClicked: page.start(page.selected)
        }
        Button95 {
            width: 64
            text: "S<u>t</u>op"
            enabled: !!page.selected && (page.selected.active === "active" || page.selected.active === "activating") && !Startup.busy
            onClicked: page.stop(page.selected)
        }
        Button95 {
            width: 64
            text: "<u>R</u>estart"
            enabled: !!page.selected && page.selected.file !== "masked" && !Startup.busy
            onClicked: page.restart(page.selected)
        }
        Button95 {
            width: 84
            text: "<u>P</u>roperties"
            enabled: !!page.selected
            onClicked: page.properties(page.selected)
        }
    }
    onSelectedKeyChanged: message = ""

    // systemctl status, in a window of its own
    DialogWindow95 {
        id: statusWindow
        property var unit: null
        property string text: ""
        title: (unit ? unit.name : "") + " Properties"
        width: 560
        height: 380
        onAccepted: close()
        Bevel {
            x: 8
            y: 8
            width: parent.width - 16
            height: parent.height - 52
            style: "sunken"
            face: Theme.window
            Flickable {
                id: statusFlick
                x: 3
                y: 3
                width: parent.width - 6 - statusBar.width
                height: parent.height - 6
                clip: true
                interactive: false
                contentWidth: width
                contentHeight: statusText.height
                TextEdit {
                    id: statusText
                    width: statusFlick.width
                    readOnly: true
                    selectByMouse: true
                    wrapMode: TextEdit.WrapAnywhere
                    text: statusWindow.text
                    font.family: Theme.monoFamily
                    font.pixelSize: Theme.fontSize
                    color: Theme.text
                }
                WheelHandler { onWheel: (event) => statusBar.scrollBy(-event.angleDelta.y / 120 * 48) }
            }
            ScrollBar95 {
                id: statusBar
                anchors.right: parent.right
                anchors.rightMargin: 2
                y: 2
                height: parent.height - 4
                flickable: statusFlick
                stepSize: 16
            }
        }
        Button95 {
            anchors.right: parent.right
            anchors.rightMargin: 8
            anchors.bottom: parent.bottom
            anchors.bottomMargin: 10
            text: "Close"
            isDefault: true
            onClicked: statusWindow.close()
        }
    }
}
