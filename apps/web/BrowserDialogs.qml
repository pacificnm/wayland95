import QtQuick
import Win95Ui
import Win95Web

// Web Browser's dialogs, drawn in its window: a page's prompt(), a site's
// user name and password, Add to Favorites, Organize Favorites, History,
// Clear Browsing Data and Internet Options.
Item {
    id: root

    property var browser
    property Menu95 popupMenu
    property MessageBox95 messageBox
    readonly property alias prompt: promptDialog
    readonly property alias auth: authDialog
    readonly property alias favorite: favoriteDialog
    readonly property alias organize: organizeDialog
    readonly property alias history: historyDialog
    readonly property alias clear: clearDialog
    readonly property alias options: optionsDialog
    readonly property bool anyOpen: promptDialog.visible || authDialog.visible || favoriteDialog.visible
        || organizeDialog.visible || historyDialog.visible || clearDialog.visible || optionsDialog.visible

    component Label: Text {
        height: 21
        verticalAlignment: Text.AlignVCenter
        textFormat: Text.StyledText
        font.family: Theme.fontFamily
        font.pixelSize: Theme.fontSize
        color: Theme.text
    }
    component Buttons: Row {
        anchors.right: parent.right
        anchors.rightMargin: 10
        anchors.bottom: parent.bottom
        anchors.bottomMargin: 10
        spacing: 6
    }
    function refocus() {
        if (root.browser && root.browser.tab)
            root.browser.tab.forceActiveFocus();
    }

    // ---------------------------------------------- a page's prompt()
    Dialog95 {
        id: promptDialog
        property var request: null
        dialogWidth: 380
        dialogHeight: 150
        function open(host, r) {
            request = r;
            title = host + " says";
            promptText.text = r.message;
            promptField.text = r.defaultText;
            show();
            promptField.forceActiveFocus();
            promptField.input.selectAll();
        }
        function finish(ok) {
            var r = request;
            request = null;
            close();
            if (r) {
                if (ok) r.dialogAccept(promptField.text);
                else r.dialogReject();
            }
        }
        onClosed: { if (request) { request.dialogReject(); request = null; } root.refocus(); }
        Text {
            id: promptText
            x: 10; y: 10
            width: parent.width - 20
            height: 36
            wrapMode: Text.Wrap
            elide: Text.ElideRight
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontSize
        }
        TextField95 {
            id: promptField
            x: 10; y: 50
            width: parent.width - 20
            onAccepted: promptDialog.finish(true)
            onEscaped: promptDialog.finish(false)
        }
        Buttons {
            Button95 { width: 75; text: "OK"; isDefault: true; onClicked: promptDialog.finish(true) }
            Button95 { width: 75; text: "Cancel"; onClicked: promptDialog.finish(false) }
        }
    }

    // ------------------------------------- a site's name and password
    Dialog95 {
        id: authDialog
        property var request: null
        title: "Enter Network Password"
        dialogWidth: 360
        dialogHeight: 180
        function open(r) {
            request = r;
            authText.text = (r.proxy ? "The proxy " : "") + r.url.toString().split("/")[2]
                    + " asks for a user name and password." + (r.realm ? "\nSite: " + r.realm : "");
            userField.text = "";
            passField.text = "";
            show();
            userField.forceActiveFocus();
        }
        function finish(ok) {
            // (what's typed first: closing empties the password)
            var r = request, user = userField.text, pass = passField.text;
            request = null;
            close();
            root.browser.log("auth " + (ok ? "sent " + user : "cancelled"));
            if (r) {
                if (ok) r.dialogAccept(user, pass);
                else r.dialogReject();
            }
        }
        onClosed: { if (request) { request.dialogReject(); request = null; } passField.text = ""; root.refocus(); }
        Text {
            id: authText
            x: 10; y: 8
            width: parent.width - 20
            wrapMode: Text.Wrap
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontSize
        }
        Label { x: 10; y: 50; text: "<u>U</u>ser name:" }
        TextField95 { id: userField; x: 90; y: 50; width: parent.width - 100; onAccepted: passField.forceActiveFocus(); onEscaped: authDialog.finish(false) }
        Label { x: 10; y: 78; text: "<u>P</u>assword:" }
        TextField95 {
            id: passField
            x: 90; y: 78; width: parent.width - 100
            input.echoMode: TextInput.Password
            onAccepted: authDialog.finish(true)
            onEscaped: authDialog.finish(false)
        }
        Buttons {
            Button95 { width: 75; text: "OK"; isDefault: true; onClicked: authDialog.finish(true) }
            Button95 { width: 75; text: "Cancel"; onClicked: authDialog.finish(false) }
        }
    }

    // ------------------------------------------------ Add to Favorites
    Dialog95 {
        id: favoriteDialog
        property string url: ""
        title: "Add to Favorites"
        dialogWidth: 380
        dialogHeight: 130
        function open(t, u) {
            url = u;
            favName.text = t;
            show();
            favName.forceActiveFocus();
            favName.input.selectAll();
        }
        function finish() {
            if (favName.text.trim())
                Browser.addFavorite(favName.text.trim(), url);
            root.browser.log("favorite " + favName.text.trim() + " " + url);
            close();
        }
        onClosed: root.refocus()
        WebIcon { x: 10; y: 12; size: 32; kind: "favorites" }
        Text {
            x: 52; y: 10
            width: parent.width - 62
            text: "Web Browser will add this page to your Favorites."
            wrapMode: Text.Wrap
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontSize
        }
        Label { x: 52; y: 34; text: "<u>N</u>ame:" }
        TextField95 { id: favName; x: 100; y: 34; width: parent.width - 110; onAccepted: favoriteDialog.finish(); onEscaped: favoriteDialog.close() }
        Buttons {
            Button95 { width: 75; text: "OK"; isDefault: true; onClicked: favoriteDialog.finish() }
            Button95 { width: 75; text: "Cancel"; onClicked: favoriteDialog.close() }
        }
    }

    // -------------------------------------------- Organize Favorites
    Dialog95 {
        id: organizeDialog
        property int chosen: -1
        property bool renaming: false
        title: "Organize Favorites"
        dialogWidth: 460
        dialogHeight: 300
        function open() {
            chosen = Browser.favorites.length ? 0 : -1;
            renaming = false;
            show();
        }
        onClosed: root.refocus()
        Bevel {
            x: 10; y: 10
            width: parent.width - 120
            height: parent.height - 20
            style: "sunken"
            face: Theme.window
            ListView {
                id: favList
                x: 2; y: 2
                width: parent.width - 4
                height: parent.height - 4
                clip: true
                model: Browser.favorites
                delegate: Rectangle {
                    required property var modelData
                    required property int index
                    width: ListView.view.width
                    height: 20
                    color: organizeDialog.chosen === index ? Theme.selection : "transparent"
                    WebIcon { x: 4; anchors.verticalCenter: parent.verticalCenter; kind: "page" }
                    Text {
                        x: 24
                        width: parent.width - 28
                        anchors.verticalCenter: parent.verticalCenter
                        visible: !(organizeDialog.renaming && organizeDialog.chosen === index)
                        elide: Text.ElideRight
                        text: modelData.title
                        color: organizeDialog.chosen === index ? Theme.selectionText : Theme.text
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.fontSize
                    }
                    MouseArea {
                        anchors.fill: parent
                        onClicked: { organizeDialog.chosen = index; organizeDialog.renaming = false; }
                        onDoubleClicked: { organizeDialog.close(); root.browser.go(modelData.url); }
                    }
                }
            }
            TextField95 {
                id: renameField
                visible: organizeDialog.renaming
                x: 24
                y: 2 + organizeDialog.chosen * 20 - favList.contentY
                width: parent.width - 28
                height: 20
                onAccepted: {
                    if (text.trim()) Browser.renameFavorite(organizeDialog.chosen, text.trim());
                    organizeDialog.renaming = false;
                }
                onEscaped: organizeDialog.renaming = false
            }
        }
        Column {
            anchors.right: parent.right
            anchors.rightMargin: 10
            y: 10
            spacing: 6
            Button95 {
                width: 90; text: "<u>O</u>pen"; enabled: organizeDialog.chosen >= 0
                onClicked: { var u = Browser.favorites[organizeDialog.chosen].url; organizeDialog.close(); root.browser.go(u); }
            }
            Button95 {
                width: 90; text: "<u>R</u>ename"; enabled: organizeDialog.chosen >= 0
                onClicked: {
                    renameField.text = Browser.favorites[organizeDialog.chosen].title;
                    organizeDialog.renaming = true;
                    renameField.forceActiveFocus();
                    renameField.input.selectAll();
                }
            }
            Button95 {
                width: 90; text: "<u>D</u>elete"; enabled: organizeDialog.chosen >= 0
                onClicked: {
                    Browser.removeFavorite(organizeDialog.chosen);
                    organizeDialog.chosen = Math.min(organizeDialog.chosen, Browser.favorites.length - 1);
                }
            }
        }
        Button95 {
            anchors.right: parent.right; anchors.rightMargin: 10
            anchors.bottom: parent.bottom; anchors.bottomMargin: 10
            width: 90; text: "Close"; onClicked: organizeDialog.close()
        }
    }

    // ---------------------------------------------------------- History
    Dialog95 {
        id: historyDialog
        property int chosen: -1
        property var shown: []
        title: "History"
        dialogWidth: Math.min(640, root.width - 40)
        dialogHeight: Math.min(420, root.height - 40)
        function open() {
            searchField.text = "";
            filter();
            show();
            searchField.forceActiveFocus();
        }
        function filter() {
            var words = searchField.text.toLowerCase().trim();
            shown = Browser.history.filter(function (h) {
                return !words || (h.title + " " + h.url).toLowerCase().indexOf(words) >= 0;
            }).slice(0, 500);
            chosen = shown.length ? 0 : -1;
        }
        function day(ms) {
            var d = new Date(ms), now = new Date();
            var start = new Date(now.getFullYear(), now.getMonth(), now.getDate()).getTime();
            if (ms >= start) return "Today";
            if (ms >= start - 86400000) return "Yesterday";
            return d.toLocaleDateString(Qt.locale(), Locale.ShortFormat);
        }
        Connections { target: Browser; function onHistoryChanged() { if (historyDialog.visible) historyDialog.filter(); } }
        onClosed: root.refocus()
        Label { x: 10; y: 8; text: "<u>S</u>earch:" }
        TextField95 {
            id: searchField
            x: 64; y: 8
            width: parent.width - 74
            input.onTextEdited: historyDialog.filter()
            onAccepted: if (historyDialog.chosen >= 0) { var u = historyDialog.shown[historyDialog.chosen].url; historyDialog.close(); root.browser.go(u); }
            onEscaped: historyDialog.close()
            Keys.onDownPressed: historyDialog.chosen = Math.min(historyDialog.shown.length - 1, historyDialog.chosen + 1)
            Keys.onUpPressed: historyDialog.chosen = Math.max(0, historyDialog.chosen - 1)
        }
        Bevel {
            x: 10; y: 36
            width: parent.width - 20
            height: parent.height - 36 - 44
            style: "sunken"
            face: Theme.window
            ListView {
                id: historyList
                x: 2; y: 2
                width: parent.width - 4
                height: parent.height - 4
                clip: true
                model: historyDialog.shown
                currentIndex: historyDialog.chosen
                onCurrentIndexChanged: positionViewAtIndex(currentIndex, ListView.Contain)
                delegate: Item {
                    id: hrow
                    required property var modelData
                    required property int index
                    readonly property bool newDay: index === 0 || historyDialog.day(historyDialog.shown[index - 1].time) !== historyDialog.day(modelData.time)
                    width: ListView.view.width
                    height: newDay ? 40 : 20
                    Text {
                        visible: hrow.newDay
                        x: 4; y: 3
                        text: historyDialog.day(hrow.modelData.time)
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.fontSize
                        font.bold: true
                    }
                    Rectangle {
                        y: hrow.newDay ? 20 : 0
                        width: parent.width
                        height: 20
                        color: historyDialog.chosen === hrow.index ? Theme.selection : "transparent"
                        WebIcon { x: 12; anchors.verticalCenter: parent.verticalCenter; kind: "page" }
                        Text {
                            x: 32
                            width: parent.width - 32 - 60
                            anchors.verticalCenter: parent.verticalCenter
                            elide: Text.ElideRight
                            text: (hrow.modelData.title || hrow.modelData.url) + "   " + hrow.modelData.url
                            color: historyDialog.chosen === hrow.index ? Theme.selectionText : Theme.text
                            font.family: Theme.fontFamily
                            font.pixelSize: Theme.fontSize
                        }
                        Text {
                            anchors.right: parent.right
                            anchors.rightMargin: 6
                            anchors.verticalCenter: parent.verticalCenter
                            text: new Date(hrow.modelData.time).toLocaleTimeString(Qt.locale(), Locale.ShortFormat)
                            color: historyDialog.chosen === hrow.index ? Theme.selectionText : Theme.text
                            font.family: Theme.fontFamily
                            font.pixelSize: Theme.fontSize
                        }
                        MouseArea {
                            anchors.fill: parent
                            onClicked: historyDialog.chosen = hrow.index
                            onDoubleClicked: { historyDialog.close(); root.browser.go(hrow.modelData.url); }
                        }
                    }
                }
            }
        }
        Text {
            x: 10
            anchors.bottom: parent.bottom
            anchors.bottomMargin: 14
            text: Browser.history.length + (Browser.history.length === 1 ? " page" : " pages")
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontSize
        }
        Buttons {
            Button95 {
                width: 75; text: "<u>O</u>pen"; enabled: historyDialog.chosen >= 0
                onClicked: { var u = historyDialog.shown[historyDialog.chosen].url; historyDialog.close(); root.browser.go(u); }
            }
            Button95 {
                width: 110; text: "Open in New <u>T</u>ab"; enabled: historyDialog.chosen >= 0
                onClicked: { var u = historyDialog.shown[historyDialog.chosen].url; historyDialog.close(); root.browser.newTab(u, true); }
            }
            Button95 { width: 75; text: "Close"; onClicked: historyDialog.close() }
        }
    }

    // ---------------------------------------------- Clear Browsing Data
    Dialog95 {
        id: clearDialog
        title: "Clear Browsing Data"
        dialogWidth: 340
        dialogHeight: 160
        function open() {
            clearHistoryBox.checked = true;
            clearCacheBox.checked = true;
            clearCookiesBox.checked = false;
            show();
        }
        onClosed: root.refocus()
        Text {
            x: 10; y: 10
            width: parent.width - 20
            text: "Remove these from this computer:"
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontSize
        }
        Column {
            x: 16; y: 34
            spacing: 6
            CheckBox95 { id: clearHistoryBox; text: "Pages you've visited (<u>H</u>istory)" }
            CheckBox95 { id: clearCacheBox; text: "Saved copies of pages and pictures (<u>c</u>ache)" }
            CheckBox95 { id: clearCookiesBox; text: "C<u>o</u>okies (you'll be signed out of sites)" }
        }
        Buttons {
            Button95 {
                width: 75; text: "Clear"; isDefault: true
                onClicked: {
                    var done = [];
                    if (clearHistoryBox.checked) { Browser.clearHistory(); done.push("history"); }
                    if (clearCacheBox.checked) { Profile.clearHttpCache(); done.push("cache"); }
                    var cookies = clearCookiesBox.checked;
                    if (cookies) { Browser.forgetCookies(Profile.persistentStoragePath); done.push("cookies"); }
                    root.browser.log("cleared " + done.join(" "));
                    clearDialog.close();
                    if (cookies)
                        root.messageBox.show("Web Browser", "Cookies will be removed the next time Web Browser starts. "
                                             + "Close every Web Browser window to finish.", "info", ["OK"]);
                }
            }
            Button95 { width: 75; text: "Cancel"; onClicked: clearDialog.close() }
        }
    }

    // --------------------------------------------------- Internet Options
    Dialog95 {
        id: optionsDialog
        title: "Internet Options"
        dialogWidth: 400
        dialogHeight: 236
        property int engine: 0
        function open() {
            defaultButton.isIt = Browser.isDefault();
            homeField.text = Browser.homePage;
            engine = Math.max(0, Browser.searchEngines.map(function (e) { return e.key; }).indexOf(Browser.searchEngine));
            show();
            homeField.forceActiveFocus();
        }
        function save() {
            var home = Browser.urlFor(homeField.text.trim());
            if (home)
                Browser.homePage = home;
            Browser.searchEngine = Browser.searchEngines[engine].key;
            root.browser.log("options " + Browser.homePage + " " + Browser.searchEngine);
            close();
        }
        onClosed: root.refocus()
        Label { x: 10; y: 10; text: "<b>Home page</b>" }
        Label { x: 16; y: 34; text: "Add<u>r</u>ess:" }
        TextField95 { id: homeField; x: 76; y: 34; width: parent.width - 86; onAccepted: optionsDialog.save(); onEscaped: optionsDialog.close() }
        Row {
            x: 76; y: 60
            spacing: 6
            Button95 { width: 96; text: "Use <u>C</u>urrent"; onClicked: if (root.browser.tab) homeField.text = root.browser.tab.url.toString() }
            Button95 { width: 96; text: "Use <u>B</u>lank"; onClicked: homeField.text = "about:blank" }
        }
        Label { x: 10; y: 94; text: "<b>Searching</b>" }
        Label { x: 16; y: 118; text: "Search <u>w</u>ith:" }
        ComboBox95 {
            x: 96; y: 118
            width: 160
            popupMenu: root.popupMenu
            model: Browser.searchEngines.map(function (e) { return e.name; })
            currentIndex: optionsDialog.engine
            onActivated: (i) => optionsDialog.engine = i
        }
        Label { x: 10; y: 150; text: "<b>Default browser</b>" }
        Button95 {
            id: defaultButton
            x: 16; y: 172
            width: 200
            property bool isIt: false
            text: isIt ? "This is the default browser" : "Make Web Browser the <u>d</u>efault"
            enabled: !isIt
            onClicked: { Browser.makeDefault(); isIt = Browser.isDefault(); }
        }
        Buttons {
            Button95 { width: 75; text: "OK"; isDefault: true; onClicked: optionsDialog.save() }
            Button95 { width: 75; text: "Cancel"; onClicked: optionsDialog.close() }
        }
    }
}
