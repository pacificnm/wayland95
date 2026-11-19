import QtQuick
import QtQuick.Window
import Win95Ui
import Win95App
import Win95Studio

// Wayland Studio: make Wayland 95 apps.
//
// A project (a .w95proj file) is the app's tables (the Data Designer), its
// forms (the form designer: elements on a grid, their properties, and code
// for their events) and where its data is kept (SQLite, MySQL or
// PostgreSQL). Run > Start runs it; File > Make Program writes it out as a
// program of its own.
//
// The project is one JavaScript object (`project`), changed only through
// change(), which keeps the undo history and bumps `revision`: everything
// that shows a piece of the project reads `revision` so it's redrawn.
Window {
    id: studio

    // (to fit a 1024x768 screen with the taskbar showing)
    width: 1000
    height: 680
    minimumWidth: 820
    minimumHeight: 540
    visible: true
    color: Theme.face
    title: (projectPath !== "" ? Win95Fs.fileName(projectPath) : "Project1") + (modified ? " *" : "") + " - Wayland Studio"

    property var project: JSON.parse(JSON.stringify(StudioTools.newProject()))
    property string projectPath: ""
    readonly property string projectDir: projectPath !== "" ? Win95Fs.parentDir(projectPath) : ""
    property bool modified: false
    property int revision: 0
    property var undoStack: []
    property var redoStack: []
    readonly property var registry: JSON.parse(JSON.stringify(StudioTools.registry()))

    // what's open in the middle: a form (as the "object" or its "code") or
    // a table (its "design" or its "data")
    property string viewKind: "form"
    property string viewName: "Form1"
    property string viewMode: "object"
    // the selected elements' names (none: the form itself); the last is
    // the one the Properties window shows
    property var selection: []
    // the Toolbox's element being placed ("": the pointer)
    property string tool: ""
    property string codeObject: ""
    property string codeEvent: "general"
    property var clipboard: []
    property int gridSize: 8
    property bool showGrid: true
    property bool showOutput: false
    property var outputLines: []
    property bool dataReady: false
    property bool quitting: false
    property var afterSave: null
    property string filePurpose: ""
    property var pictureCallback: null

    property alias popupMenu: popup
    property alias msgBox: msgBox
    property alias dialogs: dialogs
    property alias dialogLayer: dialogLayer
    property alias db: db

    readonly property bool modalOpen: fileDialog.visible || msgBox.visible || popup.visible || dialogs.open
    readonly property bool designing: viewKind === "form" && viewMode === "object"

    Component.onCompleted: {
        if (initialProject)
            openProject(initialProject);
    }
    onClosing: (close) => {
        if (modified && !quitting) {
            close.accepted = false;
            confirmDiscard(function () {
                studio.quitting = true;
                studio.close();
            });
            return;
        }
        StudioTools.stop();
    }

    AppDatabase { id: db }

    // ---------------------------------------------------------------- lookups

    function formNamed(name) {
        for (var i = 0; i < project.forms.length; ++i) {
            if (project.forms[i].name === name)
                return project.forms[i];
        }
        return null;
    }
    function tableNamed(name) {
        for (var i = 0; i < project.tables.length; ++i) {
            if (project.tables[i].name === name)
                return project.tables[i];
        }
        return null;
    }
    function currentForm() { return viewKind === "form" ? formNamed(viewName) : null; }
    function currentTable() { return viewKind === "table" ? tableNamed(viewName) : null; }
    function controlOf(form, name) {
        for (var i = 0; form && i < form.controls.length; ++i) {
            if (form.controls[i].name === name)
                return form.controls[i];
        }
        return null;
    }
    function elementInfo(type) {
        for (var i = 0; i < registry.elements.length; ++i) {
            if (registry.elements[i].type === type)
                return registry.elements[i];
        }
        return {};
    }
    property var propsCache: ({})
    function propsOf(type) {
        if (!propsCache[type])
            propsCache[type] = JSON.parse(JSON.stringify(StudioTools.elementProps(type)));
        return propsCache[type];
    }
    function uniqueName(prefix, taken) {
        for (var n = 1; ; ++n) {
            if (taken.indexOf(prefix + n) < 0)
                return prefix + n;
        }
    }
    // a DataGrid's headings on the canvas
    function gridColumns(control) {
        var form = currentForm();
        var name = (control.props && control.props.table) || (form && form.props.table) || "";
        var table = tableNamed(name);
        if (!table)
            return [name === "" ? "(no DataTable)" : name + "?"];
        var chosen = control.props && control.props.columns && control.props.columns.length ? control.props.columns : null;
        var out = [];
        for (var i = 0; i < table.fields.length; ++i) {
            if (!chosen || chosen.indexOf(table.fields[i].name) >= 0)
                out.push(table.fields[i].label || table.fields[i].name);
        }
        return out.length ? out : ["(no fields)"];
    }

    // ------------------------------------------------------------- the project

    // every change to the project goes through here
    function change(fn) {
        var before = JSON.stringify(project);
        fn();
        project.forms.forEach(redock);
        if (JSON.stringify(project) === before)
            return;
        var stack = undoStack;
        stack.push(before);
        if (stack.length > 100)
            stack.shift();
        undoStack = stack;
        redoStack = [];
        modified = true;
        revision++;
    }
    // A Menu and ToolBars sit across the top of the form, a Menu first,
    // in the order they were added: x, y and width are kept so here.
    function isDocked(c) { return elementInfo(c.type).dock === true; }
    function redock(form) {
        var docked = form.controls.filter(isDocked);
        docked.sort(function (a, b) {
            var ma = a.type === "Menu" ? 0 : 1, mb = b.type === "Menu" ? 0 : 1;
            return ma !== mb ? ma - mb : form.controls.indexOf(a) - form.controls.indexOf(b);
        });
        var y = 0;
        docked.forEach(function (c) {
            c.x = 0;
            c.y = y;
            c.width = form.width;
            y += c.height;
        });
    }
    function dockedHeight(form) {
        return form.controls.filter(isDocked).reduce(function (h, c) { return h + c.height; }, 0);
    }
    // the form's other elements down (or up) by `dy`, and the form taller
    // (or shorter) with them
    function shiftUndocked(form, dy) {
        form.controls.forEach(function (c) {
            if (!isDocked(c))
                c.y = Math.max(0, c.y + dy);
        });
        form.height = Math.max(24, form.height + dy);
    }

    function undo() {
        flush();
        if (undoStack.length === 0)
            return;
        var u = undoStack, r = redoStack;
        r.push(JSON.stringify(project));
        project = JSON.parse(u.pop());
        undoStack = u;
        redoStack = r;
        modified = true;
        fixView();
        revision++;
    }
    function redo() {
        flush();
        if (redoStack.length === 0)
            return;
        var u = undoStack, r = redoStack;
        u.push(JSON.stringify(project));
        project = JSON.parse(r.pop());
        undoStack = u;
        redoStack = r;
        modified = true;
        fixView();
        revision++;
    }
    // what's showing still has to be in the project
    function fixView() {
        if ((viewKind === "form" && !formNamed(viewName)) || (viewKind === "table" && !tableNamed(viewName)))
            viewKind = "";
        if (viewKind === "") {
            if (project.forms.length) {
                viewKind = "form";
                viewName = project.forms[0].name;
                viewMode = "object";
            } else if (project.tables.length) {
                viewKind = "table";
                viewName = project.tables[0].name;
                viewMode = "design";
            }
        }
        var form = currentForm();
        selection = form ? selection.filter(function (n) { return controlOf(form, n) !== null; }) : [];
        if (codeObject !== "" && (!form || !controlOf(form, codeObject))) {
            codeObject = "";
            codeEvent = "general";
        }
    }
    // the code window's text into the project
    function flush() { codeWindow.flush(); }

    function setProject(p, path) {
        closeData();
        project = JSON.parse(JSON.stringify(p));
        projectPath = path;
        StudioTools.watchProject(path);
        modified = false;
        undoStack = [];
        redoStack = [];
        selection = [];
        tool = "";
        codeObject = "";
        codeEvent = "general";
        viewKind = "";
        fixView();
        revision++;
    }
    function confirmDiscard(then) {
        flush();
        if (!modified) {
            then();
            return;
        }
        msgBox.show("Wayland Studio", "Save the changes to " + (projectPath !== "" ? Win95Fs.fileName(projectPath) : "this project") + "?",
                    "warning", ["Yes", "No", "Cancel"], function (i) {
            if (i === 0)
                ensureSaved(then);
            else if (i === 1)
                then();
        });
    }
    function newProject() {
        confirmDiscard(function () { setProject(StudioTools.newProject(), ""); });
    }
    function openProject(path) {
        var r = StudioTools.loadProject(path);
        if (!r.ok) {
            msgBox.show("Wayland Studio", r.error, "error", ["OK"], null);
            return;
        }
        setProject(r.project, path);
    }
    function showOpen() {
        confirmDiscard(function () {
            filePurpose = "open";
            fileDialog.mode = "open";
            fileDialog.filters = projectFilters;
            fileDialog.filterIndex = 0;
            fileDialog.defaultSuffix = "w95proj";
            fileDialog.openAt(projectDir !== "" ? projectDir : Win95Fs.homePath(), "");
        });
    }
    readonly property var projectFilters: [
        { label: "Wayland Studio Projects (*.w95proj)", patterns: ["*.w95proj"] },
        { label: "All Files (*.*)", patterns: ["*"] },
    ]
    function saveTo(path) {
        flush();
        var error = StudioTools.saveProject(path, project);
        if (error !== "") {
            msgBox.show("Wayland Studio", error, "error", ["OK"], null);
            return false;
        }
        projectPath = path;
        StudioTools.watchProject(path);
        modified = false;
        return true;
    }
    function saveAs(then) {
        flush();
        afterSave = then || null;
        filePurpose = "save";
        fileDialog.mode = "save";
        fileDialog.filters = projectFilters;
        fileDialog.filterIndex = 0;
        fileDialog.defaultSuffix = "w95proj";
        fileDialog.openAt(projectDir !== "" ? projectDir : Win95Fs.homePath(),
                          projectPath !== "" ? Win95Fs.fileName(projectPath) : project.app.name + ".w95proj");
    }
    function save() {
        if (projectPath === "")
            saveAs(null);
        else
            saveTo(projectPath);
    }
    // saved (asking where, the first time), then `then`
    function ensureSaved(then) {
        flush();
        if (projectPath === "") {
            saveAs(then);
            return;
        }
        if (modified && !saveTo(projectPath))
            return;
        then();
    }

    // ------------------------------------------------------------ what's open

    function openForm(name, mode) {
        flush();
        closeData();
        viewKind = "form";
        viewName = name;
        viewMode = mode || "object";
        selection = [];
        tool = "";
        codeObject = "";
        codeEvent = "general";
    }
    function openTable(name) {
        flush();
        closeData();
        viewKind = "table";
        viewName = name;
        viewMode = "design";
        selection = [];
        tool = "";
    }
    function setMode(mode) {
        flush();
        if (mode === viewMode)
            return;
        if (mode === "data") {
            connectData(function (ok) {
                if (ok)
                    studio.viewMode = "data";
            });
            return;
        }
        closeData();
        if (mode === "code" && codeEvent === "")
            codeEvent = "general";
        viewMode = mode;
        if (mode === "object")
            formDesigner.forceActiveFocus();
    }
    // an element's (or, "", the form's) code: its first event with code,
    // else its first event
    function viewCode(name) {
        flush();
        var form = currentForm();
        if (!form)
            return;
        var events, have;
        if (name === "") {
            events = registry.form.events;
            have = form.code || {};
        } else {
            var c = controlOf(form, name);
            if (!c)
                return;
            events = elementInfo(c.type).events || [];
            have = c.events || {};
        }
        var pick = events.length ? events[0].name : "";
        for (var i = events.length - 1; i >= 0; --i) {
            if (have[events[i].name] && have[events[i].name].trim() !== "")
                pick = events[i].name;
        }
        codeObject = name;
        codeEvent = pick;
        viewMode = "code";
        codeWindow.focusEditor();
    }
    function setCode(target, eventName, text) {
        var form = currentForm();
        if (!form)
            return;
        change(function () {
            if (target === "") {
                if (!form.code)
                    form.code = {};
                form.code[eventName] = text;
            } else {
                var c = controlOf(form, target);
                if (!c)
                    return;
                if (!c.events)
                    c.events = {};
                c.events[eventName] = text;
            }
        });
    }

    // the Data view's database: the project's, brought up to its tables
    function closeData() {
        if (dataReady || db.isOpen)
            db.close();
        dataReady = false;
    }
    function connectData(then) {
        ensureSaved(function () {
            closeData();
            db.setApis(project.apis || []);
            db.setSchema(project.tables);
            if (!db.open(project.database, projectDir)) {
                msgBox.show("Data", "The database didn't open.\n\n" + db.lastError + "\n\n(Project > Database says where it is.)",
                            "error", ["OK"], null);
                then(false);
                return;
            }
            var plan = db.schemaPlan();
            if (!plan.ok) {
                msgBox.show("Data", plan.error, "error", ["OK"], null);
                closeData();
                then(false);
                return;
            }
            if (plan.actions.length === 0) {
                dataReady = true;
                then(true);
                return;
            }
            msgBox.show("Data", "The database isn't up to the project yet. To do:\n\n" + plan.actions.join("\n")
                        + "\n\nMake these changes?", "question", ["Yes", "No"], function (i) {
                if (i !== 0) {
                    studio.closeData();
                    then(false);
                    return;
                }
                var r = db.syncSchema();
                if (!r.ok) {
                    msgBox.show("Data", r.error, "error", ["OK"], null);
                    studio.closeData();
                    then(false);
                    return;
                }
                studio.dataReady = true;
                then(true);
            });
        });
    }

    // ------------------------------------------------------------------ forms

    function addForm() {
        flush();
        var name = uniqueName("Form", project.forms.map(function (f) { return f.name; }));
        change(function () {
            project.forms.push({ name: name, width: 400, height: 296, props: { title: name }, code: {}, controls: [] });
            if (!project.app.startForm)
                project.app.startForm = name;
        });
        openForm(name, "object");
    }
    // a form to keep a table's records with: a label and an element for
    // each field (by its type), the records in a grid, and New, Save, Delete
    // the tables the project's database has that the project doesn't, into it
    // (`quiet`: say nothing if there are none: after Project > Database)
    function importTables(quiet) {
        var go = function () {
            closeData();
            db.setApis(project.apis || []);
            db.setSchema(project.tables);
            if (!db.open(project.database, projectDir)) {
                if (!quiet)
                    msgBox.show("Import Tables", "The database didn't open.\n\n" + db.lastError
                                + "\n\n(Project > Database says where it is.)", "error", ["OK"], null);
                closeData();
                return;
            }
            var r = db.readSchema();
            closeData();
            if (!r.ok) {
                msgBox.show("Import Tables", r.error, "error", ["OK"], null);
                return;
            }
            // (one the project has already, by either name: not again)
            var have = {};
            project.tables.forEach(function (t) { have[(t.dbName || t.name).toLowerCase()] = true; have[t.name.toLowerCase()] = true; });
            var fresh = r.tables.filter(function (t) { return !have[(t.dbName || t.name).toLowerCase()]; });
            if (fresh.length === 0) {
                if (!quiet)
                    msgBox.show("Import Tables", "Every table in the database is in the project already.", "info", ["OK"], null);
                return;
            }
            var where = project.database.driver === "sqlite" ? (project.database.file || "data.db")
                      : (project.database.name || "The database");
            var choose = function () {
                dialogs.chooseImport(where, fresh, r.notes.filter(function (n) {
                    return fresh.some(function (t) { return n.indexOf((t.dbName || t.name) + " ") === 0 || n.indexOf((t.dbName || t.name) + ".") === 0; });
                }), function (chosen, makeForms) { studio.addImported(chosen, makeForms); });
            };
            if (quiet)
                msgBox.show("Database", where + " has " + fresh.length + (fresh.length === 1 ? " table" : " tables")
                            + " the project doesn't. Bring them into the project, to build forms around them?",
                            "question", ["Yes", "No"], function (i) { if (i === 0) choose(); });
            else
                choose();
        };
        // (after Project > Database, a project that isn't saved yet isn't made to be: a
        // server doesn't need its folder, and a SQLite file beside it isn't there yet)
        if (quiet && projectPath === "") {
            var file = project.database.file || "data.db";
            if (project.database.driver === "sqlite" && !/^(\/|~\/)/.test(file))
                return;
            go();
        } else {
            ensureSaved(go);
        }
    }
    function addImported(chosen, makeForms) {
        flush();
        change(function () {
            chosen.forEach(function (t) {
                var table = JSON.parse(JSON.stringify(t));
                delete table.records;
                project.tables.push(table);
            });
        });
        if (makeForms)
            chosen.forEach(function (t) { studio.addFormForTable(t.name); });
        say("Imported " + chosen.map(function (t) { return t.name; }).join(", ") + ".");
        if (!makeForms && chosen.length)
            openTable(chosen[0].name);
    }

    // an API's endpoint, as a table (Project > Add API Table)
    function addApiTable() {
        if (!(project.apis || []).length) {
            msgBox.show("Add API Table", "The project has no APIs yet: Project > APIs... adds one (its address, and how to sign in).",
                        "info", ["OK"], null);
            return;
        }
        flush();
        dialogs.addApiTable(project.apis, project.tables.map(function (t) { return t.name.toLowerCase(); }), function (table, makeForm) {
            studio.change(function () { project.tables.push(table); });
            studio.say("Added " + table.name + " (" + table.api + " " + table.endpoint + ").");
            if (makeForm)
                studio.addFormForTable(table.name);
            else
                studio.openTable(table.name);
        });
    }

    function addFormForTable(tableName) {
        var table = typeof tableName === "string" ? tableNamed(tableName) : currentTable();
        if (!table)
            return;
        flush();
        var formName = table.name + "Form";
        if (StudioTools.formNameProblem(formName) !== "" || formNamed(formName))
            formName = uniqueName(table.name + "Form", project.forms.map(function (f) { return f.name; }));
        var controls = [], taken = [], y = 12, first = "";
        function named(base, suffix) {
            var n = base.charAt(0).toLowerCase() + base.substring(1) + suffix;
            if (StudioTools.elementNameProblem(n) !== "" || taken.indexOf(n) >= 0)
                n = uniqueName(n.replace(/[^A-Za-z0-9_]/g, "") || "element", taken);
            taken.push(n);
            return n;
        }
        table.fields.forEach(function (f) {
            var caption = f.label || f.name, t = f.type, h = t === "memo" ? 60 : 21;
            var datey = t === "date" || t === "time" || t === "datetime";
            var kind = t === "boolean" ? "CheckBox" : t === "choice" || t === "lookup" ? "ComboBox" : datey ? "DateTimePicker" : "TextBox";
            var name = named(f.name, kind === "CheckBox" ? "Check" : kind === "ComboBox" ? "Combo" : kind === "DateTimePicker" ? "Picker" : "Box");
            if (kind !== "CheckBox")
                controls.push({ type: "Label", name: named(f.name, "Label"), x: 12, y: y + 2, width: 92, height: 17,
                                props: { text: caption + ":" }, events: {} });
            var props = { field: f.name };
            if (kind === "CheckBox")
                props.text = caption;
            if (t === "memo")
                props.multiLine = true;
            if (datey) {
                props.mode = t === "date" ? "Date" : t === "time" ? "Time" : "Date and Time";
                // (one the database sets by itself is shown, not picked)
                if (f.auto)
                    props.enabled = false;
            }
            controls.push({ type: kind, name: name, x: 108, y: y, width: 180, height: kind === "CheckBox" ? 17 : h, props: props, events: {} });
            if (first === "" && kind === "TextBox")
                first = name;
            y += h + 7;
        });
        var buttonsY = y + 5, gridHeight = Math.max(160, buttonsY + 24 - 12);
        var buttons = [
            ["newButton", "New", "form.newRecord();" + (first !== "" ? "\n" + first + ".forceActiveFocus();" : "")],
            ["saveButton", "Save", "form.save();"],
            ["deleteButton", "Delete", "if (form.isNew)\n    return;\n"
                + "form.msgBox(\"Delete this record?\", \"Delete\", \"question\", [\"Yes\", \"No\"], function (answer) {\n"
                + "    if (answer === 0)\n        form.deleteRecord();\n});"],
        ];
        buttons.forEach(function (b, i) {
            var props = { text: b[1] };
            if (b[0] === "saveButton")
                props.isDefault = true;
            controls.push({ type: "Button", name: named(b[0], ""), x: 12 + i * 92, y: buttonsY, width: 84, height: 24,
                            props: props, events: { clicked: b[2] } });
        });
        controls.push({ type: "DataGrid", name: named("grid", ""), x: 304, y: 12, width: 328, height: gridHeight, props: {}, events: {} });
        change(function () {
            project.forms.push({ name: formName, width: 644, height: 12 + gridHeight + 12,
                                 props: { title: table.label || table.name, table: table.name }, code: {}, controls: controls });
            if (!project.app.startForm)
                project.app.startForm = formName;
        });
        openForm(formName, "object");
    }
    function addControl(type, x, y, w, h) {
        var form = currentForm(), info = elementInfo(type);
        if (!form || !info.type)
            return;
        var name = uniqueName(info.prefix, form.controls.map(function (c) { return c.name; }));
        var props = {};
        for (var i = 0; i < info.props.length; ++i) {
            if (info.props[i].usesName)
                props[info.props[i].name] = name.charAt(0).toUpperCase() + name.substring(1);
            if (info.props[i].initial !== undefined)
                props[info.props[i].name] = JSON.parse(JSON.stringify(info.props[i].initial));
        }
        var control = { type: type, name: name, x: Math.max(0, x), y: Math.max(0, y),
                        width: w > 0 ? w : info.size[0], height: h > 0 ? h : info.size[1], props: props, events: {} };
        if (info.dock) {
            // across the top (a Menu has one height, whatever was dragged out);
            // what's on the form moves down to make room
            if (type === "Menu" && form.controls.some(function (c) { return c.type === "Menu"; })) {
                msgBox.show("Menu", "This form has a Menu already: its Menus property (…) edits it.", "info", ["OK"], null);
                return;
            }
            control.height = type === "Menu" ? info.size[1] : (h >= 20 ? h : info.size[1]);
            change(function () {
                shiftUndocked(form, control.height);
                form.controls.push(control);
            });
            selection = [name];
            formDesigner.forceActiveFocus();
            return;
        }
        change(function () {
            // (a Frame or a Shape goes behind what's there: things are put on top of it)
            if (info.behind)
                form.controls.unshift(control);
            else
                form.controls.push(control);
        });
        selection = [name];
        formDesigner.forceActiveFocus();
        // (a Graph: its editor at once, to say what it shows)
        if (info.editor === "graph")
            Qt.callLater(function () {
                dialogs.editGraph(control.props.graph || {}, function (settings) { studio.setProperty("graph", settings); });
            });
    }
    // the Toolbox, double-clicked: one in the middle of the form
    function addControlCentered(type) {
        var form = currentForm(), info = elementInfo(type);
        if (!form || !info.type)
            return;
        var snap = function (v) { return Math.round(v / gridSize) * gridSize; };
        var x = snap((form.width - info.size[0]) / 2), y = snap((form.height - info.size[1]) / 2);
        // not exactly on top of the last one put there
        while (form.controls.some(function (c) { return c.x === x && c.y === y; })) {
            x += gridSize;
            y += gridSize;
        }
        addControl(type, x, y, 0, 0);
        tool = "";
    }
    function selected() {
        var form = currentForm(), out = [];
        for (var i = 0; form && i < selection.length; ++i) {
            var c = controlOf(form, selection[i]);
            if (c)
                out.push(c);
        }
        return out;
    }
    function moveSelected(dx, dy) {
        var list = selected();
        change(function () {
            for (var i = 0; i < list.length; ++i) {
                list[i].x += dx;
                list[i].y += dy;
            }
        });
    }
    function resizeSelected(dw, dh) {
        var list = selected();
        change(function () {
            for (var i = 0; i < list.length; ++i) {
                list[i].width = Math.max(1, list[i].width + dw);
                list[i].height = Math.max(1, list[i].height + dh);
            }
        });
    }
    function setGeometry(name, x, y, w, h) {
        var c = controlOf(currentForm(), name);
        if (!c)
            return;
        change(function () {
            c.x = Math.round(x);
            c.y = Math.round(y);
            c.width = Math.max(1, Math.round(w));
            c.height = Math.max(1, Math.round(h));
        });
    }
    function setFormSize(w, h) {
        var form = currentForm();
        if (!form)
            return;
        change(function () {
            form.width = Math.max(48, Math.round(w));
            form.height = Math.max(24, Math.round(h));
        });
    }
    function deleteSelected() {
        var form = currentForm();
        if (!form || selection.length === 0)
            return;
        var names = selection;
        change(function () {
            var freed = form.controls.filter(function (c) { return names.indexOf(c.name) >= 0 && isDocked(c); })
                                     .reduce(function (h, c) { return h + c.height; }, 0);
            form.controls = form.controls.filter(function (c) { return names.indexOf(c.name) < 0; });
            if (freed > 0)
                shiftUndocked(form, -freed);
        });
        selection = [];
    }
    function copy() {
        clipboard = selected().map(function (c) { return JSON.parse(JSON.stringify(c)); });
    }
    function cut() {
        copy();
        deleteSelected();
    }
    function paste() {
        var form = currentForm();
        if (!form || clipboard.length === 0 || !designing)
            return;
        var taken = form.controls.map(function (c) { return c.name; }), names = [];
        var copies = clipboard.map(function (c) {
            var n = JSON.parse(JSON.stringify(c));
            n.name = taken.indexOf(c.name) < 0 ? c.name : uniqueName(elementInfo(c.type).prefix, taken);
            taken.push(n.name);
            names.push(n.name);
            n.x += gridSize * 2;
            n.y += gridSize * 2;
            return n;
        });
        change(function () {
            for (var i = 0; i < copies.length; ++i)
                form.controls.push(copies[i]);
        });
        // (pasting again steps on from these)
        clipboard = copies.map(function (c) { return JSON.parse(JSON.stringify(c)); });
        selection = names;
    }
    function selectAll() {
        var form = currentForm();
        if (form && designing)
            selection = form.controls.map(function (c) { return c.name; });
    }
    function restack(front) {
        var form = currentForm(), names = selection;
        if (!form || names.length === 0)
            return;
        change(function () {
            var picked = form.controls.filter(function (c) { return names.indexOf(c.name) >= 0; });
            var rest = form.controls.filter(function (c) { return names.indexOf(c.name) < 0; });
            form.controls = front ? rest.concat(picked) : picked.concat(rest);
        });
    }
    // line the selection up with its last-chosen element, or make it that one's size
    function align(how) {
        var list = selected(), form = currentForm();
        if (list.length === 0)
            return;
        var lead = list[list.length - 1];
        change(function () {
            if (how === "centerH" || how === "centerV") {
                var x1 = 1e9, y1 = 1e9, x2 = -1e9, y2 = -1e9;
                list.forEach(function (c) {
                    x1 = Math.min(x1, c.x); y1 = Math.min(y1, c.y);
                    x2 = Math.max(x2, c.x + c.width); y2 = Math.max(y2, c.y + c.height);
                });
                var dx = how === "centerH" ? Math.round((form.width - (x2 - x1)) / 2) - x1 : 0;
                var dy = how === "centerV" ? Math.round((form.height - (y2 - y1)) / 2) - y1 : 0;
                list.forEach(function (c) { c.x += dx; c.y += dy; });
                return;
            }
            list.forEach(function (c) {
                if (how === "left") c.x = lead.x;
                else if (how === "right") c.x = lead.x + lead.width - c.width;
                else if (how === "top") c.y = lead.y;
                else if (how === "bottom") c.y = lead.y + lead.height - c.height;
                else if (how === "width") c.width = lead.width;
                else if (how === "height") c.height = lead.height;
            });
        });
    }

    // the Properties window's rows for what's selected
    readonly property var propertyRows: {
        revision;
        var form = currentForm();
        if (!form || viewKind !== "form")
            return [];
        var rows = [], i, p, fieldChoices = [{ value: "", label: "(none)" }], tableChoices = [];
        var table = tableNamed(form.props.table || "");
        for (i = 0; table && i < table.fields.length; ++i)
            fieldChoices.push({ value: table.fields[i].name, label: table.fields[i].name });
        for (i = 0; i < project.tables.length; ++i)
            tableChoices.push({ value: project.tables[i].name, label: project.tables[i].name });
        function row(p, value, isForm) {
            var r = { name: p.name, label: p.label || p.name, type: p.type, value: value, hint: hintFor(p, isForm) };
            if (p.type === "enum")
                r.values = p.values;
            if (p.type === "enum" && p.examples) {
                // a format: each one with now in it
                r.type = "choice";
                r.values = p.values.map(function (v) {
                    return { value: v, label: v === "Custom" ? "Custom (the pattern under it)"
                                                             : v + "    " + AppDates.example(v, p.examples === "time") };
                });
            }
            if (p.type === "timezone") {
                r.type = "choice";
                r.values = [{ value: "", label: "(this computer's)" }].concat(
                    AppDates.timeZones().map(function (z) { return { value: z, label: z.replace(/_/g, " ") }; }));
            }
            if (p.type === "color" || p.type === "font")
                r.placeholder = "(the desktop's)";
            if (p.type === "field") {
                r.type = "choice";
                r.values = fieldChoices;
            } else if (p.type === "table") {
                r.type = "choice";
                r.values = [{ value: "", label: isForm ? "(none)" : "(the form's)" }].concat(tableChoices);
            }
            return r;
        }
        var c = selection.length ? controlOf(form, selection[selection.length - 1]) : null;
        if (!c) {
            rows.push({ name: "(name)", label: "(Name)", type: "string", value: form.name,
                        hint: "The form's name: App.openForm(\"" + form.name + "\")." });
            for (i = 0; i < registry.form.props.length; ++i) {
                p = registry.form.props[i];
                rows.push(row(p, form.props[p.name] !== undefined ? form.props[p.name] : p["default"], true));
            }
            rows.push({ name: "width", label: "Width", type: "int", value: form.width, hint: "The form's width inside its frame, in pixels." });
            rows.push({ name: "height", label: "Height", type: "int", value: form.height, hint: "The form's height inside its frame, in pixels." });
            return rows;
        }
        rows.push({ name: "(name)", label: "(Name)", type: "string", value: c.name,
                    hint: "What code calls this " + c.type + ": " + c.name + ".visible = false" });
        var props = propsOf(c.type);
        for (i = 0; i < props.length; ++i) {
            p = props[i];
            var r = row(p, c.props && c.props[p.name] !== undefined ? c.props[p.name] : p["default"], false);
            if (p.name === "field" && c.type === "Label")
                r.hint = "A Label with a DataField shows the field's value, not its Caption. For a caption beside a box, "
                       + "leave this blank and set the box's DataField.";
            rows.push(r);
        }
        rows.push({ name: "x", label: "Left", type: "int", value: c.x, hint: "How far from the form's left edge, in pixels." });
        rows.push({ name: "y", label: "Top", type: "int", value: c.y, hint: "How far from the form's top edge, in pixels." });
        rows.push({ name: "width", label: "Width", type: "int", value: c.width, hint: "Its width in pixels." });
        rows.push({ name: "height", label: "Height", type: "int", value: c.height, hint: "Its height in pixels." });
        return rows;
    }
    function hintFor(p, isForm) {
        var hints = {
            visible: "Whether it's shown. Code can change it: name.visible = true",
            enabled: "Whether it can be used (it's drawn gray when it can't).",
            toolTip: "What to say when the pointer rests on it.",
            foreColor: "The color of its text. Blank: the desktop's.",
            backColor: isForm ? "The form's color. Blank: the desktop's." : "The color behind it. Blank: the desktop's.",
            fontFamily: "The font of its text. Blank: the desktop's.",
            fontSize: "The size of its text in pixels. 0: the desktop's.",
            fontBold: "Bold text.", fontItalic: "Italic text.", fontUnderline: "Underlined text.",
            field: "The field of the form's DataTable this shows and edits. What validation refuses is marked on it.",
            title: "The text in the form's title bar.",
            picture: "A picture file" + (isForm ? ", tiled behind the form's elements." : ".") + " It is put inside the program when it's made.",
            table: isForm ? "The table whose records this form shows, one at a time (form.record), in the elements with a DataField."
                          : "The table whose records it lists. Blank: the form's DataTable.",
            resizable: "Whether the window's size can be changed.",
            showErrors: "Show a message box listing what validation refused when Save fails (the elements are marked either way).",
            text: "The text it shows.",
            columns: "The fields to show, in order. Blank: all of them.",
            search: "Only records with this text in one of their text fields.",
            orderBy: "The field to sort by. Blank: the order they were added.",
            followForm: "Choosing a row shows that record in the form, when this lists the form's own DataTable.",
            items: "The items to list. Ignored when a DataField supplies them (a Choice or Lookup field).",
            currentIndex: "The item chosen to start with (0 is the first, -1 none).",
            isDefault: "The button Enter presses.",
            group: "Option buttons with the same Group clear each other.",
            optionValue: "What the DataField gets when this option is chosen. Blank: the Caption.",
            multiLine: "Several lines, with a scroll bar.",
            readOnly: "It can be read and copied but not changed.",
            password: "Show dots instead of what's typed.",
            maxLength: "The most characters that can be typed. 0: no limit.",
            source: "The sound or video: a file (MP3, WAV, FLAC, OGG, M4A; MP4, WebM, MKV, AVI, MOV...), or a web address.",
            embed: "Put the file inside the program when it's made (a big video makes a big program). False: the program plays it from where it is.",
            autoPlay: "Start playing when the form opens.",
            loop: "Start again at the end, for ever.",
            volume: "How loud, from 0 to 100.",
            muted: "No sound (the speaker button changes it).",
            showControls: "Win95's ▶ ❚❚ ■, a position bar, the time and a speaker under it. False: only the picture (or nothing, for a sound).",
            fillMode: "How a video fits: Fit (all of it, bars at the sides), Stretch (to the shape), Crop (filled, the edges cut off).",
            menus: "The menus and their items, in the Menu Editor (…): each item's Caption, Shortcut and Action (open a form, save...).",
            buttons: "The buttons, in the Menu Editor (…): each one's Caption, symbol or picture, ToolTip and Action (open a form, save...).",
            showCaptions: "Show each button's Caption beside its symbol or picture (a button with neither always shows it).",
            mode: "A date (with a calendar), a date and a time, or a time (with ▲▼).",
            value: "What it starts with: 2030-01-01, 14:30, or from now: today, today+7, now+2h. Blank: nothing (or its DataField's).",
            dateFormat: "How the date shows: the computer's Short or Long way, a pattern, or Custom.",
            timeFormat: "How the time shows: the computer's Short or Long way, 12-hour (AP) or 24-hour, or Custom.",
            customDateFormat: "With DateFormat Custom: d dd ddd dddd (day), M MM MMM MMMM (month), yy yyyy (year): dddd d MMMM yyyy",
            customTimeFormat: "With TimeFormat Custom: h hh (12-hour with AP) or H HH (24-hour), mm, ss, AP: h:mm AP",
            earliestDate: "The first day that can be picked: 2024-01-01, today, today-30. Blank: no limit (or its DataField's Earliest).",
            latestDate: "The last day that can be picked: 2030-12-31, today, today+90. Blank: no limit (or its DataField's Latest).",
            earliestTime: "The earliest time of day that can be picked: 09:00. Blank: no limit.",
            latestTime: "The latest time of day that can be picked: 17:30. Blank: no limit.",
            minuteStep: "How far ▲▼ and Ctrl+Up/Down move a time, in minutes: 15 is :00, :15, :30, :45.",
            timeZone: "Show and pick the time in another zone (the value stays this computer's time). Blank: this computer's.",
            firstDayOfWeek: "The day each week of the calendar starts on.",
            allowBlank: "Whether it can be left empty (Delete what's typed, or None under the calendar).",
            graph: "What it draws (… or F4: the Graph Editor): its type, how it looks, and the data it shows (a table's records, grouped and summed up, or what code gives it).",
            model: "Which Claude answers: Opus 5.5 (the most capable), Sonnet 5.5, Haiku 5.5 (the fastest and cheapest), Fable 5.2.",
            instructions: "What Claude should know and do, added to what it's told about the app: \"You help the shop's staff with orders. Answer in French.\"",
            greeting: "What the chat says first (it isn't sent to Claude). Blank: nothing.",
            effort: "How hard Claude thinks before it answers: Low is quicker and cheaper, Max the most thorough. Default: the model's own (Medium for Opus 5.5).",
            maxTokens: "The longest an answer can be, in tokens (about ¾ of a word each).",
            maxSteps: "The most tool calls Claude can make for one message before it stops and asks to go on.",
            dataAccess: "What Claude can do with the app's tables: nothing, read records, or also add, change and delete them (through the tables' validation).",
            formAccess: "Claude can read and fill in the app's open forms, click their buttons, save, move between records and open forms.",
            apiAccess: "Claude can make requests of the project's APIs (Project > APIs).",
            confirmChanges: "Ask the person before each change Claude makes: a record added, changed or deleted, a form saved, an API called with anything but GET.",
            showToolCalls: "Show a line in the chat for each tool Claude uses (⚙ Looked in Customers).",
        };
        return hints[p.name] || "";
    }
    function setProperty(name, value) {
        var form = currentForm();
        if (!form)
            return;
        var list = selected();
        if (list.length === 0) {
            if (name === "(name)")
                renameForm(form, String(value).trim());
            else if (name === "width" || name === "height")
                setFormSize(name === "width" ? value : form.width, name === "height" ? value : form.height);
            else
                change(function () { form.props[name] = value; });
            return;
        }
        var lead = list[list.length - 1];
        if (name === "(name)") {
            renameControl(form, lead, String(value).trim());
        } else if (name === "x" || name === "y" || name === "width" || name === "height") {
            change(function () { lead[name] = name === "x" || name === "y" ? value : Math.max(1, value); });
        } else {
            // every selected element that has this property
            change(function () {
                list.forEach(function (c) {
                    if (propsOf(c.type).some(function (p) { return p.name === name; })) {
                        if (!c.props)
                            c.props = {};
                        c.props[name] = value;
                    }
                });
            });
        }
    }
    function renameControl(form, control, name) {
        if (name === control.name)
            return;
        var problem = StudioTools.elementNameProblem(name);
        if (problem === "" && controlOf(form, name))
            problem = "Another element on this form is named " + name + ".";
        if (problem !== "") {
            msgBox.show("Properties", problem, "warning", ["OK"], null);
            return;
        }
        var old = control.name;
        change(function () { control.name = name; });
        selection = selection.map(function (n) { return n === old ? name : n; });
        if (codeObject === old)
            codeObject = name;
    }
    function renameForm(form, name) {
        if (name === form.name)
            return;
        var problem = StudioTools.formNameProblem(name);
        if (problem === "" && formNamed(name))
            problem = "Another form is named " + name + ".";
        if (problem !== "") {
            msgBox.show("Properties", problem, "warning", ["OK"], null);
            return;
        }
        var old = form.name;
        change(function () {
            form.name = name;
            if (project.app.startForm === old)
                project.app.startForm = name;
            // the menu items and tool buttons that open it
            project.forms.forEach(function (f) {
                f.controls.forEach(function (c) {
                    var lists = [];
                    if (c.props && c.props.menus)
                        c.props.menus.forEach(function (m) { lists.push([m]); lists.push(m.items || []); });
                    if (c.props && c.props.buttons)
                        lists.push(c.props.buttons);
                    lists.forEach(function (items) {
                        items.forEach(function (it) {
                            if (it.action === "openForm" && it.form === old)
                                it.form = name;
                        });
                    });
                });
            });
        });
        viewName = name;
    }

    // ----------------------------------------------------------------- tables

    function addTable() {
        flush();
        var name = uniqueName("Table", project.tables.map(function (t) { return t.name; }));
        change(function () {
            project.tables.push({ name: name, label: "", fields: [{ name: "Name", type: "text", required: true }] });
        });
        openTable(name);
    }
    function addField() {
        var table = currentTable();
        if (!table)
            return -1;
        var name = uniqueName("Field", table.fields.map(function (f) { return f.name; }));
        change(function () { table.fields.push({ name: name, type: "text" }); });
        return table.fields.length - 1;
    }
    function removeField(index) {
        var table = currentTable();
        if (!table || index < 0 || index >= table.fields.length)
            return;
        change(function () { table.fields.splice(index, 1); });
    }
    function moveField(index, by) {
        var table = currentTable(), to = index + by;
        if (!table || to < 0 || to >= table.fields.length)
            return index;
        change(function () {
            var f = table.fields.splice(index, 1)[0];
            table.fields.splice(to, 0, f);
        });
        return to;
    }
    function setFieldProp(index, name, value) {
        var table = currentTable();
        if (!table || !table.fields[index])
            return;
        var field = table.fields[index];
        if (name === "name") {
            value = String(value).trim();
            if (value === field.name)
                return;
            var problem = db.nameProblem(value);
            if (problem === "" && table.fields.some(function (f) { return f.name.toLowerCase() === value.toLowerCase(); }))
                problem = table.name + " already has a field named " + value + ".";
            if (problem !== "") {
                msgBox.show("Data Designer", problem, "warning", ["OK"], null);
                return;
            }
            var old = field.name;
            change(function () {
                // (a field of an imported table keeps its column: only code's name changes)
                if (table.imported && !field.column)
                    field.column = old;
                field.name = value;
                // the table's rules about another field follow it
                table.fields.forEach(function (f) {
                    if (f.validatorArg === old && db.validators(f.type).some(function (v) { return v.id === f.validator && v.arg === "field"; }))
                        f.validatorArg = value;
                });
                // the forms that show this field follow it
                project.forms.forEach(function (form) {
                    form.controls.forEach(function (c) {
                        var source = (c.props && c.props.table) || form.props.table || "";
                        if (form.props.table === table.name && c.props && c.props.field === old)
                            c.props.field = value;
                        if (source === table.name && c.props && c.props.columns)
                            c.props.columns = c.props.columns.map(function (n) { return n === old ? value : n; });
                    });
                });
            });
            return;
        }
        // a date's or time's Default Value, from its list: "" (none), "now"
        // or "today", "auto:created" / "auto:modified" (the database sets
        // it), "other" (typed in the Default row under it)
        if (name === "defaultKind") {
            var t = field.type;
            change(function () {
                delete field.auto;
                if (value === "auto:created" || value === "auto:modified") {
                    field.auto = value.substring(5);
                    delete field["default"];
                } else if (value === "other") {
                    // something to start from, to change
                    field["default"] = t === "date" ? "today+7" : t === "time" ? "09:00" : "now+1d";
                } else if (value === "") {
                    delete field["default"];
                } else {
                    field["default"] = value;
                }
            });
            return;
        }
        // the rule's own message, as it's in grey, is no message of the field's
        if (name === "message" && String(value).trim() === db.ruleMessage(table, field))
            value = "";
        change(function () {
            if (value === "" || value === null || ((name === "choices" || name === "validatorArg") && Array.isArray(value) && value.length === 0)
                || ((name === "required" || name === "unique" || name === "readOnly") && value === false))
                delete field[name];
            else
                field[name] = value;
            if (name === "validator") {
                // a new rule starts without the old one's value
                delete field.validatorArg;
                if (value !== "expression")
                    delete field.rule;
            }
            if (name === "type") {
                // what the new type has no use for
                if (value !== "choice")
                    delete field.choices;
                if (value !== "lookup")
                    delete field.lookupTable;
                delete field["default"];
                delete field.min;
                delete field.max;
                if (value !== "date" && value !== "time" && value !== "datetime")
                    delete field.auto;
                // a Validation Rule the new type doesn't have
                if (field.validator && !db.validators(value).some(function (v) { return v.id === field.validator; })) {
                    delete field.validator;
                    delete field.validatorArg;
                }
            }
        });
    }
    function setTableProp(name, value) {
        var table = currentTable();
        if (!table)
            return;
        if (name !== "name") {
            change(function () { table[name] = value; });
            return;
        }
        value = String(value).trim();
        if (value === table.name)
            return;
        var problem = db.nameProblem(value);
        if (problem === "" && project.tables.some(function (t) { return t.name.toLowerCase() === value.toLowerCase(); }))
            problem = "The project already has a table named " + value + ".";
        if (problem !== "") {
            msgBox.show("Data Designer", problem, "warning", ["OK"], null);
            return;
        }
        var old = table.name;
        change(function () {
            // (an imported table keeps its name in the database: only code's changes)
            if (table.imported && !table.dbName)
                table.dbName = table.name;
            table.name = value;
            project.tables.forEach(function (t) {
                t.fields.forEach(function (f) {
                    if (f.lookupTable === old)
                        f.lookupTable = value;
                });
            });
            project.forms.forEach(function (form) {
                if (form.props.table === old)
                    form.props.table = value;
                form.controls.forEach(function (c) {
                    if (c.props && c.props.table === old)
                        c.props.table = value;
                });
            });
        });
        viewName = value;
    }
    function removeCurrent() {
        var kind = viewKind, name = viewName;
        if (kind === "")
            return;
        msgBox.show("Wayland Studio", "Remove the " + kind + " " + name + " from the project?"
                    + (kind === "table" ? "\n\n(The table in the database, and what's in it, stays.)" : ""),
                    "question", ["Yes", "No"], function (i) {
            if (i !== 0)
                return;
            studio.closeData();
            studio.change(function () {
                if (kind === "form") {
                    project.forms = project.forms.filter(function (f) { return f.name !== name; });
                    if (project.app.startForm === name)
                        project.app.startForm = project.forms.length ? project.forms[0].name : "";
                } else {
                    project.tables = project.tables.filter(function (t) { return t.name !== name; });
                }
            });
            studio.fixView();
        });
    }

    // ------------------------------------------------------------ run and make

    function say(line) {
        var lines = outputLines;
        lines.push(line);
        if (lines.length > 500)
            lines.splice(0, lines.length - 500);
        outputLines = lines;
        showOutput = true;
        outputView.positionViewAtEnd();
    }
    // the project through the compiler; what's wrong goes to Output
    function checkProject(quiet) {
        var r = StudioTools.check(projectPath);
        if (!r.ok) {
            say("Problems in " + Win95Fs.fileName(projectPath) + ":");
            r.problems.forEach(function (p) { say("    " + p); });
            msgBox.show("Wayland Studio", r.problems.length === 1 ? r.problems[0]
                        : r.problems.length + " problems have to be fixed first. They're listed in Output.",
                        "warning", ["OK"], null);
        } else if (!quiet) {
            say("No problems found.");
        }
        return r.ok;
    }
    function start() {
        if (StudioTools.running)
            return;
        ensureSaved(function () {
            studio.closeData();
            if (studio.viewMode === "data")
                studio.viewMode = "design";
            if (!studio.checkProject(true))
                return;
            studio.say("--- " + studio.project.app.name + " started ---");
            StudioTools.run(studio.projectPath);
        });
    }
    function makeProgram() {
        ensureSaved(function () {
            if (!studio.checkProject(true))
                return;
            studio.filePurpose = "make";
            fileDialog.mode = "save";
            fileDialog.filters = [{ label: "Programs (*)", patterns: ["*"] }];
            fileDialog.filterIndex = 0;
            fileDialog.defaultSuffix = "";
            fileDialog.openAt(studio.projectDir, studio.project.app.name);
        });
    }
    Connections {
        target: StudioTools
        function onOutput(line) { studio.say(line); }
        function onFinished(code) { studio.say("--- ended" + (code !== 0 ? " (exit " + code + ")" : "") + " ---"); }
        function onProjectFileChanged() { reloadTimer.restart(); }
    }

    // ------------------------------------------- the project changed on disk
    // Another program wrote the project (Claude through win95studio-mcp, an
    // editor): with nothing unsaved here it's taken as it is, as one change
    // that Undo takes back; with unsaved changes, the Studio asks first.
    // Not while a dialog or menu is open (it may hold a copy of a piece).
    Timer {
        id: reloadTimer
        interval: 200
        onTriggered: studio.reloadFromDisk()
    }
    property bool reloadAsking: false
    // the same JSON whatever order its keys were made in
    function canonical(v) {
        if (Array.isArray(v))
            return "[" + v.map(canonical).join(",") + "]";
        if (v !== null && typeof v === "object")
            return "{" + Object.keys(v).sort().map(function (k) { return JSON.stringify(k) + ":" + canonical(v[k]); }).join(",") + "}";
        return JSON.stringify(v);
    }
    function reloadFromDisk() {
        if (projectPath === "" || reloadAsking)
            return;
        if (modalOpen) {
            reloadTimer.restart();
            return;
        }
        var r = StudioTools.loadProject(projectPath);
        if (!r.ok)
            return; // (gone, or not all there yet: the next change says)
        flush();
        if (canonical(r.project) === canonical(project))
            return;
        if (!modified) {
            takeFromDisk(r.project);
            return;
        }
        reloadAsking = true;
        msgBox.show("Wayland Studio", Win95Fs.fileName(projectPath) + " was changed by another program.\n\n"
                    + "Load it? The changes you haven't saved here will be lost.", "question", ["Yes", "No"], function (i) {
            reloadAsking = false;
            if (i !== 0)
                return;
            var again = StudioTools.loadProject(projectPath);
            if (again.ok)
                takeFromDisk(again.project);
        });
    }
    function takeFromDisk(p) {
        var stack = undoStack;
        stack.push(JSON.stringify(project));
        if (stack.length > 100)
            stack.shift();
        undoStack = stack;
        redoStack = [];
        if (viewMode === "data") {
            // (the table may have changed under the Data view)
            closeData();
            viewMode = "design";
        }
        project = JSON.parse(JSON.stringify(p));
        modified = false;
        fixView();
        revision++;
    }

    // ------------------------------------------------------------------ menus

    function fileMenu() {
        return [
            { text: "&New Project", shortcut: "Ctrl+N", action: newProject },
            { text: "&Open Project...", shortcut: "Ctrl+O", action: showOpen },
            { text: "&Save Project", shortcut: "Ctrl+S", action: save },
            { text: "Save Project &As...", action: function () { studio.saveAs(null); } },
            { separator: true },
            { text: "Ma&ke Program...", action: makeProgram },
            { separator: true },
            { text: "E&xit", action: function () { studio.close(); } },
        ];
    }
    function editMenu() {
        var has = designing && selection.length > 0;
        return [
            { text: "&Undo", shortcut: "Ctrl+Z", enabled: undoStack.length > 0, action: undo },
            { text: "&Redo", shortcut: "Ctrl+Y", enabled: redoStack.length > 0, action: redo },
            { separator: true },
            { text: "Cu&t", shortcut: "Ctrl+X", enabled: has, action: cut },
            { text: "&Copy", shortcut: "Ctrl+C", enabled: has, action: copy },
            { text: "&Paste", shortcut: "Ctrl+V", enabled: designing && clipboard.length > 0, action: paste },
            { text: "&Delete", shortcut: "Del", enabled: has, action: deleteSelected },
            { separator: true },
            { text: "Select &All", shortcut: "Ctrl+A", enabled: designing, action: selectAll },
        ];
    }
    function viewMenu() {
        var form = viewKind === "form", table = viewKind === "table";
        return [
            { text: "&Object", shortcut: "Shift+F7", enabled: form, checked: form && viewMode === "object", action: function () { studio.setMode("object"); } },
            { text: "&Code", shortcut: "F7", enabled: form, checked: form && viewMode === "code", action: function () { studio.setMode("code"); } },
            { text: "&QML Source", enabled: form, action: showQml },
            { separator: true },
            { text: "&Design", enabled: table, checked: table && viewMode === "design", action: function () { studio.setMode("design"); } },
            { text: "D&ata", enabled: table, checked: table && viewMode === "data", action: function () { studio.setMode("data"); } },
            { separator: true },
            { text: "&Grid Dots", checked: showGrid, action: function () { studio.showGrid = !studio.showGrid; } },
            { text: "Grid of &4", checked: gridSize === 4, action: function () { studio.gridSize = 4; } },
            { text: "Grid of &8", checked: gridSize === 8, action: function () { studio.gridSize = 8; } },
            { text: "Grid of 1&6", checked: gridSize === 16, action: function () { studio.gridSize = 16; } },
            { separator: true },
            { text: "Out&put", checked: showOutput, action: function () { studio.showOutput = !studio.showOutput; } },
        ];
    }
    function projectMenu() {
        return [
            { text: "Add &Form", action: addForm },
            { text: "Add &Table", action: addTable },
            { text: "Add Form for " + (viewKind === "table" ? viewName : "a Table"), enabled: viewKind === "table", action: addFormForTable },
            { text: "&Remove " + (viewKind !== "" ? viewName : ""), enabled: viewKind !== "", action: removeCurrent },
            { separator: true },
            { text: "&Database...", action: function () {
                dialogs.editDatabase(project.database, function (config) {
                    studio.closeData();
                    var changed = JSON.stringify(config) !== JSON.stringify(project.database);
                    studio.change(function () { project.database = config; });
                    // another database: what's in it already can come into the project
                    if (changed)
                        studio.importTables(true);
                });
            } },
            { text: "&Import Tables...", action: function () { studio.importTables(false); } },
            { text: "AP&Is...", action: function () {
                dialogs.editApis(project.apis || [], function (apis) {
                    studio.closeData();
                    studio.change(function () { project.apis = apis; });
                });
            } },
            { text: "Add API Ta&ble...", action: studio.addApiTable },
            { text: "&Properties...", action: function () {
                dialogs.editProject(project.app, project.ai || {}, function (app, ai) {
                    studio.change(function () {
                        project.app = app;
                        if (ai)
                            project.ai = ai;
                        else
                            delete project.ai;
                    });
                });
            } },
        ];
    }
    function formatMenu() {
        var some = designing && selection.length > 0, several = designing && selection.length > 1;
        return [
            { text: "Align &Lefts", enabled: several, action: function () { studio.align("left"); } },
            { text: "Align &Rights", enabled: several, action: function () { studio.align("right"); } },
            { text: "Align &Tops", enabled: several, action: function () { studio.align("top"); } },
            { text: "Align &Bottoms", enabled: several, action: function () { studio.align("bottom"); } },
            { separator: true },
            { text: "Make Same &Width", enabled: several, action: function () { studio.align("width"); } },
            { text: "Make Same &Height", enabled: several, action: function () { studio.align("height"); } },
            { separator: true },
            { text: "&Center in Form Horizontally", enabled: some, action: function () { studio.align("centerH"); } },
            { text: "Center in Form &Vertically", enabled: some, action: function () { studio.align("centerV"); } },
            { separator: true },
            { text: "Bring to &Front", enabled: some, action: function () { studio.restack(true); } },
            { text: "&Send to Back", enabled: some, action: function () { studio.restack(false); } },
        ];
    }
    function runMenu() {
        return [
            { text: "&Start", shortcut: "F5", enabled: !StudioTools.running, action: start },
            { text: "&End", enabled: StudioTools.running, action: function () { StudioTools.stop(); } },
            { separator: true },
            { text: "&Check for Problems", action: function () {
                studio.ensureSaved(function () { studio.checkProject(false); });
            } },
        ];
    }
    // the manual, in the Markdown Editor
    function openManual(page) {
        var problem = StudioTools.openManual(page || "index.md");
        if (problem !== "")
            msgBox.show("Wayland Studio Manual", problem, "warning", ["OK"], null);
    }
    function openSample() {
        confirmDiscard(function () {
            var path = StudioTools.sampleProject();
            if (path === "")
                msgBox.show("Book Club Sample", "The sample isn't installed (it comes with the manual).", "warning", ["OK"], null);
            else
                openProject(path);
        });
    }
    // Help > Build Apps with Claude: Claude Code (or another MCP client)
    // starts win95studio-mcp and builds apps with its tools
    function showClaude() {
        var server = StudioTools.mcpServer();
        if (server === "") {
            msgBox.show("Build Apps with Claude", "Claude's tools for Wayland Studio (win95studio-mcp) aren't installed.",
                        "error", ["OK"], null);
            return;
        }
        var command = "claude mcp add --scope user wayland-studio -- " + server;
        var claude = StudioTools.claudeCodeInstalled();
        msgBox.show("Build Apps with Claude",
                    "Claude can build Wayland Studio apps for you: the tables, the forms, their code. It works on the "
                    + "project files, and a project open here follows each change (Undo takes one back).\n\n"
                    + "To give Claude Code the tools, run:\n\n" + command + "\n\n"
                    + "Then ask Claude for an app, like \"Make a Wayland Studio app in ~/Recipes for my recipes.\"\n\n"
                    + "Another MCP client: its command is " + server + " (stdio). Help > Contents, Building Apps with Claude, says more.",
                    "info", claude ? ["Add to Claude Code", "Copy Command", "Close"] : ["Copy Command", "Close"], function (i) {
            if (claude && i === 0) {
                var error = StudioTools.addToClaudeCode();
                msgBox.show("Build Apps with Claude", error === ""
                            ? "Claude Code has Wayland Studio's tools now. Start a new Claude Code session and ask it for an app."
                            : "It wasn't added:\n\n" + error, error === "" ? "info" : "error", ["OK"], null);
            } else if (i === (claude ? 1 : 0)) {
                StudioTools.copyText(command);
            }
        });
    }
    function helpMenu() {
        return [
            { text: "&Contents", shortcut: "F1", action: function () { studio.openManual("index.md"); } },
            { text: "&Building Your First App", action: function () { studio.openManual("first-app.md"); } },
            { text: "&Data Designer", action: function () { studio.openManual("data-designer.md"); } },
            { text: "&Forms and Elements", action: function () { studio.openManual("forms.md"); } },
            { text: "&Writing Code", action: function () { studio.openManual("code.md"); } },
            { text: "An &AI Chat in Your App", action: function () { studio.openManual("ai-chat.md"); } },
            { separator: true },
            { text: "Open the Book Club &Sample", action: studio.openSample },
            { text: "Build Apps with &Claude...", action: studio.showClaude },
            { separator: true },
            { text: "&About Wayland Studio", action: function () {
                msgBox.show("About Wayland Studio",
                            "Wayland Studio\n\nMake Wayland 95 apps: tables with rules in the Data Designer, forms on a grid, "
                            + "code for their events, and a program at the end.\n\nWayland 95 " + Wayland95Settings.version,
                            "info", ["OK"], null);
            } },
        ];
    }
    function showQml() {
        flush();
        var form = currentForm();
        if (form)
            dialogs.editText("QML Source - " + form.name + " (to read: changes here aren't kept)",
                             StudioTools.formQml(project, form.name, projectDir), function (text) {});
    }

    Shortcut { sequence: "F1"; enabled: !studio.modalOpen; onActivated: studio.openManual("index.md") }
    Shortcut { sequence: "Ctrl+N"; enabled: !studio.modalOpen; onActivated: studio.newProject() }
    Shortcut { sequence: "Ctrl+O"; enabled: !studio.modalOpen; onActivated: studio.showOpen() }
    Shortcut { sequence: "Ctrl+S"; enabled: !studio.modalOpen; onActivated: studio.save() }
    Shortcut { sequence: "F5"; enabled: !studio.modalOpen; onActivated: studio.start() }
    Shortcut { sequence: "F7"; enabled: !studio.modalOpen && studio.viewKind === "form"; onActivated: studio.setMode("code") }
    Shortcut { sequence: "Shift+F7"; enabled: !studio.modalOpen && studio.viewKind === "form"; onActivated: studio.setMode("object") }
    // (while the canvas has the keyboard: a text box keeps these for itself)
    Shortcut { sequence: "Ctrl+Z"; enabled: !studio.modalOpen && formDesigner.activeFocus; onActivated: studio.undo() }
    Shortcut { sequence: "Ctrl+Y"; enabled: !studio.modalOpen && formDesigner.activeFocus; onActivated: studio.redo() }
    Shortcut { sequence: "Ctrl+X"; enabled: !studio.modalOpen && formDesigner.activeFocus; onActivated: studio.cut() }
    Shortcut { sequence: "Ctrl+C"; enabled: !studio.modalOpen && formDesigner.activeFocus; onActivated: studio.copy() }
    Shortcut { sequence: "Ctrl+V"; enabled: !studio.modalOpen && formDesigner.activeFocus; onActivated: studio.paste() }
    Shortcut { sequence: "Ctrl+A"; enabled: !studio.modalOpen && formDesigner.activeFocus; onActivated: studio.selectAll() }

    // ----------------------------------------------------------------- layout

    MenuBar95 {
        id: menuBar
        x: 2
        y: 2
        width: parent.width - 4
        height: 20
        popupMenu: popup
        menus: [
            { title: "File", items: studio.fileMenu },
            { title: "Edit", items: studio.editMenu },
            { title: "View", items: studio.viewMenu },
            { title: "Project", items: studio.projectMenu },
            { title: "Format", items: studio.formatMenu },
            { title: "Run", items: studio.runMenu },
            { title: "Help", items: studio.helpMenu },
        ]
    }

    Rectangle { x: 0; y: 23; width: parent.width; height: 1; color: Theme.shadow }
    Rectangle { x: 0; y: 24; width: parent.width; height: 1; color: Theme.highlight }
    Row {
        id: toolbar
        x: 4
        y: 27
        height: 24
        spacing: 2
        ToolButton95 { height: 24; text: "New"; onClicked: studio.newProject() }
        ToolButton95 { height: 24; text: "Open"; onClicked: studio.showOpen() }
        ToolButton95 { height: 24; text: "Save"; onClicked: studio.save() }
        Item { width: 8; height: 1 }
        ToolButton95 { height: 24; text: "Add Form"; onClicked: studio.addForm() }
        ToolButton95 { height: 24; text: "Add Table"; onClicked: studio.addTable() }
        Item { width: 8; height: 1 }
        ToolButton95 { height: 24; text: "Undo"; enabled: studio.undoStack.length > 0; onClicked: studio.undo() }
        ToolButton95 { height: 24; text: "Redo"; enabled: studio.redoStack.length > 0; onClicked: studio.redo() }
        Item { width: 8; height: 1 }
        ToolButton95 { height: 24; glyph: "▶"; text: "Start"; enabled: !StudioTools.running; onClicked: studio.start() }
        ToolButton95 { height: 24; glyph: "■"; text: "End"; enabled: StudioTools.running; onClicked: StudioTools.stop() }
        ToolButton95 { height: 24; text: "Make Program"; onClicked: studio.makeProgram() }
    }

    readonly property int topEdge: 56
    readonly property int rightWidth: 264
    readonly property int outputHeight: showOutput ? 124 : 0
    readonly property int bottomEdge: height - 24 - outputHeight

    // the Toolbox
    Item {
        id: toolbox
        visible: studio.designing
        x: 4
        y: studio.topEdge
        width: 104
        height: studio.bottomEdge - y

        Rectangle {
            width: parent.width
            height: 18
            color: Theme.titleActiveStart
            Label95 { x: 4; height: parent.height; text: "Toolbox"; color: Theme.titleActiveText; font.bold: true }
        }
        Column {
            y: 20
            Repeater {
                model: [{ type: "", glyph: "↖", label: "Pointer" }].concat(studio.registry.elements)
                delegate: Item {
                    id: toolItem
                    required property var modelData
                    readonly property string type: modelData.type
                    readonly property bool chosen: studio.tool === type
                    width: toolbox.width
                    height: 24
                    Bevel {
                        anchors.fill: parent
                        style: toolItem.chosen || toolMouse.pressed ? "pressed" : "raised"
                        face: toolItem.chosen ? Theme.light : Theme.face
                    }
                    Text {
                        x: 4 + (toolItem.chosen ? 1 : 0)
                        width: 24
                        height: parent.height
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                        text: toolItem.modelData.glyph
                        color: Theme.text
                        font.family: Theme.fontFamily
                        font.pixelSize: 12
                        font.bold: true
                    }
                    Label95 {
                        x: 32 + (toolItem.chosen ? 1 : 0)
                        height: parent.height
                        text: toolItem.modelData.label || toolItem.type
                    }
                    MouseArea {
                        id: toolMouse
                        anchors.fill: parent
                        onClicked: studio.tool = toolItem.type
                        onDoubleClicked: {
                            if (toolItem.type !== "")
                                studio.addControlCentered(toolItem.type);
                        }
                    }
                }
            }
        }
    }

    // what's open
    Item {
        id: workArea
        x: studio.designing ? toolbox.x + toolbox.width + 4 : 4
        y: studio.topEdge
        width: parent.width - x - studio.rightWidth - 8
        height: studio.bottomEdge - y

        Rectangle {
            width: parent.width
            height: 18
            color: Theme.titleActiveStart
            Label95 {
                x: 4
                height: parent.height
                color: Theme.titleActiveText
                font.bold: true
                text: studio.viewKind === "" ? "Nothing is open"
                    : studio.viewName + (studio.viewKind === "form" ? " (Form)" : " (Table)")
            }
        }
        Row {
            id: modeTabs
            y: 21
            spacing: 2
            visible: studio.viewKind !== ""
            ToolButton95 {
                height: 22
                text: studio.viewKind === "form" ? "Object" : "Design"
                checked: studio.viewMode === "object" || studio.viewMode === "design"
                onClicked: studio.setMode(studio.viewKind === "form" ? "object" : "design")
            }
            ToolButton95 {
                height: 22
                text: studio.viewKind === "form" ? "Code" : "Data"
                checked: studio.viewMode === "code" || studio.viewMode === "data"
                onClicked: studio.setMode(studio.viewKind === "form" ? "code" : "data")
            }
        }
        Label95 {
            anchors.right: parent.right
            y: 21
            height: 22
            color: Theme.shadow
            text: {
                studio.revision;
                if (!studio.designing)
                    return "";
                var list = studio.selected();
                if (list.length === 0) {
                    var f = studio.currentForm();
                    return f ? f.width + " x " + f.height : "";
                }
                var c = list[list.length - 1];
                return (list.length > 1 ? list.length + " selected    " : "") + c.x + ", " + c.y + "    " + c.width + " x " + c.height;
            }
        }

        FormDesigner {
            id: formDesigner
            y: 46
            width: parent.width
            height: parent.height - y
            visible: studio.viewKind === "form" && studio.viewMode === "object"
            studio: studio
        }
        CodeWindow {
            id: codeWindow
            y: 46
            width: parent.width
            height: parent.height - y
            visible: studio.viewKind === "form" && studio.viewMode === "code"
            studio: studio
        }
        TableDesigner {
            id: tableDesigner
            y: 46
            width: parent.width
            height: parent.height - y
            visible: studio.viewKind === "table"
            studio: studio
        }
    }

    // the Project window
    Item {
        id: explorer
        x: parent.width - studio.rightWidth - 4
        y: studio.topEdge
        width: studio.rightWidth
        height: 190

        readonly property var entries: {
            studio.revision;
            var out = [{ kind: "head", label: "Forms" }];
            studio.project.forms.forEach(function (f) {
                out.push({ kind: "form", name: f.name, note: studio.project.app.startForm === f.name ? "  (startup)" : "" });
            });
            out.push({ kind: "head", label: "Tables" });
            studio.project.tables.forEach(function (t) { out.push({ kind: "table", name: t.name, note: t.source === "api" ? "  (" + t.api + " API)" : "" }); });
            return out;
        }

        Rectangle {
            width: parent.width
            height: 18
            color: Theme.titleActiveStart
            Label95 {
                x: 4
                width: parent.width - 8
                height: parent.height
                elide: Text.ElideRight
                color: Theme.titleActiveText
                font.bold: true
                text: { studio.revision; return "Project - " + (studio.project.app.title || studio.project.app.name); }
            }
        }
        Bevel { y: 20; width: parent.width; height: parent.height - 20; style: "sunken"; face: Theme.window }
        ListView {
            id: explorerList
            x: 2
            y: 22
            width: parent.width - 4 - (explorerBar.needed ? explorerBar.width : 0)
            height: parent.height - 24
            clip: true
            boundsBehavior: Flickable.StopAtBounds
            model: explorer.entries
            delegate: Rectangle {
                id: entry
                required property var modelData
                readonly property bool head: modelData.kind === "head"
                readonly property bool current: !head && studio.viewKind === modelData.kind && studio.viewName === modelData.name
                width: explorerList.width
                height: 18
                color: current ? Theme.selection : "transparent"
                Label95 {
                    x: entry.head ? 4 : 22
                    width: parent.width - x - 4
                    height: parent.height
                    elide: Text.ElideRight
                    font.bold: entry.head
                    color: entry.current ? Theme.selectionText : Theme.text
                    text: entry.head ? entry.modelData.label : entry.modelData.name + entry.modelData.note
                }
                MouseArea {
                    anchors.fill: parent
                    acceptedButtons: Qt.LeftButton | Qt.RightButton
                    onPressed: (event) => {
                        if (entry.head)
                            return;
                        if (!entry.current) {
                            if (entry.modelData.kind === "form")
                                studio.openForm(entry.modelData.name, "object");
                            else
                                studio.openTable(entry.modelData.name);
                        }
                        if (event.button !== Qt.RightButton)
                            return;
                        var p = mapToItem(popup, event.x, event.y);
                        var isForm = entry.modelData.kind === "form", name = entry.modelData.name;
                        popup.popup(p.x, p.y, isForm ? [
                            { text: "View &Object", action: function () { studio.setMode("object"); } },
                            { text: "View &Code", action: function () { studio.setMode("code"); } },
                            { separator: true },
                            { text: "&Startup Form", checked: studio.project.app.startForm === name, action: function () {
                                studio.change(function () { studio.project.app.startForm = name; });
                            } },
                            { text: "&Remove", action: studio.removeCurrent },
                        ] : [
                            { text: "&Design", action: function () { studio.setMode("design"); } },
                            { text: "D&ata", action: function () { studio.setMode("data"); } },
                            { text: "Add a &Form for It", action: studio.addFormForTable },
                            { separator: true },
                            { text: "&Remove", action: studio.removeCurrent },
                        ]);
                    }
                    onDoubleClicked: {
                        if (entry.modelData.kind === "form")
                            studio.setMode("code");
                    }
                }
            }
        }
        ScrollBar95 { id: explorerBar; flickable: explorerList; anchors.right: parent.right; anchors.rightMargin: 2; y: 22; height: explorerList.height }
    }

    // the Properties window
    Item {
        id: properties
        x: explorer.x
        y: explorer.y + explorer.height + 6
        width: studio.rightWidth
        height: studio.bottomEdge - y

        readonly property var objects: {
            studio.revision;
            var form = studio.currentForm(), out = [];
            if (!form)
                return out;
            out.push({ name: "", label: form.name + "  Form" });
            form.controls.forEach(function (c) { out.push({ name: c.name, label: c.name + "  " + c.type }); });
            return out;
        }
        readonly property string leadName: studio.selection.length ? studio.selection[studio.selection.length - 1] : ""

        Rectangle {
            width: parent.width
            height: 18
            color: Theme.titleActiveStart
            Label95 {
                x: 4
                width: parent.width - 8
                height: parent.height
                elide: Text.ElideRight
                color: Theme.titleActiveText
                font.bold: true
                text: "Properties" + (studio.selection.length > 1 ? " - " + studio.selection.length + " elements" : "")
            }
        }
        ComboBox95 {
            id: objectCombo
            y: 21
            width: parent.width
            visible: studio.viewKind === "form"
            popupMenu: popup
            model: properties.objects.map(function (o) { return o.label; })
            currentIndex: {
                for (var i = 0; i < properties.objects.length; ++i) {
                    if (properties.objects[i].name === properties.leadName)
                        return i;
                }
                return 0;
            }
            onActivated: (index) => {
                var name = properties.objects[index].name;
                studio.selection = name === "" ? [] : [name];
            }
        }
        PropertyGrid {
            id: propertyGrid
            y: 45
            width: parent.width
            height: parent.height - y
            visible: studio.viewKind === "form"
            popupMenu: popup
            tools: dialogs
            rows: studio.propertyRows
            onEdited: (name, value) => studio.setProperty(name, value)
        }
        Label95 {
            visible: studio.viewKind !== "form"
            x: 4
            y: 26
            width: parent.width - 8
            wrapMode: Text.Wrap
            verticalAlignment: Text.AlignTop
            color: Theme.shadow
            text: studio.viewKind === "table" ? "A table's and its fields' properties are in the Data Designer, under the list of fields."
                                              : "Project > Add Form or Add Table starts one."
        }
    }

    // Output: what the running app says, and what the compiler finds wrong
    Item {
        id: output
        visible: studio.showOutput
        x: 4
        y: studio.bottomEdge + 4
        width: parent.width - 8
        height: studio.outputHeight - 4

        Rectangle {
            width: parent.width
            height: 18
            color: Theme.titleActiveStart
            Label95 { x: 4; height: parent.height; text: "Output"; color: Theme.titleActiveText; font.bold: true }
            Label95 {
                anchors.right: parent.right
                anchors.rightMargin: 6
                height: parent.height
                color: Theme.titleActiveText
                text: "Clear"
                MouseArea { anchors.fill: parent; anchors.margins: -3; onClicked: studio.outputLines = [] }
            }
        }
        Bevel { y: 20; width: parent.width; height: parent.height - 20; style: "sunken"; face: Theme.window }
        ListView {
            id: outputView
            x: 4
            y: 22
            width: parent.width - 8 - (outputBar.needed ? outputBar.width : 0)
            height: parent.height - 24
            clip: true
            boundsBehavior: Flickable.StopAtBounds
            model: studio.outputLines
            delegate: Text {
                required property string modelData
                width: outputView.width
                text: modelData
                textFormat: Text.PlainText
                elide: Text.ElideRight
                color: Theme.text
                font.family: Theme.monoFamily
                font.pixelSize: Theme.fontSize
            }
        }
        ScrollBar95 { id: outputBar; flickable: outputView; anchors.right: parent.right; anchors.rightMargin: 2; y: 22; height: outputView.height }
    }

    // the status bar
    Item {
        x: 2
        y: parent.height - 22
        width: parent.width - 4
        height: 20
        Bevel { width: parent.width - 262; height: parent.height; style: "thinSunken" }
        Label95 {
            x: 5
            width: parent.width - 272
            height: parent.height
            elide: Text.ElideRight
            text: StudioTools.running ? "Running " + studio.project.app.name + "..."
                : studio.tool !== "" ? "Drag on the form where the " + studio.tool + " goes (or double-click it in the Toolbox)."
                : studio.projectPath !== "" ? studio.projectPath : "Not saved yet"
        }
        Bevel { x: parent.width - 260; width: 260; height: parent.height; style: "thinSunken" }
        Label95 {
            x: parent.width - 255
            width: 250
            height: parent.height
            elide: Text.ElideRight
            text: {
                studio.revision;
                var d = studio.project.database || {};
                return d.driver === "mysql" ? "MySQL: " + (d.name || "?") + (d.host ? " on " + d.host : "")
                     : d.driver === "postgresql" ? "PostgreSQL: " + (d.name || "?") + (d.host ? " on " + d.host : "")
                     : "SQLite: " + (d.file || "data.db");
            }
        }
    }

    // ---------------------------------------------------------------- dialogs

    Item { id: dialogLayer; anchors.fill: parent; z: 1400 }

    StudioDialogs {
        id: dialogs
        popupMenu: popup
        messageBox: msgBox
        projectDir: studio.projectDir
        formNames: { studio.revision; return studio.project.forms.map(function (f) { return f.name; }); }
        projectTables: { studio.revision; return studio.project.tables || []; }
        projectDatabase: { studio.revision; return studio.project.database || {}; }
        projectApis: { studio.revision; return studio.project.apis || []; }
        onPictureWanted: (callback) => {
            studio.pictureCallback = callback;
            studio.filePurpose = "picture";
            fileDialog.mode = "open";
            fileDialog.filters = [
                { label: "Pictures (*.png *.jpg *.gif *.bmp *.svg)", patterns: ["*.png", "*.jpg", "*.jpeg", "*.gif", "*.bmp", "*.svg"] },
                { label: "All Files (*.*)", patterns: ["*"] },
            ];
            fileDialog.filterIndex = 0;
            fileDialog.defaultSuffix = "";
            fileDialog.openAt(studio.projectDir !== "" ? studio.projectDir : Win95Fs.homePath(), "");
        }
        onMediaWanted: (callback) => {
            studio.pictureCallback = callback;
            studio.filePurpose = "picture";
            fileDialog.mode = "open";
            var sound = ["*.mp3", "*.wav", "*.flac", "*.ogg", "*.oga", "*.opus", "*.m4a", "*.aac", "*.wma"];
            var video = ["*.mp4", "*.m4v", "*.webm", "*.mkv", "*.avi", "*.mov", "*.mpg", "*.mpeg", "*.ogv", "*.wmv"];
            fileDialog.filters = [
                { label: "Sound and Video", patterns: sound.concat(video) },
                { label: "Sound (*.mp3 *.wav *.flac *.ogg *.m4a...)", patterns: sound },
                { label: "Video (*.mp4 *.webm *.mkv *.avi *.mov...)", patterns: video },
                { label: "All Files (*.*)", patterns: ["*"] },
            ];
            fileDialog.filterIndex = 0;
            fileDialog.defaultSuffix = "";
            fileDialog.openAt(studio.projectDir !== "" ? studio.projectDir : Win95Fs.homePath(), "");
        }
    }

    FileDialog95 {
        id: fileDialog
        z: 1600
        messageBox: msgBox
        popupMenu: popup
        onAccepted: (path) => {
            var purpose = studio.filePurpose;
            studio.filePurpose = "";
            if (purpose === "open") {
                studio.openProject(path);
            } else if (purpose === "save") {
                var then = studio.afterSave;
                studio.afterSave = null;
                if (studio.saveTo(path) && then)
                    then();
            } else if (purpose === "make") {
                var r = StudioTools.build(studio.projectPath, path);
                if (r.ok) {
                    studio.say("Made " + path);
                    msgBox.show("Make Program", path + "\n\nis " + studio.project.app.name + " as a program of its own: it runs on any Wayland 95.",
                                "info", ["OK"], null);
                } else {
                    r.problems.forEach(function (p) { studio.say("    " + p); });
                    msgBox.show("Make Program", r.problems.join("\n"), "error", ["OK"], null);
                }
            } else if (purpose === "picture") {
                var cb = studio.pictureCallback;
                studio.pictureCallback = null;
                if (cb)
                    cb(StudioTools.projectRelative(studio.projectDir, path));
            }
        }
    }

    MessageBox95 { id: msgBox }
    Menu95 { id: popup }
}
