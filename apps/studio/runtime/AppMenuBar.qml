import QtQuick
import Win95Ui

// Menu: the form's menu bar, across its top. `menus` is what the Studio's
// Menu Editor makes:
//
//   [{ text: "&File", name: "fileMenu", items: [
//        { text: "&Open...", name: "fileOpen", shortcut: "Ctrl+O", action: "openForm", form: "Open" },
//        { separator: true },
//        { text: "E&xit", name: "fileExit", action: "quit", enabled: true, checked: false } ] }]
//
// Choosing an item runs its Action (form.runAction), then itemClicked with
// `item` set to its name. "&" marks a menu's or an item's access key: Alt
// and the key opens a menu. Code can change an item:
//
//   menu1.setEnabled("fileSave", false)    menu1.setChecked("viewGrid", true)
//   menu1.setText("fileOpen", "&Open Recent")
AppElement {
    id: bar

    property var menus: []
    // the item chosen last (its name), for itemClicked
    property string item: ""
    signal itemClicked()

    function plain(t) { return String(t || "").replace(/&(.)/g, "$1"); }

    function choose(it) {
        if (!it || it.separator || it.enabled === false || !bar.enabled)
            return;
        bar.item = it.name || "";
        if (bar.form && it.action)
            bar.form.runAction(it.action, it.form);
        bar.itemClicked();
    }

    // an item by its name: changed in a copy, so what shows follows
    function change(name, how) {
        var copy = JSON.parse(JSON.stringify(menus)), found = false;
        copy.forEach(function (m) {
            if (m.name === name) {
                how(m);
                found = true;
            }
            (m.items || []).forEach(function (it) {
                if (it.name === name) {
                    how(it);
                    found = true;
                }
            });
        });
        if (found)
            menus = copy;
        return found;
    }
    function setEnabled(name, on) { return change(name, function (it) { it.enabled = !!on; }); }
    function setChecked(name, on) { return change(name, function (it) { it.checked = !!on; }); }
    function setText(name, text) { return change(name, function (it) { it.text = String(text); }); }
    function itemNamed(name) {
        for (var i = 0; i < menus.length; ++i) {
            if (menus[i].name === name)
                return menus[i];
            var items = menus[i].items || [];
            for (var j = 0; j < items.length; ++j) {
                if (items[j].name === name)
                    return items[j];
            }
        }
        return null;
    }

    // MenuBar95's menus: titles, and the items as Menu95 wants them
    readonly property var barMenus: menus.map(function (m) {
        return {
            title: bar.plain(m.text),
            items: function () {
                if (m.enabled === false)
                    return [{ text: "(not available)", enabled: false }];
                return (m.items || []).map(function (it) {
                    if (it.separator)
                        return { separator: true };
                    return { text: it.text || "", shortcut: it.shortcut || "", enabled: it.enabled !== false,
                             checked: !!it.checked, action: function () { bar.choose(it); } };
                });
            }
        };
    })

    Rectangle { anchors.fill: parent; color: bar.fillColor }

    MenuBar95 {
        id: menuBar
        anchors.fill: parent
        menus: bar.barMenus
        enabled: !bar.designMode && bar.enabled
        popupMenu: bar.form && !bar.designMode ? bar.form.popupMenu : null
    }

    // Alt and a menu's access key opens it; an item's shortcut chooses it
    Repeater {
        model: bar.designMode ? [] : bar.menus
        delegate: Item {
            id: title
            required property var modelData
            required property int index
            readonly property string key: {
                var m = String(modelData ? modelData.text || "" : "").replace(/&&/g, "").match(/&(.)/);
                return m ? m[1] : "";
            }
            Shortcut {
                sequence: title.key !== "" ? "Alt+" + title.key : ""
                enabled: title.key !== "" && bar.enabled && bar.visible
                onActivated: menuBar.open(title.index)
            }
        }
    }
    Repeater {
        model: {
            if (bar.designMode)
                return [];
            var out = [];
            bar.menus.forEach(function (m) {
                (m.items || []).forEach(function (it) {
                    if (it.shortcut && !it.separator)
                        out.push(it);
                });
            });
            return out;
        }
        delegate: Item {
            id: keyed
            required property var modelData
            readonly property string sequence: modelData ? modelData.shortcut || "" : ""
            Shortcut {
                sequence: keyed.sequence
                enabled: keyed.sequence !== "" && keyed.modelData.enabled !== false && bar.enabled && bar.visible
                onActivated: bar.choose(keyed.modelData)
            }
        }
    }
}
