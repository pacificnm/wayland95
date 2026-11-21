import QtQuick
import Win95Ui

// The Library Manager: the installed libraries (details, updates, remove,
// add a file, the download folder), the Kiwix catalog (also offline, from a
// saved copy) and the downloads (progress, cancel, resume, verify).
Page95 {
    id: page
    property int startTab: 0
    title: "Library Manager"
    readonly property var catalog: LibraryManager.catalog
    actions: [
        { text: "Check for &Updates", enabled: !LibraryManager.checkingUpdates && LibraryManager.installed.count > 0 && Connectivity.online,
          action: function () { LibraryManager.checkForUpdates(false); } },
        { text: "&Add a ZIM File...", action: window.addZimFile },
        { text: "&Rescan the Folder", action: function () { LibraryManager.rescan(); } },
        { text: "&Change the Download Folder...", action: chooseFolder },
        { separator: true },
        { text: "Refresh the &Catalog", enabled: !LibraryManager.catalogLoading && Connectivity.online, action: function () { LibraryManager.refreshCatalog(); } },
    ]
    Component.onCompleted: {
        tabs.currentIndex = startTab;
        LibraryManager.loadCatalog();
    }
    function chooseFolder() {
        window.chooseFile("folder", [{ label: "Folders", patterns: ["*"] }], "", "", function (path) {
            if (!LibraryManager.setFolder("file://" + path))
                window.notify("That folder cannot be used for downloads.", true);
        });
    }
    function size(n) { return Util.byteSize(n); }
    function duration(s) {
        s = Math.round(s);
        if (s < 60) return s + " s";
        if (s < 3600) return Math.floor(s / 60) + " min " + (s % 60) + " s";
        return Math.floor(s / 3600) + " h " + Math.floor((s % 3600) / 60) + " min";
    }

    TabWidget95 {
        id: tabs
        anchors.fill: parent
        titles: [LibraryManager.updateCount > 0 ? "Installed (" + LibraryManager.updateCount + (LibraryManager.updateCount === 1 ? " update)" : " updates)")
                 : LibraryManager.installed.count > 0 ? "Installed (" + LibraryManager.installed.count + ")" : "Installed",
                 "Catalog",
                 LibraryManager.activeDownloads > 0 ? "Downloads (" + LibraryManager.activeDownloads + ")" : "Downloads"]

        // ---- installed
        Item {
            Label95 {
                id: folderLabel
                width: parent.width - 110
                height: 22
                verticalAlignment: Text.AlignVCenter
                elide: Text.ElideMiddle
                text: "Downloads go to " + LibraryManager.folder + " · " + page.size(LibraryManager.freeSpace) + " free"
            }
            Button95 { anchors.right: parent.right; width: 100; height: 22; text: "Change..."; onClicked: page.chooseFolder() }
            ListWell {
                id: installed
                y: 28
                width: parent.width
                height: parent.height - y - installedInfo.height - 36
                model: LibraryManager.installed
                emptyText: "No libraries installed.\n\nDownload one from the catalog, or add a ZIM file you already have."
                columns: [
                    { title: "Library", flex: true, min: 160, icon: function () { return "book"; }, text: function (l) { return l.title; } },
                    { title: "Contents", w: 140, text: function (l) { return Util.flavourLabel(l.flavour); } },
                    { title: "Size", w: 72, right: true, text: function (l) { return page.size(l.size); } },
                    { title: "Date", w: 80, text: function (l) { return l.date; } },
                    { title: "Status", w: 130, text: function (l) {
                        return l.missing ? "File missing" : l.updateVersion !== "" ? "Update: " + l.updateVersion
                             : !l.isWikipedia ? "Not Wikipedia-style" : !l.hasFulltext ? "Titles only" : "Ready"; } },
                ]
                rowColor: function (l) { return l.missing ? "#800000" : Theme.text; }
                onActivated: (i, l) => details.open(l.libraryId)
                onMenuRequested: (i, l, x, y) => {
                    var p = mapToItem(popup, x, y);
                    popup.popup(p.x, p.y, [
                        { text: "&Open", enabled: !l.missing, action: function () { window.openLibrary(l.libraryId); } },
                        { text: "&Details...", action: function () { details.open(l.libraryId); } },
                        { text: "&Update to " + l.updateVersion, enabled: l.updateVersion !== "", action: function () { LibraryManager.updateLibrary(l.libraryId); } },
                        { separator: true },
                        { text: "&Remove...", action: function () { removeDialog.ask(l); } },
                    ]);
                }
            }
            Label95 {
                id: installedInfo
                y: installed.y + installed.height + 4
                width: parent.width
                wrapMode: Text.Wrap
                height: 30
                color: installed.currentItem && installed.currentItem.missing ? "#800000" : Theme.text
                text: {
                    var l = installed.currentItem;
                    if (!l) return "";
                    if (l.missing) return "The file is missing: " + l.path;
                    if (!l.isWikipedia) return "Not a Wikipedia-style library: you may be able to read it, but Athenaeum cannot search it or answer questions from it.";
                    if (!l.hasFulltext) return "This library has no full-text index, so articles can only be found by title.";
                    return l.description || "";
                }
            }
            Row {
                anchors.bottom: parent.bottom
                spacing: 6
                Button95 { width: 80; text: "Details..."; enabled: !!installed.currentItem; onClicked: details.open(installed.currentItem.libraryId) }
                Button95 {
                    width: 110
                    text: installed.currentItem && installed.currentItem.updateVersion !== "" ? "Update" : "Update"
                    enabled: !!installed.currentItem && installed.currentItem.updateVersion !== "" && LibraryManager.aria2Available
                    onClicked: LibraryManager.updateLibrary(installed.currentItem.libraryId)
                }
                Button95 { width: 80; text: "Remove..."; enabled: !!installed.currentItem; onClicked: removeDialog.ask(installed.currentItem) }
            }
            Row {
                anchors.bottom: parent.bottom
                anchors.right: parent.right
                spacing: 6
                Button95 { width: 110; text: "Add File..."; onClicked: window.addZimFile() }
                Button95 {
                    width: 130
                    text: LibraryManager.checkingUpdates ? "Checking..." : "Check for Updates"
                    enabled: !LibraryManager.checkingUpdates && LibraryManager.installed.count > 0 && Connectivity.online
                    onClicked: LibraryManager.checkForUpdates(false)
                }
            }
        }

        // ---- the catalog
        Item {
            Row {
                id: filters
                spacing: 6
                height: 22
                TextField95 {
                    id: query
                    width: Math.max(120, parent.parent.width - 3 * 136 - 24)
                    height: 22
                    text: page.catalog.query
                    onTextChanged: page.catalog.query = text
                    Text {
                        visible: query.text === "" && !query.input.activeFocus
                        x: 4
                        anchors.verticalCenter: parent.verticalCenter
                        text: "Search the catalog..."
                        color: Theme.shadow
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.fontSize
                    }
                }
                ComboBox95 {
                    id: kind
                    width: 130
                    height: 22
                    popupMenu: popup
                    readonly property var values: [{ value: "", label: "All kinds", count: page.catalog.totalCount }].concat(page.catalog.categories)
                    model: values.map(function (m) { return Util.plain(m.label) + " (" + m.count + ")"; })
                    currentIndex: Math.max(0, values.findIndex(function (m) { return m.value === page.catalog.category; }))
                    onActivated: (i) => page.catalog.category = values[i].value
                }
                ComboBox95 {
                    width: 130
                    height: 22
                    popupMenu: popup
                    readonly property var values: ["", "mini", "nopic", "maxi"]
                    model: ["Any size", Util.flavourLabel("mini"), Util.flavourLabel("nopic"), Util.flavourLabel("maxi")]
                    currentIndex: Math.max(0, values.indexOf(page.catalog.flavour))
                    onActivated: (i) => page.catalog.flavour = values[i]
                }
                ComboBox95 {
                    width: 130
                    height: 22
                    popupMenu: popup
                    readonly property var values: ["title", "newest", "smallest", "largest"]
                    model: ["By title", "Newest first", "Smallest first", "Largest first"]
                    currentIndex: Math.max(0, values.indexOf(page.catalog.sortOrder))
                    onActivated: (i) => page.catalog.sortOrder = values[i]
                }
            }
            Column {
                id: catNotices
                y: filters.height + 4
                width: parent.width
                spacing: 2
                Notice {
                    width: parent.width
                    kind: "error"
                    text: !LibraryManager.aria2Available ? "Downloading needs aria2, which is not installed. Install it with: <b>sudo apt install aria2</b>" : ""
                }
                Notice {
                    width: parent.width
                    kind: "info"
                    text: !Connectivity.online && page.catalog.totalCount === 0
                          ? (Connectivity.allowed ? "There is no internet connection, so the Kiwix catalog cannot be fetched."
                                                  : "The internet is switched off (Tools > Use the Internet), so the Kiwix catalog cannot be fetched.") : ""
                }
                Notice {
                    width: parent.width
                    kind: "warning"
                    text: LibraryManager.catalogError === "" ? ""
                        : page.catalog.totalCount > 0 ? "Could not reach the Kiwix catalog (" + Util.plain(LibraryManager.catalogError) + "). Showing the copy from "
                                                        + LibraryManager.catalogFetchedAt.toLocaleString(Qt.locale(), Locale.ShortFormat) + "."
                        : "Could not reach the Kiwix catalog: " + Util.plain(LibraryManager.catalogError)
                    buttons: [{ text: "Try Again", action: function () { LibraryManager.refreshCatalog(); } }]
                }
            }
            ListWell {
                id: catalogWell
                y: catNotices.y + catNotices.height + 4
                width: parent.width
                height: parent.height - y - catalogFoot.height - 34
                model: page.catalog
                emptyText: LibraryManager.catalogLoading ? "Loading the catalog..." : page.catalog.totalCount > 0 ? "Nothing matches. Try other words, or another kind or size." : ""
                columns: [
                    { title: "Library", flex: true, min: 160, icon: function () { return "globe"; }, text: function (e) { return e.title; } },
                    { title: "Contents", w: 140, text: function (e) { return Util.flavourLabel(e.flavour); } },
                    { title: "Size", w: 72, right: true, text: function (e) { return page.size(e.size); } },
                    { title: "Date", w: 80, text: function (e) { return e.date; } },
                    { title: "State", w: 100, text: function (e) {
                        return e.state === "installed" ? "Installed" : e.state === "update" ? "Newer version"
                             : e.state === "downloading" ? Math.round(e.progress * 100) + "%" : ""; } },
                ]
                rowBold: function (e) { return e.state === "installed"; }
                onActivated: (i, e) => { if (e.state === "available" || e.state === "update") download(i); }
                function download(i) {
                    if (!LibraryManager.aria2Available)
                        return;
                    LibraryManager.download(i);
                    tabs.currentIndex = 2;
                }
            }
            Label95 {
                id: catalogFoot
                y: catalogWell.y + catalogWell.height + 2
                width: parent.width
                height: 32
                wrapMode: Text.Wrap
                maximumLineCount: 2
                elide: Text.ElideRight
                text: {
                    var e = catalogWell.currentItem;
                    if (!e)
                        return page.catalog.category !== "wikipedia" && page.catalog.totalCount > 0
                               ? "Athenaeum searches and answers from Wikipedia-style libraries. Other kinds can be downloaded, but may not be searchable." : "";
                    return (e.summary || "") + (e.hasFulltext ? "" : "  (No full-text index: articles can only be found by title, and answers will be poor.)");
                }
            }
            Label95 {
                anchors.bottom: parent.bottom
                height: 23
                verticalAlignment: Text.AlignVCenter
                dim: true
                visible: page.catalog.totalCount > 0
                text: catalogWell.count + " of " + page.catalog.totalCount + " libraries · catalog from "
                      + LibraryManager.catalogFetchedAt.toLocaleString(Qt.locale(), Locale.ShortFormat)
            }
            Row {
                anchors.bottom: parent.bottom
                anchors.right: parent.right
                spacing: 6
                Button95 {
                    width: 100
                    text: catalogWell.currentItem && catalogWell.currentItem.state === "update" ? "Update" : "Download"
                    enabled: !!catalogWell.currentItem && LibraryManager.aria2Available
                             && (catalogWell.currentItem.state === "available" || catalogWell.currentItem.state === "update")
                    onClicked: catalogWell.download(catalogWell.currentIndex)
                }
                Button95 {
                    width: 110
                    text: LibraryManager.catalogLoading ? "Loading..." : "Refresh"
                    enabled: !LibraryManager.catalogLoading && Connectivity.online
                    onClicked: LibraryManager.refreshCatalog()
                }
            }
        }

        // ---- the downloads
        Item {
            ListWell {
                id: downloads
                width: parent.width
                height: parent.height - 34
                model: LibraryManager.downloads
                rowHeight: 20
                emptyText: "No downloads.\n\nLibraries you download from the catalog appear here while they arrive."
                columns: [
                    { title: "Library", flex: true, min: 160, icon: function (d) {
                        return d.status === "complete" ? "ok" : d.status === "failed" ? "error" : d.status === "cancelled" ? "pause" : "download"; },
                      text: function (d) { return d.title; } },
                    { title: "Progress", w: 60, right: true, text: function (d) { return d.active || d.status === "complete" ? Math.round(d.progress * 100) + "%" : ""; } },
                    { title: "Status", w: 300, text: function (d) { return page.statusText(d); } },
                ]
                rowColor: function (d) { return d.status === "failed" ? "#c00000" : Theme.text; }
            }
            ProgressBar95 {
                anchors.bottom: parent.bottom
                anchors.bottomMargin: 2
                width: parent.width - buttonsRow.width - 10
                height: 20
                visible: !!downloads.currentItem && downloads.currentItem.active
                value: !downloads.currentItem ? 0 : (downloads.currentItem.status === "verifying" || downloads.currentItem.status === "queued")
                       ? -1 : Math.round(downloads.currentItem.progress * 100)
            }
            Row {
                id: buttonsRow
                anchors.bottom: parent.bottom
                anchors.right: parent.right
                spacing: 6
                readonly property var d: downloads.currentItem
                Button95 {
                    width: 80
                    text: "Cancel"
                    enabled: !!parent.d && (parent.d.status === "downloading" || parent.d.status === "queued")
                    onClicked: LibraryManager.cancelDownload(parent.d.downloadId)
                }
                Button95 {
                    width: 80
                    text: "Resume"
                    enabled: !!parent.d && (parent.d.status === "failed" || parent.d.status === "cancelled") && LibraryManager.aria2Available
                    onClicked: LibraryManager.resumeDownload(parent.d.downloadId)
                }
                Button95 {
                    width: 90
                    text: parent.d && parent.d.status === "complete" ? "Clear" : "Remove"
                    enabled: !!parent.d && !parent.d.active
                    onClicked: LibraryManager.removeDownload(parent.d.downloadId)
                }
            }
        }
    }
    function statusText(m) {
        switch (m.status) {
        case "queued": return "Starting...";
        case "downloading": {
            var t = m.total > 0 ? size(m.done) + " of " + size(m.total) : size(m.done);
            if (m.speed > 0)
                t += " · " + size(m.speed) + "/s";
            if (m.eta > 0)
                t += " · " + duration(m.eta) + " left";
            return t;
        }
        case "verifying": return "Checking the file... (a big library takes a few minutes)";
        case "complete": return "Done: " + size(m.total);
        case "cancelled": return "Cancelled. Resume continues where it stopped.";
        case "failed": return m.error || "Failed";
        default: return m.status;
        }
    }

    // ---- a library's details
    Dialog95 {
        id: details
        property var info: ({})
        title: Util.plain(info.title || "Library")
        dialogWidth: 460
        dialogHeight: 330
        function open(id) {
            info = LibraryManager.details(id);
            show();
        }
        Keys.onEscapePressed: close()
        readonly property var rows: [
            ["Description:", info.description || ""],
            ["Contents:", Util.flavourLabel(info.flavour || "")],
            ["Entries:", Math.round(info.articleCount || 0) + " articles, counting redirects"],
            ["Full-text index:", info.hasFulltext ? "Yes" : "No: articles can only be found by title"],
            ["Size:", Util.byteSize(info.size || 0)],
            ["Date:", info.date || ""],
            ["Language:", info.language || ""],
            ["Source:", [info.creator, info.publisher].filter(function (s) { return s; }).join(", ")],
            ["Made with:", info.scraper || ""],
            ["File:", info.path || ""],
            ["Added:", info.addedAt ? info.addedAt.toLocaleString(Qt.locale(), Locale.ShortFormat) : ""],
        ]
        Column {
            x: 12
            y: 12
            width: parent.width - 24
            spacing: 4
            Repeater {
                model: details.rows
                Row {
                    required property var modelData
                    spacing: 8
                    visible: modelData[1] !== ""
                    Label95 { width: 100; text: parent.modelData[0] }
                    Label95 { width: details.dialogWidth - 24 - 108; wrapMode: Text.WrapAnywhere; maximumLineCount: 3; elide: Text.ElideRight; text: parent.modelData[1] }
                }
            }
        }
        Button95 {
            anchors.right: parent.right
            anchors.rightMargin: 12
            anchors.bottom: parent.bottom
            anchors.bottomMargin: 10
            text: "Close"
            isDefault: true
            onClicked: details.close()
        }
    }

    // ---- removing one
    Dialog95 {
        id: removeDialog
        property var libraryId
        property string libraryTitle: ""
        property bool canDelete: false
        title: "Remove Library"
        dialogWidth: 420
        dialogHeight: 170
        function ask(l) {
            libraryId = l.libraryId;
            libraryTitle = l.title;
            canDelete = !l.missing;
            deleteFile.text = "Also <u>d</u>elete the file from disk (" + Util.byteSize(l.size) + ")";
            deleteFile.checked = l.managed;
            show();
        }
        Keys.onEscapePressed: close()
        Icon95 { x: 14; y: 14; size: 32; name: "warning" }
        Label95 {
            x: 60
            y: 14
            width: parent.width - 74
            wrapMode: Text.Wrap
            text: "Remove “" + removeDialog.libraryTitle + "”?\n\nIts articles will no longer open. Chats and projects keep their text, and if you add this library again later, they link to it again."
        }
        CheckBox95 { id: deleteFile; x: 60; y: 100; visible: removeDialog.canDelete }
        Row {
            anchors.right: parent.right
            anchors.rightMargin: 12
            anchors.bottom: parent.bottom
            anchors.bottomMargin: 10
            spacing: 6
            Button95 {
                text: "Remove"
                onClicked: {
                    removeDialog.close();
                    LibraryManager.removeLibrary(removeDialog.libraryId, removeDialog.canDelete && deleteFile.checked);
                }
            }
            Button95 { text: "Cancel"; isDefault: true; onClicked: removeDialog.close() }
        }
    }
}
