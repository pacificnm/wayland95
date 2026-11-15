import QtQuick
import QtQuick.Window
import Win95Ui
import Win95Control

// Fonts: a folder of the installed fonts, like Win95's. Large Icons, List
// or Details; Hide Variations shows one entry per family. Double-click a
// font to see it; File > Install New Font... adds fonts from a folder.
Window {
    id: win

    width: 560
    height: 400
    minimumWidth: 320
    minimumHeight: 220
    visible: true
    color: Theme.face
    title: "Fonts"

    property string view: "icons"          // "icons", "list", "details"
    property bool hideVariations: false
    property var selected: ({})             // key -> true
    property string current: ""             // the focused entry's key
    property var viewers: ({})              // file#style -> viewer window

    // entries: every face, or one per family (its regular face)
    readonly property var entries: {
        if (!hideVariations)
            return Fonts.fonts;
        var families = {}, order = [];
        var regular = /^(regular|book|normal|roman|medium)$/i;
        for (var i = 0; i < Fonts.fonts.length; ++i) {
            var f = Fonts.fonts[i];
            var have = families[f.family];
            if (!have) {
                order.push(f.family);
                families[f.family] = { font: f, count: 1 };
            } else {
                have.count++;
                if (regular.test(f.style) && !regular.test(have.font.style))
                    have.font = f;
            }
        }
        order.sort(function (a, b) { return a.localeCompare(b); });
        return order.map(function (name) {
            var e = Object.assign({}, families[name].font);
            e.key = "family:" + name;
            e.name = name;
            e.faces = families[name].count;
            return e;
        });
    }
    readonly property var selectedEntries: entries.filter(function (e) { return selected[e.key]; })

    // "Arial (TrueType)", like Win95; families say how many faces
    function label(e) {
        if (hideVariations)
            return e.name + (e.faces > 1 ? " (" + e.faces + " styles)" : "");
        return e.name + " (" + e.format + ")";
    }

    function entryIndex(key) {
        for (var i = 0; i < entries.length; ++i)
            if (entries[i].key === key)
                return i;
        return -1;
    }
    function select(i, extend) {
        if (i < 0 || i >= entries.length)
            return;
        var s = extend ? Object.assign({}, selected) : {};
        if (extend && s[entries[i].key])
            delete s[entries[i].key];
        else
            s[entries[i].key] = true;
        selected = s;
        current = entries[i].key;
        view.positionViewAtIndex(i, GridView.Contain);
    }

    function openFont(e) {
        if (!e)
            return;
        var id = e.file + "#" + e.style;
        if (viewers[id]) {
            viewers[id].raise();
            viewers[id].requestActivate();
            return;
        }
        var w = viewerComponent.createObject(null, { font: e });
        var v = Object.assign({}, viewers);
        v[id] = w;
        viewers = v;
        w.closing.connect(function () {
            var c = Object.assign({}, win.viewers);
            delete c[id];
            win.viewers = c;
            Qt.callLater(w.destroy);
        });
    }
    Component {
        id: viewerComponent
        FontViewer {}
    }

    function deleteSelected() {
        var list = selectedEntries;
        if (list.length === 0)
            return;
        var theirs = list.filter(function (e) { return !e.mine; });
        if (theirs.length > 0) {
            msgBox.show("Fonts", (theirs.length === 1 ? theirs[0].name + " comes" : "Some of these fonts come")
                        + " with the system, so it can't be deleted here. Only fonts you installed yourself can.",
                        "info", ["OK"]);
            return;
        }
        // with variations hidden, a family entry stands for all its faces
        var files = [];
        for (var i = 0; i < list.length; ++i) {
            var e = list[i];
            for (var j = 0; j < Fonts.fonts.length; ++j) {
                var f = Fonts.fonts[j];
                if ((hideVariations ? f.family === e.family : f.key === e.key) && f.mine && files.indexOf(f.file) < 0)
                    files.push(f.file);
            }
        }
        msgBox.show("Delete Fonts", "Are you sure you want to delete "
                    + (list.length === 1 ? list[0].name : "these " + list.length + " fonts")
                    + "? They'll go to the Recycle Bin.", "question", ["Yes", "No"], function (i) {
            if (i !== 0)
                return;
            var error = Fonts.remove(files);
            win.selected = {};
            if (error)
                msgBox.show("Delete Fonts", error, "error", ["OK"]);
        });
    }

    readonly property var fileMenu: [
        { text: "&Open", enabled: !!current && entryIndex(current) >= 0, action: function () { win.openFont(win.entries[win.entryIndex(win.current)]); } },
        { separator: true },
        { text: "&Install New Font...", action: function () { addFonts.open(); } },
        { separator: true },
        { text: "&Delete", shortcut: "Del", enabled: selectedEntries.length > 0, action: win.deleteSelected },
        { separator: true },
        { text: "&Close", action: function () { win.close(); } },
    ]
    readonly property var viewMenu: [
        { text: "Lar&ge Icons", checked: view === "icons", action: function () { win.view = "icons"; } },
        { text: "&List", checked: view === "list", action: function () { win.view = "list"; } },
        { text: "&Details", checked: view === "details", action: function () { win.view = "details"; } },
        { separator: true },
        { text: "&Hide Variations (Bold, Italic, etc.)", checked: hideVariations,
          action: function () { win.hideVariations = !win.hideVariations; win.selected = {}; } },
        { separator: true },
        { text: "&Refresh", shortcut: "F5", action: function () { Fonts.reload(); } },
    ]

    Shortcut { sequence: "F5"; onActivated: Fonts.reload() }
    Shortcut { sequence: "Del"; enabled: !msgBox.visible && !addFonts.visible; onActivated: win.deleteSelected() }
    Shortcut { sequence: "Esc"; enabled: !msgBox.visible && !popup.visible && !addFonts.visible; onActivated: win.close() }

    MenuBar95 {
        id: menuBar
        x: 2
        y: 2
        width: parent.width - 4
        height: 20
        popupMenu: popup
        menus: [
            { title: "File", items: win.fileMenu },
            { title: "View", items: win.viewMenu },
        ]
    }

    Bevel {
        id: well
        anchors.top: menuBar.bottom
        anchors.topMargin: 2
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: statusBar.top
        anchors.bottomMargin: 2
        style: "sunken"
        face: Theme.window

        // Details: column headings
        Row {
            id: header
            visible: win.view === "details"
            x: 2
            y: 2
            height: visible ? 18 : 0
            Repeater {
                model: [["Font Name", 230], ["Filename", 170], ["Size", 60], ["Modified", 120]]
                Item {
                    required property var modelData
                    width: modelData[1]
                    height: 18
                    Bevel { anchors.fill: parent; style: "raised" }
                    Text {
                        x: 5
                        anchors.verticalCenter: parent.verticalCenter
                        text: parent.modelData[0]
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.fontSize
                    }
                }
            }
        }

        GridView {
            id: view
            x: 2
            y: 2 + header.height
            width: parent.width - 4 - (vbar.visible ? vbar.width : 0)
            height: parent.height - 4 - header.height - (hbar.visible ? hbar.height : 0)
            clip: true
            interactive: false
            focus: true
            model: win.entries
            flow: win.view === "list" ? GridView.FlowTopToBottom : GridView.FlowLeftToRight
            cellWidth: win.view === "icons" ? 86 : win.view === "list" ? 220 : width
            cellHeight: win.view === "icons" ? 78 : 18
            keyNavigationEnabled: false
            Keys.onPressed: (event) => {
                var i = win.entryIndex(win.current);
                var perRow = win.view === "icons" ? Math.max(1, Math.floor(view.width / view.cellWidth)) : 1;
                var perCol = win.view === "list" ? Math.max(1, Math.floor(view.height / view.cellHeight)) : 1;
                var extend = event.modifiers & Qt.ShiftModifier;
                switch (event.key) {
                case Qt.Key_Down: win.select(i < 0 ? 0 : i + (win.view === "icons" ? perRow : 1), extend); break;
                case Qt.Key_Up: win.select(Math.max(0, i - (win.view === "icons" ? perRow : 1)), extend); break;
                case Qt.Key_Right: win.select(i + (win.view === "list" ? perCol : 1), extend); break;
                case Qt.Key_Left: win.select(Math.max(0, i - (win.view === "list" ? perCol : 1)), extend); break;
                case Qt.Key_Home: win.select(0, false); break;
                case Qt.Key_End: win.select(win.entries.length - 1, false); break;
                case Qt.Key_Return: case Qt.Key_Enter: win.openFont(win.entries[i]); break;
                default: return;
                }
                event.accepted = true;
            }

            delegate: Item {
                id: cell
                required property var modelData
                required property int index
                readonly property bool isSelected: !!win.selected[modelData.key]
                width: view.cellWidth
                height: view.cellHeight

                // Large Icons
                Item {
                    visible: win.view === "icons"
                    anchors.fill: parent
                    FontIcon {
                        id: bigIcon
                        anchors.horizontalCenter: parent.horizontalCenter
                        y: 4
                        kind: cell.modelData.kind
                        opacity: cell.isSelected ? 0.6 : 1
                    }
                    Rectangle {
                        anchors.horizontalCenter: parent.horizontalCenter
                        y: bigIcon.y + 36
                        width: Math.min(bigLabel.implicitWidth, cell.width - 6) + 4
                        height: bigLabel.height + 2
                        color: cell.isSelected ? Theme.selection : "transparent"
                        Text {
                            id: bigLabel
                            anchors.centerIn: parent
                            width: Math.min(implicitWidth, cell.width - 6)
                            horizontalAlignment: Text.AlignHCenter
                            wrapMode: Text.Wrap
                            maximumLineCount: 2
                            elide: Text.ElideRight
                            text: win.label(cell.modelData)
                            color: cell.isSelected ? Theme.selectionText : Theme.text
                            font.family: Theme.fontFamily
                            font.pixelSize: Theme.fontSize
                        }
                    }
                }

                // List and Details: small icon and name
                Row {
                    visible: win.view !== "icons"
                    x: 2
                    height: parent.height
                    spacing: 4
                    FontIcon {
                        anchors.verticalCenter: parent.verticalCenter
                        size: 16
                        kind: cell.modelData.kind
                    }
                    Rectangle {
                        anchors.verticalCenter: parent.verticalCenter
                        width: Math.min(smallLabel.implicitWidth, (win.view === "details" ? 230 : view.cellWidth) - 26) + 4
                        height: 15
                        color: cell.isSelected ? Theme.selection : "transparent"
                        Text {
                            id: smallLabel
                            x: 2
                            width: parent.width - 4
                            anchors.verticalCenter: parent.verticalCenter
                            elide: Text.ElideRight
                            text: win.label(cell.modelData)
                            color: cell.isSelected ? Theme.selectionText : Theme.text
                            font.family: Theme.fontFamily
                            font.pixelSize: Theme.fontSize
                        }
                    }
                }
                Row {
                    visible: win.view === "details"
                    x: 232
                    anchors.verticalCenter: parent.verticalCenter
                    Repeater {
                        model: [
                            [String(cell.modelData.file).split("/").pop(), 170],
                            [Math.max(1, Math.round(cell.modelData.size / 1024)) + "KB", 60],
                            [Qt.formatDateTime(cell.modelData.modified, "M/d/yy h:mm AP"), 120],
                        ]
                        Text {
                            required property var modelData
                            width: modelData[1]
                            leftPadding: 4
                            elide: Text.ElideRight
                            text: modelData[0]
                            color: Theme.text
                            font.family: Theme.fontFamily
                            font.pixelSize: Theme.fontSize
                        }
                    }
                }

                MouseArea {
                    anchors.fill: parent
                    onPressed: (mouse) => {
                        view.forceActiveFocus();
                        win.select(cell.index, mouse.modifiers & Qt.ControlModifier);
                    }
                    onDoubleClicked: win.openFont(cell.modelData)
                }
            }

            WheelHandler {
                onWheel: (event) => {
                    if (win.view === "list")
                        hbar.scrollBy(-event.angleDelta.y / 120 * 66);
                    else
                        vbar.scrollBy(-event.angleDelta.y / 120 * 54);
                }
            }
        }

        ScrollBar95 {
            id: vbar
            visible: needed && win.view !== "list"
            anchors.right: parent.right
            anchors.rightMargin: 2
            y: 2
            height: parent.height - 4
            flickable: view
            stepSize: view.cellHeight
        }
        ScrollBar95 {
            id: hbar
            visible: needed && win.view === "list"
            orientation: Qt.Horizontal
            anchors.bottom: parent.bottom
            anchors.bottomMargin: 2
            x: 2
            width: parent.width - 4
            flickable: view
            stepSize: view.cellWidth
        }
    }

    Bevel {
        id: statusBar
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        height: 20
        style: "thinSunken"
        Text {
            x: 4
            anchors.verticalCenter: parent.verticalCenter
            text: Fonts.busy ? "Updating the font list..."
                : win.selectedEntries.length > 1 ? win.selectedEntries.length + " font(s) selected"
                : win.entries.length + (win.hideVariations ? " font families" : " font(s)")
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontSize
        }
        SizeGrip95 {
            anchors.right: parent.right
            anchors.rightMargin: 1
            anchors.bottom: parent.bottom
            anchors.bottomMargin: 1
        }
    }

    // ------------------------------------------------------- Install New Font
    Dialog95 {
        id: addFonts
        title: "Add Fonts"
        dialogWidth: 440
        dialogHeight: 330

        property string folder: Win95Fs.homePath() + "/Downloads"
        property var found: []
        property var chosen: ({})

        function open() {
            if (!Win95Fs.isDir(folder))
                folder = Win95Fs.homePath();
            scan();
            show();
        }
        function scan() {
            found = Fonts.fontsInFolder(folder);
            var c = {};
            for (var i = 0; i < found.length; ++i)
                if (!found[i].installed)
                    c[found[i].file] = true;
            chosen = c;
        }
        function install() {
            var files = found.filter(function (f) { return addFonts.chosen[f.file]; }).map(function (f) { return f.file; });
            if (files.length === 0)
                return;
            var error = Fonts.install(files);
            if (error) {
                msgBox.show("Add Fonts", error, "error", ["OK"]);
                return;
            }
            addFonts.close();
        }

        Text {
            x: 12
            y: 12
            text: "List of fonts:"
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontSize
        }
        ProgramList {
            id: foundList
            x: 12
            y: 30
            width: parent.width - 24
            height: 170
            checkable: true
            model: addFonts.found.map(function (f) {
                return { key: f.file, name: f.name, kind: f.kind, installed: f.installed };
            })
            iconComponent: FontIcon { size: parent.size; kind: parent.item.kind }
            checked: addFonts.chosen
            noteFor: function (item) { return item.installed ? "Installed" : ""; }
            emptyText: "There are no fonts in this folder. Choose another one with Browse."
            onCheckToggled: (key, on) => {
                var c = Object.assign({}, addFonts.chosen);
                if (on) c[key] = true; else delete c[key];
                addFonts.chosen = c;
            }
        }
        Text {
            x: 12
            y: foundList.y + foundList.height + 10
            text: "Folder:"
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontSize
        }
        Text {
            x: 60
            y: foundList.y + foundList.height + 10
            width: parent.width - x - browse.width - 24
            elide: Text.ElideMiddle
            text: addFonts.folder
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontSize
        }
        Button95 {
            id: browse
            anchors.right: parent.right
            anchors.rightMargin: 12
            y: foundList.y + foundList.height + 6
            text: "<u>B</u>rowse..."
            onClicked: picker.openAt(addFonts.folder, "")
        }
        Text {
            x: 12
            y: browse.y + browse.height + 8
            width: parent.width - 24
            wrapMode: Text.Wrap
            color: Theme.shadow
            text: "Fonts are copied to " + Fonts.userFontDir + ", for your account."
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontSize
        }
        Row {
            anchors.right: parent.right
            anchors.rightMargin: 10
            anchors.bottom: parent.bottom
            anchors.bottomMargin: 10
            spacing: 6
            Button95 {
                text: "Select <u>A</u>ll"
                enabled: addFonts.found.length > 0
                onClicked: {
                    var c = {};
                    for (var i = 0; i < addFonts.found.length; ++i)
                        c[addFonts.found[i].file] = true;
                    addFonts.chosen = c;
                }
            }
            Button95 {
                text: "OK"
                isDefault: true
                enabled: Object.keys(addFonts.chosen).length > 0 && !Fonts.busy
                onClicked: addFonts.install()
            }
            Button95 {
                text: "Close"
                onClicked: addFonts.close()
            }
        }
    }

    // Browse...: pick any font file; its folder's fonts get listed
    FileDialog95 {
        id: picker
        mode: "open"
        title: "Choose a Font File"
        filters: [
            { label: "Font files (*.ttf; *.otf; *.ttc)", patterns: ["*.ttf", "*.otf", "*.ttc", "*.otc", "*.pfb", "*.woff", "*.woff2", "*.TTF", "*.OTF"] },
            { label: "All Files (*.*)", patterns: ["*"] },
        ]
        messageBox: msgBox
        popupMenu: popup
        onAccepted: (path) => {
            addFonts.folder = Win95Fs.isDir(path) ? path : Win95Fs.parentDir(path);
            addFonts.scan();
        }
    }

    Connections {
        target: Fonts
        function onInstallFinished(count) {
            if (count > 0)
                msgBox.show("Add Fonts", count === 1 ? "1 font was installed." : count + " fonts were installed.", "info", ["OK"]);
        }
    }

    Menu95 { id: popup }
    MessageBox95 { id: msgBox }
}
