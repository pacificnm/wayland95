import QtQuick

// A Win95 popup menu. Place one at the window's root (it fills its parent
// so it can catch clicks outside itself), then call popup(x, y, items).
//
// items: [{ text, shortcut?, enabled?, checked?, action? } | { separator: true }]
Item {
    id: menu

    anchors.fill: parent
    visible: false
    // above the dialogs beside it (Dialog95 is 1500, MessageBox95 2000):
    // a drop-down in a dialog opens over the dialog
    z: 2100

    property var items: []
    property int current: -1

    // emitted for a press outside the menu panel, in this item's coordinates,
    // so a menu bar can switch straight to another menu
    signal outsidePressed(real x, real y)
    signal closed()

    function popup(px, py, menuItems) {
        items = menuItems;
        current = -1;
        measure();
        // keep inside the window, flipping up/left if needed
        panel.x = Math.max(0, Math.min(px, menu.width - panel.width));
        panel.y = (py + panel.height > menu.height) ? Math.max(0, py - panel.height) : py;
        visible = true;
        forceActiveFocus();
        Wayland95Settings.playSound("menuPopup");
    }

    function close() {
        if (!visible)
            return;
        visible = false;
        closed();
    }

    function trigger(i) {
        var item = items[i];
        if (!item || item.separator || item.enabled === false)
            return;
        close();
        if (item.action)
            item.action();
    }

    function step(dir) {
        if (items.length === 0)
            return;
        // (nothing chosen yet: Down starts at the first, Up at the last)
        var i = current >= 0 ? current : dir > 0 ? -1 : items.length;
        for (var n = 0; n < items.length; ++n) {
            i = (i + dir + items.length) % items.length;
            if (!items[i].separator && items[i].enabled !== false) {
                current = i;
                return;
            }
        }
    }

    // "&Restore": R is the access key, shown underlined ("&&" is a plain &)
    function plain(t) { return String(t || "").replace(/&(.)/g, "$1"); }
    function label(t) {
        return String(t || "").replace(/&/g, "\u0001").replace(/\u0001\u0001/g, "&amp;")
                              .replace(/\u0001(.)/, "<u>$1</u>").replace(/\u0001/g, "");
    }
    function accessKey(t) {
        var m = String(t || "").replace(/&&/g, "").match(/&(.)/);
        return m ? m[1].toLowerCase() : "";
    }

    property real textColumn: 0
    property real shortcutColumn: 0
    property real rowsHeight: 0
    function measure() {
        var t = 0, s = 0, h = 0;
        for (var i = 0; i < items.length; ++i) {
            h += items[i].separator ? 8 : 18;
            if (items[i].separator)
                continue;
            metrics.text = plain(items[i].text);
            t = Math.max(t, metrics.advanceWidth);
            if (items[i].shortcut) {
                metrics.text = items[i].shortcut;
                s = Math.max(s, metrics.advanceWidth);
            }
        }
        textColumn = t;
        shortcutColumn = s;
        rowsHeight = h;
    }

    TextMetrics {
        id: metrics
        font.family: Theme.fontFamily
        font.pixelSize: Theme.fontSize
    }

    // click-away catcher
    MouseArea {
        anchors.fill: parent
        acceptedButtons: Qt.AllButtons
        onPressed: (mouse) => {
            menu.close();
            menu.outsidePressed(mouse.x, mouse.y);
        }
        onWheel: (wheel) => { wheel.accepted = true; }
    }

    // open, Enter, Escape and the arrows are the menu's, not a Shortcut's
    // (a form's default button takes Enter with one)
    Keys.onShortcutOverride: (event) => {
        event.accepted = menu.visible && [Qt.Key_Return, Qt.Key_Enter, Qt.Key_Escape, Qt.Key_Up, Qt.Key_Down,
                                          Qt.Key_Left, Qt.Key_Right].indexOf(event.key) >= 0;
    }
    Keys.onPressed: (event) => {
        // (closed, it can keep the keyboard: what's typed isn't for it)
        if (!menu.visible)
            return;
        switch (event.key) {
        case Qt.Key_Escape: menu.close(); break;
        case Qt.Key_Up: menu.step(-1); break;
        case Qt.Key_Down: menu.step(1); break;
        case Qt.Key_Return:
        case Qt.Key_Enter: menu.trigger(menu.current); break;
        default: {
            // an item's access key picks it
            var ch = event.text.toLowerCase();
            if (ch.length !== 1)
                return;
            for (var i = 0; i < menu.items.length; ++i)
                if (!menu.items[i].separator && menu.accessKey(menu.items[i].text) === ch) {
                    menu.trigger(i);
                    break;
                }
            break;
        }
        }
        event.accepted = true;
    }

    Item {
        id: panel
        width: 22 + menu.textColumn + (menu.shortcutColumn > 0 ? 24 + menu.shortcutColumn : 0) + 20
        height: menu.rowsHeight + 6

        Bevel { anchors.fill: parent; style: "raised"; face: Theme.menu }

        MouseArea { anchors.fill: parent } // swallow clicks on the frame

        Column {
            id: column
            x: 3
            y: 3
            width: panel.width - 6

            Repeater {
                model: menu.items
                delegate: Item {
                    id: row
                    required property var modelData
                    required property int index
                    readonly property bool isSeparator: !!modelData.separator
                    readonly property bool isEnabled: modelData.enabled !== false
                    readonly property bool hot: menu.current === index && isEnabled && !isSeparator

                    width: column.width
                    height: isSeparator ? 8 : 18

                    Bevel {
                        visible: row.isSeparator
                        anchors.verticalCenter: parent.verticalCenter
                        x: 1
                        width: parent.width - 2
                        height: 2
                        style: "thinSunken"
                        filled: false
                    }

                    Rectangle {
                        visible: row.hot
                        anchors.fill: parent
                        color: Theme.selection
                    }

                    Text {
                        visible: !row.isSeparator && !!row.modelData.checked
                        x: 6
                        anchors.verticalCenter: parent.verticalCenter
                        text: "✓"
                        color: row.hot ? Theme.selectionText : Theme.menuText
                        font.pixelSize: Theme.fontSize
                        font.bold: true
                    }

                    EtchedText {
                        visible: !row.isSeparator
                        x: 20
                        anchors.verticalCenter: parent.verticalCenter
                        text: menu.label(row.modelData.text)
                        enabled: row.isEnabled
                        color: row.hot ? Theme.selectionText : Theme.menuText
                    }
                    EtchedText {
                        visible: !row.isSeparator && !!row.modelData.shortcut
                        x: 20 + menu.textColumn + 24
                        anchors.verticalCenter: parent.verticalCenter
                        text: row.modelData.shortcut || ""
                        enabled: row.isEnabled
                        color: row.hot ? Theme.selectionText : Theme.menuText
                    }

                    MouseArea {
                        anchors.fill: parent
                        hoverEnabled: true
                        onContainsMouseChanged: if (containsMouse && !row.isSeparator) menu.current = row.index
                        onClicked: menu.trigger(row.index)
                    }
                }
            }
        }
    }
}
