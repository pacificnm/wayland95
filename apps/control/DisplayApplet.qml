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
        displayPage.applyDisplays();
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
    // The displays (monitors): where each is, which is the main one (the
    // taskbar's), and each one's resolution, refresh rate, scale and
    // orientation. Applied by the compositor when written (Displays); then
    // "Keep these settings?" puts the old ones back unless it's answered.
    Item {
        id: displayPage

        // a working copy of Displays.outputs, edited here
        property var items: []
        property int current: 0
        property bool edited: false
        readonly property var cur: items.length > current ? items[current] : null
        readonly property var scales: [1, 1.25, 1.5, 1.75, 2, 2.5, 3]
        readonly property var turns: [0, 90, 180, 270]
        // sizes offered where a display takes any (a window, a virtual one)
        readonly property var anySizes: [[800, 600], [1024, 768], [1280, 720], [1280, 800], [1280, 1024], [1366, 768],
                                         [1440, 900], [1600, 900], [1680, 1050], [1920, 1080], [1920, 1200], [2560, 1440]]

        function reset() {
            items = Displays.outputs.map(function (o) { return Object.assign({}, o); });
            if (current >= items.length)
                current = 0;
            edited = false;
            sync();
        }
        Component.onCompleted: reset()
        Connections {
            target: Displays
            function onChanged() { if (!displayPage.edited) displayPage.reset(); }
        }

        function change(key, value) {
            var copy = items.map(function (o) { return Object.assign({}, o); });
            copy[current][key] = value;
            items = copy;
            touched();
        }
        function touched() {
            edited = true;
            sheet.dirty = true;
            sync();
        }
        // a display's size on the desktop, in its points
        function logicalSize(o) {
            var w = o.width / o.scale, h = o.height / o.scale;
            return (o.transform === 90 || o.transform === 270) ? Qt.size(Math.round(h), Math.round(w))
                                                               : Qt.size(Math.round(w), Math.round(h));
        }
        function sizes(o) { // [[w, h]...], largest first
            var out = [], seen = {};
            var from = o.modes.length ? o.modes.map(function (m) { return [m.width, m.height]; }) : anySizes.concat([[o.width, o.height]]);
            for (var i = 0; i < from.length; ++i) {
                var k = from[i][0] + "x" + from[i][1];
                if (!seen[k]) { seen[k] = true; out.push(from[i]); }
            }
            out.sort(function (a, b) { return b[0] * b[1] - a[0] * a[1] || b[0] - a[0]; });
            return out;
        }
        function rates(o) { // refresh rates (mHz) for its chosen size, highest first
            var out = [];
            for (var i = 0; i < o.modes.length; ++i)
                if (o.modes[i].width === o.width && o.modes[i].height === o.height && out.indexOf(o.modes[i].refresh) < 0)
                    out.push(o.modes[i].refresh);
            out.sort(function (a, b) { return b - a; });
            return out;
        }
        function rateText(mhz) {
            var hz = mhz / 1000;
            return (Math.abs(hz - Math.round(hz)) < 0.05 ? Math.round(hz) : hz.toFixed(2)) + " Hz";
        }
        function setSize(w, h) {
            var copy = items.map(function (o) { return Object.assign({}, o); });
            var o = copy[current];
            o.width = w;
            o.height = h;
            var r = rates(o);
            if (r.length && r.indexOf(o.refresh) < 0)
                o.refresh = r[0];
            items = copy;
            pack();
            touched();
        }
        function setPrimary() {
            var copy = items.map(function (o) { return Object.assign({}, o); });
            for (var i = 0; i < copy.length; ++i)
                copy[i].primary = i === current;
            items = copy;
            touched();
        }
        function enabledCount() {
            var n = 0;
            for (var i = 0; i < items.length; ++i)
                if (items[i].enabled) n++;
            return n;
        }
        // Displays side by side with no gaps or overlaps, in the order
        // they're in from left to right (after a size, scale or turn
        // changed, or one was switched on or off)
        function pack() {
            var copy = items.map(function (o) { return Object.assign({}, o); });
            var on = copy.filter(function (o) { return o.enabled; });
            var row = on.every(function (o) { return o.y === on[0].y; });
            if (row) {
                on.sort(function (a, b) { return a.x - b.x; });
                var x = 0;
                for (var i = 0; i < on.length; ++i) { on[i].x = x; on[i].y = 0; x += logicalSize(on[i]).width; }
            } else {
                // stacked or mixed: against the first, as they were
                var minX = Math.min.apply(null, on.map(function (o) { return o.x; }));
                var minY = Math.min.apply(null, on.map(function (o) { return o.y; }));
                for (var k = 0; k < on.length; ++k) { on[k].x -= minX; on[k].y -= minY; }
            }
            items = copy;
        }
        // one dropped at x, y (desktop points): against the nearest side of
        // another, then everything from 0,0
        function drop(index, x, y) {
            var copy = items.map(function (o) { return Object.assign({}, o); });
            var me = copy[index], size = logicalSize(me), best = null;
            for (var i = 0; i < copy.length; ++i) {
                if (i === index || !copy[i].enabled)
                    continue;
                var o = copy[i], os = logicalSize(o);
                var spots = [
                    { x: o.x + os.width, y: o.y }, { x: o.x - size.width, y: o.y },
                    { x: o.x, y: o.y + os.height }, { x: o.x, y: o.y - size.height },
                ];
                for (var k = 0; k < spots.length; ++k) {
                    var d = Math.abs(spots[k].x - x) + Math.abs(spots[k].y - y);
                    if (!best || d < best.d)
                        best = { d: d, x: spots[k].x, y: spots[k].y };
                }
            }
            if (!best)
                return;
            me.x = best.x;
            me.y = best.y;
            var on = copy.filter(function (o) { return o.enabled; });
            var minX = Math.min.apply(null, on.map(function (o) { return o.x; }));
            var minY = Math.min.apply(null, on.map(function (o) { return o.y; }));
            for (var j = 0; j < on.length; ++j) { on[j].x -= minX; on[j].y -= minY; }
            items = copy;
            touched();
        }
        function settings() {
            return items.map(function (o) {
                return { name: o.name, enabled: o.enabled, primary: o.primary, width: o.width, height: o.height,
                         refresh: o.refresh, scale: o.scale, transform: o.transform, x: o.x, y: o.y,
                         adaptiveSync: o.adaptiveSync };
            });
        }
        // the boxes show the chosen display's settings
        function sync() {
            if (!cur)
                return;
            displayBox.currentIndex = current;
            var s = sizes(cur);
            sizeBox.model = s.map(function (wh) { return wh[0] + " by " + wh[1] + " pixels"; });
            for (var i = 0; i < s.length; ++i)
                if (s[i][0] === cur.width && s[i][1] === cur.height)
                    sizeBox.currentIndex = i;
            var r = rates(cur);
            rateBox.model = r.length ? r.map(rateText) : [rateText(cur.refresh || 60000)];
            rateBox.currentIndex = Math.max(0, r.indexOf(cur.refresh));
            var best = 0;
            for (var k = 0; k < scales.length; ++k)
                if (Math.abs(scales[k] - cur.scale) < Math.abs(scales[best] - cur.scale))
                    best = k;
            scaleBox.currentIndex = best;
            turnBox.currentIndex = Math.max(0, turns.indexOf(cur.transform));
            // (set, not bound: a click leaves a check box's own idea in it)
            useBox.checked = cur.enabled;
            mainBox.checked = cur.primary;
            syncBox.checked = cur.adaptiveSync;
        }
        onCurrentChanged: sync()

        Text {
            visible: !Displays.available
            width: parent.width
            y: 20
            wrapMode: Text.Wrap
            text: "Display settings are available when Wayland 95 is your desktop (they're its compositor's). "
                  + "This desktop area is " + Screen.width + " by " + Screen.height + " pixels."
            color: Theme.text
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontSize
        }

        Item {
            anchors.fill: parent
            visible: Displays.available && displayPage.cur !== null

            // --- the picture: drag a display to where it stands ---------
            Rectangle {
                id: picture
                width: parent.width
                height: 112
                color: Theme.window
                Bevel { anchors.fill: parent; style: "sunken"; filled: false }

                // the desktop's extent, and the picture's scale
                readonly property var on: displayPage.items.filter(function (o) { return o.enabled; })
                readonly property real fullW: Math.max(1, Math.max.apply(null, on.map(function (o) { return o.x + displayPage.logicalSize(o).width; }).concat([1])))
                readonly property real fullH: Math.max(1, Math.max.apply(null, on.map(function (o) { return o.y + displayPage.logicalSize(o).height; }).concat([1])))
                readonly property real factor: Math.min((width - 60) / fullW, (height - 28) / fullH)
                readonly property real originX: (width - fullW * factor) / 2
                readonly property real originY: (height - fullH * factor) / 2

                Repeater {
                    model: displayPage.items
                    Rectangle {
                        id: monitor
                        required property var modelData
                        required property int index
                        readonly property size logical: displayPage.logicalSize(modelData)
                        visible: modelData.enabled
                        x: picture.originX + modelData.x * picture.factor
                        y: picture.originY + modelData.y * picture.factor
                        width: Math.max(16, logical.width * picture.factor)
                        height: Math.max(12, logical.height * picture.factor)
                        color: index === displayPage.current ? Theme.selection : Theme.face
                        border.color: Theme.text
                        border.width: 1
                        z: dragArea.drag.active ? 2 : 1
                        Text {
                            anchors.centerIn: parent
                            text: modelData.number
                            color: index === displayPage.current ? Theme.selectionText : Theme.text
                            font.family: Theme.fontFamily
                            font.pixelSize: Math.max(11, Math.min(28, parent.height * 0.5))
                            font.bold: true
                        }
                        // the main display has the taskbar
                        Rectangle {
                            visible: modelData.primary
                            anchors.left: parent.left; anchors.right: parent.right; anchors.bottom: parent.bottom
                            anchors.margins: 1
                            height: Math.max(2, parent.height * 0.1)
                            color: Theme.shadow
                        }
                        MouseArea {
                            id: dragArea
                            anchors.fill: parent
                            drag.target: displayPage.enabledCount() > 1 ? monitor : null
                            drag.threshold: 3
                            onPressed: displayPage.current = monitor.index
                            onReleased: {
                                if (!drag.active)
                                    return;
                                displayPage.drop(monitor.index, (monitor.x - picture.originX) / picture.factor,
                                                 (monitor.y - picture.originY) / picture.factor);
                            }
                        }
                    }
                }
            }
            Text {
                y: picture.height + 4
                width: parent.width
                visible: displayPage.enabledCount() > 1
                text: "Drag a display to where it stands on your desk."
                color: Theme.text
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontSize
            }

            Text {
                x: 0; y: 142
                text: "<u>D</u>isplay:"
                textFormat: Text.StyledText
                color: Theme.text
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontSize
            }
            ComboBox95 {
                id: displayBox
                x: 60; y: 138
                width: parent.width - 60
                popupMenu: sheet.popupMenu
                model: displayPage.items.map(function (o) { return o.label; })
                onActivated: (index) => displayPage.current = index
            }
            CheckBox95 {
                id: useBox
                x: 0; y: 166
                text: "<u>U</u>se this display"
                // (not the last one off)
                enabled: displayPage.cur && (!displayPage.cur.enabled || displayPage.enabledCount() > 1)
                onToggled: (on) => {
                    var wasMain = displayPage.cur.primary;
                    displayPage.change("enabled", on);
                    if (!on && wasMain) {
                        // the taskbar needs a display that's on
                        var copy = displayPage.items.map(function (o) { return Object.assign({}, o); });
                        var given = false;
                        for (var i = 0; i < copy.length; ++i) {
                            copy[i].primary = !given && copy[i].enabled;
                            given = given || copy[i].primary;
                        }
                        displayPage.items = copy;
                    }
                    if (on) {
                        // to the right of the others
                        var c2 = displayPage.items.map(function (o) { return Object.assign({}, o); });
                        var right = 0;
                        for (var k = 0; k < c2.length; ++k)
                            if (k !== displayPage.current && c2[k].enabled)
                                right = Math.max(right, c2[k].x + displayPage.logicalSize(c2[k]).width);
                        c2[displayPage.current].x = right;
                        c2[displayPage.current].y = 0;
                        displayPage.items = c2;
                    }
                    displayPage.pack();
                    displayPage.touched();
                }
            }
            CheckBox95 {
                id: mainBox
                x: 180; y: 166
                text: "<u>M</u>ain display (taskbar)"
                enabled: displayPage.cur && displayPage.cur.enabled && !displayPage.cur.primary
                onToggled: displayPage.setPrimary()
            }

            GroupBox95 {
                x: 0; y: 192
                width: parent.width * 0.58 - 4
                height: 54
                title: "Desktop area"
                ComboBox95 {
                    id: sizeBox
                    width: parent.width
                    popupMenu: sheet.popupMenu
                    enabled: displayPage.cur && displayPage.cur.enabled
                    onActivated: (index) => {
                        var s = displayPage.sizes(displayPage.cur)[index];
                        displayPage.setSize(s[0], s[1]);
                    }
                }
            }
            GroupBox95 {
                x: parent.width * 0.58 + 4; y: 192
                width: parent.width * 0.42 - 4
                height: 54
                title: "Refresh rate"
                ComboBox95 {
                    id: rateBox
                    width: parent.width
                    popupMenu: sheet.popupMenu
                    enabled: displayPage.cur && displayPage.cur.enabled && displayPage.rates(displayPage.cur).length > 1
                    onActivated: (index) => displayPage.change("refresh", displayPage.rates(displayPage.cur)[index])
                }
            }
            GroupBox95 {
                x: 0; y: 250
                width: parent.width * 0.42 - 4
                height: 54
                title: "Size of text and windows"
                ComboBox95 {
                    id: scaleBox
                    width: parent.width
                    popupMenu: sheet.popupMenu
                    enabled: displayPage.cur && displayPage.cur.enabled
                    model: displayPage.scales.map(function (s) { return Math.round(s * 100) + "%"; })
                    onActivated: (index) => { displayPage.change("scale", displayPage.scales[index]); displayPage.pack(); }
                }
            }
            GroupBox95 {
                x: parent.width * 0.42 + 4; y: 250
                width: parent.width * 0.58 - 4
                height: 54
                title: "Orientation"
                ComboBox95 {
                    id: turnBox
                    width: parent.width
                    popupMenu: sheet.popupMenu
                    enabled: displayPage.cur && displayPage.cur.enabled
                    model: ["Landscape", "Portrait (turned right)", "Landscape (upside down)", "Portrait (turned left)"]
                    onActivated: (index) => { displayPage.change("transform", displayPage.turns[index]); displayPage.pack(); }
                }
            }
            CheckBox95 {
                id: syncBox
                x: 0; y: 310
                visible: displayPage.cur ? displayPage.cur.adaptiveSyncSupported : false
                text: "<u>A</u>daptive sync (variable refresh rate)"
                enabled: displayPage.cur && displayPage.cur.enabled
                onToggled: (on) => displayPage.change("adaptiveSync", on)
            }
        }

        // --- "Keep these settings?": the old ones come back unless it's
        // answered (a mode the display can't show leaves nothing to click)
        property int keepSeconds: 0
        property bool confirming: false
        property bool closeAfter: false
        function applyDisplays() {
            if (!edited || !Displays.available)
                return;
            Displays.apply(settings());
            edited = false;
            confirming = true;
            keepSeconds = 15;
            keepTimer.restart();
            askKeep();
        }
        function askKeep() {
            sheet.messageBox.show("Display Properties",
                "Do you want to keep these display settings?\n\nThe settings before come back in "
                + keepSeconds + (keepSeconds === 1 ? " second." : " seconds."), "question", ["Yes", "No"],
                function (index) { displayPage.answered(index === 0); });
        }
        function answered(keep) {
            if (!confirming)
                return;
            confirming = false;
            keepTimer.stop();
            if (!keep) {
                Displays.revert();
                console.info("Display settings: not kept; the settings before are back");
            } else {
                console.info("Display settings: kept");
            }
            edited = false;
            if (closeAfter)
                sheet.close();
        }
        Timer {
            id: keepTimer
            interval: 1000
            repeat: true
            onTriggered: {
                displayPage.keepSeconds -= 1;
                if (displayPage.keepSeconds <= 0) {
                    sheet.messageBox.visible = false;
                    sheet.messageBox.callback = null;
                    displayPage.answered(false);
                } else {
                    sheet.messageBox.message = "Do you want to keep these display settings?\n\nThe settings before come back in "
                        + displayPage.keepSeconds + (displayPage.keepSeconds === 1 ? " second." : " seconds.");
                }
            }
        }
    }
    // (OK applies and closes: not before the displays' question is answered)
    onClosing: (close) => {
        if (displayPage.confirming) {
            close.accepted = false;
            displayPage.closeAfter = true;
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
