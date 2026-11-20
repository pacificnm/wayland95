import QtQuick

// A Win95 popup menu. Place one at the window's root (it fills its parent
// so it can catch clicks outside itself), then call popup(x, y, items).
//
// items: [{ text, shortcut?, enabled?, checked?, action?, submenu? } | { separator: true }]
// submenu: items of the same kind, cascading to the right (hover, click or
// Right opens it; Left or Escape goes back)
Item {
    id: menu

    anchors.fill: parent
    visible: false
    // above the dialogs beside it (Dialog95 is 1500, MessageBox95 2000):
    // a drop-down in a dialog opens over the dialog
    z: 2100

    property var items: []
    property int current: -1
    // the open submenu: its items, which row of ours it hangs from, and
    // its own highlighted row
    property var subItems: []
    property int subFor: -1
    property int subCurrent: -1
    readonly property bool subOpen: subFor >= 0

    // emitted for a press outside the menu panel, in this item's coordinates,
    // so a menu bar can switch straight to another menu
    signal outsidePressed(real x, real y)
    signal closed()

    function popup(px, py, menuItems) {
        items = menuItems;
        current = -1;
        closeSub();
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
        subTimer.stop();
        closeSub();
        visible = false;
        closed();
    }

    function trigger(i) {
        var item = items[i];
        if (!item || item.separator || item.enabled === false)
            return;
        if (item.submenu) {
            openSub(i, true);
            return;
        }
        close();
        if (item.action)
            item.action();
    }
    function subTrigger(i) {
        var item = subItems[i];
        if (!item || item.separator || item.enabled === false)
            return;
        close();
        if (item.action)
            item.action();
    }

    // a row's top, in the panel
    function rowY(list, i) {
        var y = 3;
        for (var n = 0; n < i; ++n)
            y += list[n].separator ? 8 : 18;
        return y;
    }
    function openSub(i, selectFirst) {
        subTimer.stop();
        var item = items[i];
        if (!item || !item.submenu || item.enabled === false) {
            closeSub();
            return;
        }
        if (subFor !== i) {
            subItems = item.submenu;
            subFor = i;
            subMetrics = measureList(subItems);
            // to the right of the menu, or to its left when there's no room
            var right = panel.x + panel.width - 3;
            subPanel.x = right + subPanel.width <= menu.width ? right : Math.max(0, panel.x - subPanel.width + 3);
            var top = panel.y + rowY(items, i) - 3;
            subPanel.y = Math.max(0, Math.min(top, menu.height - subPanel.height));
            subCurrent = -1;
        }
        if (selectFirst && subCurrent < 0)
            subCurrent = nextIndex(subItems, -1, 1);
    }
    function closeSub() {
        subFor = -1;
        subItems = [];
        subCurrent = -1;
    }

    function nextIndex(list, from, dir) {
        if (list.length === 0)
            return -1;
        // (nothing chosen yet: Down starts at the first, Up at the last)
        var i = from >= 0 ? from : dir > 0 ? -1 : list.length;
        for (var n = 0; n < list.length; ++n) {
            i = (i + dir + list.length) % list.length;
            if (!list[i].separator && list[i].enabled !== false)
                return i;
        }
        return from;
    }
    function step(dir) {
        if (subOpen && subCurrent >= 0) {
            subCurrent = nextIndex(subItems, subCurrent, dir);
            return;
        }
        current = nextIndex(items, current, dir);
    }

    // hovering a row with a submenu opens it after a moment (as Win95's
    // did), hovering another closes it
    Timer {
        id: subTimer
        interval: 350
        property int target: -1
        onTriggered: {
            if (target >= 0 && menu.items[target] && menu.items[target].submenu)
                menu.openSub(target, false);
            else
                menu.closeSub();
        }
    }
    function hover(i) {
        current = i;
        if (subFor === i) {
            subTimer.stop();
            return;
        }
        subTimer.target = i;
        subTimer.restart();
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
    property bool hasSubmenus: false
    property var subMetrics: ({ text: 0, shortcut: 0, height: 0, sub: false })
    function measureList(list) {
        var t = 0, s = 0, h = 0, sub = false;
        for (var i = 0; i < list.length; ++i) {
            h += list[i].separator ? 8 : 18;
            if (list[i].separator)
                continue;
            metrics.text = plain(list[i].text);
            t = Math.max(t, metrics.advanceWidth);
            if (list[i].shortcut) {
                metrics.text = list[i].shortcut;
                s = Math.max(s, metrics.advanceWidth);
            }
            if (list[i].submenu)
                sub = true;
        }
        return { text: t, shortcut: s, height: h, sub: sub };
    }
    function measure() {
        var m = measureList(items);
        textColumn = m.text;
        shortcutColumn = m.shortcut;
        rowsHeight = m.height;
        hasSubmenus = m.sub;
    }
    function panelWidth(m) {
        return 22 + m.text + (m.shortcut > 0 ? 24 + m.shortcut : 0) + (m.sub ? 14 : 0) + 20;
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
        // in the submenu while one of its rows is chosen
        var inSub = menu.subOpen && menu.subCurrent >= 0;
        switch (event.key) {
        case Qt.Key_Escape:
        case Qt.Key_Left:
            if (menu.subOpen)
                menu.closeSub();
            else if (event.key === Qt.Key_Escape)
                menu.close();
            break;
        case Qt.Key_Right:
            if (!inSub && menu.current >= 0)
                menu.openSub(menu.current, true);
            break;
        case Qt.Key_Up: menu.step(-1); break;
        case Qt.Key_Down: menu.step(1); break;
        case Qt.Key_Return:
        case Qt.Key_Enter:
            if (inSub)
                menu.subTrigger(menu.subCurrent);
            else
                menu.trigger(menu.current);
            break;
        default: {
            // an item's access key picks it
            var ch = event.text.toLowerCase();
            if (ch.length !== 1)
                return;
            var list = inSub ? menu.subItems : menu.items;
            for (var i = 0; i < list.length; ++i)
                if (!list[i].separator && menu.accessKey(list[i].text) === ch) {
                    if (inSub) {
                        menu.subTrigger(i);
                    } else {
                        menu.current = i;
                        menu.trigger(i);
                    }
                    break;
                }
            break;
        }
        }
        event.accepted = true;
    }

    // one row of either panel (a Component, not an inline component type,
    // so it sees this file's ids)
    Component {
        id: menuRow
    Item {
        id: row
        required property var modelData
        required property int index
        readonly property bool inSub: parent === subColumn
        readonly property real textColumn: inSub ? menu.subMetrics.text : menu.textColumn
        width: parent ? parent.width : 0
        readonly property bool isSeparator: !!modelData.separator
        readonly property bool isEnabled: modelData.enabled !== false
        readonly property bool hot: (inSub ? menu.subCurrent === index
                                           : menu.current === index || (menu.subFor === index && !!modelData.submenu))
                                    && isEnabled && !isSeparator

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
            x: 20 + row.textColumn + 24
            anchors.verticalCenter: parent.verticalCenter
            text: row.modelData.shortcut || ""
            enabled: row.isEnabled
            color: row.hot ? Theme.selectionText : Theme.menuText
        }
        // a submenu's arrow: a small solid triangle pointing right
        Canvas {
            visible: !row.isSeparator && !!row.modelData.submenu
            anchors.right: parent.right
            anchors.rightMargin: 8
            anchors.verticalCenter: parent.verticalCenter
            width: 4
            height: 7
            readonly property color ink: !row.isEnabled ? Theme.shadow : row.hot ? Theme.selectionText : Theme.menuText
            onInkChanged: requestPaint()
            onPaint: {
                var ctx = getContext("2d");
                ctx.reset();
                ctx.fillStyle = ink;
                ctx.beginPath();
                ctx.moveTo(0, 0);
                ctx.lineTo(4, 3.5);
                ctx.lineTo(0, 7);
                ctx.closePath();
                ctx.fill();
            }
        }

        MouseArea {
            anchors.fill: parent
            hoverEnabled: true
            onContainsMouseChanged: {
                if (!containsMouse || row.isSeparator)
                    return;
                if (row.inSub) {
                    // (on the way here the pointer may have crossed other rows)
                    subTimer.stop();
                    menu.current = menu.subFor;
                    menu.subCurrent = row.index;
                } else
                    menu.hover(row.index);
            }
            onClicked: row.inSub ? menu.subTrigger(row.index) : menu.trigger(row.index)
        }
    }
    }

    Item {
        id: panel
        width: menu.panelWidth({ text: menu.textColumn, shortcut: menu.shortcutColumn, sub: menu.hasSubmenus })
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
                delegate: menuRow
            }
        }
    }

    Item {
        id: subPanel
        visible: menu.subOpen
        width: menu.panelWidth(menu.subMetrics)
        height: menu.subMetrics.height + 6

        Bevel { anchors.fill: parent; style: "raised"; face: Theme.menu }

        MouseArea { anchors.fill: parent } // swallow clicks on the frame

        Column {
            id: subColumn
            x: 3
            y: 3
            width: subPanel.width - 6

            Repeater {
                model: menu.subItems
                delegate: menuRow
            }
        }
    }
}
