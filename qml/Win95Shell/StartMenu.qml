import QtQuick
import Win95Ui as Ui
import Win95Shell

// The Wayland 95 Start menu, laid out like Win95's: a dark sidebar with the
// product name running up it, large-icon entries with underlined access
// keys, cascading submenus with small icons, and Shut Down below a
// separator. Programs lists every installed program, in folders by
// category (ProgramMenu); a folder too long for the screen spreads over
// several columns, as in Win95.
//
// Keyboard: Up/Down move, Right/Enter open a submenu, Left/Esc back out,
// and an entry's underlined letter (first letter in submenus) picks it.
//
// Right-clicking a program (or the Menu key, Shift+F10) gives its menu:
// Open, Open as Admin (Root), Add Shortcut to Desktop, and Uninstall...
// (installed programs only: Control Panel > Add/Remove Programs removes it).
FocusScope {
    id: menu

    visible: false
    // Control Panel > Taskbar: Start menu size (small is Win95's own size)
    readonly property real zoom: [1, 1.25, 1.5][Ui.Wayland95Settings.startMenuSize] || 1
    function sz(n) { return Math.round(n * zoom); }
    readonly property int fontSize: sz(Theme.fontSize)

    width: sidebar.width + 6 + sz(168)
    height: mainHeight + 6

    signal runRequested()
    signal shutDownRequested()
    signal helpRequested()
    signal closed()
    // something in a program's menu went wrong: the taskbar says so
    signal problem(string title, string text)

    // main-menu entries; `key` is the index of the underlined letter
    readonly property var entries: [
        // like Windows 98's Windows Update, above the rest
        { label: "Wayland 95 Update", key: 11, icon: "update", program: "win95control",
          args: ["--applet", "programs", "--tab", "1", "--check"] },
        { separator: true },
        { label: "Programs",     key: 0, icon: "programs",  submenu: "programs" },
        { label: "Documents",    key: 0, icon: "documents", submenu: "documents" },
        { label: "Settings",     key: 0, icon: "settings",  submenu: "settings" },
        { label: "Find",         key: 0, icon: "find",      submenu: "find" },
        { label: "Help",         key: 0, icon: "help",      action: "help" },
        { label: "Run...",       key: 0, icon: "run",       action: "run" },
        { separator: true },
        { label: "Shut Down...", key: 2, icon: "shutdown",  action: "shutdown" },
    ]

    // Submenu entries: `program` with `args` runs a program (see Launcher),
    // `desktopId` an installed one (see ProgramMenu), and `submenu` (a list
    // of entries) opens another menu. Anything else is shown disabled.
    readonly property var submenus: ({
        programs: programsMenu,
        documents: [
            { label: "Home",         icon: "home",   program: "win95files", args: ["~"] },
            { label: "Documents",    icon: "folder", program: "win95files", args: ["~/Documents"] },
            { label: "Downloads",    icon: "folder", program: "win95files", args: ["~/Downloads"] },
            { label: "Recycle Bin",  icon: "folder", program: "win95files", args: ["trash:"] },
        ],
        settings: [
            { label: "Control Panel", icon: "control", program: "win95control" },
            { label: "Taskbar...",    icon: "taskbar", program: "win95control", args: ["--applet", "taskbar"] },
        ],
        find: [
            { label: "Files or Folders...", icon: "find", program: "win95files", args: ["--find"] },
        ],
    })

    // Wayland 95's own programs, and where they go
    readonly property var ownPrograms: ({
        "Accessories":  [{ label: "Archive Manager", icon: "archive", program: "win95archive" },
                         { label: "Athenaeum",      icon: "athenaeum", program: "win95athenaeum" },
                         { label: "Calculator",     icon: "calc",    program: "win95calc" },
                         { label: "Markdown Editor", icon: "markdown", program: "win95markdown" },
                         { label: "Notepad",        icon: "notepad", program: "win95notepad" },
                         { label: "Photo Gallery",  icon: "photos",  program: "win95photos" }],
        "Internet":     [{ label: "Email",          icon: "email",   program: "win95email" },
                         { label: "Web Browser",    icon: "web",     program: "win95web" }],
        "Programming":  [{ label: "Repositories",   icon: "repos",   program: "win95repos" },
                         { label: "Wayland Studio", icon: "studio",  program: "win95studio" }],
        "Games":        [{ label: "Mines",          icon: "mines",   program: "win95mines" },
                         { label: "Solitaire",      icon: "solitaire", program: "win95solitaire" }],
        "Multimedia":   [{ label: "Volume Control", icon: "volume",  program: "win95volume" }],
        "System Tools": [{ label: "Disk Manager",   icon: "disks",   program: "win95disks" },
                         { label: "Image Writer",   icon: "imagewriter", program: "win95imagewriter" },
                         { label: "System Monitor", icon: "sysmon",  program: "win95sysmon" }],
    })

    // Programs: the category folders, then (like Win95's MS-DOS Prompt and
    // Windows Explorer) the file manager and terminal
    readonly property var programsMenu: {
        var byName = {};
        var folders = ProgramMenu.folders;
        for (var i = 0; i < folders.length; ++i)
            byName[folders[i].label] = folders[i].items;
        for (var own in ownPrograms)
            byName[own] = ownPrograms[own].concat(byName[own] || []);
        var names = Object.keys(byName).sort(function (a, b) {
            // "Other" last, like a catch-all should be
            if (a === "Other" || b === "Other")
                return a === "Other" ? 1 : -1;
            return a.localeCompare(b);
        });
        var list = names.map(function (name) {
            var items = byName[name].slice().sort(function (a, b) { return a.label.localeCompare(b.label); });
            return { label: name, icon: "folder", submenu: items };
        });
        return list.concat([
            { label: "File Manager", icon: "files",    program: "win95files" },
            { label: "Terminal",     icon: "terminal", program: "win95terminal" },
        ]);
    }

    readonly property int rowHeight: sz(32)
    readonly property int separatorHeight: 8
    readonly property int subRowHeight: sz(22)

    // path[k]: the entry in level k whose submenu is open (level 0 is the
    // main menu), so path.length submenus are showing
    property var path: []
    // hots[k]: the highlighted entry in level k
    property var hots: [-1]
    // the level the keyboard is in
    property int depth: 0

    readonly property int hot: hots[0] === undefined ? -1 : hots[0]

    // submenu levels the menu can show (Programs > folder is two)
    readonly property int maxLevels: 4

    // Like Win95's MenuShowDelay: the pointer highlights an entry at once,
    // but its submenu opens (and the old one closes) only once the pointer
    // rests there, so sweeping down a menu doesn't open every folder on the
    // way, and a diagonal move into a submenu can cross other entries.
    readonly property int showDelay: 300
    property var pendingPath: null
    Timer {
        id: showTimer
        interval: menu.showDelay
        onTriggered: {
            if (menu.pendingPath)
                menu.setPath(menu.pendingPath);
            menu.pendingPath = null;
        }
    }

    // one list for every empty folder, so it isn't rebuilt each time
    readonly property var emptyList: [{ label: "(Empty)" }]

    function childrenOf(entry) {
        if (!entry || !entry.submenu)
            return [];
        var list = typeof entry.submenu === "string" ? submenus[entry.submenu] : entry.submenu;
        return list.length ? list : emptyList;
    }
    function levelEntries(level) {
        var list = entries;
        for (var k = 0; k < level; ++k)
            list = childrenOf(list[path[k]]);
        return list;
    }
    function enabledEntry(e) {
        return !!e && !e.separator && (!!e.program || !!e.desktopId || !!e.submenu || !!e.action);
    }

    FontMetrics {
        id: metrics
        font.family: Theme.fontFamily
        font.pixelSize: menu.fontSize
    }

    // the open submenus' entries and geometry, in this item's coordinates
    readonly property var levels: {
        var out = [];
        if (!visible || !Window.window)
            return out;
        var origin = menu.mapToItem(null, 0, 0);
        var screenW = Window.width, screenH = Window.height;
        for (var k = 1; k <= Math.min(path.length, maxLevels); ++k) {
            var list = levelEntries(k);
            var w = 0;
            for (var j = 0; j < list.length; ++j) {
                w = Math.max(w, metrics.advanceWidth(list[j].label || ""));
            }
            var colWidth = Math.max(sz(140), Math.ceil(sz(34) + w + 24));
            var fit = Math.max(1, Math.floor((screenH - 6) / subRowHeight));
            var rows = Math.min(list.length, fit);
            var cols = Math.ceil(list.length / rows);
            var width = cols * colWidth + 6, height = rows * subRowHeight + 6;

            var x, y;
            if (k === 1) {
                x = menu.width - 3;
                y = rowY(path[0]) - 3;
            } else {
                var prev = out[k - 2];
                var at = path[k - 1];
                x = prev.x + 3 + (Math.floor(at / prev.rows) + 1) * prev.colWidth;
                y = prev.y + (at % prev.rows) * subRowHeight;
                // no room on the right: open to the left instead
                if (origin.x + x + width > screenW)
                    x = prev.x + 3 + Math.floor(at / prev.rows) * prev.colWidth - width + 3;
            }
            x = Math.max(-origin.x, Math.min(x, screenW - origin.x - width));
            y = Math.max(-origin.y, Math.min(y, screenH - origin.y - height));
            out.push({ entries: list, x: x, y: y, width: width, height: height,
                       rows: rows, colWidth: colWidth, level: k });
        }
        return out;
    }

    readonly property real mainHeight: {
        var h = 0;
        for (var i = 0; i < entries.length; ++i)
            h += entries[i].separator ? separatorHeight : rowHeight;
        return h;
    }

    function rowY(i) {
        var y = 3;
        for (var k = 0; k < i; ++k)
            y += entries[k].separator ? separatorHeight : rowHeight;
        return y;
    }

    function open() {
        cancelPending();
        path = [];
        hots = [-1];
        depth = 0;
        visible = true;
        forceActiveFocus();
        Ui.Wayland95Settings.playSound("menuPopup");
    }

    function close() {
        if (!visible)
            return;
        itemMenu.close();
        visible = false;
        cancelPending();
        path = [];
        closed();
    }

    function underlined(label, key) {
        if (key === undefined || key < 0)
            return label;
        return label.substring(0, key) + "<u>" + label.charAt(key) + "</u>" + label.substring(key + 1);
    }

    function setHot(level, i) {
        var h = hots.slice(0, level + 1);
        while (h.length <= level)
            h.push(-1);
        h[level] = i;
        hots = h;
    }

    // the pointer is over entry i of `level`: highlight it, and after
    // showDelay show its submenu (closing deeper ones)
    function hover(level, i) {
        var e = levelEntries(level)[i];
        depth = level;
        // back in a submenu: its parents' open entries are the hot ones again
        var h = hots.slice(0, level + 1);
        for (var k = 0; k < level && k < path.length; ++k)
            h[k] = path[k];
        hots = h;
        setHot(level, i);
        var p = path.slice(0, level);
        if (e && e.submenu)
            p.push(i);
        if (samePath(p, path)) {
            cancelPending();
            return;
        }
        pendingPath = p;
        showTimer.restart();
    }

    function cancelPending() {
        showTimer.stop();
        pendingPath = null;
    }

    function samePath(a, b) {
        return a.length === b.length && a.every(function (v, k) { return v === b[k]; });
    }

    // only a real change: a new path rebuilds the submenus under the pointer
    function setPath(p) {
        if (!samePath(p, path))
            path = p;
    }

    function openSubmenu(level, i) {
        hover(level, i);
        cancelPending();
        if (!levelEntries(level)[i].submenu)
            return;
        setPath(path.slice(0, level).concat([i]));
        depth = level + 1;
        var list = levelEntries(level + 1);
        var first = 0;
        while (first < list.length && !enabledEntry(list[first]))
            ++first;
        setHot(level + 1, first < list.length ? first : 0);
    }

    function activate(level, i) {
        var e = levelEntries(level)[i];
        if (!enabledEntry(e))
            return;
        if (e.submenu) {
            openSubmenu(level, i);
            return;
        }
        close();
        if (e.desktopId)
            ProgramMenu.launch(e.desktopId);
        else if (e.program)
            Launcher.launch(e.program, e.args || []);
        else if (e.action === "run")
            runRequested();
        else if (e.action === "shutdown")
            shutDownRequested();
        else if (e.action === "help")
            helpRequested();
    }

    function back() {
        cancelPending();
        if (depth === 0) {
            close();
            return;
        }
        setPath(path.slice(0, depth - 1));
        hots = hots.slice(0, depth);
        depth = depth - 1;
    }

    function step(dir) {
        var list = levelEntries(depth);
        var n = list.length;
        var i = hots[depth] === undefined ? -1 : hots[depth];
        for (var k = 0; k < n; ++k) {
            i = ((i < 0 ? (dir > 0 ? -1 : 0) : i) + dir + n) % n;
            if (!list[i].separator)
                break;
        }
        setHot(depth, i);
        cancelPending();
        setPath(path.slice(0, depth)); // moving closes what the old entry opened
    }

    Keys.onPressed: (event) => {
        var list = levelEntries(menu.depth);
        var current = menu.hots[menu.depth] === undefined ? -1 : menu.hots[menu.depth];
        switch (event.key) {
        case Qt.Key_Escape:
        case Qt.Key_Left:
            back();
            break;
        case Qt.Key_Up: step(-1); break;
        case Qt.Key_Down: step(1); break;
        case Qt.Key_Right:
            if (current >= 0 && list[current] && list[current].submenu)
                openSubmenu(menu.depth, current);
            break;
        case Qt.Key_Return:
        case Qt.Key_Enter:
        case Qt.Key_Space:
            if (current >= 0)
                activate(menu.depth, current);
            break;
        case Qt.Key_Menu:
            if (current >= 0)
                showItemMenuAt(menu.depth, current);
            break;
        case Qt.Key_F10:
            if ((event.modifiers & Qt.ShiftModifier) && current >= 0)
                showItemMenuAt(menu.depth, current);
            break;
        default: {
            // access keys: the underlined letter, or the first letter in
            // submenus; several entries with that letter take turns
            var ch = event.text.toLowerCase();
            if (ch.length !== 1)
                return;
            var matches = [];
            for (var j = 0; j < list.length; ++j) {
                var e = list[j];
                if (!enabledEntry(e))
                    continue;
                var letter = menu.depth === 0 ? e.label.charAt(e.key) : e.label.charAt(0);
                if (letter.toLowerCase() === ch)
                    matches.push(j);
            }
            if (matches.length === 1) {
                activate(menu.depth, matches[0]);
            } else if (matches.length > 1) {
                var next = matches.find(function (m) { return m > current; });
                setHot(menu.depth, next === undefined ? matches[0] : next);
                cancelPending();
                setPath(path.slice(0, menu.depth));
            }
            break;
        }
        }
        event.accepted = true;
    }

    Bevel { anchors.fill: parent; raised: true; face: Theme.menu }

    // --- a program's menu (right-click) ------------------------------------
    // programs in the submenus have one: an installed one (desktopId) or one
    // of Wayland 95's own (program, args)
    function hasItemMenu(level, e) {
        return level > 0 && !!e && !e.separator && !e.submenu && (!!e.desktopId || !!e.program);
    }
    // the menu for entry i of `level`, at (x, y) in this item's coordinates
    function showItemMenu(level, i, x, y) {
        var e = levelEntries(level)[i];
        if (!hasItemMenu(level, e))
            return;
        cancelPending();
        depth = level;
        setHot(level, i);
        var p = menu.mapToItem(itemMenu, x, y);
        itemMenu.popup(p.x, p.y, [
            { text: "&Open", action: function () { menu.activate(level, i); } },
            { text: "Open as &Admin (Root)", action: function () { menu.openAsRoot(e); } },
            { separator: true },
            { text: "Add &Shortcut to Desktop", action: function () { menu.addShortcut(e); } },
            { separator: true },
            // (Wayland 95's own programs come with the desktop itself)
            { text: "&Uninstall...", enabled: !!e.desktopId, action: function () { menu.uninstall(e); } },
        ]);
    }
    // from the keyboard: under the entry, a little way in
    function showItemMenuAt(level, i) {
        var l = levels[level - 1];
        if (!l)
            return;
        showItemMenu(level, i, l.x + 3 + Math.floor(i / l.rows) * l.colWidth + sz(24),
                     l.y + 3 + (i % l.rows + 1) * subRowHeight);
    }
    function openAsRoot(e) {
        close();
        var ok = e.desktopId ? ProgramMenu.launchAsRoot(e.desktopId) : Launcher.launchAsRoot(e.program, e.args || []);
        if (!ok)
            problem("Open as Admin", "\"" + e.label + "\" can't be opened as the administrator. "
                    + "Opening programs as root needs pkexec (sudo apt install pkexec).");
    }
    function addShortcut(e) {
        close();
        var error = e.desktopId ? DesktopShortcuts.addProgram(ProgramMenu.pathOf(e.desktopId))
                                : DesktopShortcuts.addCommand(e.label, e.program, e.args || [], e.icon || "");
        if (error)
            problem("Add Shortcut to Desktop", error);
    }
    function uninstall(e) {
        close();
        // Add/Remove Programs finds the package and asks before removing it
        Launcher.launch("win95control", ["--applet", "programs", "--uninstall", e.desktopId]);
    }
    Ui.Menu95 {
        id: itemMenu
        // over the whole pop-up parent, so a click anywhere else closes it
        // (and only it: the Start menu stays open, as in Win95)
        parent: menu.parent
        z: 3000
        onClosed: if (menu.visible) menu.forceActiveFocus()
    }

    // --- sidebar ---------------------------------------------------------
    Rectangle {
        id: sidebar
        x: 3
        y: 3
        width: menu.sz(22)
        height: parent.height - 6
        color: "#808080"

        Item {
            // the product name, reading bottom-to-top
            width: brand.implicitWidth
            height: brand.implicitHeight
            transformOrigin: Item.TopLeft
            rotation: -90
            x: Math.round((sidebar.width - height) / 2)
            y: sidebar.height - 5

            Row {
                id: brand
                spacing: 3
                Text {
                    text: "Wayland"
                    color: "#c0c0c0"
                    font.family: "Liberation Sans"
                    font.pixelSize: menu.sz(18)
                    font.bold: true
                }
                Text {
                    text: "95"
                    color: "#ffffff"
                    font.family: "Liberation Sans"
                    font.pixelSize: menu.sz(18)
                }
            }
        }
    }

    // --- main entries ----------------------------------------------------
    Column {
        id: list
        x: sidebar.x + sidebar.width
        y: 3
        width: menu.width - x - 3

        Repeater {
            model: menu.entries
            delegate: Item {
                id: row
                required property var modelData
                required property int index
                readonly property bool highlighted: menu.hot === index && !modelData.separator

                width: list.width
                height: modelData.separator ? menu.separatorHeight : menu.rowHeight

                // etched separator: gray line over white
                Column {
                    visible: !!row.modelData.separator
                    x: 2
                    anchors.verticalCenter: parent.verticalCenter
                    Rectangle { width: row.width - 4; height: 1; color: Theme.shadow }
                    Rectangle { width: row.width - 4; height: 1; color: Theme.highlight }
                }

                Rectangle {
                    anchors.fill: parent
                    visible: row.highlighted
                    color: Theme.selectionBlue
                }

                MenuIcon {
                    visible: !row.modelData.separator
                    x: 4
                    anchors.verticalCenter: parent.verticalCenter
                    size: menu.sz(32)
                    kind: row.modelData.icon || ""
                }

                Text {
                    visible: !row.modelData.separator
                    x: menu.sz(32) + 12
                    anchors.verticalCenter: parent.verticalCenter
                    textFormat: Text.StyledText
                    text: row.modelData.separator ? "" : menu.underlined(row.modelData.label, row.modelData.key)
                    color: row.highlighted ? Theme.selectionText : Theme.menuText
                    font.family: Theme.fontFamily
                    font.pixelSize: menu.fontSize
                }

                Text {
                    visible: !!row.modelData.submenu
                    anchors.right: parent.right
                    anchors.rightMargin: 6
                    anchors.verticalCenter: parent.verticalCenter
                    text: "▶"
                    font.pixelSize: menu.sz(8)
                    color: row.highlighted ? Theme.selectionText : Theme.menuText
                }

                MouseArea {
                    anchors.fill: parent
                    enabled: !row.modelData.separator
                    hoverEnabled: true
                    onContainsMouseChanged: if (containsMouse) menu.hover(0, row.index)
                    onClicked: menu.activate(0, row.index)
                }
            }
        }
    }

    // --- cascading submenus ----------------------------------------------
    // One fixed slot per level, not a Repeater over `levels`: that array is
    // new whenever the path changes, and a Repeater given a new model
    // rebuilds every flyout, so the open ones flashed as their rows and icons
    // were made again. A slot rebuilds its rows only when its list changes.
    Repeater {
        model: menu.maxLevels
        delegate: Item {
            id: flyout
            required property int index
            readonly property int level: index + 1
            readonly property var modelData: menu.levels[index] || null
            // the entries shown, reassigned only when the list itself changes
            property var shown: null
            function sync() {
                var e = modelData ? modelData.entries : null;
                if (e !== shown)
                    shown = e;
            }
            onModelDataChanged: sync()
            Component.onCompleted: sync()
            readonly property int rows: modelData ? modelData.rows : 1
            readonly property int colWidth: modelData ? modelData.colWidth : 0

            visible: !!modelData
            x: modelData ? modelData.x : 0
            y: modelData ? modelData.y : 0
            width: modelData ? modelData.width : 0
            height: modelData ? modelData.height : 0

            Bevel { anchors.fill: parent; raised: true; face: Theme.menu }

            Repeater {
                model: flyout.shown
                delegate: Item {
                    id: subRow
                    required property var modelData
                    required property int index
                    readonly property bool enabledEntry: menu.enabledEntry(modelData)
                    readonly property bool highlighted: menu.hots[flyout.level] === index
                    readonly property bool hasIcon: !!modelData.iconSource

                    x: 3 + Math.floor(index / flyout.rows) * flyout.colWidth
                    y: 3 + (index % flyout.rows) * menu.subRowHeight
                    width: flyout.colWidth
                    height: menu.subRowHeight

                    Rectangle {
                        anchors.fill: parent
                        visible: subRow.highlighted
                        color: Theme.selectionBlue
                    }
                    Image {
                        id: appIcon
                        visible: subRow.hasIcon && status === Image.Ready
                        x: 4
                        anchors.verticalCenter: parent.verticalCenter
                        width: menu.sz(16)
                        height: menu.sz(16)
                        sourceSize: Qt.size(menu.sz(16), menu.sz(16))
                        source: subRow.modelData.iconSource || ""
                        asynchronous: true
                        fillMode: Image.PreserveAspectFit
                    }
                    MenuIcon {
                        // not while the app's icon loads, or it flashes in first
                        visible: (!subRow.hasIcon || appIcon.status === Image.Error)
                                 && subRow.modelData.label !== "(Empty)"
                        x: 4
                        anchors.verticalCenter: parent.verticalCenter
                        size: menu.sz(16)
                        kind: subRow.modelData.icon || (subRow.modelData.desktopId ? "app" : "")
                        opacity: subRow.enabledEntry ? 1 : 0.5
                    }
                    // disabled entries get the etched look
                    Text {
                        visible: !subRow.enabledEntry && !subRow.highlighted
                        x: subLabel.x + 1; y: subLabel.y + 1
                        text: subRow.modelData.label
                        color: Theme.highlight
                        font: subLabel.font
                    }
                    Text {
                        id: subLabel
                        x: menu.sz(16) + 12
                        width: parent.width - x - 18
                        elide: Text.ElideRight
                        anchors.verticalCenter: parent.verticalCenter
                        text: subRow.modelData.label
                        color: !subRow.enabledEntry ? Theme.shadow : (subRow.highlighted ? Theme.selectionText : Theme.menuText)
                        font.family: Theme.fontFamily
                        font.pixelSize: menu.fontSize
                    }
                    Text {
                        visible: !!subRow.modelData.submenu
                        anchors.right: parent.right
                        anchors.rightMargin: 6
                        anchors.verticalCenter: parent.verticalCenter
                        text: "▶"
                        font.pixelSize: menu.sz(8)
                        color: subRow.highlighted ? Theme.selectionText : Theme.menuText
                    }
                    MouseArea {
                        anchors.fill: parent
                        hoverEnabled: true
                        acceptedButtons: Qt.LeftButton | Qt.RightButton
                        onContainsMouseChanged: if (containsMouse) menu.hover(flyout.level, subRow.index)
                        onClicked: (mouse) => {
                            if (mouse.button === Qt.RightButton) {
                                var p = mapToItem(menu, mouse.x, mouse.y);
                                menu.showItemMenu(flyout.level, subRow.index, p.x, p.y);
                            } else {
                                menu.activate(flyout.level, subRow.index);
                            }
                        }
                    }
                }
            }
        }
    }
}
