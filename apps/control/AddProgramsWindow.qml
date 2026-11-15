import QtQuick
import QtQuick.Window
import Win95Ui
import Win95Control

// Add New Programs: browse or search the catalogs (the system's programs
// and Flatpak apps), read about one, and install it.
Window {
    id: win

    width: 580
    height: 480
    minimumWidth: 460
    minimumHeight: 360
    color: Theme.face
    title: "Add New Programs"

    // "" or a category from Software.categories
    property string category: ""
    // "", "deb" or "flatpak"
    property string source: ""
    // every package, not only programs (PackageKit search)
    property bool allPackages: false
    property var packageResults: []
    property bool searchingPackages: false

    property var results: []
    function refresh() {
        if (allPackages) {
            if (query.text.trim().length < 2) {
                packageResults = [];
                return;
            }
            searchingPackages = true;
            Software.searchPackages(query.text);
        } else {
            results = Software.search(query.text, category, source);
        }
    }
    readonly property var shown: allPackages ? packageResults : results

    Timer { id: typing; interval: allPackages ? 600 : 200; onTriggered: win.refresh() }
    Connections {
        target: Software
        function onCatalogChanged() { if (!Software.catalogLoading) win.refresh(); }
        function onProgramsChanged() { if (!win.allPackages) win.refresh(); }
        function onPackagesFound(list) {
            win.searchingPackages = false;
            win.packageResults = list;
        }
    }
    Component.onCompleted: refresh()

    Shortcut {
        sequence: "Esc"
        enabled: !msgBox.visible && !popup.visible && !tasks.visible
        onActivated: win.close()
    }

    component Label: Text {
        height: 21
        verticalAlignment: Text.AlignVCenter
        font.family: Theme.fontFamily
        font.pixelSize: Theme.fontSize
    }

    // ------------------------------------------------------------ search bar
    Row {
        id: searchRow
        x: 10
        y: 10
        spacing: 6
        Label { text: "Look <u>f</u>or:" }
        TextField95 {
            id: query
            width: 170
            height: 21
            focus: true
            onAccepted: { typing.stop(); win.refresh(); }
            Connections {
                target: query.input
                function onTextEdited() { typing.restart(); }
            }
        }
        Label { text: "<u>S</u>how:"; visible: !win.allPackages }
        ComboBox95 {
            visible: !win.allPackages
            width: 120
            popupMenu: popup
            model: ["All programs"].concat(Software.categories)
            onActivated: (i) => { win.category = i === 0 ? "" : Software.categories[i - 1]; win.refresh(); }
        }
        Label { text: "F<u>r</u>om:"; visible: !win.allPackages && Software.packageKit && Software.flatpak }
        ComboBox95 {
            visible: !win.allPackages && Software.packageKit && Software.flatpak
            width: 100
            popupMenu: popup
            model: ["Anywhere", Software.packageSource, "Flatpak"]
            onActivated: (i) => { win.source = ["", "deb", "flatpak"][i]; win.refresh(); }
        }
    }

    CheckBox95 {
        id: allBox
        x: 10
        y: searchRow.y + searchRow.height + 6
        visible: Software.packageKit
        text: "Search <u>a</u>ll of " + Software.packageSource + "'s packages, not just programs (libraries, tools, fonts...)"
        checked: win.allPackages
        onToggled: (on) => { win.allPackages = on; win.refresh(); }
    }

    // Flathub, when Flatpak is here but Flathub isn't set up yet
    Row {
        id: flathubRow
        x: 10
        y: allBox.y + (allBox.visible ? allBox.height + 6 : 0)
        height: visible ? 24 : 0
        spacing: 8
        visible: Software.flatpak && Software.flatpakRemotes.indexOf("flathub") < 0 && !win.allPackages
        Label {
            anchors.verticalCenter: parent.verticalCenter
            text: "Flathub, the store of apps for every Linux, isn't set up yet."
        }
        Button95 {
            width: 110
            text: "Set Up <u>F</u>lathub"
            enabled: !Software.busy
            onClicked: {
                tasks.mine = true;
                Software.setUpFlathub();
                tasks.show();
            }
        }
    }

    // --------------------------------------------------------------- results
    ProgramList {
        id: list
        x: 10
        y: flathubRow.y + flathubRow.height + 6
        width: parent.width - 20
        height: parent.height - y - details.height - buttons.height - 30
        large: !win.allPackages
        model: win.shown
        noteFor: function (item) {
            return item.installed ? "Installed"
                 : item.source === "flatpak" ? (item.remote === "flathub" ? "Flathub" : item.remote)
                 : Software.packageSource;
        }
        emptyText: Software.catalogLoading && !win.allPackages ? "Loading the list of programs..."
                 : win.searchingPackages ? "Searching..."
                 : win.allPackages && query.text.trim().length < 2 ? "Type at least two letters to search."
                 : !win.allPackages && Software.catalogSize === 0
                   ? "There's no list of programs to choose from." + (Software.packageKit ? " Try searching all packages instead." : "")
                 : "Nothing matches \"" + query.text + "\"."
        onActivated: (item) => { if (!item.installed) tasks.install(item); }
    }

    // ------------------------------------------------- the selected program
    Bevel {
        id: details
        x: 10
        y: list.y + list.height + 8
        width: parent.width - 20
        height: 96
        style: "sunken"
        face: Theme.window

        Flickable {
            id: detailFlick
            x: 6
            y: 4
            width: parent.width - 12 - (detailBar.needed ? detailBar.width : 0)
            height: parent.height - 8
            clip: true
            contentHeight: detailText.height
            interactive: false
            Text {
                id: detailText
                width: detailFlick.width
                wrapMode: Text.Wrap
                textFormat: Text.StyledText
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontSize
                text: {
                    var p = list.selected;
                    if (!p)
                        return "<font color=\"" + Theme.shadow + "\">Select a program to read about it.</font>";
                    function esc(s) { return String(s || "").replace(/&/g, "&amp;").replace(/</g, "&lt;").replace(/\n/g, "<br>"); }
                    return "<b>" + esc(p.name) + "</b>" + (p.version ? " " + esc(p.version) : "")
                           + (p.summary && p.summary !== p.description ? " — " + esc(p.summary) : "")
                           + "<br>" + esc(p.description || "");
                }
            }
            WheelHandler { onWheel: (event) => detailBar.scrollBy(-event.angleDelta.y / 120 * 45) }
        }
        ScrollBar95 {
            id: detailBar
            visible: needed
            anchors.right: parent.right
            anchors.rightMargin: 2
            y: 2
            height: parent.height - 4
            flickable: detailFlick
            stepSize: 15
        }
        Connections {
            target: list
            function onSelectedKeyChanged() { detailFlick.contentY = 0; }
        }
    }

    Row {
        id: buttons
        anchors.right: parent.right
        anchors.rightMargin: 10
        anchors.bottom: parent.bottom
        anchors.bottomMargin: 10
        spacing: 6
        Button95 {
            text: list.selected && list.selected.installed ? "Installed" : "<u>I</u>nstall"
            isDefault: true
            enabled: !!list.selected && !list.selected.installed && !Software.busy
            onClicked: tasks.install(list.selected)
        }
        Button95 {
            text: "Close"
            onClicked: win.close()
        }
    }

    Label {
        x: 10
        anchors.verticalCenter: buttons.verticalCenter
        color: Theme.shadow
        text: win.shown.length === 0 ? ""
            : win.shown.length >= 400 ? "Showing the first 400. Type to narrow them down."
            : win.shown.length + (win.shown.length === 1 ? " program" : " programs")
    }

    SizeGrip95 {
        anchors.right: parent.right
        anchors.rightMargin: 2
        anchors.bottom: parent.bottom
        anchors.bottomMargin: 2
    }

    SoftwareTasks {
        id: tasks
        messageBox: msgBox
        onDone: (ok) => { if (ok) win.refresh(); }
    }
    Menu95 { id: popup }
    MessageBox95 { id: msgBox }
}
