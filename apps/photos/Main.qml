import QtQuick
import QtQuick.Window
import Win95Ui
import Win95Photos

// Photo Gallery: a folder's photos as thumbnails (the gallery), and one at
// a time full-window (the photo view), where it can be zoomed, shown as a
// slide show, set as the wallpaper and edited with the tools beside it.
//
// Edits stay in the editor (PhotoEditor) until saved: Save asks before it
// writes over the original, and leaving a changed photo asks to save it.
Window {
    id: window

    width: 820
    height: 600
    minimumWidth: 420
    minimumHeight: 300
    visible: true
    color: Theme.face
    title: (photoMode ? Win95Fs.fileName(currentPath) + (PhotoEditor.modified && editorOwns ? " *" : "")
                      : Win95Fs.displayName(folder.folder)) + " - Photo Gallery"

    // "gallery" or "photo"
    property string mode: "gallery"
    readonly property bool photoMode: mode === "photo"
    property string currentPath: ""
    property bool editing: false
    property bool cropping: false
    property bool showToolbar: true
    property bool showStatusBar: true
    property int thumbSize: Number(PhotoFiles.setting("thumbSize", 128))
    property bool quitting: false
    property string serviceStatus: ""
    readonly property bool editorOwns: PhotoEditor.loaded && PhotoEditor.path === currentPath
    readonly property bool modalOpen: msgBox.visible || popup.visible || fileDialog.visible || folderDialog.visible
                                      || saveDialog.visible || propertiesDialog.visible || resizeDialog.visible
                                      || slideShow.visible
    // the photo the gallery's selection or the photo view is on
    readonly property string selectedPath: photoMode ? currentPath
                                         : (grid.currentIndex >= 0 && !folder.isDirAt(grid.currentIndex)
                                            ? folder.pathAt(grid.currentIndex) : "")

    function log(msg) {
        if (photosDebug)
            console.log("photos: " + msg);
    }

    PhotoFolder {
        id: folder
        sortBy: String(PhotoFiles.setting("sortBy", "date"))
        descending: String(PhotoFiles.setting("descending", "true")) === "true"
        onReloaded: window.afterReload()
        onLoadingChanged: if (!loading) window.log("folder " + folder.folder + " " + folder.photoCount + " photos " + (folder.count - folder.photoCount) + " folders")
    }

    Component.onCompleted: {
        folder.folder = initialFolder || PhotoFiles.picturesFolder();
        if (initialPhoto)
            showPhoto(initialPhoto);
        else
            grid.forceActiveFocus();
        log("ready " + folder.folder);
    }

    onClosing: (close) => {
        if (slideShow.visible)
            slideShow.stop();
        if (editorOwns && PhotoEditor.modified && !quitting) {
            close.accepted = false;
            confirmDiscard(function () {
                window.quitting = true;
                window.close();
            });
        }
    }

    // ------------------------------------------------------------ the gallery

    // the folder was read again: keep the selection on the same photo
    property string keepSelected: ""
    function afterReload() {
        var row = folder.rowOf(keepSelected || selectedPath);
        grid.currentIndex = row >= 0 ? row : (grid.currentIndex >= 0 ? Math.min(grid.currentIndex, folder.count - 1) : -1);
        keepSelected = "";
        // the photo shown was deleted or renamed elsewhere
        if (photoMode && !Win95Fs.isFile(currentPath) && !editorOwns)
            showGallery();
    }

    function openFolder(path) {
        if (!path || !Win95Fs.isDir(path))
            return;
        leavePhoto(function () {
            window.keepSelected = "";
            folder.folder = path;
            window.mode = "gallery";
            grid.currentIndex = -1;
            grid.forceActiveFocus();
            window.log("open folder " + path);
        });
    }

    function goUp() {
        var up = Win95Fs.parentDir(folder.folder);
        if (up && up !== folder.folder) {
            keepSelected = folder.folder;
            var from = folder.folder;
            leavePhoto(function () {
                folder.folder = up;
                window.mode = "gallery";
                window.keepSelected = from;
                grid.forceActiveFocus();
            });
        }
    }

    function openRow(row) {
        if (row < 0)
            return;
        if (folder.isDirAt(row))
            openFolder(folder.pathAt(row));
        else
            showPhoto(folder.pathAt(row));
    }

    function setThumbSize(s) {
        thumbSize = s;
        PhotoFiles.setSetting("thumbSize", s);
    }

    function setSort(by, desc) {
        keepSelected = selectedPath;
        folder.sortBy = by;
        folder.descending = desc;
        PhotoFiles.setSetting("sortBy", by);
        PhotoFiles.setSetting("descending", desc ? "true" : "false");
    }

    // ------------------------------------------------------------- the photo

    function showPhoto(path) {
        if (!path || (photoMode && path === currentPath))
            return;
        leavePhoto(function () {
            // a photo from another folder: that folder's gallery behind it
            var dir = Win95Fs.parentDir(path);
            if (dir !== folder.folder) {
                window.keepSelected = path;
                folder.folder = dir;
            }
            window.currentPath = path;
            window.mode = "photo";
            photoView.fit();
            panel.resetSliders();
            photoView.forceActiveFocus();
            var row = folder.rowOf(path);
            if (row >= 0)
                grid.currentIndex = row;
            window.log("show " + path);
        }, true);
    }

    function showGallery() {
        leavePhoto(function () {
            window.mode = "gallery";
            window.cropping = false;
            var row = folder.rowOf(window.currentPath);
            if (row >= 0) {
                grid.currentIndex = row;
                grid.positionViewAtIndex(row, GridView.Contain);
            }
            grid.forceActiveFocus();
            window.log("gallery");
        });
    }

    // leaving the photo (for another, or the gallery): its changes saved
    // or let go first. keepEditing: the edit panel stays open
    function leavePhoto(then, keepEditing) {
        confirmDiscard(function () {
            window.cropping = false;
            if (!keepEditing)
                window.editing = false;
            PhotoEditor.close();
            then();
        });
    }

    function step(dir) {
        if (folder.photoCount === 0)
            return;
        var p = folder.stepFrom(currentPath, dir);
        if (p && p !== currentPath)
            showPhoto(p);
    }

    // ----------------------------------------------------------------- editing

    // the photo shown, into the editor (edits start from the file)
    function ensureEditor() {
        if (editorOwns)
            return true;
        if (!currentPath)
            return false;
        var error = PhotoEditor.load(currentPath);
        if (error) {
            msgBox.show("Photo Gallery", error, "error", ["OK"], refocus);
            return false;
        }
        log("editing " + currentPath);
        return true;
    }

    function setEditing(on) {
        if (!photoMode) {
            if (!on || !selectedPath)
                return;
            showPhoto(selectedPath);
        }
        editing = on;
        if (!on) {
            cropping = false;
            panel.resetSliders();
            PhotoEditor.cancelPreview();
        }
        log("edit panel " + on);
    }

    function edit(what) {
        if (!photoMode && selectedPath)
            showPhoto(selectedPath);
        if (!ensureEditor())
            return;
        cropping = false;
        switch (what) {
        case "rotateLeft": PhotoEditor.rotate(-90); break;
        case "rotateRight": PhotoEditor.rotate(90); break;
        case "flipH": PhotoEditor.flip(true); break;
        case "flipV": PhotoEditor.flip(false); break;
        case "grayscale": PhotoEditor.grayscale(); break;
        case "sepia": PhotoEditor.sepia(); break;
        case "enhance": PhotoEditor.autoEnhance(); break;
        }
        photoView.fit();
        log("edit " + what + " " + PhotoEditor.width + "x" + PhotoEditor.height);
    }

    function preview(b, c, s) {
        if (ensureEditor())
            PhotoEditor.preview(b, c, s);
    }

    function adjust(b, c, s) {
        if (!ensureEditor())
            return;
        PhotoEditor.adjust(b, c, s);
        log("adjust " + b + " " + c + " " + s);
    }

    function startCrop() {
        if (!photoMode || !ensureEditor())
            return;
        editing = true;
        cropping = true;
        log("crop mode");
    }

    function applyCrop() {
        if (!cropping)
            return;
        var r = photoView.cropRect;
        cropping = false;
        PhotoEditor.crop(Math.round(r.x), Math.round(r.y), Math.round(r.width), Math.round(r.height));
        photoView.fit();
        log("crop " + PhotoEditor.width + "x" + PhotoEditor.height);
        refocus();
    }

    function undo() {
        if (!editorOwns || !PhotoEditor.canUndo)
            return;
        cropping = false;
        PhotoEditor.undo();
        log("undo " + PhotoEditor.width + "x" + PhotoEditor.height);
    }

    function redo() {
        if (!editorOwns || !PhotoEditor.canRedo)
            return;
        cropping = false;
        PhotoEditor.redo();
        log("redo " + PhotoEditor.width + "x" + PhotoEditor.height);
    }

    function revert() {
        if (!editorOwns || !PhotoEditor.modified)
            return;
        cropping = false;
        PhotoEditor.revert();
        log("revert");
    }

    function showResize() {
        if (!photoMode || !ensureEditor())
            return;
        cropping = false;
        resizeDialog.open();
    }

    // ------------------------------------------------------------------ saving

    readonly property var saveFilters: [
        { label: "JPEG Pictures (*.jpg)", patterns: ["*.jpg", "*.jpeg"], suffix: "jpg" },
        { label: "PNG Pictures (*.png)", patterns: ["*.png"], suffix: "png" },
        { label: "Bitmap Pictures (*.bmp)", patterns: ["*.bmp"], suffix: "bmp" },
    ]

    function canWrite(path) {
        var s = Win95Fs.suffix(path).toLowerCase();
        return PhotoFiles.writeSuffixes.indexOf(s === "jpeg" ? "jpg" : s) >= 0;
    }

    // Save: over the original, after asking
    function save(then) {
        if (!editorOwns || !PhotoEditor.modified) {
            if (then)
                then();
            return;
        }
        if (!canWrite(currentPath)) {
            saveAs(then);
            return;
        }
        msgBox.show("Photo Gallery",
                    "Replace the original " + Win95Fs.fileName(currentPath) + " with your changes?\n\n"
                    + "The original photo can't be got back afterwards. To keep it, choose Save As.",
                    "warning", ["Replace", "Save As...", "Cancel"], function (i) {
                        if (i === 0)
                            window.writeTo(window.currentPath, then);
                        else if (i === 1)
                            window.saveAs(then);
                        else
                            window.refocus();
                    });
    }

    property var afterSave: null
    function saveAs(then) {
        if (!editorOwns && !ensureEditor())
            return;
        afterSave = then || null;
        var name = Win95Fs.fileName(currentPath);
        var dot = name.lastIndexOf(".");
        var base = dot > 0 ? name.substring(0, dot) : name;
        var suffix = canWrite(currentPath) ? name.substring(dot + 1) : "jpg";
        var fi = suffix.toLowerCase() === "png" ? 1 : suffix.toLowerCase() === "bmp" ? 2 : 0;
        saveDialog.filterIndex = fi;
        saveDialog.defaultSuffix = saveFilters[fi].suffix;
        saveDialog.openAt(Win95Fs.parentDir(currentPath), base + " (edited)." + suffix);
    }

    function writeTo(path, then) {
        var error = PhotoEditor.save(path);
        if (error) {
            msgBox.show("Photo Gallery", error, "error", ["OK"], refocus);
            return;
        }
        log("saved " + path);
        currentPath = PhotoEditor.path;
        keepSelected = currentPath;
        if (then)
            then();
        else
            refocus();
    }

    // a changed photo: save it (over the original: the Yes is the asking),
    // let the changes go, or stay
    function confirmDiscard(then) {
        if (!editorOwns || !PhotoEditor.modified) {
            then();
            return;
        }
        msgBox.show("Photo Gallery", "Save changes to " + Win95Fs.fileName(currentPath) + "?",
                    "warning", ["Yes", "No", "Cancel"], function (i) {
                        if (i === 0) {
                            if (window.canWrite(window.currentPath))
                                window.writeTo(window.currentPath, then);
                            else
                                window.saveAs(then);
                        } else if (i === 1) {
                            PhotoEditor.close();
                            then();
                        } else {
                            window.refocus();
                        }
                    });
    }

    // --------------------------------------------------------- photo actions

    function deletePhoto() {
        var path = selectedPath;
        if (!path)
            return;
        msgBox.show("Confirm File Delete", "Are you sure you want to send '" + Win95Fs.fileName(path)
                    + "' to the Recycle Bin?", "question", ["Yes", "No"], function (i) {
                        if (i === 0)
                            window.trashPhoto(path);
                        window.refocus();
                    });
    }

    // (asked already) to the Recycle Bin, and on to the next photo
    function trashPhoto(path) {
        var next = folder.photoCount > 1 ? folder.stepFrom(path, 1) : "";
        var row = folder.rowOf(path);
        var error = PhotoFiles.moveToTrash(path);
        if (error) {
            msgBox.show("Photo Gallery", error, "error", ["OK"], window.refocus);
            return;
        }
        log("deleted " + path);
        if (PhotoEditor.path === path)
            PhotoEditor.close();
        folder.forget(path);
        if (photoMode) {
            if (next && next !== path) {
                currentPath = "";
                showPhoto(next);
            } else {
                showGallery();
            }
        } else {
            grid.currentIndex = Math.min(row, folder.count - 1);
        }
    }

    function setWallpaper(how) {
        var path = selectedPath;
        if (!path)
            return;
        var apply = function () {
            Wayland95Settings.wallpaper = path;
            Wayland95Settings.wallpaperMode = how;
            window.log("wallpaper " + how + " " + path);
            window.refocus();
        };
        if (editorOwns && PhotoEditor.modified && path === currentPath) {
            msgBox.show("Photo Gallery", "The wallpaper is the photo as it's saved. Save your changes first?",
                        "question", ["Yes", "No", "Cancel"], function (i) {
                            if (i === 0)
                                window.save(apply);
                            else if (i === 1)
                                apply();
                            else
                                window.refocus();
                        });
            return;
        }
        apply();
    }

    function showProperties() {
        var path = selectedPath;
        if (!path)
            return;
        propertiesDialog.rows = PhotoFiles.properties(path);
        propertiesDialog.title = Win95Fs.fileName(path) + " Properties";
        propertiesDialog.show();
        log("properties " + path);
    }

    function startSlideShow() {
        if (folder.photoCount === 0)
            return;
        leavePhoto(function () {
            slideShow.start(window.selectedPath || window.currentPath);
            window.log("slide show");
        }, true);
    }

    function runService(id, action) {
        var paths = selectedPath ? [selectedPath] : [];
        if (!PhotoServices.run(id, action, paths, folder.folder))
            msgBox.show("Photo Gallery", "That photo service can't do that.", "error", ["OK"], refocus);
    }

    Connections {
        target: PhotoServices
        function onProgress(name, percent, message) { window.serviceStatus = name + ": " + message + " (" + percent + "%)"; }
        function onImported(paths) { folder.refresh(); }
        function onFinished(name, action, ok, message) {
            window.serviceStatus = "";
            msgBox.show(name, message || (ok ? "Done." : "That didn't work."), ok ? "info" : "error", ["OK"], window.refocus);
        }
    }

    function refocus() {
        if (photoMode)
            photoView.forceActiveFocus();
        else
            grid.forceActiveFocus();
    }

    // ------------------------------------------------------------------ menus

    // File > Import From / Export To / Share: the photo services that can
    function serviceMenu(action, flag) {
        var items = [];
        var list = PhotoServices.services;
        for (var i = 0; i < list.length; ++i) {
            if (!list[i][flag])
                continue;
            (function (id) {
                items.push({ text: list[i].name, enabled: action === "import" || !!window.selectedPath,
                             action: function () { window.runService(id, action); } });
            })(list[i].id);
        }
        if (items.length === 0)
            items.push({ text: "(No photo services installed)", enabled: false });
        return items;
    }

    function wallpaperMenu() {
        return [
            { text: "&Centered", action: function () { window.setWallpaper("center"); } },
            { text: "&Tiled", action: function () { window.setWallpaper("tile"); } },
            { text: "&Stretched", action: function () { window.setWallpaper("stretch"); } },
        ];
    }

    function fileMenu() {
        var has = !!selectedPath;
        var changed = editorOwns && PhotoEditor.modified;
        return [
            { text: "&Open Folder...", shortcut: "Ctrl+O", action: function () { folderDialog.openAt(folder.folder, ""); } },
            { text: "Open &Photo...", action: function () { fileDialog.openAt(folder.folder, ""); } },
            { separator: true },
            { text: "&Save", shortcut: "Ctrl+S", enabled: changed, action: function () { window.save(null); } },
            { text: "Save &As...", enabled: photoMode, action: function () { window.saveAs(null); } },
            { separator: true },
            { text: "&Import From", enabled: !PhotoServices.busy, submenu: serviceMenu("import", "canImport") },
            { text: "E&xport To", enabled: !PhotoServices.busy, submenu: serviceMenu("export", "canExport") },
            { text: "S&hare", enabled: !PhotoServices.busy, submenu: serviceMenu("share", "canShare") },
            { separator: true },
            { text: "Set as &Wallpaper", enabled: has, submenu: wallpaperMenu() },
            { text: "&Delete", shortcut: "Del", enabled: has, action: window.deletePhoto },
            { text: "P&roperties", shortcut: "Alt+Enter", enabled: has, action: window.showProperties },
            { separator: true },
            { text: "E&xit", action: window.close },
        ];
    }

    function editMenu() {
        var on = photoMode;
        var u = editorOwns && PhotoEditor.canUndo, r = editorOwns && PhotoEditor.canRedo;
        return [
            { text: u ? "&Undo " + PhotoEditor.undoText : "&Undo", shortcut: "Ctrl+Z", enabled: u, action: window.undo },
            { text: r ? "&Redo " + PhotoEditor.redoText : "&Redo", shortcut: "Ctrl+Y", enabled: r, action: window.redo },
            { separator: true },
            { text: "Edit &Panel", shortcut: "E", checked: editing, enabled: !!selectedPath,
              action: function () { window.setEditing(!window.editing); } },
            { separator: true },
            { text: "Rotate &Left", shortcut: "Ctrl+L", enabled: !!selectedPath, action: function () { window.edit("rotateLeft"); } },
            { text: "Rotate R&ight", shortcut: "Ctrl+R", enabled: !!selectedPath, action: function () { window.edit("rotateRight"); } },
            { text: "Flip &Horizontal", enabled: !!selectedPath, action: function () { window.edit("flipH"); } },
            { text: "Flip &Vertical", enabled: !!selectedPath, action: function () { window.edit("flipV"); } },
            { separator: true },
            { text: "&Crop...", shortcut: "C", enabled: on, action: window.startCrop },
            { text: "Re&size...", enabled: on, action: window.showResize },
            { text: "Light and C&olor...", enabled: on, action: function () { window.setEditing(true); } },
            { separator: true },
            { text: "&Auto-Enhance", enabled: !!selectedPath, action: function () { window.edit("enhance"); } },
            { text: "&Black and White", enabled: !!selectedPath, action: function () { window.edit("grayscale"); } },
            { text: "Se&pia", enabled: !!selectedPath, action: function () { window.edit("sepia"); } },
            { separator: true },
            { text: "Revert to Sa&ved", enabled: editorOwns && PhotoEditor.modified, action: window.revert },
        ];
    }

    function arrangeMenu() {
        return [
            { text: "by &Date, Newest First", checked: folder.sortBy === "date" && folder.descending, action: function () { window.setSort("date", true); } },
            { text: "by D&ate, Oldest First", checked: folder.sortBy === "date" && !folder.descending, action: function () { window.setSort("date", false); } },
            { text: "by &Name, A to Z", checked: folder.sortBy === "name" && !folder.descending, action: function () { window.setSort("name", false); } },
            { text: "by N&ame, Z to A", checked: folder.sortBy === "name" && folder.descending, action: function () { window.setSort("name", true); } },
        ];
    }

    function viewMenu() {
        var g = !photoMode, p = photoMode;
        return [
            { text: "&Gallery", shortcut: "Esc", checked: g, action: window.showGallery },
            { text: "&Photo", shortcut: "Enter", checked: p, enabled: !!selectedPath,
              action: function () { window.showPhoto(window.selectedPath); } },
            { text: "Sli&de Show", shortcut: "F11", enabled: folder.photoCount > 0, action: window.startSlideShow },
            { separator: true },
            { text: "&Toolbar", checked: showToolbar, action: function () { window.showToolbar = !window.showToolbar; } },
            { text: "&Status Bar", checked: showStatusBar, action: function () { window.showStatusBar = !window.showStatusBar; } },
            { separator: true },
            { text: "S&mall Thumbnails", checked: thumbSize === 96, action: function () { window.setThumbSize(96); } },
            { text: "M&edium Thumbnails", checked: thumbSize === 128, action: function () { window.setThumbSize(128); } },
            { text: "&Large Thumbnails", checked: thumbSize === 192, action: function () { window.setThumbSize(192); } },
            { text: "Arrange &Icons", submenu: arrangeMenu() },
            { separator: true },
            { text: "Zoom &In", shortcut: "+", enabled: p, action: photoView.zoomIn },
            { text: "Zoom &Out", shortcut: "-", enabled: p, action: photoView.zoomOut },
            { text: "&Actual Size", shortcut: "1", enabled: p, action: photoView.actualSize },
            { text: "&Best Fit", shortcut: "0", enabled: p, action: photoView.fit },
            { separator: true },
            { text: "Pre&vious Photo", shortcut: "Left", enabled: p, action: function () { window.step(-1); } },
            { text: "&Next Photo", shortcut: "Right", enabled: p, action: function () { window.step(1); } },
            { separator: true },
            { text: "&Refresh", shortcut: "F5", action: function () { folder.refresh(); } },
        ];
    }

    function helpMenu() {
        return [
            { text: "&About Photo Gallery", action: function () {
                msgBox.show("About Photo Gallery",
                            "Photo Gallery\nWayland 95 version " + Wayland95Settings.version
                            + "\n\nBrowse, show and touch up your photos.", "info", ["OK"], window.refocus);
            } },
        ];
    }

    // the right-click menu, for the photo (or folder) under the mouse
    function contextMenu() {
        if (!photoMode && grid.currentIndex >= 0 && folder.isDirAt(grid.currentIndex))
            return [{ text: "&Open", action: function () { window.openRow(grid.currentIndex); } }];
        var has = !!selectedPath;
        return [
            { text: photoMode ? "Back to &Gallery" : "&Open", enabled: has,
              action: photoMode ? window.showGallery : function () { window.showPhoto(window.selectedPath); } },
            { text: "&Edit", enabled: has, action: function () { window.setEditing(true); } },
            { separator: true },
            { text: "Rotate &Left", enabled: has, action: function () { window.edit("rotateLeft"); } },
            { text: "Rotate &Right", enabled: has, action: function () { window.edit("rotateRight"); } },
            { separator: true },
            { text: "Set as &Wallpaper", enabled: has, submenu: wallpaperMenu() },
            { text: "S&hare", enabled: has && !PhotoServices.busy, submenu: serviceMenu("share", "canShare") },
            { separator: true },
            { text: "&Delete", enabled: has, action: window.deletePhoto },
            { text: "P&roperties", enabled: has, action: window.showProperties },
        ];
    }

    // W95_PHOTOS_SCRIPT (the tests): actions, ";"-separated, a moment apart.
    // show:PATH  gallery  step:N  folder:PATH  sort:date|name,asc|desc
    // edit:rotateLeft|rotateRight|flipH|flipV|grayscale|sepia|enhance
    // crop:X,Y,W,H  resize:W,H  adjust:B,C,S  preview:B,C,S  undo  redo
    // revert  write  saveas:PATH  props  trash  slideshow  stopshow  quit
    // panel:on|off  cropmode  zoom:in|out|1|fit  grab:PNG
    property var scriptSteps: photosScript ? photosScript.split(";") : []
    Timer {
        id: scriptTimer
        interval: 700
        repeat: true
        running: window.scriptSteps.length > 0
        onTriggered: {
            if (folder.loading || photoView.status === Image.Loading)
                return;
            var stepText = window.scriptSteps.shift();
            window.scriptSteps = window.scriptSteps;
            if (window.scriptSteps.length === 0)
                stop();
            window.runStep(stepText);
        }
    }
    function runStep(stepText) {
        var at = stepText.indexOf(":");
        var cmd = at < 0 ? stepText : stepText.substring(0, at);
        var arg = at < 0 ? "" : stepText.substring(at + 1);
        var n = arg.split(",").map(Number);
        log("step " + stepText);
        switch (cmd) {
        case "show": showPhoto(arg); break;
        case "gallery": showGallery(); break;
        case "step": step(n[0]); break;
        case "folder": openFolder(arg); break;
        case "sort": setSort(arg.split(",")[0], arg.split(",")[1] === "desc"); break;
        case "edit": edit(arg); break;
        case "panel": setEditing(arg !== "off"); break;
        case "cropmode": startCrop(); break;
        case "zoom": if (arg === "in") photoView.zoomIn(); else if (arg === "out") photoView.zoomOut(); else if (arg === "1") photoView.actualSize(); else photoView.fit(); log("zoom " + photoView.scale); break;
        case "crop": if (ensureEditor()) { cropping = true; photoView.cropRect = Qt.rect(n[0], n[1], n[2], n[3]); applyCrop(); } break;
        case "resize": if (ensureEditor()) { PhotoEditor.resize(n[0], n[1]); log("resize " + PhotoEditor.width + "x" + PhotoEditor.height); } break;
        case "adjust": adjust(n[0], n[1], n[2]); break;
        case "preview": preview(n[0], n[1], n[2]); log("preview " + PhotoEditor.previewing); break;
        case "undo": undo(); break;
        case "redo": redo(); break;
        case "revert": revert(); break;
        case "write": writeTo(currentPath, null); break;
        case "saveas": writeTo(arg, null); break;
        case "props": {
            var rows = PhotoFiles.properties(selectedPath);
            for (var i = 0; i < rows.length; ++i)
                log("prop " + rows[i].label + " " + rows[i].value);
            break;
        }
        case "trash": trashPhoto(selectedPath); break;
        case "slideshow": startSlideShow(); break;
        case "stopshow": slideShow.stop(); break;
        case "grab": window.contentItem.grabToImage(function (r) { r.saveToFile(arg); window.log("grabbed " + arg); }); break;
        case "quit": quitting = true; Qt.quit(); break;
        }
        log("state mode=" + mode + " path=" + currentPath + " modified=" + PhotoEditor.modified
            + " size=" + photoView.imageWidth + "x" + photoView.imageHeight
            + " undo=" + PhotoEditor.canUndo + " redo=" + PhotoEditor.canRedo + " row=" + grid.currentIndex);
    }

    // --------------------------------------------------------------- shortcuts

    readonly property bool keys: !modalOpen
    Shortcut { sequence: "Ctrl+O"; enabled: window.keys; onActivated: folderDialog.openAt(folder.folder, "") }
    Shortcut { sequence: "Ctrl+S"; enabled: window.keys; onActivated: window.save(null) }
    Shortcut { sequences: ["Ctrl+Z"]; enabled: window.keys; onActivated: window.undo() }
    Shortcut { sequences: ["Ctrl+Y", "Ctrl+Shift+Z"]; enabled: window.keys; onActivated: window.redo() }
    Shortcut { sequence: "Ctrl+L"; enabled: window.keys && !!window.selectedPath; onActivated: window.edit("rotateLeft") }
    Shortcut { sequence: "Ctrl+R"; enabled: window.keys && !!window.selectedPath; onActivated: window.edit("rotateRight") }
    Shortcut { sequence: "Del"; enabled: window.keys && !window.cropping && !!window.selectedPath; onActivated: window.deletePhoto() }
    Shortcut { sequences: ["Alt+Return", "Alt+Enter"]; enabled: window.keys && !!window.selectedPath; onActivated: window.showProperties() }
    Shortcut { sequence: "F11"; enabled: window.keys; onActivated: window.startSlideShow() }
    Shortcut { sequence: "F5"; enabled: window.keys; onActivated: folder.refresh() }

    // ----------------------------------------------------------------- layout

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
            { title: "Help", items: window.helpMenu },
        ]
    }

    component Etch: Item {
        width: parent ? parent.width : 0
        height: 2
        Rectangle { width: parent.width; height: 1; color: Theme.shadow }
        Rectangle { y: 1; width: parent.width; height: 1; color: Theme.highlight }
    }
    component BarSep: Item {
        width: 8
        height: 22
        Rectangle { x: 3; y: 2; width: 1; height: 18; color: Theme.shadow }
        Rectangle { x: 4; y: 2; width: 1; height: 18; color: Theme.highlight }
    }

    Column {
        id: bars
        anchors.top: menuBar.bottom
        anchors.topMargin: 1
        width: parent.width

        Etch {}

        Row {
            id: toolbar
            visible: window.showToolbar
            x: 4
            height: 28
            topPadding: 3
            spacing: 0

            PhotoButton { icon: "gallery"; tip: "Shows the folder's photos (Esc)."; checked: !window.photoMode; onClicked: window.showGallery() }
            PhotoButton { icon: "open"; tip: "Opens another folder of photos (Ctrl+O)."; onClicked: folderDialog.openAt(folder.folder, "") }
            PhotoButton { icon: "up"; tip: "Goes to the folder this one is in."; enabled: Win95Fs.parentDir(folder.folder) !== folder.folder; onClicked: window.goUp() }
            PhotoButton { icon: "save"; tip: "Saves your changes to the photo (Ctrl+S)."; enabled: window.editorOwns && PhotoEditor.modified; onClicked: window.save(null) }
            BarSep {}
            PhotoButton { icon: "previous"; tip: "Shows the previous photo (Left)."; enabled: window.photoMode && folder.photoCount > 1; onClicked: window.step(-1) }
            PhotoButton { icon: "next"; tip: "Shows the next photo (Right)."; enabled: window.photoMode && folder.photoCount > 1; onClicked: window.step(1) }
            PhotoButton { icon: "slideshow"; tip: "Shows the photos full-screen, one after another (F11)."; enabled: folder.photoCount > 0; onClicked: window.startSlideShow() }
            BarSep {}
            PhotoButton { icon: "zoomIn"; tip: "Makes the photo bigger (+)."; enabled: window.photoMode; onClicked: photoView.zoomIn() }
            PhotoButton { icon: "zoomOut"; tip: "Makes the photo smaller (-)."; enabled: window.photoMode; onClicked: photoView.zoomOut() }
            PhotoButton { icon: "fit"; tip: "Fits the whole photo in the window (0)."; enabled: window.photoMode; checked: window.photoMode && photoView.zoom === 0; onClicked: photoView.fit() }
            PhotoButton { icon: "actual"; tip: "Shows the photo at its actual size (1)."; enabled: window.photoMode; onClicked: photoView.actualSize() }
            BarSep {}
            PhotoButton { icon: "rotateLeft"; tip: "Turns the photo a quarter left (Ctrl+L)."; enabled: !!window.selectedPath; onClicked: window.edit("rotateLeft") }
            PhotoButton { icon: "rotateRight"; tip: "Turns the photo a quarter right (Ctrl+R)."; enabled: !!window.selectedPath; onClicked: window.edit("rotateRight") }
            PhotoButton { icon: "adjust"; tip: "Shows the editing tools: crop, size, light and color, effects (E)."; enabled: !!window.selectedPath; checked: window.editing && window.photoMode; onClicked: window.setEditing(!window.editing) }
            PhotoButton { icon: "undo"; tip: "Undoes the last change (Ctrl+Z)."; enabled: window.editorOwns && PhotoEditor.canUndo; onClicked: window.undo() }
            PhotoButton { icon: "redo"; tip: "Does the undone change again (Ctrl+Y)."; enabled: window.editorOwns && PhotoEditor.canRedo; onClicked: window.redo() }
            BarSep {}
            PhotoButton { icon: "wallpaper"; tip: "Makes the photo the desktop's wallpaper."; enabled: !!window.selectedPath
                onClicked: { var p = mapToItem(popup, 0, height); popup.popup(p.x, p.y, window.wallpaperMenu()); } }
            PhotoButton { icon: "delete"; tip: "Sends the photo to the Recycle Bin (Del)."; enabled: !!window.selectedPath; onClicked: window.deletePhoto() }
            PhotoButton { icon: "info"; tip: "Shows the photo's size, dimensions, date and camera (Alt+Enter)."; enabled: !!window.selectedPath; onClicked: window.showProperties() }
        }
        Etch { visible: window.showToolbar }

        // the gallery's address: which folder
        Row {
            id: addressBar
            visible: !window.photoMode
            x: 4
            height: 28
            topPadding: 3
            spacing: 6
            Text {
                height: 21
                verticalAlignment: Text.AlignVCenter
                text: "Folder:"
                color: Theme.text
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontSize
            }
            TextField95 {
                id: addressField
                width: window.width - 60
                text: folder.folder
                onAccepted: {
                    var p = Win95Fs.resolve(folder.folder, addressField.text);
                    if (Win95Fs.isDir(p))
                        window.openFolder(p);
                    else if (Win95Fs.isFile(p))
                        window.showPhoto(p);
                    else
                        msgBox.show("Photo Gallery", "Cannot find '" + addressField.text + "'.", "error", ["OK"],
                                    function () { addressField.forceActiveFocus(); });
                }
                onEscaped: {
                    addressField.text = folder.folder;
                    grid.forceActiveFocus();
                }
            }
        }
        Etch { visible: addressBar.visible }
    }

    // the status bar shows what a button does while the mouse is over it
    function buttonTip(row) {
        for (var i = 0; i < row.children.length; ++i) {
            var c = row.children[i];
            if (c.hovered && c.tip)
                return c.tip;
        }
        return "";
    }

    Item {
        id: content
        anchors.top: bars.bottom
        anchors.topMargin: 2
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: statusBar.visible ? statusBar.top : parent.bottom
        anchors.bottomMargin: statusBar.visible ? 2 : 0

        // ---- the gallery
        Bevel {
            id: galleryWell
            anchors.fill: parent
            visible: !window.photoMode
            style: "sunken"
            face: Theme.window

            GridView {
                id: grid
                x: 2
                y: 2
                width: parent.width - 4 - gridBar.width
                height: parent.height - 4
                clip: true
                focus: true
                interactive: false
                model: folder
                currentIndex: -1
                readonly property int columns: Math.max(1, Math.floor(width / (window.thumbSize + 24)))
                cellWidth: Math.floor(width / columns)
                cellHeight: window.thumbSize + 40
                keyNavigationEnabled: true
                highlightFollowsCurrentItem: false
                onCurrentIndexChanged: if (currentIndex >= 0) positionViewAtIndex(currentIndex, GridView.Contain)

                delegate: Item {
                    id: cell
                    required property int index
                    required property string path
                    required property string name
                    required property bool isDir
                    required property var stamp
                    required property string dateText
                    readonly property bool selected: GridView.isCurrentItem

                    width: grid.cellWidth
                    height: grid.cellHeight

                    Item {
                        id: frame
                        x: (parent.width - window.thumbSize) / 2
                        y: 6
                        width: window.thumbSize
                        height: window.thumbSize

                        Image {
                            id: thumb
                            visible: !cell.isDir
                            anchors.centerIn: parent
                            width: Math.min(parent.width, implicitWidth)
                            height: Math.min(parent.height, implicitHeight)
                            fillMode: Image.PreserveAspectFit
                            source: cell.isDir ? "" : PhotoFiles.thumbUrl(cell.path, cell.stamp)
                            sourceSize: Qt.size(window.thumbSize, window.thumbSize)
                            asynchronous: true
                            smooth: true
                            // a thin frame around the picture, as a print's border
                            Rectangle {
                                anchors.fill: parent
                                anchors.margins: -1
                                color: "transparent"
                                border.color: cell.selected ? Theme.selection : Theme.shadow
                                border.width: cell.selected ? 2 : 1
                                visible: thumb.status === Image.Ready
                            }
                            // selected: tinted, as Win95 tints a selected icon
                            Rectangle {
                                anchors.fill: parent
                                color: Theme.selection
                                opacity: cell.selected && grid.activeFocus ? 0.25 : 0
                            }
                        }
                        Text {
                            anchors.centerIn: parent
                            visible: !cell.isDir && thumb.status !== Image.Ready
                            text: thumb.status === Image.Error ? "?" : "..."
                            color: Theme.disabledText
                            font.family: Theme.fontFamily
                            font.pixelSize: Theme.fontSize
                        }
                        Icon95 {
                            visible: cell.isDir
                            anchors.centerIn: parent
                            name: "folder"
                            size: Math.round(window.thumbSize / 2)
                        }
                    }

                    Rectangle {
                        id: label
                        anchors.horizontalCenter: parent.horizontalCenter
                        anchors.top: frame.bottom
                        anchors.topMargin: 6
                        width: Math.min(cell.width - 6, labelText.implicitWidth + 4)
                        height: labelText.implicitHeight + 2
                        color: cell.selected && grid.activeFocus ? Theme.selection : "transparent"
                        FocusRect95 { anchors.fill: parent; visible: cell.selected && grid.activeFocus }
                        Text {
                            id: labelText
                            x: 2
                            y: 1
                            width: parent.width - 4
                            horizontalAlignment: Text.AlignHCenter
                            elide: Text.ElideMiddle
                            text: cell.name
                            color: cell.selected && grid.activeFocus ? Theme.selectionText : Theme.text
                            font.family: Theme.fontFamily
                            font.pixelSize: Theme.fontSize
                        }
                    }

                    MouseArea {
                        anchors.fill: parent
                        acceptedButtons: Qt.LeftButton | Qt.RightButton
                        onPressed: (mouse) => {
                            grid.currentIndex = cell.index;
                            grid.forceActiveFocus();
                            if (mouse.button === Qt.RightButton) {
                                var p = mapToItem(popup, mouse.x, mouse.y);
                                popup.popup(p.x, p.y, window.contextMenu());
                            }
                        }
                        onDoubleClicked: (mouse) => {
                            if (mouse.button === Qt.LeftButton)
                                window.openRow(cell.index);
                        }
                    }
                }

                // a click in the empty space: nothing selected
                MouseArea {
                    z: -1
                    width: grid.width
                    height: Math.max(grid.height, grid.contentHeight)
                    acceptedButtons: Qt.LeftButton | Qt.RightButton
                    onPressed: (mouse) => {
                        grid.currentIndex = -1;
                        grid.forceActiveFocus();
                        if (mouse.button === Qt.RightButton) {
                            var p = mapToItem(popup, mouse.x, mouse.y);
                            popup.popup(p.x, p.y, [
                                { text: "Arrange &Icons", submenu: window.arrangeMenu() },
                                { text: "&Refresh", action: function () { folder.refresh(); } },
                            ]);
                        }
                    }
                }

                Keys.onPressed: (event) => {
                    if ((event.key === Qt.Key_Return || event.key === Qt.Key_Enter) && !(event.modifiers & Qt.AltModifier)) {
                        window.openRow(grid.currentIndex);
                    } else if (event.key === Qt.Key_Backspace) {
                        window.goUp();
                    } else if (event.key === Qt.Key_E && !event.modifiers && window.selectedPath) {
                        window.setEditing(true);
                    } else if (event.key === Qt.Key_Home) {
                        grid.currentIndex = folder.count > 0 ? 0 : -1;
                    } else if (event.key === Qt.Key_End) {
                        grid.currentIndex = folder.count - 1;
                    } else if (event.key === Qt.Key_PageDown || event.key === Qt.Key_PageUp) {
                        var rows = Math.max(1, Math.floor(grid.height / grid.cellHeight));
                        var d = rows * grid.columns * (event.key === Qt.Key_PageDown ? 1 : -1);
                        grid.currentIndex = Math.max(0, Math.min(folder.count - 1, grid.currentIndex + d));
                    } else if (event.key === Qt.Key_Menu) {
                        popup.popup(grid.width / 2, grid.height / 2, window.contextMenu());
                    } else {
                        // (arrows: GridView's own; a first press selects the first)
                        if (grid.currentIndex < 0 && folder.count > 0
                                && [Qt.Key_Left, Qt.Key_Right, Qt.Key_Up, Qt.Key_Down].indexOf(event.key) >= 0) {
                            grid.currentIndex = 0;
                            event.accepted = true;
                        }
                        return;
                    }
                    event.accepted = true;
                }

                WheelHandler {
                    acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad
                    onWheel: (event) => gridBar.scrollBy(-event.angleDelta.y / 120 * grid.cellHeight / 2)
                }
            }

            ScrollBar95 {
                id: gridBar
                anchors.right: parent.right
                anchors.rightMargin: 2
                y: 2
                height: parent.height - 4
                flickable: grid
                stepSize: grid.cellHeight / 3
            }

            Text {
                anchors.centerIn: parent
                visible: folder.count === 0
                width: parent.width - 40
                horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.Wrap
                text: folder.loading ? "Looking for photos..."
                    : folder.error ? folder.error
                    : "There are no photos in this folder.\n\nFile > Open Folder chooses another."
                color: Theme.disabledText
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontSize
            }
        }

        // ---- the photo, and the editing tools beside it
        Bevel {
            visible: window.photoMode
            anchors.left: parent.left
            anchors.top: parent.top
            anchors.bottom: parent.bottom
            anchors.right: panel.visible ? panel.left : parent.right
            anchors.rightMargin: panel.visible ? 2 : 0
            style: "sunken"

            PhotoView {
                id: photoView
                x: 2
                y: 2
                width: parent.width - 4
                height: parent.height - 4
                focus: true
                source: !window.photoMode ? ""
                        : window.editorOwns ? PhotoEditor.imageUrl : PhotoFiles.viewUrl(window.currentPath)
                logicalWidth: window.editorOwns ? PhotoEditor.width : 0
                logicalHeight: window.editorOwns ? PhotoEditor.height : 0
                cropping: window.cropping
                aspectChoice: panel.aspectChoice
                onWheelStep: (dir) => window.step(dir)
                onContextMenu: (x, y) => {
                    var p = photoView.mapToItem(popup, x, y);
                    popup.popup(p.x, p.y, window.contextMenu());
                }
                onStatusChanged: if (status === Image.Ready) window.log("shown " + imageWidth + "x" + imageHeight)

                Keys.onPressed: (event) => {
                    var k = event.key, mods = event.modifiers & ~Qt.KeypadModifier;
                    if (window.cropping) {
                        if (k === Qt.Key_Return || k === Qt.Key_Enter)
                            window.applyCrop();
                        else if (k === Qt.Key_Escape)
                            window.cropping = false;
                        else
                            return;
                        event.accepted = true;
                        return;
                    }
                    if (mods & (Qt.ControlModifier | Qt.AltModifier))
                        return;
                    switch (k) {
                    case Qt.Key_Left:
                        if (!photoView.pan(-40, 0)) window.step(-1);
                        break;
                    case Qt.Key_Right:
                        if (!photoView.pan(40, 0)) window.step(1);
                        break;
                    case Qt.Key_Up:
                        if (!photoView.pan(0, -40)) window.step(-1);
                        break;
                    case Qt.Key_Down:
                        if (!photoView.pan(0, 40)) window.step(1);
                        break;
                    case Qt.Key_PageUp:
                    case Qt.Key_Backspace:
                        window.step(-1);
                        break;
                    case Qt.Key_PageDown:
                    case Qt.Key_Space:
                        window.step(1);
                        break;
                    case Qt.Key_Home:
                        window.showPhoto(folder.photoAt(0));
                        break;
                    case Qt.Key_End:
                        window.showPhoto(folder.photoAt(folder.photoCount - 1));
                        break;
                    case Qt.Key_Escape:
                    case Qt.Key_Return:
                    case Qt.Key_Enter:
                        window.showGallery();
                        break;
                    case Qt.Key_Plus:
                    case Qt.Key_Equal:
                        photoView.zoomIn();
                        break;
                    case Qt.Key_Minus:
                        photoView.zoomOut();
                        break;
                    case Qt.Key_0:
                        photoView.fit();
                        break;
                    case Qt.Key_1:
                        photoView.actualSize();
                        break;
                    case Qt.Key_E:
                        window.setEditing(!window.editing);
                        break;
                    case Qt.Key_C:
                        window.startCrop();
                        break;
                    case Qt.Key_Menu:
                        popup.popup(photoView.width / 2, photoView.height / 2, window.contextMenu());
                        break;
                    default:
                        return;
                    }
                    event.accepted = true;
                }
            }
        }

        EditPanel {
            id: panel
            visible: window.photoMode && window.editing
            anchors.right: parent.right
            anchors.top: parent.top
            anchors.bottom: parent.bottom
            host: window
            popupMenu: popup
            cropping: window.cropping
            aspectNames: photoView.aspectNames
            onCropRequested: window.startCrop()
            onCropApplied: window.applyCrop()
            onCropCancelled: { window.cropping = false; window.refocus(); }
        }
    }

    // -------------------------------------------------------------- status bar

    Item {
        id: statusBar
        visible: window.showStatusBar
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        height: 22

        readonly property string hint: {
            var t = window.buttonTip(toolbar);
            if (t)
                return t;
            if (window.serviceStatus)
                return window.serviceStatus;
            if (window.cropping)
                return "Drag a frame over the part to keep, then press Enter (Esc: cancel). "
                       + Math.round(photoView.cropRect.width) + " x " + Math.round(photoView.cropRect.height);
            if (window.photoMode) {
                var i = folder.photoIndexOf(window.currentPath);
                return Win95Fs.fileName(window.currentPath)
                        + (i >= 0 ? "    " + (i + 1) + " of " + folder.photoCount : "");
            }
            if (grid.currentIndex >= 0 && grid.currentItem)
                return grid.currentItem.name + (grid.currentItem.isDir ? "" : "    " + grid.currentItem.dateText);
            return folder.photoCount === 1 ? "1 photo" : folder.photoCount + " photos";
        }
        readonly property string info: {
            if (window.photoMode && photoView.imageWidth > 0)
                return photoView.imageWidth + " x " + photoView.imageHeight + "    " + Math.round(photoView.scale * 100) + "%";
            if (!window.photoMode)
                return folder.photoCount === 1 ? "1 photo" : folder.photoCount + " photos";
            return "";
        }

        Bevel {
            x: 2
            y: 2
            width: parent.width - infoBox.width - 22
            height: 19
            style: "sunken"
            Text {
                x: 4
                anchors.verticalCenter: parent.verticalCenter
                width: parent.width - 8
                elide: Text.ElideRight
                text: statusBar.hint
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontSize
                color: Theme.text
            }
        }
        Bevel {
            id: infoBox
            anchors.right: parent.right
            anchors.rightMargin: 16
            y: 2
            width: 150
            height: 19
            style: "sunken"
            Text {
                x: 4
                anchors.verticalCenter: parent.verticalCenter
                width: parent.width - 8
                elide: Text.ElideRight
                text: statusBar.info
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontSize
                color: Theme.text
            }
        }
        SizeGrip95 {
            anchors.right: parent.right
            anchors.bottom: parent.bottom
        }
    }

    // ----------------------------------------------------------------- dialogs

    FileDialog95 {
        id: folderDialog
        mode: "folder"
        messageBox: msgBox
        popupMenu: popup
        onAccepted: (path) => window.openFolder(path)
        onClosed: if (!msgBox.visible) window.refocus()
    }

    FileDialog95 {
        id: fileDialog
        messageBox: msgBox
        popupMenu: popup
        filters: [
            { label: "Pictures", patterns: PhotoFiles.readPatterns },
            { label: "All Files (*.*)", patterns: ["*"] },
        ]
        onAccepted: (path) => window.showPhoto(path)
        onClosed: if (!msgBox.visible) window.refocus()
    }

    FileDialog95 {
        id: saveDialog
        mode: "save"
        messageBox: msgBox
        popupMenu: popup
        filters: window.saveFilters
        defaultSuffix: "jpg"
        onFilterIndexChanged: defaultSuffix = window.saveFilters[filterIndex].suffix
        onAccepted: (path) => {
            var then = window.afterSave;
            window.afterSave = null;
            if (!window.canWrite(path)) {
                msgBox.show("Photo Gallery", "Photo Gallery can't save pictures of that type. "
                            + "Choose JPEG (.jpg), PNG (.png) or BMP (.bmp).", "error", ["OK"], window.refocus);
                return;
            }
            window.writeTo(path, then);
        }
        onClosed: if (!msgBox.visible) window.refocus()
    }

    Dialog95 {
        id: propertiesDialog
        property var rows: []
        dialogWidth: 380
        dialogHeight: propsColumn.height + 70
        onClosed: window.refocus()

        Column {
            id: propsColumn
            x: 12
            y: 12
            width: parent.width - 24
            spacing: 6
            Repeater {
                model: propertiesDialog.rows
                Row {
                    required property var modelData
                    spacing: 8
                    Text {
                        width: 90
                        text: modelData.label
                        color: Theme.text
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.fontSize
                    }
                    Text {
                        width: propsColumn.width - 98
                        wrapMode: Text.WrapAnywhere
                        text: modelData.value
                        color: Theme.text
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.fontSize
                    }
                }
            }
        }
        Button95 {
            anchors.right: parent.right
            anchors.rightMargin: 10
            anchors.bottom: parent.bottom
            anchors.bottomMargin: 10
            text: "OK"
            isDefault: true
            onClicked: propertiesDialog.close()
        }
        Keys.onReturnPressed: propertiesDialog.close()
        Keys.onEnterPressed: propertiesDialog.close()
    }

    Dialog95 {
        id: resizeDialog
        title: "Resize"
        dialogWidth: 300
        dialogHeight: 150
        property real ratio: 1
        onClosed: window.refocus()

        function open() {
            widthSpin.value = PhotoEditor.width;
            heightSpin.value = PhotoEditor.height;
            ratio = PhotoEditor.width / Math.max(1, PhotoEditor.height);
            show();
            widthSpin.forceActiveFocus();
        }
        function accept() {
            close();
            PhotoEditor.resize(widthSpin.value, heightSpin.value);
            photoView.fit();
            window.log("resize " + PhotoEditor.width + "x" + PhotoEditor.height);
        }
        function percent(p) {
            widthSpin.value = Math.max(1, Math.round(PhotoEditor.width * p / 100));
            heightSpin.value = Math.max(1, Math.round(PhotoEditor.height * p / 100));
        }

        Text {
            x: 10; y: 12; height: 21
            verticalAlignment: Text.AlignVCenter
            text: "Width:"
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontSize
        }
        SpinBox95 {
            id: widthSpin
            x: 70; y: 12; width: 80
            from: 1; to: 30000
            onValueModified: (v) => { if (keepBox.checked) heightSpin.value = Math.max(1, Math.round(v / resizeDialog.ratio)); }
        }
        Text {
            x: 10; y: 40; height: 21
            verticalAlignment: Text.AlignVCenter
            text: "Height:"
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontSize
        }
        SpinBox95 {
            id: heightSpin
            x: 70; y: 40; width: 80
            from: 1; to: 30000
            onValueModified: (v) => { if (keepBox.checked) widthSpin.value = Math.max(1, Math.round(v * resizeDialog.ratio)); }
        }
        CheckBox95 {
            id: keepBox
            x: 10; y: 70
            text: "<u>K</u>eep proportions"
            checked: true
        }
        Row {
            x: 10; y: 94
            spacing: 4
            Repeater {
                model: [25, 50, 75]
                Button95 {
                    required property int modelData
                    width: 44
                    text: modelData + "%"
                    onClicked: resizeDialog.percent(modelData)
                }
            }
        }
        Button95 {
            anchors.right: parent.right; anchors.rightMargin: 10
            y: 11
            text: "OK"
            isDefault: true
            onClicked: resizeDialog.accept()
        }
        Button95 {
            anchors.right: parent.right; anchors.rightMargin: 10
            y: 40
            text: "Cancel"
            onClicked: resizeDialog.close()
        }
        Keys.onReturnPressed: resizeDialog.accept()
        Keys.onEnterPressed: resizeDialog.accept()
    }

    SlideShow {
        id: slideShow
        folder: folder
        onEnded: (path) => {
            // back where the show got to
            if (window.photoMode)
                window.showPhoto(path);
            else {
                var row = folder.rowOf(path);
                if (row >= 0)
                    grid.currentIndex = row;
                grid.forceActiveFocus();
            }
            window.log("slide show ended");
        }
    }

    // window-level overlays (last, so they stack on top)
    Menu95 {
        id: popup
        onClosed: if (!window.modalOpen) window.refocus()
    }
    MessageBox95 { id: msgBox }
}
