import QtQuick
import Win95Ui
import Win95Control

// Services: servers (SSH, PostgreSQL, MySQL, WireGuard), programming
// languages (Python, Node.js) and tools (SQLite) turned on and off with a check box, like Windows Setup's list
// of components. Turning one on installs it first if it isn't there;
// Settings... opens its own property sheet.
PropertySheet {
    id: sheet

    title: "Services Properties"
    titles: ["Services"]
    canApply: false
    height: 472

    readonly property var items: Services.services.map(function (s) {
        return Object.assign({ key: s.id }, s);
    })
    readonly property var checkedMap: {
        var m = {};
        for (var i = 0; i < items.length; ++i)
            if (items[i].enabled)
                m[items[i].key] = true;
        return m;
    }
    readonly property var current: list.selected

    property var settingsWindows: ({})
    // the task running now was started here (not from a settings window)
    property bool startedHere: false
    function openSettings(item) {
        if (!item || !item.available)
            return;
        var existing = settingsWindows[item.id];
        if (existing) {
            existing.raise();
            existing.requestActivate();
            return;
        }
        var component = Qt.createComponent(item.settings);
        if (component.status !== Component.Ready) {
            console.warn("Services: cannot load", item.settings, component.errorString());
            return;
        }
        var w = component.createObject(null);
        settingsWindows[item.id] = w;
        w.closing.connect(function () {
            delete sheet.settingsWindows[item.id];
            Qt.callLater(w.destroy);
        });
        w.requestActivate();
    }

    function toggle(item, on) {
        if (Services.busy)
            return;
        list.selectedKey = item.key;
        if (on) {
            if (item.installed) {
                { startedHere = true; Services.turnOn(item.id); }
                return;
            }
            messageBox.show("Services",
                            item.name + " isn't installed on this computer yet.\n\nInstall it now? It's downloaded from "
                            + "your software sources (" + item.packages.join(", ") + ")"
                            + (item.kind === "server" ? ", then started." : "."),
                            "question", ["Yes", "No"], function (b) {
                if (b === 0)
                    { startedHere = true; Services.turnOn(item.id); }
            });
        } else if (item.kind === "server") {
            messageBox.show("Services",
                            "Turn off " + item.name + "?\n\nIt stops now and won't start with the computer. "
                            + "It stays installed, with its settings" + (item.id === "postgresql" || item.id === "mysql" ? " and databases" : "") + ".",
                            "question", ["Yes", "No"], function (b) {
                if (b === 0)
                    { startedHere = true; Services.turnOff(item.id); }
            });
        } else {
            messageBox.show("Services",
                            "Remove " + item.name + " from this computer?\n\n"
                            + (item.kind === "tool"
                               ? "Your database files are kept, and programs that use SQLite's library keep working; "
                                 + "the sqlite3 command (and DB Browser) go."
                               : "Programs you've written are kept, but anything that needs " + item.name
                                 + " stops working until you turn it on again."),
                            "warning", ["Yes", "No"], function (b) {
                if (b === 0)
                    { startedHere = true; Services.turnOff(item.id); }
            });
        }
    }

    Connections {
        target: Services
        function onTaskFinished(ok, message) {
            if (!sheet.startedHere)
                return;
            sheet.startedHere = false;
            if (!ok)
                sheet.messageBox.show("Services", message, "error", ["OK"]);
            else if (message)
                sheet.messageBox.show("Services", message, "info", ["OK"]);
        }
    }

    component Label: Text {
        wrapMode: Text.Wrap
        font.family: Theme.fontFamily
        font.pixelSize: Theme.fontSize
    }

    Item {
        AppletIcon { x: 4; y: 4; kind: "services" }
        Label {
            x: 46
            y: 2
            width: parent.width - x
            text: "To turn a service on, check its box; it's installed first if it isn't there. "
                  + "Servers start now and whenever the computer starts."
        }

        Label {
            y: 46
            text: "<u>S</u>ervices:"
            textFormat: Text.StyledText
        }
        ProgramList {
            id: list
            y: 62
            width: parent.width
            height: 148
            model: sheet.items
            checkable: true
            checked: sheet.checkedMap
            iconComponent: AppletIcon { size: parent.size; kind: parent.item.key }
            noteFor: function (item) { return item.status; }
            emptyText: Services.loading ? "Looking..." : ""
            onCheckToggled: (key, on) => {
                for (var i = 0; i < sheet.items.length; ++i)
                    if (sheet.items[i].key === key)
                        sheet.toggle(sheet.items[i], on);
            }
            onActivated: (item) => sheet.openSettings(item)
            Component.onCompleted: if (sheet.items.length) selectedKey = sheet.items[0].key
            onModelChanged: if (!selectedKey && sheet.items.length) selectedKey = sheet.items[0].key
        }

        GroupBox95 {
            id: descriptionBox
            y: list.y + list.height + 8
            width: parent.width
            height: 82
            title: "Description"
            Label {
                width: parent.width
                text: sheet.current ? sheet.current.description : ""
            }
        }

        Row {
            id: buttons
            y: descriptionBox.y + descriptionBox.height + 8
            anchors.right: parent.right
            spacing: 6
            Button95 {
                visible: !!sheet.current && sheet.current.kind === "server"
                text: sheet.current && sheet.current.active ? "S<u>t</u>op" : "St<u>a</u>rt"
                enabled: !Services.busy && !!sheet.current && sheet.current.installed
                onClicked: { sheet.startedHere = true; Services.control(sheet.current.id, sheet.current.active ? "stop" : "start"); }
            }
            Button95 {
                visible: !!sheet.current && sheet.current.kind === "server"
                text: "<u>R</u>estart"
                enabled: !Services.busy && !!sheet.current && sheet.current.active
                onClicked: { sheet.startedHere = true; Services.control(sheet.current.id, "restart"); }
            }
            Button95 {
                width: 90
                text: "S<u>e</u>ttings..."
                enabled: !!sheet.current && sheet.current.available
                onClicked: sheet.openSettings(sheet.current)
            }
        }

        // what's happening now
        Item {
            y: buttons.y + 34
            width: parent.width
            height: parent.height - y
            visible: Services.busy
            Label {
                id: taskLabel
                width: parent.width
                text: Services.task
                font.bold: true
            }
            Label {
                id: statusLabel
                y: taskLabel.height + 2
                width: parent.width
                elide: Text.ElideRight
                wrapMode: Text.NoWrap
                text: Services.status
            }
            ProgressBar95 {
                y: statusLabel.y + statusLabel.height + 4
                width: parent.width
                value: Services.percent
            }
        }
        Label {
            y: buttons.y + 34
            width: parent.width
            visible: !Services.busy
            color: Theme.shadow
            text: sheet.current && sheet.current.available
                  ? "Double-click a service, or click Settings, to change how it works."
                  : ""
        }
    }
}
