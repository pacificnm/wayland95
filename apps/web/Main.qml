import QtQuick
import QtQuick.Window
import QtWebEngine
import Win95Ui
import Win95Web

// A Web Browser window: menus, the toolbar (Back, Forward, Stop, Refresh,
// Home, Favorites, History), the address bar, tabs, the page, a find bar
// and the status bar. Pages are WebTabs (Qt WebEngine: Chromium); what
// they ask for (menus, dialogs, permissions) is asked here, in the
// desktop's style.
Window {
    id: window

    property var urls: []
    property var tabs: []
    property int current: -1
    readonly property var tab: current >= 0 && current < tabs.length ? tabs[current] : null
    property string hovered: ""
    property bool showStatus: true
    property bool fullScreenPage: false
    property var closedTabs: []
    property int tabsVersion: 0 // (bumped when a tab's title or icon changes)

    width: 960
    height: 700
    minimumWidth: 480
    minimumHeight: 320
    visible: true
    color: Theme.face
    title: (tab && tab.title ? tab.title : "Web Browser") + (tab && tab.title ? " - Web Browser" : "")

    function log(msg) { if (webDebug) console.log("web: " + msg); }
    function refreshTabs() { tabsVersion++; }

    Component.onCompleted: {
        var list = urls.length ? urls : [Browser.homePage];
        for (var i = 0; i < list.length; ++i)
            newTab(Browser.urlFor(list[i]) || list[i], i === 0);
        Browser.lastWindow = window;
        log("ready");
    }
    Connections {
        target: Browser
        // another `win95web URL` started: here, in new tabs
        enabled: Browser.lastWindow === window
        function onOpenRequested(list) {
            for (var i = 0; i < list.length; ++i)
                window.newTab(Browser.urlFor(list[i]) || list[i], true);
            window.raise();
            window.requestActivate();
            window.log("opened from outside " + list.join(" "));
        }
    }
    Connections {
        target: Profile
        function onDownloadStarted(download) {
            window.log("download " + download.downloadFileName);
            if (window.active)
                downloadsDialog.show();
        }
    }

    // ------------------------------------------------------------- tabs
    Component { id: tabComponent; WebTab {} }

    function newTab(url, makeCurrent) {
        var t = tabComponent.createObject(pages, { browser: window });
        t.visible = false;
        t.anchors.fill = pages;
        if (url)
            t.url = url;
        tabs = tabs.concat([t]);
        if (makeCurrent || current < 0)
            setCurrent(tabs.length - 1);
        log("tabs " + tabs.length);
        return t;
    }
    function setCurrent(i) {
        for (var k = 0; k < tabs.length; ++k)
            tabs[k].visible = k === i;
        current = i;
        address.editing = false;
        if (tab)
            tab.forceActiveFocus();
    }
    function closeTab(t) {
        var i = tabs.indexOf(t);
        if (i < 0)
            return;
        if (tabs.length === 1) {
            window.close();
            return;
        }
        closedTabs = closedTabs.concat([t.url.toString()]);
        var list = tabs.slice();
        list.splice(i, 1);
        tabs = list;
        t.destroy();
        setCurrent(Math.min(i, tabs.length - 1));
        log("tabs " + tabs.length);
    }
    function reopenTab() {
        if (!closedTabs.length)
            return;
        var url = closedTabs[closedTabs.length - 1];
        closedTabs = closedTabs.slice(0, -1);
        newTab(url, true);
    }
    function go(text) {
        var url = Browser.urlFor(text);
        if (!url || !tab)
            return;
        tab.pending = url;
        tab.url = url;
        address.editing = false;
        suggestions.close();
        tab.forceActiveFocus();
        log("go " + url);
    }
    function newWindow(urls) { Browser.newWindow(urls || []); }

    // where `win95web URL` opens: the window used last
    onActiveChanged: if (active) Browser.lastWindow = window
    onClosing: {
        // (the window stays, hidden: its pages go, so nothing plays on)
        for (var k = 0; k < tabs.length; ++k)
            tabs[k].destroy();
        tabs = [];
        current = -1;
        if (Browser.lastWindow === window)
            Browser.lastWindow = null;
        log("window closed");
    }

    // a page asked for a new tab, window or popup: a tab here
    function newWindowRequest(view, request) {
        var t = newTab("", request.destination !== WebEngineNewWindowRequest.InNewBackgroundTab);
        request.openIn(t);
        log("new window request " + request.requestedUrl);
    }

    // ------------------------------------------------- what pages ask for
    function contextMenu(view, r) {
        var items = [];
        var p = view.mapToItem(popup, r.position.x, r.position.y);
        if (r.linkUrl.toString()) {
            var link = r.linkUrl.toString();
            items.push({ text: "&Open Link", action: function () { view.url = link; } });
            items.push({ text: "Open Link in New &Tab", action: function () { window.newTab(link, false); window.log("link in new tab " + link); } });
            items.push({ text: "Open Link in New &Window", action: function () { window.newWindow([link]); } });
            items.push({ text: "Save Link &As...", action: function () { view.triggerWebAction(WebEngineView.DownloadLinkToDisk); } });
            items.push({ text: "&Copy Link Address", action: function () { view.triggerWebAction(WebEngineView.CopyLinkToClipboard); } });
            items.push({ separator: true });
        }
        if (r.mediaType === ContextMenuRequest.MediaTypeImage) {
            var img = r.mediaUrl.toString();
            items.push({ text: "Open &Image in New Tab", action: function () { window.newTab(img, false); } });
            items.push({ text: "Sa&ve Image As...", action: function () { view.triggerWebAction(WebEngineView.DownloadImageToDisk); } });
            items.push({ text: "Cop&y Image", action: function () { view.triggerWebAction(WebEngineView.CopyImageToClipboard); } });
            items.push({ separator: true });
        }
        if (r.isContentEditable) {
            items.push({ text: "&Undo", action: function () { view.triggerWebAction(WebEngineView.Undo); } });
            items.push({ separator: true });
            items.push({ text: "Cu&t", action: function () { view.triggerWebAction(WebEngineView.Cut); } });
            items.push({ text: "&Copy", action: function () { view.triggerWebAction(WebEngineView.Copy); } });
            items.push({ text: "&Paste", action: function () { view.triggerWebAction(WebEngineView.Paste); } });
            items.push({ text: "Select &All", action: function () { view.triggerWebAction(WebEngineView.SelectAll); } });
        } else if (r.selectedText) {
            var sel = r.selectedText;
            items.push({ text: "&Copy", action: function () { view.triggerWebAction(WebEngineView.Copy); } });
            items.push({ text: "&Search for \"" + (sel.length > 24 ? sel.slice(0, 24) + "..." : sel) + "\"",
                         action: function () { window.newTab(Browser.searchUrl(sel), true); } });
        } else if (!r.linkUrl.toString()) {
            items.push({ text: "&Back", enabled: view.canGoBack, action: view.goBack });
            items.push({ text: "&Forward", enabled: view.canGoForward, action: view.goForward });
            items.push({ text: "&Reload", action: view.reload });
            items.push({ separator: true });
            items.push({ text: "Add to &Favorites...", action: window.addFavorite });
            items.push({ text: "View &Source", action: function () { window.newTab("view-source:" + view.url, true); } });
        }
        while (items.length && items[items.length - 1].separator)
            items.pop();
        if (items.length)
            popup.popup(p.x, p.y, items);
    }
    function jsDialog(view, r) {
        var host = view.url.toString().replace(/^[a-z]+:\/\//, "").split("/")[0];
        log(["alert", "confirm", "prompt", "beforeunload"][r.type] + " " + r.message);
        if (r.type === JavaScriptDialogRequest.DialogTypePrompt) {
            promptDialog.open(host, r);
            return;
        }
        if (r.type === JavaScriptDialogRequest.DialogTypeAlert) {
            msgBox.show(host + " says", r.message, "info", ["OK"], function () { r.dialogAccept(); });
            return;
        }
        var text = r.type === JavaScriptDialogRequest.DialogTypeBeforeUnload
            ? "Leave this page? Changes you made may not be saved." : r.message;
        msgBox.show(host + " says", text, "question", r.type === JavaScriptDialogRequest.DialogTypeBeforeUnload ? ["Leave", "Stay"] : ["OK", "Cancel"],
                    function (b) { if (b === 0) r.dialogAccept(); else r.dialogReject(); });
    }
    function askPermission(view, permission) {
        // (QWebEnginePermission::PermissionType's values: its names aren't
        // reachable from QML in 6.8)
        var what = {
            1: "use your microphone", 2: "use your camera", 3: "use your camera and microphone",
            4: "see your screen", 5: "see your screen and hear its sound", 6: "hide and hold your mouse pointer",
            7: "show notifications", 8: "know your location", 9: "read and change the clipboard",
            10: "see the fonts on this computer",
        };
        var thing = what[permission.permissionType];
        var origin = permission.origin.toString().replace(/\/$/, "");
        log("permission " + origin + " " + (thing || "?"));
        if (!thing) {
            permission.deny();
            return;
        }
        msgBox.show("Web Browser", origin + " wants to " + thing + ".\n\nAllow it?",
                    "question", ["Allow", "Block"], function (b) {
            if (b === 0) permission.grant(); else permission.deny();
        });
    }
    function certificateProblem(view, error) {
        log("certificate " + error.url + " " + error.description);
        if (!error.overridable) {
            error.rejectCertificate();
            msgBox.show("Web Browser", "This site's security certificate isn't valid, so it can't be shown:\n\n"
                        + error.url + "\n" + error.description, "error", ["OK"]);
            return;
        }
        msgBox.show("Web Browser", "There's a problem with this site's security certificate:\n\n" + error.description
                    + "\n\nSomeone may be pretending to be " + error.url.toString().split("/")[2]
                    + ". Going on is not recommended.", "warning", ["Go Back", "Go On Anyway"], function (b) {
            if (b === 1) error.acceptCertificate(); else error.rejectCertificate();
        });
    }
    function fullScreen(request) {
        request.accept();
        fullScreenPage = request.toggleOn;
        if (request.toggleOn)
            window.showFullScreen();
        else
            window.showNormal();
    }
    function authenticate(view, request) {
        authDialog.open(request);
    }
    function chooseFiles(request) {
        fileDialog.request = request;
        fileDialog.mode = "open";
        fileDialog.multiple = request.mode === FileDialogRequest.FileModeOpenMultiple;
        fileDialog.openAt(Browser.homePath(), "");
    }

    // ------------------------------------------------------------ actions
    function addFavorite() {
        if (!tab)
            return;
        favoriteDialog.open(tab.title || tab.url.toString(), tab.url.toString());
    }
    function zoom(by) {
        if (!tab)
            return;
        tab.zoomFactor = by === 0 ? 1 : Math.max(0.25, Math.min(5, Math.round((tab.zoomFactor + by) * 100) / 100));
        log("zoom " + Math.round(tab.zoomFactor * 100));
    }
    function find(back) {
        if (!tab || !findField.text)
            return;
        tab.lastSearch = findField.text;
        tab.findText(findField.text, (back ? WebEngineView.FindBackward : 0) | (matchCase.checked ? WebEngineView.FindCaseSensitively : 0));
    }
    function closeFind() {
        findBar.visible = false;
        if (tab) {
            tab.findText("");
            tab.forceActiveFocus();
        }
    }

    // -------------------------------------------------------------- menus
    function fileMenu() {
        return [
            { text: "New &Tab", shortcut: "Ctrl+T", action: function () { window.newTab("about:blank", true); address.focusIt(); } },
            { text: "&New Window", shortcut: "Ctrl+N", action: function () { window.newWindow([]); } },
            { text: "&Open File...", shortcut: "Ctrl+O", action: function () { openDialog.openAt(Browser.homePath(), ""); } },
            { separator: true },
            { text: "Save as &PDF...", enabled: !!tab, action: function () {
                pdfDialog.openAt(Browser.downloadsDir, (tab.title || "page").replace(/[\/\\:*?"<>|]/g, "_") + ".pdf");
            } },
            { separator: true },
            { text: "&Close Tab", shortcut: "Ctrl+W", action: function () { window.closeTab(window.tab); } },
            { text: "E&xit", action: window.close },
        ];
    }
    function editMenu() {
        return [
            { text: "Cu&t", shortcut: "Ctrl+X", action: function () { tab.triggerWebAction(WebEngineView.Cut); } },
            { text: "&Copy", shortcut: "Ctrl+C", action: function () { tab.triggerWebAction(WebEngineView.Copy); } },
            { text: "&Paste", shortcut: "Ctrl+V", action: function () { tab.triggerWebAction(WebEngineView.Paste); } },
            { text: "Select &All", shortcut: "Ctrl+A", action: function () { tab.triggerWebAction(WebEngineView.SelectAll); } },
            { separator: true },
            { text: "&Find on This Page...", shortcut: "Ctrl+F", action: window.openFind },
        ];
    }
    function viewMenu() {
        return [
            { text: "&Status Bar", checked: window.showStatus, action: function () { window.showStatus = !window.showStatus; } },
            { separator: true },
            { text: "Zoom &In", shortcut: "Ctrl++", action: function () { window.zoom(0.1); } },
            { text: "Zoom &Out", shortcut: "Ctrl+-", action: function () { window.zoom(-0.1); } },
            { text: "Actual Si&ze", shortcut: "Ctrl+0", action: function () { window.zoom(0); } },
            { separator: true },
            { text: "&Full Screen", shortcut: "F11", checked: window.visibility === Window.FullScreen, action: window.toggleFullScreen },
            { text: "&Refresh", shortcut: "F5", action: function () { tab.reload(); } },
            { text: "Page So&urce", shortcut: "Ctrl+U", action: function () { window.newTab("view-source:" + tab.url, true); } },
        ];
    }
    function goMenu() {
        return [
            { text: "&Back", shortcut: "Alt+Left", enabled: !!tab && tab.canGoBack, action: function () { tab.goBack(); } },
            { text: "&Forward", shortcut: "Alt+Right", enabled: !!tab && tab.canGoForward, action: function () { tab.goForward(); } },
            { text: "&Home Page", shortcut: "Alt+Home", action: function () { window.go(Browser.homePage); } },
            { separator: true },
            { text: "&History", shortcut: "Ctrl+H", action: function () { historyDialog.open(); } },
            { text: "&Reopen Closed Tab", shortcut: "Ctrl+Shift+T", enabled: window.closedTabs.length > 0, action: window.reopenTab },
        ];
    }
    function favoritesMenu() {
        var items = [
            { text: "&Add to Favorites...", shortcut: "Ctrl+D", action: window.addFavorite },
            { text: "&Organize Favorites...", action: function () { organizeDialog.open(); } },
        ];
        if (Browser.favorites.length)
            items.push({ separator: true });
        Browser.favorites.slice(0, 30).forEach(function (f) {
            items.push({ text: f.title.replace(/&/g, "&&"), action: function () { window.go(f.url); } });
        });
        return items;
    }
    function toolsMenu() {
        return [
            { text: "&Downloads", shortcut: "Ctrl+J", action: function () { downloadsDialog.show(); } },
            { text: "&Clear Browsing Data...", action: function () { clearDialog.open(); } },
            { separator: true },
            { text: "Internet &Options...", action: function () { optionsDialog.open(); } },
        ];
    }
    function helpMenu() {
        return [
            { text: "&About Web Browser", action: function () {
                msgBox.show("About Web Browser", "Web Browser\n\nFor the Wayland 95 desktop, with Qt WebEngine "
                            + "(the Chromium engine) showing the pages.", "info", ["OK"]);
            } },
        ];
    }
    function openFind() {
        findBar.visible = true;
        findField.forceActiveFocus();
        findField.input.selectAll();
    }
    function toggleFullScreen() {
        if (window.visibility === Window.FullScreen) window.showNormal();
        else window.showFullScreen();
    }

    Shortcut { sequence: "Ctrl+T"; onActivated: { window.newTab("about:blank", true); address.focusIt(); } }
    Shortcut { sequence: "Ctrl+N"; onActivated: window.newWindow([]) }
    Shortcut { sequence: "Ctrl+W"; onActivated: window.closeTab(window.tab) }
    Shortcut { sequence: "Ctrl+Shift+T"; onActivated: window.reopenTab() }
    Shortcut { sequences: ["Ctrl+L", "Alt+D", "F6"]; onActivated: address.focusIt() }
    Shortcut { sequences: ["F5", "Ctrl+R"]; onActivated: if (window.tab) window.tab.reload() }
    Shortcut { sequence: "Alt+Left"; onActivated: if (window.tab) window.tab.goBack() }
    Shortcut { sequence: "Alt+Right"; onActivated: if (window.tab) window.tab.goForward() }
    Shortcut { sequence: "Alt+Home"; onActivated: window.go(Browser.homePage) }
    Shortcut { sequence: "Ctrl+D"; onActivated: window.addFavorite() }
    Shortcut { sequence: "Ctrl+F"; onActivated: window.openFind() }
    Shortcut { sequence: "Ctrl+H"; onActivated: historyDialog.open() }
    Shortcut { sequence: "Ctrl+J"; onActivated: downloadsDialog.show() }
    Shortcut { sequence: "Ctrl+U"; onActivated: if (window.tab) window.newTab("view-source:" + window.tab.url, true) }
    Shortcut { sequences: ["Ctrl++", "Ctrl+="]; onActivated: window.zoom(0.1) }
    Shortcut { sequence: "Ctrl+-"; onActivated: window.zoom(-0.1) }
    Shortcut { sequence: "Ctrl+0"; onActivated: window.zoom(0) }
    Shortcut { sequence: "F11"; onActivated: window.toggleFullScreen() }
    Shortcut { sequence: "Ctrl+Tab"; onActivated: window.setCurrent((window.current + 1) % window.tabs.length) }
    Shortcut { sequence: "Ctrl+Shift+Tab"; onActivated: window.setCurrent((window.current - 1 + window.tabs.length) % window.tabs.length) }
    Shortcut {
        sequence: "Escape"
        // (not while a menu or dialog has it: Escape is theirs)
        enabled: !msgBox.visible && !popup.visible && !dialogs.anyOpen && !fileDialog.visible && !openDialog.visible && !pdfDialog.visible
        onActivated: {
            if (window.fullScreenPage && window.tab) { window.tab.triggerWebAction(WebEngineView.ExitFullScreen); return; }
            if (findBar.visible) { window.closeFind(); return; }
            if (window.tab && window.tab.loading) window.tab.stop();
        }
    }

    // ------------------------------------------------------------- chrome
    Item {
        id: chrome
        width: parent.width
        height: window.fullScreenPage ? 0 : column.height
        visible: !window.fullScreenPage

        Column {
            id: column
            width: parent.width

            MenuBar95 {
                x: 2
                width: parent.width - 4
                height: 22
                popupMenu: popup
                menus: [
                    { title: "File", items: window.fileMenu },
                    { title: "Edit", items: window.editMenu },
                    { title: "View", items: window.viewMenu },
                    { title: "Go", items: window.goMenu },
                    { title: "Favorites", items: window.favoritesMenu },
                    { title: "Tools", items: window.toolsMenu },
                    { title: "Help", items: window.helpMenu },
                ]
            }
            Rectangle { width: parent.width; height: 1; color: Theme.shadow }
            Rectangle { width: parent.width; height: 1; color: Theme.highlight }

            // Back, Forward, Stop, Refresh, Home | Favorites, History
            Row {
                id: toolbar
                x: 4
                height: 48
                topPadding: 2
                component Big: Item {
                    id: big
                    property string label
                    property string icon
                    property bool enabled: true
                    signal clicked()
                    width: 56
                    height: 44
                    Bevel { anchors.fill: parent; visible: m.containsMouse && big.enabled; style: m.pressed ? "pressed" : "raised" }
                    WebIcon { anchors.horizontalCenter: parent.horizontalCenter; y: 3 + (m.pressed ? 1 : 0); size: 24; kind: big.icon; dim: !big.enabled }
                    EtchedText { anchors.horizontalCenter: parent.horizontalCenter; y: 29; text: big.label; enabled: big.enabled }
                    MouseArea { id: m; anchors.fill: parent; hoverEnabled: true; enabled: big.enabled; onClicked: big.clicked() }
                }
                Big { label: "Back"; icon: "back"; enabled: !!window.tab && window.tab.canGoBack; onClicked: window.tab.goBack() }
                Big { label: "Forward"; icon: "forward"; enabled: !!window.tab && window.tab.canGoForward; onClicked: window.tab.goForward() }
                Big { label: "Stop"; icon: "stop"; enabled: !!window.tab && window.tab.loading; onClicked: window.tab.stop() }
                Big { label: "Refresh"; icon: "refresh"; enabled: !!window.tab; onClicked: window.tab.reload() }
                Big { label: "Home"; icon: "home"; onClicked: window.go(Browser.homePage) }
                Item { width: 10; height: 44; Rectangle { x: 4; y: 4; width: 1; height: 36; color: Theme.shadow } Rectangle { x: 5; y: 4; width: 1; height: 36; color: Theme.highlight } }
                Big {
                    label: "Favorites"; icon: "favorites"
                    onClicked: {
                        var p = mapToItem(popup, 0, height);
                        popup.popup(p.x, p.y, window.favoritesMenu());
                    }
                }
                Big { label: "History"; icon: "history"; onClicked: historyDialog.open() }
                Big { label: "Downloads"; icon: "downloads"; width: 64; onClicked: downloadsDialog.show() }
            }
            Rectangle { width: parent.width; height: 1; color: Theme.shadow }
            Rectangle { width: parent.width; height: 1; color: Theme.highlight }

            // the address bar
            Item {
                width: parent.width
                height: 28
                Text {
                    id: addressLabel
                    x: 6
                    anchors.verticalCenter: parent.verticalCenter
                    text: "A<u>d</u>dress"
                    textFormat: Text.StyledText
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontSize
                }
                TextField95 {
                    id: address
                    objectName: "address"
                    property bool editing: false
                    x: addressLabel.x + addressLabel.width + 6
                    anchors.verticalCenter: parent.verticalCenter
                    width: parent.width - x - goButton.width - 12
                    function focusIt() {
                        editing = true;
                        address.forceActiveFocus();
                        address.input.selectAll();
                    }
                    Binding on text {
                        when: !address.editing
                        value: window.tab ? (window.tab.pending || (window.tab.url.toString() === "about:blank" ? "" : window.tab.url.toString())) : ""
                        restoreMode: Binding.RestoreNone
                    }
                    onAccepted: window.go(suggestions.chosen >= 0 ? suggestions.items[suggestions.chosen].url : text)
                    onEscaped: { editing = false; suggestions.close(); if (window.tab) window.tab.forceActiveFocus(); }
                    input.onTextEdited: { editing = true; suggestions.update(text); }
                    input.onActiveFocusChanged: if (!input.activeFocus) { suggestions.close(); }
                    Keys.onDownPressed: suggestions.move(1)
                    Keys.onUpPressed: suggestions.move(-1)
                    // a padlock for a secure page
                    WebIcon {
                        anchors.right: parent.right
                        anchors.rightMargin: 4
                        anchors.verticalCenter: parent.verticalCenter
                        size: 14
                        kind: "lock"
                        visible: !address.editing && !!window.tab && window.tab.url.toString().indexOf("https:") === 0
                    }
                }
                Button95 {
                    id: goButton
                    anchors.right: parent.right
                    anchors.rightMargin: 6
                    anchors.verticalCenter: parent.verticalCenter
                    width: 40
                    text: "Go"
                    onClicked: window.go(address.text)
                }
            }

            // the tabs
            Item {
                id: tabBar
                width: parent.width
                height: 24
                readonly property real tabWidth: Math.max(60, Math.min(200, (width - 40) / Math.max(1, window.tabs.length)))
                Row {
                    x: 4
                    y: 2
                    spacing: 0
                    Repeater {
                        model: window.tabs.length
                        delegate: Item {
                            id: tabItem
                            required property int index
                            readonly property var t: window.tabs[index]
                            readonly property bool isCurrent: index === window.current
                            width: tabBar.tabWidth
                            height: isCurrent ? 22 : 20
                            y: isCurrent ? 0 : 2
                            Bevel { anchors.fill: parent; style: "raised" }
                            Image {
                                id: favicon
                                x: 5
                                anchors.verticalCenter: parent.verticalCenter
                                width: 16
                                height: 16
                                sourceSize: Qt.size(16, 16)
                                source: tabItem.t && tabItem.t.icon ? tabItem.t.icon : ""
                                visible: status === Image.Ready && !(tabItem.t && tabItem.t.loading)
                            }
                            WebIcon {
                                x: 5
                                anchors.verticalCenter: parent.verticalCenter
                                size: 16
                                kind: tabItem.t && tabItem.t.loading ? "loading" : "page"
                                visible: !favicon.visible
                            }
                            Text {
                                x: 25
                                width: parent.width - x - 22
                                anchors.verticalCenter: parent.verticalCenter
                                elide: Text.ElideRight
                                text: tabItem.t ? (tabItem.t.title || (tabItem.t.loading ? "Opening page..." : "New Tab")) : ""
                                font.family: Theme.fontFamily
                                font.pixelSize: Theme.fontSize
                                font.bold: tabItem.isCurrent
                            }
                            MouseArea {
                                anchors.fill: parent
                                acceptedButtons: Qt.LeftButton | Qt.MiddleButton
                                onClicked: (mouse) => {
                                    if (mouse.button === Qt.MiddleButton) window.closeTab(tabItem.t);
                                    else window.setCurrent(tabItem.index);
                                }
                            }
                            // its close box
                            Item {
                                anchors.right: parent.right
                                anchors.rightMargin: 4
                                anchors.verticalCenter: parent.verticalCenter
                                width: 14
                                height: 13
                                Bevel { anchors.fill: parent; style: closeMouse.pressed ? "pressed" : "raised" }
                                Text { anchors.centerIn: parent; text: "✕"; font.pixelSize: 9; font.bold: true }
                                MouseArea { id: closeMouse; anchors.fill: parent; onClicked: window.closeTab(tabItem.t) }
                            }
                        }
                    }
                    Item {
                        width: 26
                        height: 20
                        y: 2
                        Bevel { anchors.fill: parent; style: plusMouse.pressed ? "pressed" : "raised" }
                        Text { anchors.centerIn: parent; text: "+"; font.pixelSize: 14; font.bold: true }
                        MouseArea {
                            id: plusMouse
                            anchors.fill: parent
                            onClicked: { window.newTab("about:blank", true); address.focusIt(); }
                        }
                    }
                }
            }
        }
    }

    // ------------------------------------------------------------- the page
    Bevel {
        id: pageFrame
        y: chrome.height
        width: parent.width
        height: parent.height - y - (findBar.visible ? findBar.height : 0) - (statusBar.visible ? statusBar.height : 0)
        style: window.fullScreenPage ? "flat" : "sunken"
        face: "#ffffff"
        Item {
            id: pages
            objectName: "pages"
            x: window.fullScreenPage ? 0 : 2
            y: window.fullScreenPage ? 0 : 2
            width: parent.width - 2 * x
            height: parent.height - 2 * y
            // (for the tests) where the page is, in the window
            onWidthChanged: if (width > 0) window.log("page at " + Math.round(mapToItem(null, 0, 0).x) + " " + Math.round(mapToItem(null, 0, 0).y))
        }
    }

    // the address bar's suggestions (history and favorites)
    Bevel {
        id: suggestions
        property var items: []
        property int chosen: -1
        visible: items.length > 0 && address.input.activeFocus
        x: address.x
        y: chrome.height - tabBar.height - 2
        z: 50
        width: address.width
        height: items.length * 20 + 4
        style: "raised"
        function update(text) { items = Browser.suggestions(text); chosen = -1; }
        function close() { items = []; chosen = -1; }
        function move(d) {
            if (!items.length) return;
            chosen = Math.max(-1, Math.min(items.length - 1, chosen + d));
            if (chosen >= 0) address.text = items[chosen].url;
        }
        Column {
            x: 2; y: 2
            Repeater {
                model: suggestions.items
                delegate: Rectangle {
                    required property var modelData
                    required property int index
                    width: suggestions.width - 4
                    height: 20
                    color: index === suggestions.chosen ? Theme.selection : Theme.window
                    Text {
                        x: 6
                        width: parent.width - 12
                        anchors.verticalCenter: parent.verticalCenter
                        elide: Text.ElideRight
                        text: modelData.title ? modelData.title + "  —  " + modelData.url : modelData.url
                        color: index === suggestions.chosen ? Theme.selectionText : Theme.text
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.fontSize
                    }
                    MouseArea { anchors.fill: parent; onClicked: window.go(modelData.url) }
                }
            }
        }
    }

    // -------------------------------------------------------- the find bar
    Item {
        id: findBar
        visible: false
        anchors.bottom: statusBar.visible ? statusBar.top : parent.bottom
        width: parent.width
        height: 28
        Rectangle { width: parent.width; height: 1; color: Theme.highlight }
        Row {
            x: 6
            anchors.verticalCenter: parent.verticalCenter
            spacing: 6
            Text { anchors.verticalCenter: parent.verticalCenter; text: "Find:"; font.family: Theme.fontFamily; font.pixelSize: Theme.fontSize }
            TextField95 {
                id: findField
                objectName: "findField"
                width: 200
                onAccepted: window.find(false)
                onEscaped: window.closeFind()
                input.onTextEdited: window.find(false)
            }
            Button95 { width: 60; text: "<u>N</u>ext"; onClicked: window.find(false) }
            Button95 { width: 70; text: "<u>P</u>revious"; onClicked: window.find(true) }
            CheckBox95 { id: matchCase; anchors.verticalCenter: parent.verticalCenter; text: "Match <u>c</u>ase" }
            Text {
                anchors.verticalCenter: parent.verticalCenter
                text: !window.tab || !findField.text ? "" : window.tab.matchCount === 0 ? "Not found"
                      : window.tab.matchIndex + " of " + window.tab.matchCount
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontSize
            }
        }
        Item {
            anchors.right: parent.right
            anchors.rightMargin: 8
            anchors.verticalCenter: parent.verticalCenter
            width: 16
            height: 14
            Bevel { anchors.fill: parent; style: "raised" }
            Text { anchors.centerIn: parent; text: "✕"; font.pixelSize: 9; font.bold: true }
            MouseArea { anchors.fill: parent; onClicked: window.closeFind() }
        }
    }

    // --------------------------------------------------------- status bar
    Item {
        id: statusBar
        visible: window.showStatus && !window.fullScreenPage
        anchors.bottom: parent.bottom
        width: parent.width
        height: 22
        Bevel {
            x: 2; y: 2
            width: parent.width - 4 - 120 - 60 - 8
            height: 19
            style: "sunken"
            Text {
                x: 4
                width: parent.width - 8
                anchors.verticalCenter: parent.verticalCenter
                elide: Text.ElideMiddle
                text: window.hovered ? window.hovered
                      : window.tab && window.tab.loading ? "Opening page " + window.tab.url + "..."
                      : "Done"
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontSize
            }
        }
        Bevel {
            x: parent.width - 120 - 60 - 4
            y: 2
            width: 120
            height: 19
            style: "sunken"
            ProgressBar95 {
                anchors.fill: parent
                anchors.margins: 2
                visible: !!window.tab && window.tab.loading
                value: window.tab ? window.tab.loadProgress : 0
            }
        }
        Bevel {
            x: parent.width - 60 - 2
            y: 2
            width: 60
            height: 19
            style: "sunken"
            Text {
                anchors.centerIn: parent
                text: window.tab ? Math.round(window.tab.zoomFactor * 100) + "%" : ""
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontSize
            }
        }
    }

    // ------------------------------------------------------------ dialogs
    BrowserDialogs {
        id: dialogs
        anchors.fill: parent
        browser: window
        popupMenu: popup
        messageBox: msgBox
    }
    readonly property alias promptDialog: dialogs.prompt
    readonly property alias authDialog: dialogs.auth
    readonly property alias favoriteDialog: dialogs.favorite
    readonly property alias organizeDialog: dialogs.organize
    readonly property alias historyDialog: dialogs.history
    readonly property alias clearDialog: dialogs.clear
    readonly property alias optionsDialog: dialogs.options

    DownloadsWindow { id: downloadsDialog }

    FileDialog95 {
        id: fileDialog
        property var request: null
        messageBox: msgBox
        popupMenu: popup
        filters: [{ label: "All Files (*.*)", patterns: ["*"] }]
        onAcceptedPaths: (paths) => {
            if (request) request.dialogAccept(paths.map(function (p) { return "file://" + p; }));
            request = null;
        }
        onClosed: if (request) { request.dialogReject(); request = null; }
    }
    FileDialog95 {
        id: openDialog
        mode: "open"
        messageBox: msgBox
        popupMenu: popup
        filters: [{ label: "Web Pages (*.html;*.htm)", patterns: ["*.html", "*.htm", "*.xhtml"] },
                  { label: "All Files (*.*)", patterns: ["*"] }]
        onAccepted: (path) => window.go("file://" + path)
    }
    FileDialog95 {
        id: pdfDialog
        mode: "save"
        messageBox: msgBox
        popupMenu: popup
        defaultSuffix: "pdf"
        filters: [{ label: "PDF Documents (*.pdf)", patterns: ["*.pdf"] }]
        onAccepted: (path) => { if (window.tab) window.tab.printToPdf(path); window.log("pdf " + path); }
    }

    Menu95 {
        id: popup
        onClosed: if (window.tab && !msgBox.visible) window.tab.forceActiveFocus()
    }
    MessageBox95 { id: msgBox }
}
