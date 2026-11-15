import QtQuick
import QtQuick.Window
import Win95Ui
import Win95Control

// Display Properties: desktop wallpaper (centered, tiled or stretched) and
// background color, the screen saver, window colors, and screen info, with
// preview monitors. Applied live by the compositor.
PropertySheet {
    id: sheet

    title: "Display Properties"
    titles: ["Background", "Screen Saver", "Appearance", "Settings"]

    // local copies; written on OK/Apply
    property color bgColor: Wayland95Settings.backgroundColor
    property string wallpaper: Wayland95Settings.wallpaper
    property string mode: Wayland95Settings.wallpaperMode

    // ---- Screen Saver state
    property string saverId: Wayland95Settings.screenSaver
    property int saverWait: Wayland95Settings.screenSaverWait
    property var saverOptions: Wayland95Settings.screenSaverOptions
    property bool saverPassword: Wayland95Settings.screenSaverPassword
    readonly property var savers: saverCatalog.savers
    function saverIndex() {
        for (var i = 0; i < savers.length; ++i)
            if (savers[i].id === saverId)
                return i;
        return 0;
    }
    // what each saver's Settings... dialog offers (keys match the savers'
    // view.option() calls in Win95Ui)
    readonly property var saverFields: ({
        starfield: [
            { key: "speed", label: "Warp speed:", type: "slider", from: 1, to: 10, def: 5 },
            { key: "stars", label: "Starfield density:", type: "slider", from: 50, to: 500, def: 200 },
        ],
        polygons: [
            { key: "count", label: "Polygons:", type: "spin", from: 1, to: 4, def: 2 },
            { key: "trail", label: "Trail length:", type: "slider", from: 1, to: 20, def: 8 },
            { key: "speed", label: "Speed:", type: "slider", from: 1, to: 10, def: 5 },
        ],
        curves: [
            { key: "speed", label: "Speed:", type: "slider", from: 1, to: 10, def: 5 },
            { key: "density", label: "Lines:", type: "slider", from: 5, to: 40, def: 20 },
        ],
        marquee: [
            { key: "text", label: "Text:", type: "text", def: "Wayland 95" },
            { key: "speed", label: "Speed:", type: "slider", from: 1, to: 10, def: 5 },
            { key: "color", label: "Text color:", type: "choice", def: "#ffff00",
              choices: [["Yellow", "#ffff00"], ["White", "#ffffff"], ["Red", "#ff0000"],
                        ["Green", "#00ff00"], ["Cyan", "#00ffff"]] },
            { key: "background", label: "Background:", type: "choice", def: "#000000",
              choices: [["Black", "#000000"], ["Blue", "#000080"], ["Teal", "#008080"]] },
        ],
    })

    // "" is (None); the current wallpaper is always listed
    property var wallpapers: {
        var list = SystemInfo.wallpapers();
        if (wallpaper && list.indexOf(wallpaper) < 0)
            list.unshift(wallpaper);
        return [""].concat(list);
    }

    readonly property var palette: [
        "#008080", "#000000", "#808080", "#c0c0c0", "#ffffff", "#800000",
        "#ff0000", "#808000", "#ffff00", "#008000", "#00ff00", "#00ffff",
        "#000080", "#0000ff", "#800080", "#ff00ff", "#404040", "#004040",
    ]

    function choose(prop, value) {
        sheet[prop] = value;
        sheet.dirty = true;
    }

    // ---- Appearance state: the window colors being edited (see
    // Wayland95Settings.colorKeys()) and the scheme they came from
    property var colors: Wayland95Settings.colors
    property string scheme: Wayland95Settings.colorScheme
    readonly property var schemes: Wayland95Settings.colorSchemes()

    // the things you can recolor; key2 = gradient end, text = caption color
    readonly property var appearanceItems: [
        { label: "Desktop", key: "desktop" },
        { label: "Active Title Bar", key: "activeTitle", key2: "activeTitle2", text: "activeTitleText" },
        { label: "Inactive Title Bar", key: "inactiveTitle", key2: "inactiveTitle2", text: "inactiveTitleText" },
        { label: "3D Objects", key: "face" },
        { label: "Window", key: "window", text: "windowText" },
        { label: "Menu", key: "menu", text: "menuText" },
        { label: "Selected Items", key: "selection", text: "selectionText" },
    ]
    property int appearanceItem: 0

    function colorOf(key) {
        return key === "desktop" ? bgColor : colors[key];
    }
    function setColor(key, value) {
        if (key === "desktop") {
            bgColor = value;
        } else {
            var copy = Object.assign({}, colors);
            copy[key] = value;
            colors = copy;
        }
        scheme = "(Custom)";
        dirty = true;
    }
    function applyScheme(name) {
        for (var i = 0; i < schemes.length; ++i) {
            if (schemes[i].name !== name)
                continue;
            colors = Object.assign({}, schemes[i].colors);
            bgColor = schemes[i].colors.desktop;
            scheme = name;
            dirty = true;
        }
    }
    function schemeNames() {
        var names = schemes.map(function (s) { return s.name; });
        if (names.indexOf(scheme) < 0)
            names.push(scheme);
        return names;
    }

    onApplyRequested: {
        Wayland95Settings.setColors(colors, scheme);
        Wayland95Settings.backgroundColor = bgColor;
        Wayland95Settings.wallpaper = wallpaper;
        Wayland95Settings.wallpaperMode = mode;
        Wayland95Settings.setScreenSaver(saverId, saverWait, saverOptions);
        Wayland95Settings.screenSaverPassword = saverPassword;
    }


    // ------------------------------------------------------- Background tab
    Item {
        // the preview monitor
        Item {
            id: preview
            width: 180
            height: 150
            anchors.horizontalCenter: parent.horizontalCenter

            Bevel { x: 10; width: 160; height: 122; style: "raised" }
            Rectangle {
                id: screen
                x: 22; y: 12
                width: 136; height: 98
                color: sheet.bgColor
                clip: true

                // how big the real desktop is, to scale the picture down
                readonly property real scale: width / Math.max(1, Screen.width)

                Image {
                    id: probe
                    visible: false
                    source: sheet.wallpaper ? Win95Fs.toUrl(sheet.wallpaper) : ""
                    asynchronous: true
                }
                Image {
                    visible: sheet.wallpaper.length > 0 && probe.status === Image.Ready
                    source: probe.source
                    asynchronous: true
                    smooth: true
                    fillMode: sheet.mode === "tile" ? Image.Tile : Image.Stretch
                    x: sheet.mode === "center" ? (screen.width - width) / 2 : 0
                    y: sheet.mode === "center" ? (screen.height - height) / 2 : 0
                    width: sheet.mode === "center" ? probe.implicitWidth * screen.scale : screen.width
                    height: sheet.mode === "center" ? probe.implicitHeight * screen.scale : screen.height
                    sourceSize.width: sheet.mode === "tile" ? Math.max(1, probe.implicitWidth * screen.scale) : 0
                }
            }
            Rectangle { x: 156; y: 112; width: 4; height: 3; color: "#00c000" } // power light
            Bevel { x: 70; y: 122; width: 40; height: 10; style: "raised" }   // neck
            Bevel { x: 45; y: 132; width: 90; height: 12; style: "raised" }   // base
        }

        GroupBox95 {
            id: wallpaperGroup
            y: preview.height + 4
            width: parent.width * 0.6 - 4
            height: parent.height - y
            title: "<u>W</u>allpaper"

            Bevel {
                id: listBox
                width: parent.width
                height: parent.height - 52
                style: "sunken"
                face: Theme.window

                ListView {
                    id: list
                    x: 2; y: 2
                    width: parent.width - 4 - (listBar.visible ? listBar.width : 0)
                    height: parent.height - 4
                    clip: true
                    interactive: false
                    model: sheet.wallpapers
                    // follows the chosen wallpaper; clicks and arrow keys
                    // change the wallpaper, never the index directly
                    currentIndex: Math.max(0, sheet.wallpapers.indexOf(sheet.wallpaper))
                    activeFocusOnTab: true
                    keyNavigationEnabled: false
                    onCurrentIndexChanged: positionViewAtIndex(currentIndex, ListView.Contain)
                    function pick(i) {
                        if (i >= 0 && i < sheet.wallpapers.length)
                            sheet.choose("wallpaper", sheet.wallpapers[i]);
                    }
                    Keys.onUpPressed: pick(currentIndex - 1)
                    Keys.onDownPressed: pick(currentIndex + 1)
                    Keys.onPressed: (event) => {
                        if (event.key === Qt.Key_Home) pick(0);
                        else if (event.key === Qt.Key_End) pick(sheet.wallpapers.length - 1);
                        else return;
                        event.accepted = true;
                    }

                    delegate: Rectangle {
                        required property string modelData
                        required property int index
                        width: list.width
                        height: 16
                        color: ListView.isCurrentItem ? Theme.selection : "transparent"
                        Text {
                            x: 3
                            width: parent.width - 6
                            anchors.verticalCenter: parent.verticalCenter
                            text: modelData ? Win95Fs.fileName(modelData) : "(None)"
                            elide: Text.ElideRight
                            color: parent.ListView.isCurrentItem ? Theme.selectionText : Theme.text
                            font.family: Theme.fontFamily
                            font.pixelSize: Theme.fontSize
                        }
                        MouseArea {
                            anchors.fill: parent
                            onPressed: {
                                list.forceActiveFocus();
                                list.pick(index);
                            }
                        }
                    }

                    WheelHandler { onWheel: (event) => listBar.scrollBy(-event.angleDelta.y / 120 * 48) }
                }
                ScrollBar95 {
                    id: listBar
                    anchors.right: parent.right
                    anchors.rightMargin: 2
                    y: 2
                    height: parent.height - 4
                    flickable: list
                    stepSize: 16
                }
            }

            Row {
                y: listBox.height + 6
                spacing: 8
                Button95 {
                    text: "Browse..."
                    onClicked: browseDialog.openAt(sheet.wallpaper ? Win95Fs.parentDir(sheet.wallpaper)
                                                                   : Win95Fs.homePath(), "")
                }
            }
            Row {
                y: listBox.height + 32
                spacing: 8
                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    text: "Display:"
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontSize
                }
                RadioButton95 { text: "<u>C</u>enter"; checked: sheet.mode === "center"; onClicked: sheet.choose("mode", "center") }
                RadioButton95 { text: "<u>T</u>ile"; checked: sheet.mode === "tile"; onClicked: sheet.choose("mode", "tile") }
                RadioButton95 { text: "<u>S</u>tretch"; checked: sheet.mode === "stretch"; onClicked: sheet.choose("mode", "stretch") }
            }
        }

        GroupBox95 {
            x: wallpaperGroup.width + 8
            y: wallpaperGroup.y
            width: parent.width - x
            height: wallpaperGroup.height
            title: "C<u>o</u>lor"

            Grid {
                columns: 3
                spacing: 4
                anchors.horizontalCenter: parent.horizontalCenter
                Repeater {
                    model: sheet.palette
                    delegate: Item {
                        required property string modelData
                        width: 24
                        height: 18
                        readonly property bool chosen: Qt.colorEqual(sheet.bgColor, modelData)
                        Rectangle { // selection frame
                            anchors.fill: parent
                            color: "transparent"
                            border.color: Theme.dark
                            border.width: parent.chosen ? 2 : 0
                        }
                        Bevel {
                            anchors.fill: parent
                            anchors.margins: 2
                            style: "thinSunken"
                            face: parent.modelData
                        }
                        MouseArea {
                            anchors.fill: parent
                            onClicked: sheet.choose("bgColor", parent.modelData)
                        }
                    }
                }
            }
        }
    }

    // ----------------------------------------------------- Screen Saver tab
    Item {
        // the preview monitor, running the chosen saver
        Item {
            id: saverMonitor
            width: 180
            height: 150
            anchors.horizontalCenter: parent.horizontalCenter

            Bevel { x: 10; width: 160; height: 122; style: "raised" }
            Rectangle {
                x: 22; y: 12
                width: 136; height: 98
                color: sheet.bgColor // (None) shows the desktop
                clip: true
                ScreenSaverView {
                    anchors.fill: parent
                    visible: sheet.saverId !== "none"
                    saver: sheet.saverId
                    options: sheet.saverOptions
                    running: visible && sheet.visible && sheet.currentTab === 1
                }
            }
            Rectangle { x: 156; y: 112; width: 4; height: 3; color: "#00c000" }
            Bevel { x: 70; y: 122; width: 40; height: 10; style: "raised" }
            Bevel { x: 45; y: 132; width: 90; height: 12; style: "raised" }
        }

        GroupBox95 {
            y: saverMonitor.height + 4
            width: parent.width
            height: 96
            title: "Screen Saver"

            ComboBox95 {
                id: saverBox
                width: 170
                popupMenu: sheet.popupMenu
                model: sheet.savers.map(function (s) { return s.name; })
                currentIndex: sheet.saverIndex()
                onActivated: (i) => {
                    sheet.saverId = sheet.savers[i].id;
                    sheet.dirty = true;
                }
            }
            Button95 {
                x: 180
                text: "Se<u>t</u>tings..."
                enabled: sheet.savers[saverBox.currentIndex] && sheet.savers[saverBox.currentIndex].hasSettings
                onClicked: saverSettings.open()
            }
            Button95 {
                x: 265
                text: "Pre<u>v</u>iew"
                enabled: sheet.saverId !== "none"
                onClicked: {
                    // the desktop shows what's saved, so save first
                    sheet.apply();
                    if (!Wayland95Settings.previewScreenSaver())
                        sheet.messageBox.show("Display Properties",
                                              "The screen saver can only be previewed in a running Wayland 95 desktop.",
                                              "info", ["OK"], null);
                }
            }
            Row {
                y: 36
                spacing: 6
                Text {
                    height: 21
                    verticalAlignment: Text.AlignVCenter
                    text: "<u>W</u>ait:"
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontSize
                }
                SpinBox95 {
                    width: 54
                    from: 1
                    to: 600
                    value: sheet.saverWait
                    enabled: sheet.saverId !== "none"
                    onValueModified: (v) => {
                        sheet.saverWait = v;
                        sheet.dirty = true;
                    }
                }
                Text {
                    height: 21
                    verticalAlignment: Text.AlignVCenter
                    text: "minutes"
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontSize
                }
                Item { width: 14; height: 1 }
                // your login password: the Passwords applet changes it
                CheckBox95 {
                    anchors.verticalCenter: parent.verticalCenter
                    text: "<u>P</u>assword protected"
                    enabled: sheet.saverId !== "none" && AccountPassword.canCheck
                    checked: sheet.saverPassword
                    onToggled: (on) => {
                        sheet.saverPassword = on;
                        sheet.dirty = true;
                    }
                }
            }
        }

        Text {
            anchors.bottom: parent.bottom
            width: parent.width
            wrapMode: Text.Wrap
            color: Theme.shadow
            text: "Any key or mouse movement brings your desktop back"
                  + (sheet.saverPassword ? ", once you type your login password" : "")
                  + ". Programs playing video can keep the screen saver from starting."
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontSize
        }
    }

    // ------------------------------------------------------- Appearance tab
    Item {
        // ---- the preview -------------------------------------------------
        Bevel {
            id: appearancePreview
            width: parent.width
            height: 160
            style: "sunken"
            face: sheet.bgColor
            clip: true

            PreviewBevel {
                face: sheet.colors.face
                x: 10; y: 8
                width: 230; height: 70
                PreviewTitle { colors: sheet.colors; active: false; text: "Inactive Window" }
            }

            PreviewBevel {
                face: sheet.colors.face
                x: 26; y: 32
                width: 250; height: 116
                PreviewTitle { colors: sheet.colors; active: true; text: "Active Window" }

                // menu bar: Normal / Disabled / Selected
                Rectangle {
                    x: 3; y: 20
                    width: parent.width - 6; height: 17
                    color: sheet.colors.menu
                    Row {
                        x: 4
                        anchors.verticalCenter: parent.verticalCenter
                        spacing: 10
                        Text { text: "Normal"; color: sheet.colors.menuText; font.family: Theme.fontFamily; font.pixelSize: Theme.fontSize }
                        Text { text: "Disabled"; color: Wayland95Settings.edgeColors(sheet.colors.menu).shadow; font.family: Theme.fontFamily; font.pixelSize: Theme.fontSize }
                        Rectangle {
                            width: selText.implicitWidth + 8; height: 15
                            color: sheet.colors.selection
                            Text { id: selText; anchors.centerIn: parent; text: "Selected"; color: sheet.colors.selectionText; font.family: Theme.fontFamily; font.pixelSize: Theme.fontSize }
                        }
                    }
                }
                // window area with a scrollbar
                PreviewBevel {
                    face: sheet.colors.face
                    x: 5; y: 40
                    width: parent.width - 10; height: parent.height - 45
                    raised: false
                    Rectangle {
                        x: 2; y: 2
                        width: parent.width - 4 - 14; height: parent.height - 4
                        color: sheet.colors.window
                        Text { x: 4; y: 3; text: "Window Text"; color: sheet.colors.windowText; font.family: Theme.fontFamily; font.pixelSize: Theme.fontSize }
                    }
                    Rectangle {
                        x: parent.width - 16; y: 2
                        width: 14; height: parent.height - 4
                        color: Wayland95Settings.edgeColors(sheet.colors.face).light
                        PreviewBevel { face: sheet.colors.face; width: 14; height: 14 }
                        PreviewBevel { face: sheet.colors.face; y: 18; width: 14; height: 18 }
                    }
                }
            }

            PreviewBevel {
                face: sheet.colors.face
                x: 150; y: 88
                width: 150; height: 64
                PreviewTitle { colors: sheet.colors; active: true; text: "Message Box" }
                Text { x: 10; y: 24; text: "Message Text"; color: sheet.colors.windowText; font.family: Theme.fontFamily; font.pixelSize: Theme.fontSize }
                PreviewBevel {
                    face: sheet.colors.face
                    x: 50; y: 40
                    width: 50; height: 18
                    Text { anchors.centerIn: parent; text: "OK"; color: sheet.colors.windowText; font.family: Theme.fontFamily; font.pixelSize: Theme.fontSize }
                }
            }
        }

        // ---- scheme / item / colors ---------------------------------------
        Text {
            y: appearancePreview.height + 12
            height: 21
            verticalAlignment: Text.AlignVCenter
            textFormat: Text.StyledText
            text: "<u>S</u>cheme:"
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontSize
        }
        ComboBox95 {
            x: 60
            y: appearancePreview.height + 12
            width: parent.width - x
            popupMenu: sheet.popupMenu
            model: sheet.schemeNames()
            currentIndex: Math.max(0, model.indexOf(sheet.scheme))
            onActivated: (i) => sheet.applyScheme(model[i])
        }

        GroupBox95 {
            y: appearancePreview.height + 42
            width: parent.width
            height: parent.height - y
            title: "Item"

            ComboBox95 {
                id: itemCombo
                y: 15
                width: 150
                popupMenu: sheet.popupMenu
                model: sheet.appearanceItems.map(function (it) { return it.label; })
                currentIndex: sheet.appearanceItem
                onActivated: (i) => sheet.appearanceItem = i
            }

            Row {
                id: colorRow
                x: itemCombo.width + 12
                y: 2
                spacing: 6
                readonly property var item: sheet.appearanceItems[sheet.appearanceItem]

                ColorButton {
                    label: "Color"
                    color: sheet.colorOf(colorRow.item.key)
                    onClicked: (button) => colorPicker.openAt(button, function (c) { sheet.setColor(colorRow.item.key, c); })
                }
                ColorButton {
                    visible: !!colorRow.item.key2
                    label: "Color 2"
                    color: colorRow.item.key2 ? sheet.colorOf(colorRow.item.key2) : "transparent"
                    onClicked: (button) => colorPicker.openAt(button, function (c) { sheet.setColor(colorRow.item.key2, c); })
                }
                ColorButton {
                    visible: !!colorRow.item.text
                    label: "Text"
                    color: colorRow.item.text ? sheet.colorOf(colorRow.item.text) : "transparent"
                    onClicked: (button) => colorPicker.openAt(button, function (c) { sheet.setColor(colorRow.item.text, c); })
                }
            }
        }
    }

    // --------------------------------------------------------- Settings tab
    Item {
        AppletIcon {
            kind: "display"
            size: 64
            anchors.horizontalCenter: parent.horizontalCenter
            y: 10
        }
        Column {
            y: 96
            width: parent.width
            spacing: 12

            GroupBox95 {
                width: parent.width
                height: 64
                title: "Desktop area"
                Text {
                    text: Screen.width + " by " + Screen.height + " pixels"
                          + (Screen.devicePixelRatio !== 1 ? "  (scale " + Screen.devicePixelRatio + ")" : "")
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontSize
                }
            }
            GroupBox95 {
                width: parent.width
                height: 64
                title: "Color palette"
                Text {
                    text: "True Color (32 bit)"
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontSize
                }
            }
            Text {
                width: parent.width
                wrapMode: Text.Wrap
                text: "Screen resolution comes from the display Wayland 95 runs on. When it runs in a window, resize that window to change the desktop area."
                color: Theme.text
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontSize
            }
        }
    }

    extras: [
        // not shown: just for its list of savers
        ScreenSaverView { id: saverCatalog; visible: false; running: false; saver: "none" },

        // Screen Saver > Settings...: built from saverFields
        Dialog95 {
            id: saverSettings
            property var fields: sheet.saverFields[sheet.saverId] || []
            property var edit: ({})
            title: (sheet.savers[sheet.saverIndex()] || {}).name + " Setup"
            dialogWidth: 330
            dialogHeight: 84 + fields.length * 32
            function get(key) {
                var f = fields.find(function (x) { return x.key === key; });
                return edit[key] !== undefined ? edit[key] : (f ? f.def : undefined);
            }

            function open() {
                var copy = {};
                for (var i = 0; i < fields.length; ++i) {
                    var f = fields[i];
                    copy[f.key] = sheet.saverOptions[f.key] !== undefined ? sheet.saverOptions[f.key] : f.def;
                }
                edit = copy;
                show();
            }
            function set(key, value) {
                var copy = Object.assign({}, edit);
                copy[key] = value;
                edit = copy;
            }
            function accept() {
                sheet.saverOptions = Object.assign({}, sheet.saverOptions, edit);
                sheet.dirty = true;
                close();
            }

            Column {
                x: 12
                y: 14
                spacing: 11
                Repeater {
                    model: saverSettings.fields
                    Row {
                        id: fieldRow
                        required property var modelData
                        spacing: 8
                        Text {
                            width: 110
                            height: 21
                            verticalAlignment: Text.AlignVCenter
                            text: fieldRow.modelData.label
                            font.family: Theme.fontFamily
                            font.pixelSize: Theme.fontSize
                        }
                        Loader {
                            sourceComponent: ({ slider: sliderField, spin: spinField, text: textField,
                                                choice: choiceField })[fieldRow.modelData.type]
                            property var field: fieldRow.modelData
                        }
                    }
                }
            }
            Component {
                id: sliderField
                Slider95 {
                    width: 180
                    from: parent.field.from
                    to: parent.field.to
                    stepSize: 1
                    tickCount: 10
                    value: saverSettings.get(parent.field.key)
                    onMoved: saverSettings.set(parent.field.key, value)
                }
            }
            Component {
                id: spinField
                SpinBox95 {
                    width: 54
                    from: parent.field.from
                    to: parent.field.to
                    value: saverSettings.get(parent.field.key)
                    onValueModified: (v) => saverSettings.set(parent.field.key, v)
                }
            }
            Component {
                id: textField
                TextField95 {
                    width: 180
                    text: saverSettings.get(parent.field.key)
                    onTextChanged: if (text !== saverSettings.get(parent.field.key)) saverSettings.set(parent.field.key, text)
                }
            }
            Component {
                id: choiceField
                ComboBox95 {
                    width: 120
                    popupMenu: sheet.popupMenu
                    model: parent.field.choices.map(function (c) { return c[0]; })
                    currentIndex: Math.max(0, parent.field.choices.findIndex(function (c) {
                        return c[1] === saverSettings.get(parent.field.key); }))
                    onActivated: (i) => saverSettings.set(parent.field.key, parent.field.choices[i][1])
                }
            }
            Row {
                anchors.right: parent.right
                anchors.rightMargin: 10
                anchors.bottom: parent.bottom
                anchors.bottomMargin: 10
                spacing: 6
                Button95 { text: "OK"; isDefault: true; onClicked: saverSettings.accept() }
                Button95 { text: "Cancel"; onClicked: saverSettings.close() }
            }
        },

        // the drop-down color grid behind every color button
        Item {
            id: colorPicker
            anchors.fill: parent
            visible: false
            z: 100
            property var callback: null
            readonly property var choices: [
                "#000000", "#800000", "#008000", "#808000", "#000080",
                "#800080", "#008080", "#c0c0c0", "#808080", "#ff0000",
                "#00ff00", "#ffff00", "#0000ff", "#ff00ff", "#00ffff",
                "#ffffff", "#a0a0a4", "#fffbf0", "#c0dcc0", "#a6caf0",
            ]
            function openAt(item, cb) {
                var p = item.mapToItem(colorPicker, 0, item.height);
                pickerPanel.x = Math.min(p.x, width - pickerPanel.width - 2);
                pickerPanel.y = Math.min(p.y, height - pickerPanel.height - 2);
                callback = cb;
                visible = true;
            }
            MouseArea { anchors.fill: parent; onPressed: colorPicker.visible = false }
            Bevel {
                id: pickerPanel
                width: 5 * 22 + 10
                height: 4 * 18 + 10
                style: "raised"
                Grid {
                    x: 5; y: 5
                    columns: 5
                    Repeater {
                        model: colorPicker.choices
                        delegate: Item {
                            required property string modelData
                            width: 22; height: 18
                            Bevel { anchors.fill: parent; anchors.margins: 2; style: "thinSunken"; face: parent.modelData }
                            MouseArea {
                                anchors.fill: parent
                                onClicked: {
                                    colorPicker.visible = false;
                                    if (colorPicker.callback)
                                        colorPicker.callback(parent.modelData);
                                }
                            }
                        }
                    }
                }
            }
        },
        FileDialog95 {
            id: browseDialog
            title: "Browse"
            messageBox: sheet.messageBox
            popupMenu: sheet.popupMenu
            defaultSuffix: ""
            filters: [
                { label: "Pictures (*.png;*.jpg;*.bmp;*.webp)",
                  patterns: ["*.png", "*.jpg", "*.jpeg", "*.bmp", "*.webp"] },
                { label: "All Files (*.*)", patterns: ["*"] },
            ]
            onAccepted: (path) => sheet.choose("wallpaper", path)
        }
    ]
}
