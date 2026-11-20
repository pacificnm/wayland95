import QtQuick
import QtQuick.Window
import Win95Ui
import Win95Files

// A Win95-style folder window: menu bar, toolbar, address bar, a white
// well showing the folder as large icons or a details list, and a status
// bar. All file logic lives in DirectoryModel (C++).
Window {
    id: window

    width: 640
    height: 460
    minimumWidth: 320
    minimumHeight: 220
    visible: true
    color: Theme.face
    title: dir.title

    property string viewMode: initialViewMode // "icons" or "details"
    property int renameRow: -1
    property string typeAhead: ""

    // details-view column widths (resizable from the header)
    property real colName: 220
    property real colSize: 80
    property real colType: 140
    property real colModified: 140

    function currentView() { return viewMode === "icons" ? iconView : detailsView; }
    function focusView() { currentView().forceActiveFocus(); }

    DirectoryModel {
        id: dir
        autoRefresh: window.renameRow < 0
        Component.onCompleted: {
            path = initialPath;
            window.selectPending();
        }

        onError: (title, message) => msgBox.show(title, message, "error", ["OK"], window.focusView)
        onNotice: (title, message) => msgBox.show(title, message, "info", ["OK"], window.focusView)
        onPathChanged: {
            window.renameRow = -1;
            address.text = displayPath;
            iconView.currentIndex = -1;
            detailsView.currentIndex = -1;
            iconView.positionViewAtBeginning();
            detailsView.positionViewAtBeginning();
            window.focusView();
        }
        onRowFocusRequested: (row) => window.setCurrent(row)

        // keep the focused item across refreshes
        property string savedCurrent: ""
        onModelAboutToBeReset: savedCurrent = nameAt(window.currentView().currentIndex)
        onModelReset: {
            var row = savedCurrent.length ? rowOfName(savedCurrent) : -1;
            iconView.currentIndex = row;
            detailsView.currentIndex = row;
            window.selectPending();
        }
    }

    // --select NAME (Find's Open Containing Folder): that one, selected
    property string pendingSelect: initialSelect
    function selectPending() {
        if (!pendingSelect)
            return;
        var row = dir.rowOfName(pendingSelect);
        if (row < 0)
            return;
        pendingSelect = "";
        dir.select(row, 0);
        setCurrent(row);
    }

    function setCurrent(row) {
        iconView.currentIndex = row;
        detailsView.currentIndex = row;
        if (row >= 0)
            currentView().positionViewAtIndex(row, viewMode === "icons" ? GridView.Contain : ListView.Contain);
    }

    // ---------------------------------------------------------------- actions

    function confirmDelete(permanent) {
        var n = dir.selectionCount;
        if (n === 0 || dir.isComputer)
            return;
        var names = dir.selectedNames();
        var toTrash = !permanent && !dir.isTrash;
        var what = n === 1 ? "'" + names[0] + "'" : "these " + n + " items";
        var text = toTrash ? "Are you sure you want to send " + what + " to the Recycle Bin?"
                           : "Are you sure you want to permanently delete " + what + "?";
        msgBox.show(n === 1 ? "Confirm File Delete" : "Confirm Multiple File Delete", text,
                    toTrash ? "question" : "warning", ["Yes", "No"], function (i) {
                        if (i === 0) {
                            if (toTrash)
                                dir.trashSelected();
                            else
                                dir.deleteSelectedPermanently();
                        }
                        window.focusView();
                    });
    }

    function confirmEmptyTrash() {
        msgBox.show("Confirm Delete", "Are you sure you want to permanently delete everything in the Recycle Bin?",
                    "warning", ["Yes", "No"], function (i) {
                        if (i === 0) {
                            dir.emptyTrash();
                            Wayland95Settings.playSound("emptyTrash");
                        }
                        window.focusView();
                    });
    }

    function startRename() {
        if (!dir.writable || dir.selectionCount !== 1)
            return;
        var row = dir.isSelected(currentView().currentIndex) ? currentView().currentIndex : dir.firstSelectedRow();
        setCurrent(row);
        renameRow = row;
    }

    function commitRename(row, text) {
        if (renameRow !== row)
            return;
        renameRow = -1;
        focusView();
        dir.renameRow(row, text);
    }

    function cancelRename() {
        renameRow = -1;
        focusView();
    }

    function newFolder() {
        var row = dir.newFolder();
        if (row >= 0) {
            setCurrent(row);
            renameRow = row;
        }
    }

    function showProperties() {
        msgBox.show("Properties", dir.propertiesText(), "info", ["OK"], window.focusView);
    }

    function setView(mode) {
        var row = currentView().currentIndex;
        viewMode = mode;
        setCurrent(row);
        focusView();
    }

    function sortBy(column) {
        if (dir.sortColumn === column)
            dir.sortAscending = !dir.sortAscending;
        else {
            dir.sortColumn = column;
            dir.sortAscending = true;
        }
    }

    // ------------------------------------------------------------------ menus

    function fileMenu() {
        var sel = dir.selectionCount;
        var items = [
            { text: "Open", shortcut: "Enter", enabled: sel > 0 && !dir.isTrash, action: dir.openSelected },
            { text: "Open Terminal Here", action: dir.openTerminalHere },
            { separator: true },
            { text: "New Folder", shortcut: "Ctrl+Shift+N", enabled: dir.writable, action: window.newFolder },
            { separator: true },
        ];
        if (dir.isTrash) {
            items.push({ text: "Restore", enabled: sel > 0, action: dir.restoreSelected });
            items.push({ text: "Empty Recycle Bin", enabled: dir.count > 0, action: window.confirmEmptyTrash });
            items.push({ separator: true });
        }
        items.push({ text: "Delete", shortcut: "Del", enabled: sel > 0 && !dir.isComputer,
                     action: function () { window.confirmDelete(false); } });
        items.push({ text: "Rename", shortcut: "F2", enabled: sel === 1 && dir.writable, action: window.startRename });
        items.push({ text: "Properties", shortcut: "Alt+Enter", action: window.showProperties });
        items.push({ separator: true });
        items.push({ text: "Close", shortcut: "Ctrl+W", action: window.close });
        return items;
    }

    function editMenu() {
        var sel = dir.selectionCount;
        return [
            { text: "Cut", shortcut: "Ctrl+X", enabled: sel > 0 && dir.writable, action: dir.cutSelected },
            { text: "Copy", shortcut: "Ctrl+C", enabled: sel > 0 && !dir.isComputer, action: dir.copySelected },
            { text: "Paste", shortcut: "Ctrl+V", enabled: dir.canPaste && dir.writable, action: dir.paste },
            { separator: true },
            { text: "Select All", shortcut: "Ctrl+A", enabled: dir.count > 0, action: dir.selectAll },
        ];
    }

    function viewMenu() {
        return [
            { text: "Large Icons", checked: viewMode === "icons", action: function () { window.setView("icons"); } },
            { text: "Details", checked: viewMode === "details", action: function () { window.setView("details"); } },
            { separator: true },
            { text: "Arrange by Name", checked: dir.sortColumn === DirectoryModel.SortName,
              action: function () { dir.sortColumn = DirectoryModel.SortName; } },
            { text: "Arrange by Type", checked: dir.sortColumn === DirectoryModel.SortType,
              action: function () { dir.sortColumn = DirectoryModel.SortType; } },
            { text: "Arrange by Size", checked: dir.sortColumn === DirectoryModel.SortSize,
              action: function () { dir.sortColumn = DirectoryModel.SortSize; } },
            { text: "Arrange by Date", checked: dir.sortColumn === DirectoryModel.SortModified,
              action: function () { dir.sortColumn = DirectoryModel.SortModified; } },
            { separator: true },
            { text: "Show Hidden Files", shortcut: "Ctrl+H", checked: dir.showHidden,
              action: function () { dir.showHidden = !dir.showHidden; } },
            { text: "Refresh", shortcut: "F5", action: dir.refresh },
        ];
    }

    function goMenu() {
        return [
            { text: "Back", shortcut: "Alt+Left", enabled: dir.canGoBack, action: dir.goBack },
            { text: "Forward", shortcut: "Alt+Right", enabled: dir.canGoForward, action: dir.goForward },
            { text: "Up One Level", shortcut: "Backspace", enabled: dir.canGoUp, action: dir.goUp },
            { separator: true },
            { text: "Home", action: dir.goHome },
            { text: "My Computer", action: function () { dir.path = "computer:"; } },
            { text: "Recycle Bin", action: function () { dir.path = "trash:"; } },
        ].concat(Drives.drives.length === 0 ? [] : [{ separator: true }].concat(Drives.drives.map(function (d) {
            // the drives plugged in: opened (mounted) when chosen
            return { text: d.name + (d.sizeText ? " (" + d.sizeText + ")" : ""), action: function () { dir.openDrive(d.id); } };
        })));
    }

    function helpMenu() {
        return [
            { text: "About File Manager", action: function () {
                msgBox.show("About File Manager",
                            "File Manager\n\nA Win95-style file manager for the win95-wayland desktop.",
                            "info", ["OK"], window.focusView);
            } },
        ];
    }

    function itemMenu() {
        var sel = dir.selectionCount;
        if (dir.isTrash) {
            return [
                { text: "Restore", action: dir.restoreSelected },
                { separator: true },
                { text: "Delete", action: function () { window.confirmDelete(true); } },
                { separator: true },
                { text: "Properties", action: window.showProperties },
            ];
        }
        if (dir.isComputer && dir.selectedIsDrive()) {
            return [
                { text: "&Open", action: dir.openSelected },
                { separator: true },
                { text: "&Eject", action: dir.ejectSelected },
                { separator: true },
                { text: "P&roperties", action: window.showProperties },
            ];
        }
        return [
            { text: "Open", action: dir.openSelected },
        ].concat(window.archiveItems(), [
            { separator: true },
            { text: "Cut", enabled: dir.writable, action: dir.cutSelected },
            { text: "Copy", enabled: !dir.isComputer, action: dir.copySelected },
            { separator: true },
            { text: "Delete", enabled: !dir.isComputer, action: function () { window.confirmDelete(false); } },
            { text: "Rename", enabled: sel === 1 && dir.writable, action: window.startRename },
            { separator: true },
            { text: "Properties", action: window.showProperties },
        ]);
    }

    // Archive Manager's: Extract Here / Extract to name\ / Open with on
    // archives, Add to on the rest (none if it isn't installed)
    function archiveItems() {
        if (dir.isComputer || dir.isTrash || dir.selectionCount === 0 || !dir.hasArchiver())
            return [];
        if (dir.selectionIsArchives()) {
            var name = dir.archiveFolderName().replace(/&/g, "&&");
            return [
                { separator: true },
                { text: "E&xtract Here", enabled: dir.writable, action: function () { dir.extractSelected(false); } },
                { text: name ? "Extract &to " + name + "\\" : "Extract &to Separate Folders", enabled: dir.writable,
                  action: function () { dir.extractSelected(true); } },
                { text: "Open with &Archive Manager", action: dir.openSelectedInArchiver },
            ];
        }
        return [
            { separator: true },
            { text: "&Add to Archive...", action: function () { dir.addSelectedToArchive(true); } },
            { text: "Add &to " + dir.newArchiveName().replace(/&/g, "&&"), enabled: dir.writable, action: function () { dir.addSelectedToArchive(false); } },
        ];
    }

    function backgroundMenu() {
        if (dir.isTrash) {
            return [
                { text: "Empty Recycle Bin", enabled: dir.count > 0, action: window.confirmEmptyTrash },
                { text: "Refresh", action: dir.refresh },
            ];
        }
        return [
            { text: "Large Icons", checked: viewMode === "icons", action: function () { window.setView("icons"); } },
            { text: "Details", checked: viewMode === "details", action: function () { window.setView("details"); } },
            { separator: true },
            { text: "Refresh", action: dir.refresh },
            { separator: true },
            { text: "Paste", enabled: dir.canPaste && dir.writable, action: dir.paste },
            { separator: true },
            { text: "New Folder", enabled: dir.writable, action: window.newFolder },
            { text: "Open Terminal Here", action: dir.openTerminalHere },
            { separator: true },
            { text: "Properties", action: window.showProperties },
        ];
    }

    function popupAt(item, x, y, items) {
        var p = item.mapToItem(popup, x, y);
        popup.popup(p.x, p.y, items);
    }

    // ------------------------------------------------------- mouse & keyboard

    function pressRow(view, row, mouse, item) {
        view.forceActiveFocus();
        var ctrl = mouse.modifiers & Qt.ControlModifier;
        var shift = mouse.modifiers & Qt.ShiftModifier;
        if (mouse.button === Qt.RightButton) {
            if (!dir.isSelected(row))
                dir.select(row, DirectoryModel.Replace);
            view.currentIndex = row;
            popupAt(item, mouse.x, mouse.y, itemMenu());
            return;
        }
        if (shift)
            dir.select(row, DirectoryModel.Extend);
        else if (ctrl)
            dir.select(row, DirectoryModel.Toggle);
        else if (!dir.isSelected(row))
            dir.select(row, DirectoryModel.Replace);
        view.currentIndex = row;
    }

    function clickRow(row, mouse) {
        // a plain click on an item that was part of a bigger selection
        // narrows the selection to it (deferred so multi-item drags could work)
        if (mouse.button === Qt.LeftButton && !(mouse.modifiers & (Qt.ControlModifier | Qt.ShiftModifier))
                && dir.selectionCount > 1)
            dir.select(row, DirectoryModel.Replace);
    }

    function moveCurrent(view, key, shift) {
        var n = dir.count;
        if (n === 0)
            return;
        var icons = view === iconView;
        var cols = icons ? Math.max(1, Math.floor(iconView.width / iconView.cellWidth)) : 1;
        var rowH = icons ? iconView.cellHeight : 18;
        var pageItems = cols * Math.max(1, Math.floor(view.height / rowH) - 1);
        var i = view.currentIndex;
        if (i < 0) {
            i = 0;
        } else {
            switch (key) {
            case Qt.Key_Left: if (icons) i -= 1; break;
            case Qt.Key_Right: if (icons) i += 1; break;
            case Qt.Key_Up: i -= cols; break;
            case Qt.Key_Down: i += cols; break;
            case Qt.Key_Home: i = 0; break;
            case Qt.Key_End: i = n - 1; break;
            case Qt.Key_PageUp: i -= pageItems; break;
            case Qt.Key_PageDown: i += pageItems; break;
            }
        }
        i = Math.max(0, Math.min(n - 1, i));
        setCurrent(i);
        dir.select(i, shift ? DirectoryModel.Extend : DirectoryModel.Replace);
    }

    function handleKey(view, event) {
        var ctrl = event.modifiers & Qt.ControlModifier;
        var shift = event.modifiers & Qt.ShiftModifier;
        var alt = event.modifiers & Qt.AltModifier;
        var k = event.key;

        if (alt && k === Qt.Key_Left) dir.goBack();
        else if (alt && k === Qt.Key_Right) dir.goForward();
        else if (alt && k === Qt.Key_Up) dir.goUp();
        else if (alt && (k === Qt.Key_Return || k === Qt.Key_Enter)) showProperties();
        else if (k === Qt.Key_Backspace) dir.goUp();
        else if (k === Qt.Key_Return || k === Qt.Key_Enter) dir.openSelected();
        else if (k === Qt.Key_Delete) confirmDelete(shift);
        else if (k === Qt.Key_F2) startRename();
        else if (k === Qt.Key_Menu || (shift && k === Qt.Key_F10)) {
            // the right-click menu from the keyboard, at the current item
            var it = view.currentItem;
            if (dir.selectionCount > 0 && it)
                popupAt(it, it.width / 2, it.height / 2, itemMenu());
            else
                popupAt(view, 20, 20, backgroundMenu());
        }
        else if (k === Qt.Key_F5) dir.refresh();
        else if (ctrl && shift && k === Qt.Key_N) newFolder();
        else if (ctrl && k === Qt.Key_A) dir.selectAll();
        else if (ctrl && k === Qt.Key_C) dir.copySelected();
        else if (ctrl && k === Qt.Key_X) { if (dir.writable) dir.cutSelected(); }
        else if (ctrl && k === Qt.Key_V) { if (dir.writable) dir.paste(); }
        else if (ctrl && k === Qt.Key_H) dir.showHidden = !dir.showHidden;
        else if (ctrl && k === Qt.Key_W) window.close();
        else if (ctrl && k === Qt.Key_L) { address.input.selectAll(); address.forceActiveFocus(); }
        else if (ctrl && k === Qt.Key_Space) { if (view.currentIndex >= 0) dir.select(view.currentIndex, DirectoryModel.Toggle); }
        else if ([Qt.Key_Left, Qt.Key_Right, Qt.Key_Up, Qt.Key_Down, Qt.Key_Home, Qt.Key_End,
                  Qt.Key_PageUp, Qt.Key_PageDown].indexOf(k) >= 0) moveCurrent(view, k, shift);
        else if (!ctrl && !alt && event.text.length === 1 && event.text > " ") {
            // type-ahead: jump to the next item starting with what was typed
            typeAhead += event.text;
            typeAheadReset.restart();
            var from = typeAhead.length > 1 ? view.currentIndex - 1 : view.currentIndex;
            var row = dir.findNextByPrefix(typeAhead, Math.max(-1, from));
            if (row >= 0) {
                setCurrent(row);
                dir.select(row, DirectoryModel.Replace);
            }
        } else {
            return;
        }
        event.accepted = true;
    }

    Timer {
        id: typeAheadReset
        interval: 1000
        onTriggered: window.typeAhead = ""
    }

    // ------------------------------------------------------------------ layout

    MenuBar95 {
        id: menuBar
        x: 2
        y: 2
        width: parent.width - 4
        height: 20
        popupMenu: popup
        menus: [
            { title: "File", items: window.fileMenu },
            { title: "Edit", items: window.editMenu },
            { title: "View", items: window.viewMenu },
            { title: "Go", items: window.goMenu },
            { title: "Help", items: window.helpMenu },
        ]
    }

    Bevel {
        // etched line between the menu bar and the toolbar
        anchors.top: menuBar.bottom
        anchors.topMargin: 1
        x: 0
        width: parent.width
        height: 2
        style: "thinSunken"
        filled: false
    }

    Row {
        id: toolbar
        anchors.top: menuBar.bottom
        anchors.topMargin: 5
        x: 4
        height: 24
        spacing: 2

        ToolButton95 { glyph: "◀"; text: "Back"; enabled: dir.canGoBack; onClicked: dir.goBack() }
        ToolButton95 { glyph: "▶"; enabled: dir.canGoForward; onClicked: dir.goForward() }
        ToolButton95 { glyph: "↑"; text: "Up"; enabled: dir.canGoUp; onClicked: dir.goUp() }

        Item { width: 6; height: 1 }

        ToolButton95 { text: "Cut"; enabled: dir.selectionCount > 0 && dir.writable; onClicked: dir.cutSelected() }
        ToolButton95 { text: "Copy"; enabled: dir.selectionCount > 0 && !dir.isComputer; onClicked: dir.copySelected() }
        ToolButton95 { text: "Paste"; enabled: dir.canPaste && dir.writable; onClicked: dir.paste() }
        ToolButton95 {
            glyph: "✕"
            enabled: dir.selectionCount > 0 && !dir.isComputer
            onClicked: window.confirmDelete(false)
        }

        Item { width: 6; height: 1 }

        ToolButton95 { text: "Icons"; checked: window.viewMode === "icons"; onClicked: window.setView("icons") }
        ToolButton95 { text: "Details"; checked: window.viewMode === "details"; onClicked: window.setView("details") }
    }

    Item {
        id: addressBar
        anchors.top: toolbar.bottom
        anchors.topMargin: 4
        x: 4
        width: parent.width - 8
        height: 21

        Text {
            id: addressLabel
            anchors.verticalCenter: parent.verticalCenter
            text: "Address"
            color: Theme.text
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontSize
        }
        TextField95 {
            id: address
            anchors.left: addressLabel.right
            anchors.leftMargin: 6
            anchors.right: parent.right
            height: parent.height
            onAccepted: dir.path = text
            onEscaped: { text = dir.displayPath; window.focusView(); }
            onActiveFocusChanged: if (activeFocus) input.selectAll()
        }
    }

    // the white well holding the active view
    Bevel {
        id: well
        anchors.top: addressBar.bottom
        anchors.topMargin: 4
        anchors.bottom: statusBar.top
        anchors.bottomMargin: 3
        x: 2
        width: parent.width - 4
        style: "sunken"
        face: Theme.window

        Item {
            id: viewport
            anchors.fill: parent
            anchors.margins: 2
            clip: true

            // empty-space clicks and the wheel land here
            MouseArea {
                id: backgroundMouse
                anchors.fill: parent
                acceptedButtons: Qt.LeftButton | Qt.RightButton
                onPressed: (mouse) => {
                    window.focusView();
                    if (!(mouse.modifiers & (Qt.ControlModifier | Qt.ShiftModifier)))
                        dir.clearSelection();
                    if (mouse.button === Qt.RightButton)
                        window.popupAt(backgroundMouse, mouse.x, mouse.y, window.backgroundMenu());
                }
                onWheel: (wheel) => {
                    var bar = window.viewMode === "icons" ? iconBar : detailsBar;
                    bar.scrollBy(-wheel.angleDelta.y / 120 * 54);
                }
            }

            // ---------------------------------------------------- icon view
            GridView {
                id: iconView
                visible: window.viewMode === "icons"
                anchors.left: parent.left
                anchors.top: parent.top
                anchors.bottom: parent.bottom
                anchors.right: iconBar.visible ? iconBar.left : parent.right
                // Keeps icons alive off screen. Without this, resizing the window
                // destroys the ones that leave the view and makes them again on the
                // way back, and a new Canvas is blank until its first queued paint:
                // the icons flicker.
                cacheBuffer: 2000
                cellWidth: 78
                cellHeight: 74
                interactive: false
                currentIndex: -1
                model: dir
                focus: window.viewMode === "icons"
                topMargin: 4
                leftMargin: 2
                Keys.onPressed: (event) => window.handleKey(iconView, event)

                delegate: Item {
                    id: cell
                    required property int index
                    required property string name
                    required property string iconKind
                    required property bool selected
                    required property bool symlink
                    required property bool hidden

                    // (only the view that's showing: the hidden one's rename box
                    // took the keyboard, and the typing went to the folder)
                    readonly property bool editing: window.renameRow === index && window.viewMode === "icons"
                    readonly property bool isCurrent: iconView.currentIndex === index

                    width: iconView.cellWidth
                    height: iconView.cellHeight

                    FileIcon {
                        id: cellIcon
                        anchors.horizontalCenter: parent.horizontalCenter
                        y: 2
                        kind: cell.iconKind
                        selected: cell.selected
                        symlink: cell.symlink
                        opacity: cell.hidden ? 0.5 : 1
                    }

                    Rectangle {
                        id: labelBox
                        visible: !cell.editing
                        anchors.horizontalCenter: parent.horizontalCenter
                        y: cellIcon.y + cellIcon.height + 3
                        width: Math.min(cell.width - 2, label.paintedWidth + 4)
                        height: label.paintedHeight + 2
                        color: cell.selected ? Theme.selection : "transparent"

                        Text {
                            id: label
                            anchors.centerIn: parent
                            width: cell.width - 6
                            text: cell.name
                            horizontalAlignment: Text.AlignHCenter
                            wrapMode: Text.WrapAtWordBoundaryOrAnywhere
                            maximumLineCount: cell.selected ? 4 : 2
                            elide: Text.ElideRight
                            color: cell.selected ? Theme.selectionText : Theme.text
                            font.family: Theme.fontFamily
                            font.pixelSize: Theme.fontSize
                        }

                        Loader {
                            anchors.fill: parent
                            active: cell.isCurrent && iconView.activeFocus
                            sourceComponent: FocusRect95 { color: cell.selected ? "#ffff80" : "#000000" }
                        }
                    }

                    RenameEditor {
                        visible: cell.editing
                        anchors.horizontalCenter: parent.horizontalCenter
                        y: labelBox.y
                        width: cell.width - 2
                        editing: cell.editing
                        originalName: cell.name
                        onCommit: (text) => window.commitRename(cell.index, text)
                        onCancel: window.cancelRename()
                    }

                    MouseArea {
                        id: cellMouse
                        // the icon and label are clickable, not the empty corners
                        x: Math.min(cellIcon.x, labelBox.x)
                        y: cellIcon.y
                        width: Math.max(cellIcon.width, labelBox.width)
                        height: labelBox.y + labelBox.height - cellIcon.y
                        enabled: !cell.editing
                        acceptedButtons: Qt.LeftButton | Qt.RightButton
                        onPressed: (mouse) => window.pressRow(iconView, cell.index, mouse, cellMouse)
                        onClicked: (mouse) => window.clickRow(cell.index, mouse)
                        onDoubleClicked: (mouse) => { if (mouse.button === Qt.LeftButton) dir.openRow(cell.index); }
                    }
                }
            }

            ScrollBar95 {
                id: iconBar
                anchors.right: parent.right
                anchors.top: parent.top
                anchors.bottom: parent.bottom
                flickable: iconView
                stepSize: iconView.cellHeight / 2
                visible: window.viewMode === "icons" && needed
            }

            // ------------------------------------------------- details view
            ListView {
                id: detailsView
                visible: window.viewMode === "details"
                anchors.left: parent.left
                anchors.top: parent.top
                anchors.bottom: parent.bottom
                anchors.right: detailsBar.visible ? detailsBar.left : parent.right
                interactive: false
                currentIndex: -1
                model: dir
                focus: window.viewMode === "details"
                headerPositioning: ListView.OverlayHeader
                Keys.onPressed: (event) => window.handleKey(detailsView, event)

                header: Row {
                    z: 2
                    height: 18
                    HeaderButton {
                        width: window.colName; text: "Name"
                        sorted: dir.sortColumn === DirectoryModel.SortName; ascending: dir.sortAscending
                        onActivated: window.sortBy(DirectoryModel.SortName)
                        onResized: (w) => window.colName = Math.max(60, w)
                    }
                    HeaderButton {
                        width: window.colSize; text: dir.isComputer ? "Free Space" : "Size"; alignRight: true
                        sorted: dir.sortColumn === DirectoryModel.SortSize; ascending: dir.sortAscending
                        onActivated: window.sortBy(DirectoryModel.SortSize)
                        onResized: (w) => window.colSize = Math.max(40, w)
                    }
                    HeaderButton {
                        width: window.colType; text: "Type"
                        sorted: dir.sortColumn === DirectoryModel.SortType; ascending: dir.sortAscending
                        onActivated: window.sortBy(DirectoryModel.SortType)
                        onResized: (w) => window.colType = Math.max(40, w)
                    }
                    HeaderButton {
                        width: window.colModified; text: "Modified"
                        sorted: dir.sortColumn === DirectoryModel.SortModified; ascending: dir.sortAscending
                        onActivated: window.sortBy(DirectoryModel.SortModified)
                        onResized: (w) => window.colModified = Math.max(40, w)
                    }
                    // fill the rest of the header strip
                    Bevel { width: Math.max(0, detailsView.width - x); height: 18; style: "raised" }
                }

                delegate: Item {
                    id: row
                    required property int index
                    required property string name
                    required property string iconKind
                    required property string sizeText
                    required property string typeText
                    required property string modifiedText
                    required property bool selected
                    required property bool symlink
                    required property bool hidden

                    readonly property bool editing: window.renameRow === index && window.viewMode === "details"
                    readonly property bool isCurrent: detailsView.currentIndex === index

                    width: detailsView.width
                    height: 18

                    Item {
                        id: nameCell
                        width: window.colName
                        height: parent.height
                        clip: true

                        FileIcon {
                            id: smallIcon
                            x: 3
                            anchors.verticalCenter: parent.verticalCenter
                            size: 16
                            kind: row.iconKind
                            selected: row.selected
                            symlink: row.symlink
                            opacity: row.hidden ? 0.5 : 1
                        }
                        Rectangle {
                            id: nameBox
                            visible: !row.editing
                            x: smallIcon.x + smallIcon.width + 3
                            anchors.verticalCenter: parent.verticalCenter
                            width: Math.min(nameText.implicitWidth + 4, nameCell.width - x - 2)
                            height: 15
                            color: row.selected ? Theme.selection : "transparent"
                            Text {
                                id: nameText
                                x: 2
                                width: parent.width - 4
                                anchors.verticalCenter: parent.verticalCenter
                                text: row.name
                                elide: Text.ElideRight
                                color: row.selected ? Theme.selectionText : Theme.text
                                font.family: Theme.fontFamily
                                font.pixelSize: Theme.fontSize
                            }
                            Loader {
                                anchors.fill: parent
                                active: row.isCurrent && detailsView.activeFocus
                                sourceComponent: FocusRect95 { color: row.selected ? "#ffff80" : "#000000" }
                            }
                        }
                        RenameEditor {
                            visible: row.editing
                            x: nameBox.x
                            anchors.verticalCenter: parent.verticalCenter
                            width: nameCell.width - x - 2
                            singleLine: true
                            editing: row.editing
                            originalName: row.name
                            onCommit: (text) => window.commitRename(row.index, text)
                            onCancel: window.cancelRename()
                        }
                        MouseArea {
                            id: rowMouse
                            x: 0
                            width: nameBox.x + nameBox.width
                            height: parent.height
                            enabled: !row.editing
                            acceptedButtons: Qt.LeftButton | Qt.RightButton
                            onPressed: (mouse) => window.pressRow(detailsView, row.index, mouse, rowMouse)
                            onClicked: (mouse) => window.clickRow(row.index, mouse)
                            onDoubleClicked: (mouse) => { if (mouse.button === Qt.LeftButton) dir.openRow(row.index); }
                        }
                    }

                    DetailText { x: window.colName; width: window.colSize; text: row.sizeText; horizontalAlignment: Text.AlignRight }
                    DetailText { x: window.colName + window.colSize; width: window.colType; text: row.typeText }
                    DetailText { x: window.colName + window.colSize + window.colType; width: window.colModified; text: row.modifiedText }
                }
            }

            ScrollBar95 {
                id: detailsBar
                anchors.right: parent.right
                anchors.top: parent.top
                anchors.bottom: parent.bottom
                flickable: detailsView
                stepSize: 18
                visible: window.viewMode === "details" && needed
            }
        }
    }

    // ------------------------------------------------------------- status bar
    Item {
        id: statusBar
        anchors.bottom: parent.bottom
        anchors.bottomMargin: 2
        x: 2
        width: parent.width - 4
        height: 18

        Bevel {
            id: leftPane
            width: Math.round(parent.width * 0.4)
            height: parent.height
            style: "thinSunken"
            Text {
                x: 4
                width: parent.width - 8
                anchors.verticalCenter: parent.verticalCenter
                text: dir.statusText
                elide: Text.ElideRight
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontSize
            }
        }
        Bevel {
            anchors.left: leftPane.right
            anchors.leftMargin: 2
            anchors.right: parent.right
            height: parent.height
            style: "thinSunken"
            Text {
                x: 4
                width: parent.width - 20
                anchors.verticalCenter: parent.verticalCenter
                text: dir.sizeStatusText
                elide: Text.ElideRight
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
    }

    // ---------------------------------------------------------- small pieces

    component DetailText: Text {
        height: 18
        leftPadding: 6
        rightPadding: 6
        verticalAlignment: Text.AlignVCenter
        elide: Text.ElideRight
        color: Theme.text
        font.family: Theme.fontFamily
        font.pixelSize: Theme.fontSize
    }

    component HeaderButton: Item {
        id: header
        property string text
        property bool sorted: false
        property bool ascending: true
        property bool alignRight: false
        signal activated()
        signal resized(real newWidth)
        height: 18
        readonly property bool down: headerMouse.pressed && headerMouse.containsMouse

        Bevel { anchors.fill: parent; style: header.down ? "thinSunken" : "raised" }
        Text {
            x: header.alignRight ? parent.width - width - 6 - (header.sorted ? 10 : 0) : 6
            width: Math.min(implicitWidth, parent.width - 18)
            anchors.verticalCenter: parent.verticalCenter
            text: header.text
            elide: Text.ElideRight
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontSize
        }
        Text {
            visible: header.sorted
            anchors.right: parent.right
            anchors.rightMargin: 6
            anchors.verticalCenter: parent.verticalCenter
            text: header.ascending ? "▴" : "▾"
            font.pixelSize: 10
            color: Theme.shadow
        }
        MouseArea {
            id: headerMouse
            anchors.fill: parent
            anchors.rightMargin: 4
            onClicked: header.activated()
        }
        // drag the right edge to resize the column
        MouseArea {
            anchors.right: parent.right
            width: 6
            height: parent.height
            cursorShape: Qt.SplitHCursor
            property real startX: 0
            property real startW: 0
            onPressed: (mouse) => {
                startX = mapToItem(null, mouse.x, 0).x;
                startW = header.width;
            }
            onPositionChanged: (mouse) => {
                if (pressed)
                    header.resized(startW + mapToItem(null, mouse.x, 0).x - startX);
            }
        }
    }

    component RenameEditor: Rectangle {
        id: editor
        property bool editing: false
        property string originalName: ""
        property bool singleLine: false
        signal commit(string text)
        signal cancel()

        height: singleLine ? 16 : Math.max(18, input.contentHeight + 4)
        color: Theme.window
        border.color: Theme.dark

        function begin() {
            input.text = originalName;
            // select the name without its extension, so typing keeps ".txt"
            var dot = originalName.lastIndexOf(".");
            if (dot > 0)
                input.select(0, dot);
            else
                input.selectAll();
            input.forceActiveFocus();
        }
        onEditingChanged: if (editing) begin()
        Component.onCompleted: if (editing) begin()

        TextInput {
            id: input
            x: 2
            y: 2
            width: parent.width - 4
            wrapMode: editor.singleLine ? TextInput.NoWrap : TextInput.WrapAnywhere
            horizontalAlignment: editor.singleLine ? TextInput.AlignLeft : TextInput.AlignHCenter
            clip: true
            selectByMouse: true
            color: Theme.text
            selectionColor: Theme.selection
            selectedTextColor: Theme.selectionText
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontSize
            onAccepted: editor.commit(text)
            Keys.onEscapePressed: editor.cancel()
            onActiveFocusChanged: if (!activeFocus && editor.editing) editor.commit(text)
        }
    }

    // window-level overlays (must be last so they stack on top)
    Menu95 {
        id: popup
        onClosed: if (!msgBox.visible && !address.activeFocus && window.renameRow < 0) window.focusView()
    }
    MessageBox95 { id: msgBox }
}
