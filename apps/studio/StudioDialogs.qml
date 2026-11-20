import QtQuick
import Win95Ui
import Win95App

// The Studio's dialog boxes (drawn inside its window): Color, a list to
// pick from, a text box for several lines, Database, Project Properties
// and the Menu Editor. Each takes a function to call with the answer.
Item {
    id: dialogs

    anchors.fill: parent
    z: 1500

    property Menu95 popupMenu
    property MessageBox95 messageBox
    // the project's folder ("" until it's saved) and its forms' names
    property string projectDir: ""
    property var formNames: []
    // (the Graph Editor's preview reads the project's database: its tables, where it is, its APIs)
    property var projectTables: []
    property var projectDatabase: ({})
    property var projectApis: []

    readonly property bool open: colorDialog.visible || listDialog.visible || textDialog.visible || iconDialog.visible
                                 || databaseDialog.visible || projectDialog.visible || menuEditor.visible || importDialog.visible
                                 || apiDialog.visible || endpointDialog.visible || graphEditor.visible

    // a picture file: the Studio's window has the file dialog
    signal pictureWanted(var callback)
    function pickPicture(callback) { pictureWanted(callback); }
    // a sound or video file
    signal mediaWanted(var callback)
    function pickMedia(callback) { mediaWanted(callback); }

    function pickColor(value, callback) {
        colorDialog.callback = callback;
        colorDialog.value = value || "";
        hexField.text = value || "";
        colorDialog.show();
    }
    // one of Wayland 95's icons (Icon95): its name, or "" for none
    function pickIcon(current, callback) {
        iconDialog.callback = callback;
        iconDialog.index = Math.max(0, iconDialog.indexOf(current || ""));
        iconDialog.show();
        iconGrid.positionViewAtIndex(iconDialog.index, GridView.Contain);
        iconGrid.forceActiveFocus();
    }
    // items: [{ value, label }]
    function pickFromList(title, items, current, callback) {
        listDialog.title = title;
        listDialog.items = items;
        listDialog.callback = callback;
        listDialog.index = -1;
        for (var i = 0; i < items.length; ++i) {
            if (items[i].value === current)
                listDialog.index = i;
        }
        listDialog.show();
        listView.positionViewAtIndex(Math.max(0, listDialog.index), ListView.Center);
        listView.forceActiveFocus();
    }
    function editText(title, text, callback) {
        textDialog.title = title;
        textDialog.callback = callback;
        textArea.text = text;
        textDialog.show();
        textArea.forceActiveFocus();
    }
    function editDatabase(config, callback) {
        databaseDialog.callback = callback;
        databaseDialog.driver = config.driver || "sqlite";
        dbFile.text = config.file || "";
        dbHost.text = config.host || "";
        dbPort.text = config.port ? String(config.port) : "";
        dbName.text = config.name || "";
        dbUser.text = config.user || "";
        dbPassword.text = config.password || "";
        databaseDialog.result = "";
        databaseDialog.show();
    }
    // the program's name, title, version and startup form; and `ai`: the
    // AI Chat's Anthropic API key ({ apiKey })
    function editProject(app, ai, callback) {
        projectDialog.callback = callback;
        appName.text = app.name || "";
        appTitle.text = app.title || "";
        appVersion.text = app.version || "";
        aiKey.text = (ai && ai.apiKey) || "";
        projectDialog.testResult = "";
        startCombo.currentIndex = Math.max(0, formNames.indexOf(app.startForm));
        projectDialog.show();
        appName.forceActiveFocus();
    }

    // a Menu's menus or a ToolBar's buttons (kind: "menu" or "toolbar")
    function editItems(kind, items, callback) {
        menuEditor.kind = kind;
        menuEditor.callback = callback;
        var rows = [];
        if (kind === "menu") {
            (items || []).forEach(function (m) {
                rows.push(menuEditor.row(m, 0));
                (m.items || []).forEach(function (it) { rows.push(menuEditor.row(it, 1)); });
            });
        } else {
            (items || []).forEach(function (b) { rows.push(menuEditor.row(b, 0)); });
        }
        menuEditor.rows = rows;
        menuEditor.select(rows.length ? 0 : -1);
        if (rows.length === 0)
            menuEditor.insert(false, false);
        menuEditor.show();
        itemCaption.input.forceActiveFocus();
    }

    // a Graph's settings, in the Graph Editor (Type, Display, Data, Stock, and a preview)
    function editGraph(settings, callback) {
        graphEditor.callback = callback;
        graphEditor.g = Object.assign({}, graphEditor.defaults, JSON.parse(JSON.stringify(settings || {})));
        var list = (graphEditor.g.series || []).slice(0, 4);
        while (list.length < 4)
            list.push(null);
        graphEditor.rows = list;
        // (a stock graph's lines over the prices, and its panes under them: so many rows each)
        var overs = (graphEditor.g.overlays || []).map(String).slice(0, 3);
        while (overs.length < 3)
            overs.push("");
        graphEditor.overlayRows = overs;
        var studies = (graphEditor.g.studies || []).map(String);
        graphEditor.volumePane = studies.some(function (x) { return /^vol/i.test(x); });
        var unders = studies.filter(function (x) { return !/^vol/i.test(x); }).slice(0, 2);
        while (unders.length < 2)
            unders.push("");
        graphEditor.studyRows = unders;
        graphEditor.openDatabase();
        graphTabs.currentIndex = 0;
        graphEditor.show();
        graphEditor.load();
        graphTabs.forceActiveFocus();
    }

    // the tables a database has that the project doesn't: which to bring in
    // (callback(tables, makeForms))
    function chooseImport(where, tables, notes, callback) {
        importDialog.where = where;
        importDialog.tables = tables;
        importDialog.picked = tables.map(function () { return true; });
        importDialog.notes = notes;
        importDialog.callback = callback;
        importForms.checked = true;
        importDialog.show();
    }

    // the project's web APIs (connections and their sign-in): callback(apis)
    function editApis(apis, callback) {
        apiDialog.apis = JSON.parse(JSON.stringify(apis || []));
        apiDialog.callback = callback;
        apiDialog.result = "";
        apiDialog.select(apiDialog.apis.length ? 0 : -1);
        apiDialog.show();
    }
    // an endpoint of one of them, as a table: callback(table, makeForm)
    function addApiTable(apis, taken, callback) {
        endpointDialog.apis = apis;
        endpointDialog.taken = taken;
        endpointDialog.callback = callback;
        endpointDialog.read = null;
        endpointDialog.result = "";
        endpointApi.currentIndex = 0;
        endpointPath.text = "";
        endpointRecords.text = "";
        endpointQuery.text = "";
        endpointName.text = "";
        endpointForm.checked = true;
        endpointDialog.show();
        endpointPath.input.forceActiveFocus();
    }

    // --- Color ---------------------------------------------------------------
    Dialog95 {
        id: colorDialog
        title: "Color"
        dialogWidth: 236
        dialogHeight: 268
        property var callback: null
        property string value: ""
        readonly property var basic: [
            "#ff8080", "#ffff80", "#80ff80", "#00ff80", "#80ffff", "#0080ff", "#ff80c0", "#ff80ff",
            "#ff0000", "#ffff00", "#80ff00", "#00ff40", "#00ffff", "#0080c0", "#8080c0", "#ff00ff",
            "#804040", "#ff8040", "#00ff00", "#008080", "#004080", "#8080ff", "#800040", "#ff0080",
            "#800000", "#ff8000", "#008000", "#008040", "#0000ff", "#0000a0", "#800080", "#8000ff",
            "#400000", "#804000", "#004000", "#004040", "#000080", "#000040", "#400040", "#400080",
            "#000000", "#808000", "#808040", "#808080", "#408080", "#c0c0c0", "#dfdfdf", "#ffffff",
        ]
        function answer(color) {
            var cb = callback;
            callback = null;
            close();
            if (cb)
                cb(color);
        }
        function accept() {
            var typed = hexField.text.trim();
            if (typed !== "" && !StudioTools.isColor(typed)) {
                dialogs.messageBox.show("Color", "\"" + typed + "\" isn't a color. Use a name like red, or #rrggbb.",
                                        "warning", ["OK"], function () { hexField.forceActiveFocus(); });
                return;
            }
            answer(typed);
        }

        Label95 { x: 10; y: 8; text: "Basic colors:" }
        Grid {
            x: 10
            y: 28
            columns: 8
            spacing: 5
            Repeater {
                model: colorDialog.basic
                delegate: Item {
                    id: cell
                    required property string modelData
                    width: 22
                    height: 18
                    Bevel { anchors.fill: parent; style: "sunken"; face: cell.modelData }
                    Rectangle {
                        visible: hexField.text.toLowerCase() === cell.modelData
                        anchors.fill: parent
                        anchors.margins: -2
                        color: "transparent"
                        border.color: Theme.text
                        border.width: 1
                    }
                    MouseArea {
                        anchors.fill: parent
                        onClicked: hexField.text = cell.modelData
                        onDoubleClicked: colorDialog.answer(cell.modelData)
                    }
                }
            }
        }
        Label95 { x: 10; y: 172; height: 21; text: "Color:" }
        TextField95 {
            id: hexField
            x: 52
            y: 172
            width: 96
            onAccepted: colorDialog.accept()
        }
        Bevel {
            x: 156
            y: 172
            width: 64
            height: 21
            style: "sunken"
            face: hexField.text.trim() !== "" && StudioTools.isColor(hexField.text.trim()) ? hexField.text.trim() : Theme.face
        }
        Label95 {
            x: 10
            y: 196
            color: Theme.shadow
            text: "Blank: the desktop's own color."
        }
        Button95 { x: 10; y: 216; width: 66; text: "OK"; isDefault: true; onClicked: colorDialog.accept() }
        Button95 { x: 82; y: 216; width: 66; text: "Cancel"; onClicked: colorDialog.close() }
        Button95 { x: 154; y: 216; width: 66; text: "Default"; onClicked: colorDialog.answer("") }
    }

    // --- the Icon chooser: Wayland 95's icons, as a grid --------------------------
    Dialog95 {
        id: iconDialog
        // (over the other dialogs: the Menu Editor opens it)
        z: 1600
        title: "Choose an Icon"
        dialogWidth: 436
        dialogHeight: 352
        // (None) first, then icons.json's, in its order
        readonly property var items: [{ name: "", label: "(None)" }].concat(StudioTools.icons())
        property int index: 0
        property var callback: null
        readonly property int columns: Math.max(1, Math.floor(iconGrid.width / iconGrid.cellWidth))
        function indexOf(name) {
            for (var i = 0; i < items.length; ++i) {
                if (items[i].name === name)
                    return i;
            }
            return -1;
        }
        function accept() {
            var cb = callback, name = items[index].name;
            callback = null;
            close();
            if (cb)
                cb(name);
        }
        function step(by) {
            index = Math.max(0, Math.min(items.length - 1, index + by));
            iconGrid.positionViewAtIndex(index, GridView.Contain);
        }

        Bevel { id: iconWell; x: 10; y: 10; width: iconDialog.dialogWidth - 26; height: 270; style: "sunken"; face: Theme.window }
        GridView {
            id: iconGrid
            x: 12
            y: 12
            width: iconWell.width - 4 - (iconBar.needed ? iconBar.width : 0)
            height: iconWell.height - 4
            clip: true
            boundsBehavior: Flickable.StopAtBounds
            cellWidth: 66
            cellHeight: 58
            model: iconDialog.items
            delegate: Item {
                id: tile
                required property var modelData
                required property int index
                readonly property bool chosen: index === iconDialog.index
                width: iconGrid.cellWidth
                height: iconGrid.cellHeight
                Rectangle {
                    // the icon framed while it's the one chosen
                    x: (parent.width - 40) / 2; y: 3; width: 40; height: 38
                    color: tile.chosen ? Theme.selection : "transparent"
                    Rectangle { anchors.fill: parent; anchors.margins: 2; color: Theme.face }
                }
                Icon95 { x: (parent.width - 32) / 2; y: 6; size: 32; name: tile.modelData.name }
                Label95 {
                    visible: tile.modelData.name === ""
                    x: 0; y: 3; width: parent.width; height: 38
                    horizontalAlignment: Text.AlignHCenter
                    color: Theme.shadow
                    text: "none"
                }
                Rectangle {
                    x: (parent.width - caption.implicitWidth) / 2 - 2; y: 42
                    width: Math.min(parent.width, caption.implicitWidth + 4); height: 15
                    color: tile.chosen ? Theme.selection : "transparent"
                }
                Label95 {
                    id: caption
                    x: 0; y: 42; width: parent.width; height: 15
                    horizontalAlignment: Text.AlignHCenter
                    elide: Text.ElideRight
                    text: tile.modelData.label
                    color: tile.chosen ? Theme.selectionText : Theme.text
                }
                MouseArea {
                    anchors.fill: parent
                    onPressed: { iconDialog.index = tile.index; iconGrid.forceActiveFocus(); }
                    onDoubleClicked: iconDialog.accept()
                }
            }
            Keys.onPressed: (event) => {
                if (event.key === Qt.Key_Right) iconDialog.step(1);
                else if (event.key === Qt.Key_Left) iconDialog.step(-1);
                else if (event.key === Qt.Key_Down) iconDialog.step(iconDialog.columns);
                else if (event.key === Qt.Key_Up) iconDialog.step(-iconDialog.columns);
                else if (event.key === Qt.Key_Home) iconDialog.step(-iconDialog.items.length);
                else if (event.key === Qt.Key_End) iconDialog.step(iconDialog.items.length);
                else if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter) iconDialog.accept();
                else if (event.key === Qt.Key_Escape) iconDialog.close();
                else return;
                event.accepted = true;
            }
        }
        ScrollBar95 {
            id: iconBar
            x: iconWell.x + iconWell.width - width - 2; y: 12
            height: iconWell.height - 4
            flickable: iconGrid
            stepSize: iconGrid.cellHeight
        }
        Label95 {
            x: 12; y: 288; height: 21
            text: iconDialog.items[iconDialog.index].name === "" ? "No icon." : "Icon: " + iconDialog.items[iconDialog.index].label
                  + " (" + iconDialog.items[iconDialog.index].name + ")"
        }
        Button95 { x: iconDialog.dialogWidth - 168; y: 318; width: 75; text: "OK"; isDefault: true; onClicked: iconDialog.accept() }
        Button95 { x: iconDialog.dialogWidth - 88; y: 318; width: 75; text: "Cancel"; onClicked: iconDialog.close() }
    }

    // --- a list to pick from --------------------------------------------------
    Dialog95 {
        id: listDialog
        dialogWidth: 340
        dialogHeight: 330
        property var items: []
        property int index: -1
        property var callback: null
        // a letter typed: the next item that starts with it
        function jumpTo(text) {
            if (!text || text.trim() === "")
                return false;
            var key = text.toLowerCase();
            for (var n = 1; n <= items.length; ++n) {
                var i = (index + n) % items.length;
                if (String(items[i].label).toLowerCase().indexOf(key) === 0) {
                    index = i;
                    return true;
                }
            }
            return true;
        }
        function accept() {
            if (index < 0 || index >= items.length)
                return;
            var cb = callback, value = items[index].value;
            callback = null;
            close();
            if (cb)
                cb(value);
        }

        Bevel { x: 10; y: 10; width: listDialog.dialogWidth - 26; height: 252; style: "sunken"; face: Theme.window }
        ListView {
            id: listView
            x: 12
            y: 12
            width: listDialog.dialogWidth - 30 - (listBar.needed ? listBar.width : 0)
            height: 248
            clip: true
            boundsBehavior: Flickable.StopAtBounds
            model: listDialog.items
            delegate: Rectangle {
                id: listRow
                required property var modelData
                required property int index
                width: listView.width
                height: 17
                color: index === listDialog.index ? Theme.selection : "transparent"
                Label95 {
                    x: 4
                    width: parent.width - 8
                    height: parent.height
                    elide: Text.ElideRight
                    text: listRow.modelData.label
                    color: listRow.index === listDialog.index ? Theme.selectionText : Theme.text
                }
                MouseArea {
                    anchors.fill: parent
                    onPressed: listDialog.index = listRow.index
                    onDoubleClicked: listDialog.accept()
                }
            }
            Keys.onPressed: (event) => {
                if (event.key === Qt.Key_Down)
                    listDialog.index = Math.min(listDialog.items.length - 1, listDialog.index + 1);
                else if (event.key === Qt.Key_Up)
                    listDialog.index = Math.max(0, listDialog.index - 1);
                else if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter)
                    listDialog.accept();
                else if (event.key === Qt.Key_Escape)
                    listDialog.close();
                else if (!listDialog.jumpTo(event.text))
                    return;
                positionViewAtIndex(listDialog.index, ListView.Contain);
                event.accepted = true;
            }
        }
        ScrollBar95 { id: listBar; flickable: listView; x: listDialog.dialogWidth - 18 - width; y: 12; height: 248 }
        Button95 { x: listDialog.dialogWidth - 162; y: 272; width: 70; text: "OK"; isDefault: true; onClicked: listDialog.accept() }
        Button95 { x: listDialog.dialogWidth - 86; y: 272; width: 70; text: "Cancel"; onClicked: listDialog.close() }
    }

    // --- several lines of text -------------------------------------------------
    Dialog95 {
        id: textDialog
        dialogWidth: 380
        dialogHeight: 290
        property var callback: null
        function accept() {
            var cb = callback;
            callback = null;
            close();
            if (cb)
                cb(textArea.text);
        }

        Bevel { x: 10; y: 10; width: 354; height: 212; style: "sunken"; face: Theme.window }
        Flickable {
            id: textFlick
            x: 13
            y: 13
            width: 348 - (textBar.needed ? textBar.width : 0)
            height: 206
            clip: true
            contentWidth: width
            contentHeight: Math.max(height, textArea.contentHeight + 4)
            boundsBehavior: Flickable.StopAtBounds
            TextEdit {
                id: textArea
                width: textFlick.width
                height: textFlick.contentHeight
                leftPadding: 2
                selectByMouse: true
                wrapMode: TextEdit.Wrap
                textFormat: TextEdit.PlainText
                color: Theme.text
                selectionColor: Theme.selection
                selectedTextColor: Theme.selectionText
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontSize
                onCursorRectangleChanged: {
                    if (cursorRectangle.y < textFlick.contentY)
                        textFlick.contentY = cursorRectangle.y;
                    else if (cursorRectangle.y + cursorRectangle.height > textFlick.contentY + textFlick.height)
                        textFlick.contentY = cursorRectangle.y + cursorRectangle.height - textFlick.height;
                }
            }
        }
        ScrollBar95 { id: textBar; flickable: textFlick; x: 362 - width; y: 12; height: 208 }
        Button95 { x: 218; y: 232; width: 70; text: "OK"; onClicked: textDialog.accept() }
        Button95 { x: 294; y: 232; width: 70; text: "Cancel"; onClicked: textDialog.close() }
    }

    // --- Database --------------------------------------------------------------
    AppDatabase { id: probe }
    Dialog95 {
        id: databaseDialog
        title: "Database"
        dialogWidth: 400
        dialogHeight: 330
        property var callback: null
        property string driver: "sqlite"
        property string result: ""
        property bool resultOk: false
        readonly property var available: probe.driversAvailable()
        readonly property bool server: driver !== "sqlite"

        function config() {
            var c = { driver: driver };
            if (driver === "sqlite") {
                c.file = dbFile.text.trim() || "data.db";
            } else {
                c.host = dbHost.text.trim();
                c.port = parseInt(dbPort.text, 10) || 0;
                c.name = dbName.text.trim();
                c.user = dbUser.text.trim();
                c.password = dbPassword.text;
            }
            return c;
        }
        function test() {
            if (driver === "sqlite" && dialogs.projectDir === "" && !/^(\/|~\/)/.test(dbFile.text.trim())) {
                resultOk = true;
                result = "The file will be made beside the project once it's saved.";
                return;
            }
            resultOk = probe.open(config(), dialogs.projectDir);
            result = resultOk ? "Connected." : probe.lastError;
            probe.close();
        }
        function accept() {
            var cb = callback;
            callback = null;
            close();
            if (cb)
                cb(config());
        }

        Label95 { x: 12; y: 10; text: "Keep this app's tables in:" }
        RadioButton95 {
            x: 20; y: 32; width: 360; height: 18
            text: "SQLite: a file of its own (nothing to set up)"
            checked: databaseDialog.driver === "sqlite"
            onClicked: databaseDialog.driver = "sqlite"
        }
        RadioButton95 {
            x: 20; y: 52; width: 360; height: 18
            text: "MySQL or MariaDB" + (databaseDialog.available.mysql ? "" : "  (Qt's driver isn't installed: libqt6sql6-mysql)")
            checked: databaseDialog.driver === "mysql"
            onClicked: databaseDialog.driver = "mysql"
        }
        RadioButton95 {
            x: 20; y: 72; width: 360; height: 18
            text: "PostgreSQL" + (databaseDialog.available.postgresql ? "" : "  (Qt's driver isn't installed: libqt6sql6-psql)")
            checked: databaseDialog.driver === "postgresql"
            onClicked: databaseDialog.driver = "postgresql"
        }

        Item {
            visible: !databaseDialog.server
            x: 12
            y: 104
            Label95 { y: 0; height: 21; text: "File:" }
            TextField95 { id: dbFile; x: 76; width: 296 }
            Label95 {
                x: 76; y: 26; width: 296
                wrapMode: Text.Wrap
                color: Theme.shadow
                text: "A name alone is a file beside the project; a program made from it keeps the file in ~/.local/share/<its name>."
            }
        }
        Item {
            visible: databaseDialog.server
            x: 12
            y: 104
            Label95 { y: 0; height: 21; text: "Host:" }
            TextField95 { id: dbHost; x: 76; width: 180 }
            Label95 { x: 268; y: 0; height: 21; text: "Port:" }
            TextField95 { id: dbPort; x: 304; width: 68 }
            Label95 { y: 28; height: 21; text: "Database:" }
            TextField95 { id: dbName; x: 76; y: 28; width: 180 }
            Label95 { y: 56; height: 21; text: "User:" }
            TextField95 { id: dbUser; x: 76; y: 56; width: 180 }
            Label95 { y: 84; height: 21; text: "Password:" }
            TextField95 { id: dbPassword; x: 76; y: 84; width: 180; input.echoMode: TextInput.Password }
            Label95 {
                x: 0; y: 112; width: 372
                wrapMode: Text.Wrap
                color: Theme.shadow
                text: "Blank Host: the server on this computer, as you (how Control Panel > Services sets accounts up). "
                      + "A password is kept in the project file and in programs made from it."
            }
        }

        Label95 {
            x: 12; y: 258; width: 372; height: 14
            elide: Text.ElideRight
            text: databaseDialog.result
            color: databaseDialog.resultOk ? "#006000" : "#c00000"
        }
        Button95 { x: 12; y: 278; width: 110; text: "Test Connection"; onClicked: databaseDialog.test() }
        Button95 { x: 238; y: 278; width: 70; text: "OK"; isDefault: true; onClicked: databaseDialog.accept() }
        Button95 { x: 314; y: 278; width: 70; text: "Cancel"; onClicked: databaseDialog.close() }
    }

    // --- Project Properties ----------------------------------------------------
    Dialog95 {
        id: projectDialog
        title: "Project Properties"
        dialogWidth: 340
        dialogHeight: 330
        property var callback: null
        property string testResult: ""
        property bool testOk: false
        function accept() {
            var name = appName.text.trim();
            if (!/^[a-z][a-z0-9_-]*$/.test(name)) {
                dialogs.messageBox.show("Project Properties",
                    "The program's name starts with a small letter and has only small letters, digits, - and _ in it "
                    + "(it names the program's file and its data folder).", "warning", ["OK"],
                    function () { appName.forceActiveFocus(); });
                return;
            }
            var cb = callback;
            callback = null;
            close();
            if (cb)
                cb({ name: name, title: appTitle.text.trim(), version: appVersion.text.trim(),
                     startForm: dialogs.formNames[startCombo.currentIndex] || "" },
                   aiKey.text.trim() !== "" ? { apiKey: aiKey.text.trim() } : null);
        }

        Label95 { x: 12; y: 14; height: 21; text: "Program name:" }
        TextField95 { id: appName; x: 110; y: 14; width: 210; onAccepted: projectDialog.accept() }
        Label95 { x: 12; y: 44; height: 21; text: "Title:" }
        TextField95 { id: appTitle; x: 110; y: 44; width: 210; onAccepted: projectDialog.accept() }
        Label95 { x: 12; y: 74; height: 21; text: "Version:" }
        TextField95 { id: appVersion; x: 110; y: 74; width: 100; onAccepted: projectDialog.accept() }
        Label95 { x: 12; y: 104; height: 21; text: "Startup form:" }
        ComboBox95 { id: startCombo; x: 110; y: 104; width: 210; model: dialogs.formNames; popupMenu: dialogs.popupMenu }

        // the AI Chat's key: the app's, never asked of the person using it
        Rectangle { x: 12; y: 140; width: 308; height: 1; color: Theme.shadow }
        Rectangle { x: 12; y: 141; width: 308; height: 1; color: Theme.highlight }
        Label95 { x: 12; y: 150; height: 17; text: "AI Chat (Claude)"; font.bold: true }
        Label95 { x: 12; y: 174; height: 21; text: "API key:" }
        TextField95 {
            id: aiKey
            x: 110; y: 174; width: 150
            // (dots, but for an environment variable's name, or while it's being typed)
            input.echoMode: activeFocus || text.indexOf("${env:") === 0 ? TextInput.Normal : TextInput.Password
            onAccepted: projectDialog.accept()
            onTextChanged: projectDialog.testResult = ""
        }
        Button95 {
            x: 266; y: 174; width: 54; text: "Test"
            onClicked: {
                projectDialog.testOk = false;
                projectDialog.testResult = "Asking Anthropic…";
                keyTester.testKey();
            }
        }
        AiClient {
            id: keyTester
            apiKey: aiKey.text.trim()
            onTested: (ok, message) => {
                projectDialog.testOk = ok;
                projectDialog.testResult = message;
            }
        }
        Label95 {
            x: 110; y: 200; width: 210
            wrapMode: Text.Wrap
            color: projectDialog.testResult === "" ? Theme.shadow : projectDialog.testOk ? "#008000" : "#c00000"
            text: projectDialog.testResult !== "" ? projectDialog.testResult
                : "For every AI Chat in the app. Blank: ANTHROPIC_API_KEY where it runs; ${env:NAME} another variable. "
                  + "A key typed here is kept in the project and inside programs made from it."
        }
        Button95 { x: 174; y: 274; width: 70; text: "OK"; isDefault: true; onClicked: projectDialog.accept() }
        Button95 { x: 250; y: 274; width: 70; text: "Cancel"; onClicked: projectDialog.close() }
    }

    // --- Graph Editor: a Graph's type, how it looks, and what it shows --------
    AppDatabase { id: graphDb }
    Dialog95 {
        id: graphEditor
        title: "Graph Editor"
        dialogWidth: 640
        dialogHeight: 446
        property var callback: null
        // the settings being edited, and the four Values rows (null: not used)
        property var g: ({})
        property var rows: [null, null, null, null]
        // (a stock graph's: "SMA 20" or "", and its panes under the prices)
        property var overlayRows: ["", "", ""]
        property var studyRows: ["", ""]
        property bool volumePane: false
        property bool loading: false
        readonly property var defaults: ({
            type: "Column", title: "", legend: "Right", xTitle: "", yTitle: "", gridLines: true, showValues: false,
            stacked: false, markers: true, smooth: false, palette: "Windows", yMin: "", yMax: "", decimals: -1,
            prefix: "", suffix: "", source: "table", table: "", labelField: "", groupDates: "", series: [],
            filterField: "", filterOp: "=", filterValue: "", sort: "", limit: 0,
            overlays: [], studies: [], candleColors: "Classic", lastPrice: true, showLast: 0,
            openField: "", highField: "", lowField: "", closeField: "", volumeField: ""
        })
        readonly property var stockTypes: ["Candlestick", "Heikin-Ashi", "OHLC", "HLC", "Price"]
        readonly property bool stock: stockTypes.indexOf(g.type) >= 0
        // what's drawn: the settings with the Values rows that are used
        readonly property var settings: {
            var s = Object.assign({}, g);
            s.series = rows.filter(function (r) { return r !== null; });
            s.overlays = overlayRows.filter(function (x) { return x !== ""; });
            s.studies = (volumePane ? ["Volume"] : []).concat(studyRows.filter(function (x) { return x !== ""; }));
            return s;
        }
        function setIndicator(which, i, text) {
            if (loading)
                return;
            var list = (which === "overlays" ? overlayRows : studyRows).slice();
            list[i] = text;
            if (which === "overlays")
                overlayRows = list;
            else
                studyRows = list;
        }

        function set(key, value) {
            if (loading)
                return;
            var s = Object.assign({}, g);
            s[key] = value;
            if (key === "table") {
                // (another table: its own fields)
                var names = fieldsOf(value).map(function (f) { return f.name; });
                if (names.indexOf(s.labelField) < 0) s.labelField = "";
                if (names.indexOf(s.filterField) < 0) s.filterField = "";
                ["openField", "highField", "lowField", "closeField", "volumeField"].forEach(function (k) {
                    if (names.indexOf(s[k]) < 0) s[k] = "";
                });
                rows = rows.map(function (r) { return r && r.field && names.indexOf(r.field) < 0 ? null : r; });
            }
            if (key === "labelField")
                s.groupDates = isDate(s.table, value) ? (s.groupDates || (stockTypes.indexOf(s.type) >= 0 ? "day" : "month")) : "";
            g = s;
            if (key === "table" || key === "labelField")
                Qt.callLater(load);
        }
        function setRow(i, pick, aggregate) {
            if (loading)
                return;
            var list = rows.slice(), old = list[i];
            if (pick === "")
                list[i] = null;
            else if (pick === "*")
                list[i] = Object.assign({}, old || {}, { field: "", aggregate: "count" });
            else
                list[i] = Object.assign({}, old || {}, { field: pick, aggregate: aggregate || (old && old.field ? old.aggregate : "sum") || "sum" });
            rows = list;
        }
        function rowPick(r) { return !r ? "" : (!r.field && r.aggregate === "count") ? "*" : r.field; }

        function fieldsOf(table) {
            var t = (dialogs.projectTables || []).filter(function (x) { return x.name === table; })[0];
            return t ? [{ name: "id", type: "integer" }].concat(t.fields || []) : [];
        }
        function isDate(table, field) {
            var f = fieldsOf(table).filter(function (x) { return x.name === field; })[0];
            return !!f && (f.type === "date" || f.type === "datetime");
        }
        readonly property var numericTypes: ["integer", "decimal", "float", "currency", "number", "boolean"]

        // every control shows what the settings say
        function load() {
            loading = true;
            (function walk(item) {
                for (var i = 0; i < item.children.length; ++i) {
                    var c = item.children[i];
                    if (typeof c.sync === "function")
                        c.sync();
                    walk(c);
                }
            })(graphTabs);
            loading = false;
        }

        // the project's database, to show the graph with its records
        function openDatabase() {
            graphDb.close();
            var cfg = dialogs.projectDatabase || {};
            var file = cfg.file || "";
            if ((cfg.driver || "sqlite") === "sqlite") {
                if (file === "" || dialogs.projectDir === "")
                    return;
                if (file.charAt(0) !== "/")
                    file = dialogs.projectDir + "/" + file;
                if (!Win95Fs.isFile(file))
                    return;
            }
            graphDb.setApis(dialogs.projectApis || []);
            graphDb.setSchema(dialogs.projectTables || []);
            graphDb.open(cfg, dialogs.projectDir);
        }
        function finish(ok) {
            var cb = callback, s = settings;
            callback = null;
            graphDb.close();
            close();
            // only what isn't as it starts, in the project
            var out = {};
            for (var k in s) {
                if (k === "series" ? s.series.length > 0 : JSON.stringify(s[k]) !== JSON.stringify(defaults[k]))
                    out[k] = s[k];
            }
            if (ok && cb)
                cb(out);
        }
        onClosed: graphDb.close()

        // a list whose items each set one setting
        component Pick: ComboBox95 {
            property string key
            property var values: []
            width: 190
            popupMenu: dialogs.popupMenu
            onActivated: (i) => graphEditor.set(key, values[i])
            function sync() { currentIndex = Math.max(0, values.indexOf(graphEditor.g[key])); }
        }
        component Tick: CheckBox95 {
            property string key
            onToggled: (on) => graphEditor.set(key, on)
            function sync() { checked = !!graphEditor.g[key]; }
        }
        component Entry: TextField95 {
            property string key
            property bool numeric: false
            width: 190
            // (a count: blank is 0, all of them)
            readonly property bool count: key === "limit" || key === "showLast"
            onTextChanged: graphEditor.set(key, numeric ? (text.trim() === "" ? (count ? 0 : "") : Number(text) || 0) : text)
            function sync() {
                var v = graphEditor.g[key];
                text = v === undefined || v === null || (count && v === 0) ? "" : String(v);
            }
        }
        component Caption: Label95 { height: 21 }
        // a line over a stock graph's prices, or a pane under them: what it is, and its numbers (periods)
        component IndicatorRow: Item {
            id: indicatorRow
            property string which: "overlays"
            property int slot: 0
            property var kinds: []
            readonly property var numbers: ({ SMA: "20", EMA: "20", Bollinger: "20 2", VWAP: "", RSI: "14", MACD: "12 26 9", Stochastic: "14 3" })
            width: 214
            height: 22
            function write() {
                var kind = kinds[kindBox.currentIndex] || "";
                graphEditor.setIndicator(which, slot, kind === "" ? "" : (kind + " " + argsBox.text.trim()).trim());
            }
            function sync() {
                var words = String((which === "overlays" ? graphEditor.overlayRows : graphEditor.studyRows)[slot] || "").split(/\s+/);
                kindBox.currentIndex = Math.max(0, kinds.indexOf(words[0] || ""));
                argsBox.text = words.slice(1).join(" ");
            }
            ComboBox95 {
                id: kindBox
                width: 120
                popupMenu: dialogs.popupMenu
                model: indicatorRow.kinds.map(function (k) { return k === "" ? "(none)" : k; })
                onActivated: (i) => {
                    var loading = graphEditor.loading;
                    graphEditor.loading = true;
                    argsBox.text = indicatorRow.numbers[indicatorRow.kinds[i]] || "";
                    graphEditor.loading = loading;
                    indicatorRow.write();
                }
            }
            TextField95 {
                id: argsBox
                x: 126
                width: 88
                enabled: kindBox.currentIndex > 0 && indicatorRow.kinds[kindBox.currentIndex] !== "VWAP"
                onTextChanged: if (!graphEditor.loading) indicatorRow.write()
            }
        }

        TabWidget95 {
            id: graphTabs
            x: 12
            y: 12
            width: 352
            height: 360
            titles: ["Type", "Display", "Data", "Stock"]

            // --- Type: a picture of each, and how it's drawn
            Item {
                Grid {
                    id: typeGrid
                    columns: 4
                    spacing: 6
                    Repeater {
                        model: ["Column", "Bar", "Line", "Area", "Pie", "Doughnut", "Scatter",
                                "Candlestick", "Heikin-Ashi", "OHLC", "HLC", "Price"]
                        delegate: Item {
                            id: tile
                            required property string modelData
                            readonly property bool chosen: graphEditor.g.type === modelData
                            readonly property bool stock: graphEditor.stockTypes.indexOf(modelData) >= 0
                            width: 78
                            height: 62
                            Bevel {
                                anchors.fill: parent
                                style: tile.chosen ? "pressed" : "raised"
                                face: tile.chosen ? Theme.light : Theme.face
                            }
                            Graph95 {
                                x: 6; y: 4; width: 66; height: 37
                                type: tile.modelData
                                labels: tile.stock ? ["a", "b", "c", "d", "e", "f", "g", "h"] : ["a", "b", "c", "d"]
                                series: tile.stock ? [{ name: "a", open: [4, 5, 6.5, 6, 5, 6, 7, 8.2], high: [5.4, 7, 7.2, 6.4, 6.3, 7.4, 8.6, 9],
                                                        low: [3.6, 4.6, 5.6, 4.6, 4.4, 5.6, 6.6, 7.6], close: [5, 6.6, 6, 5, 6, 7, 8.2, 8.8] }]
                                                    : [{ name: "a", values: [3, 5, 2, 4] }, { name: "b", values: [2, 3, 4, 3] }]
                                candleColors: graphEditor.g.candleColors || "Classic"
                                lastPrice: false
                                stacked: graphEditor.g.stacked && (tile.modelData === "Column" || tile.modelData === "Bar" || tile.modelData === "Area")
                                legend: "None"
                                axisLabels: false
                                gridLines: false
                                tips: false
                                enabled: false
                                palette: graphEditor.g.palette || "Windows"
                            }
                            Label95 {
                                y: 41; width: parent.width; height: 20
                                horizontalAlignment: Text.AlignHCenter
                                text: tile.modelData
                                font.bold: tile.chosen
                            }
                            MouseArea {
                                anchors.fill: parent
                                onClicked: graphEditor.set("type", tile.modelData)
                                onDoubleClicked: graphTabs.currentIndex = 2
                            }
                        }
                    }
                }
                Tick { key: "stacked"; y: typeGrid.height + 10; text: "Stacked (Column, Bar, Area)" }
                Tick { key: "markers"; y: typeGrid.height + 32; text: "Markers on the points (Line)" }
                Tick { key: "smooth"; y: typeGrid.height + 54; text: "Smooth lines (Line, Area)" }
                Label95 {
                    y: typeGrid.height + 80; width: parent.width
                    wrapMode: Text.Wrap
                    color: Theme.shadow
                    text: graphEditor.stock ? "A stock graph: a bar for each label, its open, high, low and close (Data), with lines and panes from Stock."
                                            : "A Pie or a Doughnut shows the first Values, a slice for each label. A Scatter's labels are numbers."
                }
            }

            // --- Display: titles, the legend, colors, the value axis, how values are written
            Item {
                Caption { y: 0; text: "Title:" }
                Entry { key: "title"; x: 120; y: 0 }
                Caption { y: 28; text: "Legend:" }
                Pick { key: "legend"; x: 120; y: 28; model: ["None", "Right", "Bottom", "Top"]; values: model }
                Caption { y: 56; text: "Across (X) title:" }
                Entry { key: "xTitle"; x: 120; y: 56 }
                Caption { y: 84; text: "Up (Y) title:" }
                Entry { key: "yTitle"; x: 120; y: 84 }
                Caption { y: 112; text: "Colors:" }
                Pick { key: "palette"; x: 120; y: 112; model: ["Windows", "Bright", "Pastel", "Grays"]; values: model }
                Caption { y: 140; text: "Values from:" }
                Entry { key: "yMin"; x: 120; y: 140; width: 70; numeric: true }
                Caption { x: 198; y: 140; text: "to:" }
                Entry { key: "yMax"; x: 220; y: 140; width: 90; numeric: true }
                Caption { y: 168; text: "Decimals:" }
                Pick { key: "decimals"; x: 120; y: 168; width: 70; model: ["Auto", "0", "1", "2", "3"]; values: [-1, 0, 1, 2, 3] }
                Caption { y: 196; text: "Before, after:" }
                Entry { key: "prefix"; x: 120; y: 196; width: 70 }
                Entry { key: "suffix"; x: 198; y: 196; width: 70 }
                Tick { key: "gridLines"; y: 228; text: "Grid lines" }
                Tick { key: "showValues"; y: 252; text: "Values on the graph (a pie: percents)" }
                Label95 {
                    y: 280; width: parent.width
                    wrapMode: Text.Wrap
                    color: Theme.shadow
                    text: "Blank values from and to: from the data. Before and after go round each value: $ before, % or kg after."
                }
            }

            // --- Data: a table's records, grouped by a label, summed up
            Item {
                id: dataPage
                readonly property var fields: graphEditor.fieldsOf(graphEditor.g.table)
                readonly property var numeric: fields.filter(function (f) { return graphEditor.numericTypes.indexOf(f.type) >= 0; })
                readonly property bool fromTable: graphEditor.g.source !== "code"

                RadioButton95 {
                    y: 0; text: "A table's records"
                    checked: dataPage.fromTable
                    onClicked: graphEditor.set("source", "table")
                }
                RadioButton95 {
                    x: 150; y: 0; text: "What code gives it"
                    checked: !dataPage.fromTable
                    onClicked: graphEditor.set("source", "code")
                }
                Item {
                    y: 28
                    width: parent.width
                    height: parent.height - 28
                    visible: dataPage.fromTable
                    Caption { y: 0; text: "Table:" }
                    Pick {
                        key: "table"; x: 96; y: 0; width: 214
                        model: [""].concat((dialogs.projectTables || []).map(function (t) { return t.name; }))
                        values: model
                    }
                    Caption { y: 28; text: "Labels:" }
                    Pick {
                        key: "labelField"; x: 96; y: 28; width: 214
                        model: ["(all the records together)"].concat(dataPage.fields.map(function (f) { return f.name; }))
                        values: [""].concat(dataPage.fields.map(function (f) { return f.name; }))
                    }
                    Caption { y: 56; text: "Dates by:"; visible: graphEditor.isDate(graphEditor.g.table, graphEditor.g.labelField) }
                    Pick {
                        key: "groupDates"; x: 96; y: 56; width: 214
                        visible: graphEditor.isDate(graphEditor.g.table, graphEditor.g.labelField)
                        model: ["Each date", "Minute", "Hour", "Day", "Week", "Month", "Year"]; values: ["", "minute", "hour", "day", "week", "month", "year"]
                    }
                    Caption { y: 84; text: "Values:"; visible: !graphEditor.stock }
                    // (a stock graph: the fields of each bar, or just a price)
                    Repeater {
                        model: [["closeField", "Close (price):"], ["openField", "Open:"], ["highField", "High:"],
                                ["lowField", "Low:"], ["volumeField", "Volume:"]]
                        delegate: Item {
                            id: stockRow
                            required property var modelData
                            required property int index
                            visible: graphEditor.stock
                            y: 84 + index * 26
                            width: parent.width
                            height: 22
                            Caption { text: stockRow.modelData[1] }
                            Pick {
                                key: stockRow.modelData[0]; x: 96; width: 214
                                model: [stockRow.index === 0 ? "(pick one)" : stockRow.index === 4 ? "(none)" : "(from the prices)"]
                                       .concat(dataPage.numeric.map(function (f) { return f.name; }))
                                values: [""].concat(dataPage.numeric.map(function (f) { return f.name; }))
                            }
                        }
                    }
                    Repeater {
                        model: 4
                        delegate: Item {
                            id: valueRow
                            required property int index
                            visible: !graphEditor.stock
                            y: 84 + index * 26
                            x: 96
                            width: 214
                            height: 22
                            readonly property var row: graphEditor.rows[index]
                            ComboBox95 {
                                id: valueField
                                width: 120
                                popupMenu: dialogs.popupMenu
                                readonly property var values: ["", "*"].concat(dataPage.numeric.map(function (f) { return f.name; }))
                                model: ["(not used)", "(how many records)"].concat(dataPage.numeric.map(function (f) { return f.name; }))
                                onActivated: (i) => graphEditor.setRow(valueRow.index, values[i])
                                function sync() { currentIndex = Math.max(0, values.indexOf(graphEditor.rowPick(graphEditor.rows[valueRow.index]))); }
                            }
                            ComboBox95 {
                                id: valueHow
                                x: 126
                                width: 88
                                popupMenu: dialogs.popupMenu
                                enabled: !!valueRow.row && !!valueRow.row.field
                                readonly property var values: ["sum", "average", "min", "max", "count"]
                                model: ["Sum", "Average", "Least", "Most", "Count"]
                                onActivated: (i) => graphEditor.setRow(valueRow.index, graphEditor.rowPick(valueRow.row), values[i])
                                function sync() { currentIndex = Math.max(0, values.indexOf(valueRow.row ? valueRow.row.aggregate : "sum")); }
                            }
                        }
                    }
                    Caption { y: graphEditor.stock ? 222 : 192; text: "Only where:" }
                    Pick {
                        key: "filterField"; x: 96; y: graphEditor.stock ? 222 : 192; width: 100
                        model: ["(all records)"].concat(dataPage.fields.map(function (f) { return f.name; }))
                        values: [""].concat(dataPage.fields.map(function (f) { return f.name; }))
                    }
                    Pick {
                        key: "filterOp"; x: 200; y: graphEditor.stock ? 222 : 192; width: 44
                        enabled: graphEditor.g.filterField !== ""
                        model: ["=", "≠", ">", "≥", "<", "≤", "has", "is blank", "isn't blank"]
                        values: ["=", "!=", ">", ">=", "<", "<=", "contains", "empty", "notEmpty"]
                    }
                    Entry {
                        key: "filterValue"; x: 248; y: graphEditor.stock ? 222 : 192; width: 62
                        enabled: graphEditor.g.filterField !== "" && graphEditor.g.filterOp !== "empty" && graphEditor.g.filterOp !== "notEmpty"
                    }
                    Caption { y: 220; text: "In order:"; visible: !graphEditor.stock }
                    Pick {
                        key: "sort"; x: 96; y: 220; width: 214
                        visible: !graphEditor.stock
                        model: ["As they come", "Labels A to Z", "Labels Z to A", "Values, smallest first", "Values, biggest first"]
                        values: ["", "label", "labelDesc", "value", "valueDesc"]
                    }
                    Caption { y: 248; text: "Only the first:"; visible: !graphEditor.stock }
                    Entry { key: "limit"; x: 96; y: 248; width: 60; numeric: true; visible: !graphEditor.stock }
                    Caption { x: 162; y: 248; text: "(blank: all of them)"; color: Theme.shadow; visible: !graphEditor.stock }
                    Label95 {
                        y: 252; width: parent.width
                        visible: graphEditor.stock
                        wrapMode: Text.Wrap
                        color: Theme.shadow
                        text: "A bar for each label (a record each, without one), in time order. Just a price: the trades of each are made into a bar."
                    }
                }
                Label95 {
                    y: 28; width: parent.width
                    visible: !dataPage.fromTable
                    wrapMode: Text.Wrap
                    text: graphEditor.stock
                          ? "The form's code gives the graph its data:\n\n"
                            + "graph1.setData([\"3 Mar\", \"4 Mar\"], [{ name: \"ACME\",\n"
                            + "    open: [10, 11], high: [11.5, 11.8],\n"
                            + "    low: [9.8, 10.6], close: [11, 11.2],\n"
                            + "    volume: [1200, 900] }])\n\n"
                            + "or a trade at a time, as they happen:\n\n"
                            + "graph1.tick(\"10:31\", 101.25, 300)\n\n"
                            + "(the same label: the same bar)."
                          : "The form's code gives the graph its data:\n\n"
                            + "graph1.setData([\"Jan\", \"Feb\", \"Mar\"],\n"
                            + "    [{ name: \"Sales\", values: [3, 5, 2] }])\n\n"
                            + "(a list of numbers is one series). The labels go across, the values up."
                }
            }

            // --- Stock: the candles, lines over the prices, panes under them
            Item {
                enabled: graphEditor.stock
                Caption { y: 0; text: "Candles:" }
                Pick {
                    key: "candleColors"; x: 120; y: 0; width: 214
                    model: ["White up, black down", "Green up, red down"]; values: ["Classic", "Green and red"]
                }
                Caption { y: 30; text: "Over the prices:" }
                Repeater {
                    model: 3
                    delegate: IndicatorRow {
                        required property int index
                        x: 120; y: 30 + index * 26
                        which: "overlays"; slot: index
                        kinds: ["", "SMA", "EMA", "Bollinger", "VWAP"]
                    }
                }
                Caption { y: 112; text: "Under them:" }
                CheckBox95 {
                    x: 120; y: 112
                    text: "Volume"
                    onToggled: (on) => { if (!graphEditor.loading) graphEditor.volumePane = on; }
                    function sync() { checked = graphEditor.volumePane; }
                }
                Repeater {
                    model: 2
                    delegate: IndicatorRow {
                        required property int index
                        x: 120; y: 136 + index * 26
                        which: "studies"; slot: index
                        kinds: ["", "RSI", "MACD", "Stochastic"]
                    }
                }
                Tick { key: "lastPrice"; y: 194; text: "The last price: a line across, and on the axis" }
                Caption { y: 220; text: "Show the latest:" }
                Entry { key: "showLast"; x: 120; y: 220; width: 60; numeric: true }
                Caption { x: 186; y: 220; text: "bars (blank: all)"; color: Theme.shadow }
                Label95 {
                    y: 250; width: parent.width
                    wrapMode: Text.Wrap
                    color: Theme.shadow
                    text: graphEditor.stock
                          ? "The numbers are periods: SMA 20 averages the last 20 closes. As it runs, the wheel zooms, dragging goes back in time, and a double-click shows the latest again."
                          : "For Candlestick, Heikin-Ashi, OHLC, HLC and Price graphs."
                }
            }
        }

        // what it looks like (the records, if the database has them)
        Label95 { x: 376; y: 12; height: 18; text: graphDb.isOpen && graphEditor.g.table && dataPage.fromTable ? "Preview (its records):" : "Preview (made-up values):" }
        Bevel {
            x: 376; y: 32; width: 252; height: 340
            style: "sunken"
            AppGraph {
                x: 2; y: 2; width: parent.width - 4; height: parent.height - 4
                graph: graphEditor.settings
                database: graphDb.isOpen ? graphDb : null
            }
        }
        Button95 { x: 482; y: 384; width: 70; text: "OK"; isDefault: true; onClicked: graphEditor.finish(true) }
        Button95 { x: 558; y: 384; width: 70; text: "Cancel"; onClicked: graphEditor.finish(false) }
    }

    // --- Menu Editor: a Menu's menus and items, or a ToolBar's buttons -------
    Dialog95 {
        id: menuEditor
        readonly property bool isMenu: kind === "menu"
        title: isMenu ? "Menu Editor" : "Menu Editor - Tool Bar Buttons"
        dialogWidth: 480
        dialogHeight: 452
        property string kind: "menu"
        property var callback: null
        // the items in a list: { level (0 a menu, 1 its item), text, name,
        // shortcut, icon, glyph, picture, toolTip, action, form, enabled, checked, separator }
        property var rows: []
        property int current: -1
        property bool loading: false
        readonly property var here: current >= 0 && current < rows.length ? rows[current] : null

        readonly property var actions: [
            { value: "", label: "(none: its code only)" },
            { value: "openForm", label: "Open a form" },
            { value: "closeForm", label: "Close this form" },
            { value: "newRecord", label: "New record" },
            { value: "save", label: "Save the record" },
            { value: "deleteRecord", label: "Delete the record (asks first)" },
            { value: "first", label: "First record" },
            { value: "previous", label: "Previous record" },
            { value: "next", label: "Next record" },
            { value: "last", label: "Last record" },
            { value: "quit", label: "Quit the program" },
        ]
        function actionIndex(value) {
            for (var i = 0; i < actions.length; ++i) {
                if (actions[i].value === (value || ""))
                    return i;
            }
            return 0;
        }

        function row(it, level) {
            return { level: level, text: it.text || "", name: it.name || "", shortcut: it.shortcut || "",
                     icon: it.icon || "", glyph: it.glyph || "", picture: it.picture || "", toolTip: it.toolTip || "",
                     action: it.action || "", form: it.form || "", enabled: it.enabled !== false,
                     checked: it.checked === true, separator: it.separator === true };
        }
        function plain(t) { return String(t || "").replace(/&(.)/g, "$1"); }

        // the fields show the current row
        function select(i) {
            current = i;
            loading = true;
            var r = here || row({}, 0);
            itemCaption.text = r.text;
            itemName.text = r.name;
            itemShortcut.text = r.shortcut;
            itemGlyph.text = r.glyph;
            itemPicture.text = r.picture;
            itemTip.text = r.toolTip;
            itemAction.currentIndex = actionIndex(r.action);
            itemForm.currentIndex = Math.max(0, dialogs.formNames.indexOf(r.form));
            itemEnabled.checked = r.enabled;
            itemChecked.checked = r.checked;
            loading = false;
            if (i >= 0)
                itemList.positionViewAtIndex(i, ListView.Contain);
        }
        // a field edited: into the current row
        function store(key, value) {
            if (loading || !here)
                return;
            var list = rows.slice();
            var r = Object.assign({}, list[current]);
            r[key] = value;
            if (key === "action" && value === "openForm" && !r.form)
                r.form = dialogs.formNames[itemForm.currentIndex] || "";
            list[current] = r;
            rows = list;
        }
        function insert(separator, after) {
            var level = isMenu ? (here ? here.level : (rows.length ? 1 : 0)) : 0;
            if (separator)
                level = isMenu ? 1 : 0;
            var r = row({ separator: separator }, level);
            var list = rows.slice(), at = here ? current + (after ? 1 : 0) : list.length;
            list.splice(at, 0, r);
            rows = list;
            select(at);
            if (!separator)
                itemCaption.input.forceActiveFocus();
        }
        // Next: to the item after, or a new one at the end
        function next() {
            if (current >= 0 && current < rows.length - 1) {
                select(current + 1);
            } else {
                var level = here ? here.level : 0;
                rows = rows.concat([row({}, isMenu && rows.length ? Math.max(1, level) : 0)]);
                select(rows.length - 1);
            }
            itemCaption.input.forceActiveFocus();
        }
        function remove() {
            if (!here)
                return;
            var list = rows.slice();
            list.splice(current, 1);
            rows = list;
            select(Math.min(current, list.length - 1));
        }
        function move(by) {
            var to = current + by;
            if (!here || to < 0 || to >= rows.length)
                return;
            var list = rows.slice(), r = list.splice(current, 1)[0];
            list.splice(to, 0, r);
            rows = list;
            select(to);
        }
        function indent(level) {
            if (!here || !isMenu || here.separator)
                return;
            store("level", level);
        }

        function camel(s) {
            var words = plain(s).replace(/[^A-Za-z0-9 ]/g, " ").trim().split(/\s+/).filter(function (w) { return w !== ""; });
            return words.map(function (w, i) {
                return i === 0 ? w.charAt(0).toLowerCase() + w.substring(1) : w.charAt(0).toUpperCase() + w.substring(1);
            }).join("");
        }
        function accept() {
            var list = rows.filter(function (r) {
                return r.separator || r.text.trim() !== "" || (!isMenu && (r.icon !== "" || r.glyph !== "" || r.picture !== ""));
            });
            if (isMenu && list.length && list[0].level !== 0) {
                dialogs.messageBox.show(title, "The first one has to be a menu (all the way left): its items go under it, indented (→).",
                                        "warning", ["OK"], null);
                return;
            }
            // names: blank ones made from the captions; no two the same
            var taken = {}, menuName = "", problem = "";
            list.forEach(function (r) {
                if (r.separator)
                    return;
                var name = r.name.trim();
                if (name !== "" && !/^[A-Za-z_][A-Za-z0-9_]*$/.test(name))
                    problem = "\"" + name + "\" can't be a name: letters, digits and _, starting with a letter.";
                else if (name !== "" && taken[name])
                    problem = "Two have the name " + name + ": code tells them apart by it.";
                if (name !== "")
                    taken[name] = true;
            });
            if (problem !== "") {
                dialogs.messageBox.show(title, problem, "warning", ["OK"], null);
                return;
            }
            var out = [], menu = null;
            list.forEach(function (r) {
                var it;
                if (r.separator) {
                    it = { separator: true };
                } else {
                    var name = r.name.trim();
                    if (name === "") {
                        var base = isMenu ? (r.level === 0 ? camel(r.text) + "Menu" : menuName + camel(" " + r.text).replace(/^./, function (ch) { return ch.toUpperCase(); }))
                                          : (camel(r.text || r.toolTip) || "tool") + "Button";
                        name = base || "item";
                        for (var n = 2; taken[name]; ++n)
                            name = base + n;
                        taken[name] = true;
                    }
                    it = { text: r.text, name: name };
                    if (isMenu && r.shortcut.trim() !== "")
                        it.shortcut = r.shortcut.trim();
                    if (!isMenu && r.icon !== "")
                        it.icon = r.icon;
                    if (!isMenu && r.glyph !== "")
                        it.glyph = r.glyph;
                    if (!isMenu && r.picture !== "")
                        it.picture = r.picture;
                    if (!isMenu && r.toolTip !== "")
                        it.toolTip = r.toolTip;
                    if ((!isMenu || r.level === 1) && r.action !== "") {
                        it.action = r.action;
                        if (r.action === "openForm")
                            it.form = r.form;
                    }
                    if (!r.enabled)
                        it.enabled = false;
                    if (r.checked && (!isMenu || r.level === 1))
                        it.checked = true;
                }
                if (isMenu && r.level === 0) {
                    menu = it;
                    menu.items = [];
                    menuName = it.name.replace(/Menu$/, "");
                    out.push(menu);
                } else if (isMenu) {
                    menu.items.push(it);
                } else {
                    out.push(it);
                }
            });
            var cb = callback;
            callback = null;
            close();
            if (cb)
                cb(out);
        }

        readonly property bool itemRow: here !== null && !here.separator && (!isMenu || here.level === 1)

        Label95 { x: 12; y: 14; height: 21; text: "Caption:" }
        TextField95 {
            id: itemCaption
            x: 84; y: 14; width: 160
            enabled: menuEditor.here !== null && !menuEditor.here.separator
            onTextChanged: menuEditor.store("text", text)
            onAccepted: menuEditor.next()
        }
        Label95 { x: 256; y: 14; height: 21; text: "Name:" }
        TextField95 {
            id: itemName
            x: 304; y: 14; width: 160
            enabled: itemCaption.enabled
            onTextChanged: menuEditor.store("name", text)
            onAccepted: menuEditor.next()
        }

        Label95 { visible: menuEditor.isMenu; x: 12; y: 44; height: 21; text: "Shortcut:" }
        TextField95 {
            id: itemShortcut
            visible: menuEditor.isMenu
            x: 84; y: 44; width: 100
            enabled: menuEditor.itemRow
            onTextChanged: menuEditor.store("shortcut", text)
        }
        Label95 { visible: menuEditor.isMenu; x: 192; y: 44; height: 21; color: Theme.shadow; text: "like Ctrl+O or F5; & in a Caption is its access key" }

        // a button's icon (the chooser), or a symbol (a character), or a picture file
        Label95 { visible: !menuEditor.isMenu; x: 12; y: 44; height: 21; text: "Icon:" }
        Bevel {
            visible: !menuEditor.isMenu
            x: 84; y: 44; width: 22; height: 21
            style: "sunken"
            face: Theme.window
            Icon95 {
                anchors.centerIn: parent
                name: menuEditor.here ? menuEditor.here.icon || "" : ""
                enabled: menuEditor.itemRow
            }
        }
        Button95 {
            id: itemIconButton
            visible: !menuEditor.isMenu
            x: 110; y: 44; width: 64; height: 21
            text: "Choose…"
            enabled: menuEditor.itemRow
            onClicked: dialogs.pickIcon(menuEditor.here.icon, function (name) { menuEditor.store("icon", name); })
        }
        Label95 { visible: !menuEditor.isMenu; x: 184; y: 44; height: 21; text: "Symbol:" }
        TextField95 {
            id: itemGlyph
            visible: !menuEditor.isMenu
            x: 232; y: 44; width: 28
            enabled: menuEditor.itemRow
            onTextChanged: menuEditor.store("glyph", text)
        }
        Label95 { visible: !menuEditor.isMenu; x: 268; y: 44; height: 21; text: "Picture:" }
        TextField95 {
            id: itemPicture
            visible: !menuEditor.isMenu
            x: 316; y: 44; width: 122
            enabled: menuEditor.itemRow
            onTextChanged: menuEditor.store("picture", text)
        }
        Button95 {
            visible: !menuEditor.isMenu
            x: 442; y: 44; width: 22; height: 21
            text: "…"
            enabled: menuEditor.itemRow
            onClicked: dialogs.pickPicture(function (path) { itemPicture.text = path; })
        }
        Label95 { visible: !menuEditor.isMenu; x: 12; y: 74; height: 21; text: "ToolTip:" }
        TextField95 {
            id: itemTip
            visible: !menuEditor.isMenu
            x: 84; y: 74; width: 380
            enabled: menuEditor.itemRow
            onTextChanged: menuEditor.store("toolTip", text)
        }

        Label95 { x: 12; y: menuEditor.isMenu ? 74 : 104; height: 21; text: "Action:" }
        ComboBox95 {
            id: itemAction
            x: 84; y: menuEditor.isMenu ? 74 : 104; width: 200
            enabled: menuEditor.itemRow
            popupMenu: dialogs.popupMenu
            model: menuEditor.actions.map(function (a) { return a.label; })
            onActivated: (i) => menuEditor.store("action", menuEditor.actions[i].value)
        }
        Label95 { x: 296; y: itemAction.y; height: 21; text: "Form:" }
        ComboBox95 {
            id: itemForm
            x: 336; y: itemAction.y; width: 128
            enabled: menuEditor.itemRow && menuEditor.here.action === "openForm"
            popupMenu: dialogs.popupMenu
            model: dialogs.formNames
            onActivated: (i) => menuEditor.store("form", dialogs.formNames[i] || "")
        }

        CheckBox95 {
            id: itemEnabled
            x: 84; y: itemAction.y + 30
            text: "Enabled"
            enabled: menuEditor.here !== null && !menuEditor.here.separator
            onToggled: (on) => menuEditor.store("enabled", on)
        }
        CheckBox95 {
            id: itemChecked
            x: 180; y: itemAction.y + 30
            text: menuEditor.isMenu ? "Checked" : "Pushed in"
            enabled: menuEditor.itemRow
            onToggled: (on) => menuEditor.store("checked", on)
        }

        Row {
            id: editButtons
            x: 12; y: itemAction.y + 56
            spacing: 4
            Button95 { visible: menuEditor.isMenu; width: 26; text: "←"; enabled: menuEditor.here !== null && menuEditor.here.level > 0 && !menuEditor.here.separator; onClicked: menuEditor.indent(0) }
            Button95 { visible: menuEditor.isMenu; width: 26; text: "→"; enabled: menuEditor.here !== null && menuEditor.here.level === 0 && menuEditor.current > 0; onClicked: menuEditor.indent(1) }
            Button95 { width: 26; text: "↑"; enabled: menuEditor.current > 0; onClicked: menuEditor.move(-1) }
            Button95 { width: 26; text: "↓"; enabled: menuEditor.here !== null && menuEditor.current < menuEditor.rows.length - 1; onClicked: menuEditor.move(1) }
            Item { width: 8; height: 1 }
            Button95 { width: 56; text: "Next"; onClicked: menuEditor.next() }
            Button95 { width: 56; text: "Insert"; onClicked: menuEditor.insert(false, false) }
            Button95 { width: 76; text: "Separator"; onClicked: menuEditor.insert(true, true) }
            Button95 { width: 56; text: "Delete"; enabled: menuEditor.here !== null; onClicked: menuEditor.remove() }
        }

        Bevel { id: itemFrame; x: 10; y: editButtons.y + 32; width: 456; height: menuEditor.dialogHeight - y - 70; style: "sunken"; face: Theme.window }
        ListView {
            id: itemList
            x: 12; y: itemFrame.y + 2
            width: 452 - (itemBar.needed ? itemBar.width : 0)
            height: itemFrame.height - 4
            clip: true
            boundsBehavior: Flickable.StopAtBounds
            model: menuEditor.rows
            delegate: Rectangle {
                id: itemRow
                required property var modelData
                required property int index
                readonly property bool isCurrent: index === menuEditor.current
                readonly property color ink: isCurrent ? Theme.selectionText : Theme.text
                width: itemList.width
                height: 17
                color: isCurrent ? Theme.selection : "transparent"
                Icon95 {
                    id: rowIcon
                    visible: !itemRow.modelData.separator && (itemRow.modelData.icon || "") !== ""
                    x: 4; y: 0
                    name: itemRow.modelData.icon || ""
                }
                Label95 {
                    x: 4 + (itemRow.modelData.level > 0 ? 24 : 0) + (rowIcon.visible ? 20 : 0)
                    width: parent.width * 0.55 - x
                    height: parent.height
                    elide: Text.ElideRight
                    color: itemRow.ink
                    text: (itemRow.modelData.level > 0 ? "···· " : "")
                          + (itemRow.modelData.separator ? "-"
                             : (!rowIcon.visible && itemRow.modelData.glyph ? itemRow.modelData.glyph + "  " : "")
                               + (menuEditor.plain(itemRow.modelData.text) || "(no caption)"))
                }
                Label95 {
                    x: parent.width * 0.55
                    width: parent.width * 0.45 - 6
                    height: parent.height
                    elide: Text.ElideRight
                    color: itemRow.isCurrent ? Theme.selectionText : Theme.shadow
                    text: {
                        var r = itemRow.modelData, bits = [];
                        if (r.shortcut)
                            bits.push(r.shortcut);
                        if (r.action === "openForm")
                            bits.push("opens " + (r.form || "?"));
                        else if (r.action)
                            bits.push(menuEditor.actions[menuEditor.actionIndex(r.action)].label.toLowerCase());
                        if (!r.enabled)
                            bits.push("not enabled");
                        return bits.join(", ");
                    }
                }
                MouseArea {
                    anchors.fill: parent
                    onPressed: {
                        menuEditor.select(itemRow.index);
                        itemList.forceActiveFocus();
                    }
                }
            }
            Keys.onPressed: (event) => {
                if (event.key === Qt.Key_Down && menuEditor.current < menuEditor.rows.length - 1)
                    menuEditor.select(menuEditor.current + 1);
                else if (event.key === Qt.Key_Up && menuEditor.current > 0)
                    menuEditor.select(menuEditor.current - 1);
                else
                    return;
                event.accepted = true;
            }
        }
        ScrollBar95 { id: itemBar; flickable: itemList; x: 464 - width; y: itemFrame.y + 2; height: itemFrame.height - 4 }

        Label95 {
            x: 12; y: menuEditor.dialogHeight - 60; width: 290; height: 30
            wrapMode: Text.Wrap
            color: Theme.shadow
            text: menuEditor.isMenu ? "A menu is all the way left; its items are under it, indented (→). Code: menu1.item is the Name of the one chosen."
                                    : "Code: toolBar1.item is the Name of the button pressed."
        }
        Button95 { x: 318; y: menuEditor.dialogHeight - 60; width: 70; text: "OK"; isDefault: true; onClicked: menuEditor.accept() }
        Button95 { x: 394; y: menuEditor.dialogHeight - 60; width: 70; text: "Cancel"; onClicked: menuEditor.close() }
    }

    // --- Import Tables: a database's own tables, into the project -----------
    Dialog95 {
        id: importDialog
        title: "Import Tables"
        dialogWidth: 460
        dialogHeight: 420
        property string where: ""
        property var tables: []
        property var picked: []
        property var notes: []
        property var callback: null
        readonly property int count: picked.filter(function (p) { return p; }).length
        function toggle(i) {
            var p = picked.slice();
            p[i] = !p[i];
            picked = p;
        }
        function accept() {
            var chosen = tables.filter(function (t, i) { return importDialog.picked[i]; });
            var cb = callback;
            callback = null;
            close();
            if (cb)
                cb(chosen, importForms.checked);
        }

        // Enter imports, as its default button
        Keys.onReturnPressed: if (importDialog.count > 0) importDialog.accept()
        Keys.onEnterPressed: if (importDialog.count > 0) importDialog.accept()

        Label95 {
            x: 12; y: 10; width: 436; height: 44
            wrapMode: Text.Wrap
            verticalAlignment: Text.AlignTop
            text: importDialog.where + " has these tables, which the project doesn't. Ticked ones come in: their fields "
                  + "and keys as the database has them. Their records stay in the database, where forms show them."
        }
        Bevel { id: importFrame; x: 10; y: 58; width: 440; height: 178; style: "sunken"; face: Theme.window }
        ListView {
            id: importList
            x: 12; y: 60
            width: 436 - (importBar.needed ? importBar.width : 0)
            height: 174
            clip: true
            boundsBehavior: Flickable.StopAtBounds
            model: importDialog.tables
            delegate: Item {
                id: importRow
                required property var modelData
                required property int index
                width: importList.width
                height: 20
                CheckBox95 {
                    x: 4
                    y: 2
                    checked: importDialog.picked[importRow.index] === true
                    text: importRow.modelData.name + (importRow.modelData.dbName ? "  (" + importRow.modelData.dbName + ")" : "")
                    onToggled: importDialog.toggle(importRow.index)
                }
                Label95 {
                    anchors.right: parent.right
                    anchors.rightMargin: 6
                    height: parent.height
                    color: Theme.shadow
                    text: importRow.modelData.fields.length + (importRow.modelData.fields.length === 1 ? " field, " : " fields, ")
                          + (importRow.modelData.records === undefined ? "?" : importRow.modelData.records)
                          + (importRow.modelData.records === 1 ? " record" : " records")
                          + (importRow.modelData.key === "" ? ", read-only" : "")
                }
            }
        }
        ScrollBar95 { id: importBar; flickable: importList; x: 448 - width; y: 60; height: 174 }

        Label95 {
            x: 12; y: 242; width: 436; height: 62
            visible: importDialog.notes.length > 0
            wrapMode: Text.Wrap
            verticalAlignment: Text.AlignTop
            maximumLineCount: 4
            elide: Text.ElideRight
            color: Theme.shadow
            text: importDialog.notes.join(" ")
        }
        CheckBox95 {
            id: importForms
            x: 12; y: 312
            text: "Make a form for each table (Add Form for)"
        }
        Button95 { x: 12; y: 344; width: 90; text: "Select All"; onClicked: importDialog.picked = importDialog.tables.map(function () { return true; }) }
        Button95 { x: 108; y: 344; width: 90; text: "Select None"; onClicked: importDialog.picked = importDialog.tables.map(function () { return false; }) }
        Button95 { x: 302; y: 344; width: 70; text: "Import"; isDefault: true; enabled: importDialog.count > 0; onClicked: importDialog.accept() }
        Button95 { x: 378; y: 344; width: 70; text: "Cancel"; onClicked: importDialog.close() }
    }

    // --- APIs: the project's web APIs, and how each one is signed in to --------
    Dialog95 {
        id: apiDialog
        title: "APIs"
        dialogWidth: 600
        dialogHeight: 480
        property var apis: []
        property int current: -1
        property var callback: null
        property string result: ""
        property bool resultOk: false
        property int revision: 0
        property bool loading: false
        readonly property var here: current >= 0 && current < apis.length ? apis[current] : null
        readonly property string type: here && here.auth ? (here.auth.type || "none") : "none"
        readonly property string grant: here && here.auth ? (here.auth.grant || "client_credentials") : "client_credentials"
        readonly property var types: [{ value: "none", label: "None" }, { value: "apikey", label: "API key" },
                                      { value: "basic", label: "User name and password (Basic)" },
                                      { value: "bearer", label: "Token (Bearer)" }, { value: "oauth2", label: "OAuth 2.0" }]
        readonly property var grants: [{ value: "client_credentials", label: "Client credentials (the app's own)" },
                                       { value: "authorization_code", label: "Authorization code (the user, in the browser)" },
                                       { value: "password", label: "Password (the user's, typed in)" }]
        // the rows under the sign-in type: which, for which type (and grant)
        readonly property var rows: [
            { key: "auth.keyName", label: "Key name:", types: ["apikey"], hint: "X-Api-Key" },
            { key: "auth.keyValue", label: "Key:", types: ["apikey"], secret: true },
            { key: "auth.user", label: "User name:", types: ["basic"] },
            { key: "auth.password", label: "Password:", types: ["basic"], secret: true },
            { key: "auth.token", label: "Token:", types: ["bearer"], secret: true },
            { key: "auth.tokenUrl", label: "Token URL:", types: ["oauth2"] },
            { key: "auth.authUrl", label: "Authorize URL:", types: ["oauth2"], grants: ["authorization_code"] },
            { key: "auth.clientId", label: "Client ID:", types: ["oauth2"] },
            { key: "auth.clientSecret", label: "Client secret:", types: ["oauth2"], secret: true },
            { key: "auth.scope", label: "Scope:", types: ["oauth2"] },
            { key: "auth.username", label: "User name:", types: ["oauth2"], grants: ["password"] },
            { key: "auth.password", label: "Password:", types: ["oauth2"], grants: ["password"], secret: true },
            { key: "auth.redirectPort", label: "Redirect port:", types: ["oauth2"], grants: ["authorization_code"], hint: "blank: any free one" },
        ]
        function shown(row) {
            return row.types.indexOf(type) >= 0 && (!row.grants || row.grants.indexOf(grant) >= 0);
        }
        function value(key) {
            if (!here)
                return "";
            var parts = key.split("."), v = here;
            for (var i = 0; i < parts.length && v !== undefined && v !== null; ++i)
                v = v[parts[i]];
            return v === undefined || v === null ? "" : String(v);
        }
        function setValue(key, v) {
            if (loading || !here)
                return;
            var list = apis.slice(), a = JSON.parse(JSON.stringify(list[current]));
            var parts = key.split("."), o = a;
            for (var i = 0; i < parts.length - 1; ++i) {
                if (!o[parts[i]])
                    o[parts[i]] = {};
                o = o[parts[i]];
            }
            if (key === "auth.redirectPort")
                v = parseInt(v, 10) || 0;
            o[parts[parts.length - 1]] = v;
            list[current] = a;
            apis = list;
        }
        function select(i) {
            current = i;
            loading = true;
            revision++;
            apiName.text = here ? here.name || "" : "";
            apiBase.text = here ? here.baseUrl || "" : "";
            apiHeaders.text = here ? (here.headers || []).join("\n") : "";
            apiTest.text = here ? here.testPath || "" : "";
            apiType.currentIndex = Math.max(0, types.map(function (t) { return t.value; }).indexOf(type));
            apiGrant.currentIndex = Math.max(0, grants.map(function (g) { return g.value; }).indexOf(grant));
            apiKeyIn.currentIndex = here && here.auth && here.auth.keyIn === "query" ? 1 : 0;
            loading = false;
            result = "";
        }
        function add() {
            var names = apis.map(function (a) { return a.name; }), n = "api";
            for (var i = 2; names.indexOf(n) >= 0; ++i)
                n = "api" + i;
            apis = apis.concat([{ name: n, baseUrl: "https://", headers: [], auth: { type: "none" } }]);
            select(apis.length - 1);
            apiName.input.forceActiveFocus();
            apiName.input.selectAll();
        }
        function removeCurrent() {
            if (!here)
                return;
            var list = apis.slice();
            list.splice(current, 1);
            apis = list;
            select(Math.min(current, list.length - 1));
        }
        // with what's typed, not what's saved
        function test() {
            if (!here)
                return;
            probe.setApis(apis);
            var r = probe.callApi(here.name, "GET", here.testPath || "");
            resultOk = r.ok;
            result = r.ok ? "It answered (" + r.status + ")." : r.error;
        }
        function signIn() {
            if (!here)
                return;
            probe.setApis(apis);
            var r = probe.signIn(here.name);
            resultOk = r.ok;
            result = r.ok ? "Signed in." : r.error;
        }
        function signOut() {
            if (!here)
                return;
            probe.setApis(apis);
            probe.signOut(here.name);
            resultOk = true;
            result = "Signed out: the next request signs in again.";
        }
        function accept() {
            var names = {}, problem = "";
            apis.forEach(function (a) {
                if (!/^[A-Za-z][A-Za-z0-9_]*$/.test(a.name || ""))
                    problem = "\"" + (a.name || "") + "\" can't be an API's name: letters, digits and _, starting with a letter.";
                else if (names[a.name.toLowerCase()])
                    problem = "Two APIs are named " + a.name + ".";
                names[(a.name || "").toLowerCase()] = true;
            });
            if (problem !== "") {
                dialogs.messageBox.show(title, problem, "warning", ["OK"], null);
                return;
            }
            var cb = callback;
            callback = null;
            close();
            if (cb)
                cb(apis);
        }

        Label95 { x: 12; y: 10; text: "APIs:" }
        Bevel { x: 10; y: 28; width: 150; height: 340; style: "sunken"; face: Theme.window }
        ListView {
            x: 12; y: 30; width: 146; height: 336
            clip: true
            model: apiDialog.apis
            delegate: Rectangle {
                id: apiRow
                required property var modelData
                required property int index
                width: 146
                height: 18
                color: index === apiDialog.current ? Theme.selection : "transparent"
                Label95 {
                    x: 4
                    height: parent.height
                    text: apiRow.modelData.name
                    color: apiRow.index === apiDialog.current ? Theme.selectionText : Theme.text
                }
                MouseArea { anchors.fill: parent; onClicked: apiDialog.select(apiRow.index) }
            }
        }
        Button95 { x: 10; y: 374; width: 72; text: "Add"; onClicked: apiDialog.add() }
        Button95 { x: 88; y: 374; width: 72; text: "Remove"; enabled: apiDialog.here !== null; onClicked: apiDialog.removeCurrent() }

        Item {
            x: 172; y: 10; width: 416; height: 390
            enabled: apiDialog.here !== null
            Label95 { y: 2; height: 21; text: "Name:" }
            TextField95 { id: apiName; x: 104; width: 160; onTextChanged: apiDialog.setValue("name", text.trim()) }
            Label95 { y: 30; height: 21; text: "Base URL:" }
            TextField95 { id: apiBase; x: 104; y: 28; width: 312; onTextChanged: apiDialog.setValue("baseUrl", text.trim()) }
            Label95 { y: 58; height: 21; text: "Headers:" }
            Bevel { x: 104; y: 56; width: 312; height: 40; style: "sunken"; face: Theme.window }
            TextEdit {
                id: apiHeaders
                x: 107; y: 58; width: 306; height: 36
                clip: true
                font.family: Theme.monoFamily
                font.pixelSize: Theme.fontSize - 1
                color: Theme.text
                onTextChanged: apiDialog.setValue("headers", text.split("\n").map(function (s) { return s.trim(); }).filter(function (s) { return s.indexOf(":") > 0; }))
            }
            Label95 { y: 102; height: 21; text: "Sign-in:" }
            ComboBox95 {
                id: apiType
                x: 104; y: 102; width: 312
                popupMenu: dialogs.popupMenu
                model: apiDialog.types.map(function (t) { return t.label; })
                onActivated: (i) => apiDialog.setValue("auth.type", apiDialog.types[i].value)
            }
            Label95 { visible: apiDialog.type === "oauth2"; y: 130; height: 21; text: "Grant:" }
            ComboBox95 {
                id: apiGrant
                visible: apiDialog.type === "oauth2"
                x: 104; y: 130; width: 312
                popupMenu: dialogs.popupMenu
                model: apiDialog.grants.map(function (g) { return g.label; })
                onActivated: (i) => apiDialog.setValue("auth.grant", apiDialog.grants[i].value)
            }
            Label95 { visible: apiDialog.type === "apikey"; y: 130; height: 21; text: "Send it in:" }
            ComboBox95 {
                id: apiKeyIn
                visible: apiDialog.type === "apikey"
                x: 104; y: 130; width: 160
                popupMenu: dialogs.popupMenu
                model: ["A header", "The query string"]
                onActivated: (i) => apiDialog.setValue("auth.keyIn", i === 1 ? "query" : "header")
            }
            Column {
                y: apiDialog.type === "oauth2" || apiDialog.type === "apikey" ? 158 : 130
                spacing: 4
                Repeater {
                    model: apiDialog.rows.filter(function (r) { return apiDialog.shown(r); })
                    delegate: Item {
                        id: authRow
                        required property var modelData
                        width: 416
                        height: 23
                        Label95 { height: 21; text: authRow.modelData.label }
                        TextField95 {
                            id: authField
                            x: 104; width: 312
                            input.echoMode: authRow.modelData.secret && !authField.activeFocus && authField.text.indexOf("${env:") !== 0
                                            ? TextInput.Password : TextInput.Normal
                            Component.onCompleted: text = apiDialog.value(authRow.modelData.key)
                            onTextChanged: apiDialog.setValue(authRow.modelData.key, text)
                            Connections {
                                target: apiDialog
                                function onRevisionChanged() { authField.text = apiDialog.value(authRow.modelData.key); }
                            }
                        }
                        Label95 {
                            visible: !!authRow.modelData.hint && authField.text === ""
                            x: 110; height: 21
                            color: Theme.shadow
                            text: authRow.modelData.hint || ""
                        }
                    }
                }
            }
            Label95 { y: 326; height: 21; text: "Test with:" }
            TextField95 { id: apiTest; x: 104; y: 326; width: 140; onTextChanged: apiDialog.setValue("testPath", text.trim()) }
            Button95 { x: 250; y: 325; width: 52; text: "Test"; onClicked: apiDialog.test() }
            Button95 { visible: apiDialog.type === "oauth2"; x: 306; y: 325; width: 54; text: "Sign In"; onClicked: apiDialog.signIn() }
            Button95 { visible: apiDialog.type === "oauth2"; x: 364; y: 325; width: 52; text: "Sign Out"; onClicked: apiDialog.signOut() }
            Label95 {
                y: 354; width: 416; height: 34
                wrapMode: Text.Wrap
                verticalAlignment: Text.AlignTop
                color: apiDialog.result === "" ? Theme.shadow : apiDialog.resultOk ? "#006000" : "#c00000"
                text: apiDialog.result !== "" ? apiDialog.result
                    : "Any of these can be ${env:NAME}: that environment variable when the app runs (then the secret isn't kept in the project, or in programs made from it)."
            }
        }
        Button95 { x: 434; y: 412; width: 70; text: "OK"; onClicked: apiDialog.accept() }
        Button95 { x: 518; y: 412; width: 70; text: "Cancel"; onClicked: apiDialog.close() }
    }

    // --- Add API Table: an endpoint, read, as a table ----------------------------
    Dialog95 {
        id: endpointDialog
        title: "Add API Table"
        dialogWidth: 520
        dialogHeight: 470
        property var apis: []
        property var taken: []
        property var callback: null
        property var read: null
        property string result: ""
        readonly property string chosenApi: apis.length ? apis[Math.max(0, endpointApi.currentIndex)].name : ""
        function readIt() {
            probe.setApis(apis);
            var r = probe.readEndpoint(chosenApi, endpointPath.text.trim(), endpointRecords.text.trim(), endpointQuery.text.trim());
            if (!r.ok) {
                read = null;
                result = r.error;
                return;
            }
            read = r;
            endpointRecords.text = r.recordsPath;
            endpointKey.text = r.key;
            result = r.count + (r.count === 1 ? " record, " : " records, ") + r.fields.length + " fields"
                     + (r.key ? "; each one's id: " + r.key : "; no id found: type the field that tells them apart");
            if (endpointName.text === "") {
                var last = endpointPath.text.split("?")[0].split("/").filter(function (s) { return s !== "" && s.indexOf("{") < 0; }).pop() || "Items";
                var base = last.replace(/[^A-Za-z0-9_]/g, "_").replace(/^[^A-Za-z]+/, "");
                base = (base.charAt(0).toUpperCase() + base.substring(1)) || "Items";
                var n = base;
                for (var i = 2; taken.indexOf(n.toLowerCase()) >= 0; ++i)
                    n = base + i;
                endpointName.text = n;
            }
        }
        function accept() {
            if (!read) {
                dialogs.messageBox.show(title, "Read the endpoint first: its fields come from its records.", "warning", ["OK"], null);
                return;
            }
            var name = endpointName.text.trim();
            if (!/^[A-Za-z][A-Za-z0-9_]*$/.test(name) || taken.indexOf(name.toLowerCase()) >= 0) {
                dialogs.messageBox.show(title, "The table needs a name of its own: letters, digits and _, starting with a letter.", "warning", ["OK"], null);
                return;
            }
            var table = { name: name, label: "", source: "api", api: chosenApi, endpoint: endpointPath.text.trim().split("?")[0],
                          recordsPath: endpointRecords.text.trim(), key: endpointKey.text.trim(), keyAuto: true, stamps: false,
                          updateMethod: endpointPut.checked ? "PUT" : "PATCH",
                          canCreate: endpointCreate.checked, canUpdate: endpointUpdate.checked, canDelete: endpointDelete.checked,
                          fields: read.fields };
            if (endpointQuery.text.trim() !== "")
                table.query = endpointQuery.text.trim();
            // (a one-record answer wrapped the way the list is: { data: {...} })
            if (table.recordsPath !== "" && table.recordsPath.indexOf(".") < 0)
                table.itemPath = table.recordsPath;
            var cb = callback;
            callback = null;
            close();
            if (cb)
                cb(table, endpointForm.checked);
        }

        Label95 { x: 12; y: 12; height: 21; text: "API:" }
        ComboBox95 { id: endpointApi; x: 110; y: 12; width: 200; popupMenu: dialogs.popupMenu; model: endpointDialog.apis.map(function (a) { return a.name; }) }
        Label95 { x: 12; y: 42; height: 21; text: "Endpoint:" }
        TextField95 { id: endpointPath; x: 110; y: 42; width: 300; onAccepted: endpointDialog.readIt() }
        Button95 { x: 418; y: 41; width: 90; text: "Read"; onClicked: endpointDialog.readIt() }
        Label95 { x: 12; y: 72; height: 21; text: "Query:" }
        TextField95 { id: endpointQuery; x: 110; y: 72; width: 200 }
        Label95 { x: 318; y: 72; height: 21; color: Theme.shadow; text: "on every list: per_page=100" }
        Label95 { x: 12; y: 102; height: 21; text: "Records at:" }
        TextField95 { id: endpointRecords; x: 110; y: 102; width: 120 }
        Label95 { x: 238; y: 102; height: 21; color: Theme.shadow; text: "where the list is in the answer (blank: the answer)" }
        Label95 { x: 12; y: 132; height: 21; text: "Id:" }
        TextField95 { id: endpointKey; x: 110; y: 132; width: 120 }
        Label95 {
            x: 12; y: 160; width: 496; height: 34
            wrapMode: Text.Wrap
            color: endpointDialog.read ? "#006000" : endpointDialog.result !== "" ? "#c00000" : Theme.shadow
            text: endpointDialog.result !== "" ? endpointDialog.result
                : "The endpoint's path (after the API's base URL), like /products. Read gets its records, and makes its fields from them."
        }
        Bevel { x: 10; y: 196; width: 500; height: 130; style: "sunken"; face: Theme.window }
        ListView {
            x: 12; y: 198; width: 496; height: 126
            clip: true
            model: endpointDialog.read ? endpointDialog.read.fields : []
            delegate: Item {
                id: fieldRow
                required property var modelData
                width: 496
                height: 17
                Label95 { x: 6; width: 180; height: parent.height; elide: Text.ElideRight; text: fieldRow.modelData.name }
                Label95 { x: 190; width: 110; height: parent.height; text: fieldRow.modelData.type }
                Label95 { x: 304; width: 186; height: parent.height; color: Theme.shadow; elide: Text.ElideRight; text: fieldRow.modelData.path || "" }
            }
        }
        Label95 { x: 12; y: 334; height: 21; text: "Table name:" }
        TextField95 { id: endpointName; x: 110; y: 334; width: 200 }
        CheckBox95 { id: endpointCreate; x: 12; y: 364; text: "Add (POST)"; checked: true }
        CheckBox95 { id: endpointUpdate; x: 112; y: 364; text: "Change"; checked: true }
        CheckBox95 { id: endpointPut; x: 192; y: 364; text: "with PUT (not PATCH)"; checked: false }
        CheckBox95 { id: endpointDelete; x: 356; y: 364; text: "Delete"; checked: true }
        CheckBox95 { id: endpointForm; x: 12; y: 390; text: "Make a form for it"; checked: true }
        Button95 { x: 352; y: 412; width: 70; text: "Add"; isDefault: true; onClicked: endpointDialog.accept() }
        Button95 { x: 438; y: 412; width: 70; text: "Cancel"; onClicked: endpointDialog.close() }
    }
}
