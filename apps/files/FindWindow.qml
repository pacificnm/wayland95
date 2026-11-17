import QtQuick
import QtQuick.Window
import Win95Ui
import Win95Files

// Find: All Files (Start > Find > Files or Folders..., or win95files
// --find [folder]): what to look for on three tabs, Find Now, and the
// files found in a list below, as in Windows 95. The search itself is
// FileSearch (src/filesearch.*), on its own thread.
Window {
    id: window

    width: 640
    height: expanded ? 440 : 210
    minimumWidth: 520
    minimumHeight: expanded ? 300 : 210
    visible: true
    color: Theme.face
    title: (named.text.trim() ? "Find: Files named " + named.text.trim() : "Find: All Files")

    // the results list shows once there's been a search
    property bool expanded: false
    property bool searched: false
    property bool caseSensitive: false
    property int current: -1

    readonly property bool modalOpen: popup.visible || msgBox.visible || folderDialog.visible

    function lookInPath() { return search.expandPath(lookIn.text) || Win95Fs.homePath(); }

    function findNow() {
        var folder = lookInPath();
        if (!Win95Fs.isDir(folder)) {
            msgBox.show("Find", "The folder '" + lookIn.text + "' doesn't exist. Check the name in Look in, or click Browse to choose one.",
                        "warning", ["OK"], function () { tabs.currentIndex = 0; lookIn.forceActiveFocus(); });
            return;
        }
        expanded = true;
        searched = true;
        current = -1;
        search.start({
            named: named.text,
            lookIn: folder,
            subfolders: subfolders.checked,
            days: modifiedRecently.checked ? days.value : 0,
            containing: containing.text,
            caseSensitive: caseSensitive,
            hidden: hidden.checked,
        });
        results.forceActiveFocus();
    }

    function newSearch() {
        msgBox.show("Find", "Your current search will be cleared.", "info", ["OK", "Cancel"], function (i) {
            if (i !== 0)
                return;
            search.stop();
            search.clear();
            named.text = "";
            containing.text = "";
            allFiles.checked = true;
            modifiedRecently.checked = false;
            hidden.checked = false;
            subfolders.checked = true;
            searched = false;
            tabs.currentIndex = 0;
            named.forceActiveFocus();
        });
    }

    function complain(problem) {
        if (problem)
            msgBox.show("Find", problem, "error", ["OK"], function () { results.forceActiveFocus(); });
    }
    function openRow(row) {
        if (row >= 0)
            complain(search.open(row));
    }
    function selectedRow() {
        return current >= 0 ? current : -1;
    }

    Component.onCompleted: {
        lookIn.text = initialLookIn || Win95Fs.homePath();
        named.forceActiveFocus();
    }

    // ---------------------------------------------------------------- menus
    function fileMenu() {
        var row = selectedRow();
        return [
            { text: "Open", enabled: row >= 0, action: function () { window.openRow(row); } },
            { text: "Open Containing Folder", enabled: row >= 0,
              action: function () { window.complain(search.openContainingFolder(row)); } },
            { separator: true },
            { text: "Properties", enabled: row >= 0, action: function () { window.showProperties(row); } },
            { separator: true },
            { text: "Close", action: window.close },
        ];
    }
    function editMenu() {
        return [
            { text: "Select All", shortcut: "Ctrl+A", enabled: search.count > 0, action: search.selectAll },
        ];
    }
    function optionsMenu() {
        return [
            { text: "Case Sensitive", checked: caseSensitive, action: function () { window.caseSensitive = !window.caseSensitive; } },
        ];
    }
    function helpMenu() {
        return [
            { text: "About Find", action: function () {
                msgBox.show("About Find", "Find: All Files\n\nLooks for files and folders by name, place, date and the text in them.\n\n"
                            + "Named takes several names with ; between them, and * and ? as wildcards;\n"
                            + "a name without them is found anywhere in a file's name.",
                            "info", ["OK"], function () { named.forceActiveFocus(); });
            } },
        ];
    }
    function rowMenu(row) {
        return [
            { text: "Open", action: function () { window.openRow(row); } },
            { text: "Open Containing Folder", action: function () { window.complain(search.openContainingFolder(row)); } },
            { separator: true },
            { text: "Properties", action: function () { window.showProperties(row); } },
        ];
    }
    function showProperties(row) {
        msgBox.show("Properties", search.properties(row), "info", ["OK"], function () { results.forceActiveFocus(); });
    }

    Shortcut { sequence: "Escape"; enabled: search.running && !window.modalOpen; onActivated: search.stop() }

    MenuBar95 {
        id: menuBar
        x: 0
        y: 0
        width: parent.width
        height: 20
        popupMenu: popup
        menus: [
            { title: "File", items: window.fileMenu },
            { title: "Edit", items: window.editMenu },
            { title: "Options", items: window.optionsMenu },
            { title: "Help", items: window.helpMenu },
        ]
    }

    // ------------------------------------------------------ what to look for
    TabWidget95 {
        id: tabs
        x: 8
        y: menuBar.height + 6
        width: parent.width - 8 - buttons.width - 20
        height: 168
        titles: ["Name & Location", "Date Modified", "Advanced"]

        Item {
            Text {
                x: 12; y: 16; height: 21
                verticalAlignment: Text.AlignVCenter
                textFormat: Text.StyledText
                text: "<u>N</u>amed:"
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontSize
            }
            TextField95 {
                id: named
                x: 76; y: 16
                width: parent.width - x - 12
                focus: true
                onAccepted: window.findNow()
            }
            Text {
                x: 12; y: 50; height: 21
                verticalAlignment: Text.AlignVCenter
                textFormat: Text.StyledText
                text: "<u>L</u>ook in:"
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontSize
            }
            TextField95 {
                id: lookIn
                x: 76; y: 50
                width: parent.width - x - 12 - 84
                onAccepted: window.findNow()
            }
            Button95 {
                x: parent.width - 12 - width
                y: 49
                width: 76
                text: "<u>B</u>rowse..."
                onClicked: {
                    window.expanded = true;
                    folderDialog.openAt(window.lookInPath(), "");
                }
            }
            CheckBox95 {
                id: subfolders
                x: 76; y: 84
                checked: true
                text: "Include <u>s</u>ubfolders"
                onToggled: (on) => { checked = on; }
            }
            Text {
                x: 76; y: 112
                width: parent.width - x - 12
                wrapMode: Text.WordWrap
                text: "For example:  *.txt; report"
                color: Theme.shadow
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontSize - 1
            }
        }

        Item {
            RadioButton95 {
                id: allFiles
                x: 12; y: 16
                checked: true
                text: "<u>A</u>ll files"
                onClicked: { checked = true; modifiedRecently.checked = false; }
            }
            RadioButton95 {
                id: modifiedRecently
                x: 12; y: 46
                text: "Find all files modified in the pre<u>v</u>ious"
                onClicked: { checked = true; allFiles.checked = false; }
            }
            SpinBox95 {
                id: days
                x: 262; y: 44
                width: 54
                from: 1
                to: 9999
                value: 7
                enabled: modifiedRecently.checked
                opacity: enabled ? 1 : 0.5
            }
            Text {
                x: 322; y: 44; height: 21
                verticalAlignment: Text.AlignVCenter
                text: "day(s)"
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontSize
            }
        }

        Item {
            Text {
                x: 12; y: 16; height: 21
                verticalAlignment: Text.AlignVCenter
                textFormat: Text.StyledText
                text: "Con<u>t</u>aining text:"
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontSize
            }
            TextField95 {
                id: containing
                x: 110; y: 16
                width: parent.width - x - 12
                onAccepted: window.findNow()
            }
            CheckBox95 {
                id: hidden
                x: 110; y: 50
                text: "Include <u>h</u>idden files and folders"
                onToggled: (on) => { checked = on; }
            }
            Text {
                x: 110; y: 78
                width: parent.width - x - 12
                wrapMode: Text.WordWrap
                text: "Text is looked for in plain-text files (not in programs, pictures or documents saved in other formats)."
                color: Theme.shadow
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontSize - 1
            }
        }
    }

    Column {
        id: buttons
        x: parent.width - width - 10
        y: tabs.y + 20
        width: 92
        spacing: 6
        Button95 {
            width: parent.width
            text: "F<u>i</u>nd Now"
            isDefault: true
            enabled: !search.running
            onClicked: window.findNow()
        }
        Button95 {
            width: parent.width
            text: "S<u>t</u>op"
            enabled: search.running
            onClicked: search.stop()
        }
        Button95 {
            width: parent.width
            text: "Ne<u>w</u> Search"
            enabled: !search.running
            onClicked: window.newSearch()
        }
        // a magnifying glass that sweeps while it looks
        Item {
            width: parent.width
            height: 54
            MenuGlass {
                id: glass
                anchors.horizontalCenter: parent.horizontalCenter
                y: 10
                property real sweep: 0
                anchors.horizontalCenterOffset: Math.round(Math.sin(sweep) * 14)
                NumberAnimation on sweep {
                    running: search.running
                    from: 0; to: 2 * Math.PI
                    duration: 1400
                    loops: Animation.Infinite
                }
            }
        }
    }

    // ---------------------------------------------------------- results
    Item {
        id: resultsArea
        visible: window.expanded
        x: 0
        y: tabs.y + tabs.height + 8
        width: parent.width
        height: parent.height - y - statusBar.height

        readonly property int colName: 170
        readonly property int colFolder: Math.max(120, width - 170 - 70 - 110 - 120 - 26)
        readonly property int colSize: 70
        readonly property int colType: 110
        readonly property int colModified: 120

        Bevel {
            anchors.fill: parent
            anchors.leftMargin: 2
            anchors.rightMargin: 2
            style: "sunken"
            face: Theme.window

            ListView {
                id: results
                x: 2
                y: 2
                width: parent.width - 4 - (scroll.visible ? scroll.width : 0)
                height: parent.height - 4
                clip: true
                model: search
                interactive: false
                headerPositioning: ListView.OverlayHeader
                boundsBehavior: Flickable.StopAtBounds
                activeFocusOnTab: true

                header: Row {
                    z: 2
                    height: 18
                    Repeater {
                        model: [["Name", FileSearch.SortName, resultsArea.colName],
                                ["In Folder", FileSearch.SortFolder, resultsArea.colFolder],
                                ["Size", FileSearch.SortSize, resultsArea.colSize],
                                ["Type", FileSearch.SortType, resultsArea.colType],
                                ["Modified", FileSearch.SortModified, resultsArea.colModified]]
                        delegate: Item {
                            id: head
                            required property var modelData
                            width: modelData[2]
                            height: 18
                            readonly property bool down: headMouse.pressed && headMouse.containsMouse
                            Bevel { anchors.fill: parent; style: head.down ? "thinSunken" : "raised" }
                            Text {
                                x: 6
                                width: parent.width - 18
                                anchors.verticalCenter: parent.verticalCenter
                                text: head.modelData[0]
                                elide: Text.ElideRight
                                font.family: Theme.fontFamily
                                font.pixelSize: Theme.fontSize
                            }
                            Text {
                                visible: search.sortColumn === head.modelData[1]
                                anchors.right: parent.right
                                anchors.rightMargin: 6
                                anchors.verticalCenter: parent.verticalCenter
                                text: search.sortAscending ? "▴" : "▾"
                                font.pixelSize: 10
                                color: Theme.shadow
                            }
                            MouseArea {
                                id: headMouse
                                anchors.fill: parent
                                onClicked: { search.sortBy(head.modelData[1]); window.current = -1; }
                            }
                        }
                    }
                    Bevel { width: Math.max(0, results.width - x); height: 18; style: "raised" }
                }

                delegate: Item {
                    id: row
                    required property int index
                    required property string name
                    required property string folder
                    required property string iconKind
                    required property string sizeText
                    required property string typeText
                    required property string modifiedText
                    required property bool selected
                    width: results.width
                    height: 18

                    Row {
                        anchors.fill: parent
                        Item {
                            width: resultsArea.colName
                            height: parent.height
                            clip: true
                            FileIcon {
                                id: icon
                                x: 3
                                anchors.verticalCenter: parent.verticalCenter
                                size: 16
                                kind: row.iconKind
                                selected: row.selected
                            }
                            Rectangle {
                                x: icon.x + icon.width + 3
                                anchors.verticalCenter: parent.verticalCenter
                                width: Math.min(nameText.implicitWidth + 4, parent.width - x - 2)
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
                                FocusRect95 {
                                    anchors.fill: parent
                                    visible: window.current === row.index && results.activeFocus
                                    color: row.selected ? "#ffff80" : "#000000"
                                }
                            }
                        }
                        Repeater {
                            model: [[row.folder, resultsArea.colFolder, false], [row.sizeText, resultsArea.colSize, true],
                                    [row.typeText, resultsArea.colType, false], [row.modifiedText, resultsArea.colModified, false]]
                            delegate: Text {
                                required property var modelData
                                width: modelData[1]
                                height: 18
                                leftPadding: 6
                                rightPadding: 6
                                verticalAlignment: Text.AlignVCenter
                                horizontalAlignment: modelData[2] ? Text.AlignRight : Text.AlignLeft
                                text: modelData[0]
                                elide: Text.ElideMiddle
                                color: Theme.text
                                font.family: Theme.fontFamily
                                font.pixelSize: Theme.fontSize
                            }
                        }
                    }
                }

                MouseArea {
                    anchors.fill: parent
                    anchors.topMargin: 18
                    acceptedButtons: Qt.LeftButton | Qt.RightButton
                    onWheel: (wheel) => scroll.scrollBy(-wheel.angleDelta.y / 120 * 54)
                    function rowAt(mouse) {
                        return results.indexAt(mouse.x + results.contentX, mouse.y + 18 + results.contentY);
                    }
                    onPressed: (mouse) => {
                        results.forceActiveFocus();
                        var r = rowAt(mouse);
                        if (r < 0)
                            return;
                        if (mouse.button === Qt.RightButton) {
                            if (!search.selectedPaths().length || window.current !== r)
                                search.select(r, 0);
                            window.current = r;
                            var p = mapToItem(popup, mouse.x, mouse.y);
                            popup.popup(p.x, p.y, window.rowMenu(r));
                            return;
                        }
                        search.select(r, (mouse.modifiers & Qt.ControlModifier) ? 1 : (mouse.modifiers & Qt.ShiftModifier) ? 2 : 0);
                        window.current = r;
                    }
                    onDoubleClicked: (mouse) => {
                        var r = rowAt(mouse);
                        if (r >= 0 && mouse.button === Qt.LeftButton)
                            window.openRow(r);
                    }
                }

                Keys.onPressed: (event) => {
                    var n = search.count;
                    if (!n)
                        return;
                    var r = window.current;
                    var page = Math.max(1, Math.floor((results.height - 18) / 18));
                    switch (event.key) {
                    case Qt.Key_Down: r = Math.min(n - 1, r + 1); break;
                    case Qt.Key_Up: r = Math.max(0, r - 1); break;
                    case Qt.Key_PageDown: r = Math.min(n - 1, r + page); break;
                    case Qt.Key_PageUp: r = Math.max(0, r - page); break;
                    case Qt.Key_Home: r = 0; break;
                    case Qt.Key_End: r = n - 1; break;
                    case Qt.Key_Return:
                    case Qt.Key_Enter:
                        window.openRow(window.current);
                        event.accepted = true;
                        return;
                    case Qt.Key_A:
                        if (event.modifiers & Qt.ControlModifier) {
                            search.selectAll();
                            event.accepted = true;
                        }
                        return;
                    default:
                        return;
                    }
                    search.select(r, (event.modifiers & Qt.ShiftModifier) ? 2 : 0);
                    window.current = r;
                    results.positionViewAtIndex(r, ListView.Contain);
                    event.accepted = true;
                }
            }

            ScrollBar95 {
                id: scroll
                anchors.right: parent.right
                anchors.rightMargin: 2
                y: 2
                height: parent.height - 4
                flickable: results
                stepSize: 18
                visible: needed
            }
        }
    }

    // ----------------------------------------------------------- status
    Item {
        id: statusBar
        anchors.bottom: parent.bottom
        width: parent.width
        height: window.expanded ? 22 : 0
        visible: window.expanded
        Bevel {
            anchors.fill: parent
            anchors.margins: 2
            style: "thinSunken"
            Text {
                x: 6
                width: parent.width - 12
                anchors.verticalCenter: parent.verticalCenter
                elide: Text.ElideMiddle
                text: search.running
                      ? (search.count + " file(s) found" + (search.searchingIn ? "  -  Searching in " + search.searchingIn : ""))
                      : !window.searched ? "Enter what to look for, then click Find Now."
                      : search.count + " file(s) found"
                        + (search.truncated ? " (stopped at " + search.count + ": narrow the search to see the rest)" : "")
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontSize
            }
        }
    }

    // --------------------------------------------------------- overlays
    FileDialog95 {
        id: folderDialog
        mode: "folder"
        messageBox: msgBox
        popupMenu: popup
        onAccepted: (path) => {
            lookIn.text = path;
            tabs.currentIndex = 0;
            lookIn.forceActiveFocus();
        }
    }
    Menu95 { id: popup }
    MessageBox95 { id: msgBox }

    // a magnifying glass over a page
    component MenuGlass: Canvas {
        width: 34
        height: 34
        onPaint: {
            var ctx = getContext("2d");
            ctx.reset();
            ctx.fillStyle = "#000000";
            ctx.fillRect(4, 2, 18, 24);
            ctx.fillStyle = "#ffffff";
            ctx.fillRect(5, 3, 16, 22);
            ctx.fillStyle = "#000080";
            for (var i = 0; i < 5; ++i)
                ctx.fillRect(8, 7 + i * 3, i === 4 ? 6 : 10, 1);
            ctx.lineWidth = 2.5;
            ctx.strokeStyle = "#000000";
            ctx.beginPath(); ctx.moveTo(22, 22); ctx.lineTo(31, 31); ctx.stroke();
            ctx.fillStyle = "rgba(160, 220, 255, 0.6)";
            ctx.beginPath(); ctx.arc(17, 17, 7, 0, 2 * Math.PI); ctx.fill();
            ctx.lineWidth = 2;
            ctx.strokeStyle = "#202020";
            ctx.beginPath(); ctx.arc(17, 17, 7, 0, 2 * Math.PI); ctx.stroke();
        }
    }
}
