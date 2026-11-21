import QtQuick
import Win95Ui

// Trading Strategies: the library of built-in lessons plus the user's and Claude's own documents, by category, with
// a level filter and search, progress per lesson, the reader (Markdown with drawn chart diagrams), check-yourself
// questions, notes, "Set up my chart", the "Ask Claude" buttons and an editor for own documents. The
// candlestick-patterns lesson carries the pattern gallery. Port of views/StrategiesView.tsx.
Item {
    id: view
    property var app

    // ---------------------------------------------------------------- data
    property var builtin: []
    property var custom: []
    property var progress: ({})        // id -> {status, note, quiz_score, updated_at}
    property var meta: ({ categories: [], levels: [], progressStates: [], progressLabel: {}, newDoc: {} })
    property var intervalCap: ({})
    property string sel: ""
    property string q: ""
    property string level: "all"
    property var editing: null         // {doc: StrategyDoc | null} while the editor is open

    // keep the open document in sight in the list
    onSelChanged: Qt.callLater(function () {
        const i = rows.findIndex(function (r) { return r.doc && r.doc.id === sel; });
        if (i >= 0) docList.list.positionViewAtIndex(i, ListView.Contain);
    })
    // a fresh editor for each document (upstream keys it by id)
    onEditingChanged: { editorLoader.active = false; editorLoader.active = !!editing; }

    readonly property var all: builtin.concat(custom)
    readonly property var doc: {
        for (let i = 0; i < all.length; ++i) if (all[i].id === sel) return all[i];
        return all.length ? all[0] : null;
    }
    readonly property var shown: {
        const t = q.trim().toLowerCase();
        return all.filter(function (d) {
            return (level === "all" || d.level === level) && (!t || (d.title + " " + d.summary + " " + d.tags.join(" ") + " " + d.category).toLowerCase().indexOf(t) >= 0);
        });
    }
    // the list: a header row per category, then its documents
    readonly property var rows: {
        const out = [];
        (meta.categories || []).forEach(function (cat) {
            const items = shown.filter(function (d) { return d.category === cat; });
            if (!items.length) return;
            out.push({ header: cat });
            items.forEach(function (d) { out.push({ doc: d }); });
        });
        return out;
    }
    function status(id) { return progress[id] && progress[id].status ? progress[id].status : "new"; }
    readonly property int done: all.filter(function (d) { const s = status(d.id); return s === "practiced" || s === "confident"; }).length

    function load() {
        api.invoke("strat:list", [], function (l) { custom = l || []; });
        api.invoke("strat:progress", [], function (p) {
            const m = {};
            (p || []).forEach(function (x) { m[x.id] = x; });
            progress = m;
        });
    }
    Component.onCompleted: {
        api.invoke("content:get", ["strategy-meta.json"], function (m) { if (m) meta = m; });
        api.invoke("content:get", ["fmp-caps.json"], function (c) { if (c && c.intervalCap) intervalCap = c.intervalCap; });
        api.invoke("strat:builtin", [], function (b) {
            builtin = b || [];
            if (!sel && builtin.length) sel = builtin[0].id;
            view.runTest();
        });
        load();
    }
    Connections {
        target: api
        function onEvent(channel, payload) { if (channel === "strategies:changed") view.load(); }
    }

    // the "W95_TRADINGLAB_TEST" screenshots: strategies-diagram opens a lesson with a drawn diagram, strategies-patterns the gallery
    function runTest() {
        if (testScript === "strategies-diagram") {
            sel = "head-and-shoulders";
            Qt.callLater(function () { const y = reader.diagramY(); if (y > 0) page.scrollTo(y - 120); });
        } else if (testScript === "strategies-patterns") {
            sel = "candlestick-patterns";
            Qt.callLater(function () { page.scrollToItem(patternsLoader); });
        } else if (testScript === "strategies-editor") {
            sel = "head-and-shoulders";
            Qt.callLater(function () { editing = { doc: null }; });
        } else if (testScript === "strategies-save") {
            save({ title: "Test gap fade", category: "My strategies", level: "beginner", minutes: 5, summary: "A saved test document.", tags: ["gap"],
                   body: "## The idea\n\nFade **large** gaps.", quiz: [{ q: "When?", a: "At the open." }], chartSetup: core.setupFromSettings(app.chartSettings) });
        } else if (testScript === "strategies-measure") {
            sel = "candlestick-patterns";
            Qt.callLater(function () {
                const g = patternsLoader.item;
                if (!g) return;
                g.measure();
                g.sel = "bullish-engulfing";
                page.scrollToItem(patternsLoader);
            });
        }
    }

    // a guide whose interval the FMP plan does not include (chart/timeframe.ts isBlocked)
    function isBlocked(interval) {
        return !!app && app.hidePlanGaps && app.unavailable.indexOf(intervalCap[interval]) >= 0;
    }
    function intervalLabel(id) {
        const iv = core.intervals().find(function (x) { return x.id === id; });
        return iv ? iv.label : id;
    }
    function levelTone(l) { return l === "beginner" ? "green" : l === "intermediate" ? "blue" : "amber"; }
    readonly property var dotColor: ({ "new": "#c0c0c0", learning: "#e0a000", practiced: "#2070d0", confident: "#20a040" })

    // ---------------------------------------------------------------- Ask Claude
    function ask(text) { app.askClaude(text); }
    function teach() { ask("I want to learn the \"" + doc.title + "\" strategy (document id " + doc.id + "). Use get_strategy to read the document and my progress, then teach me step by step in short chunks: start with the core idea, then ask me one question to check I understood before moving on. Use my chart where it helps (apply_strategy_chart, find_swings, mark_bar). Do not dump the whole document at once."); }
    function quiz() { ask("Quiz me on \"" + doc.title + "\" (document id " + doc.id + "). Read it with get_strategy, then ask me one question at a time (use its check-yourself questions plus a few of your own that apply the idea to a chart), wait for my answer, and give honest feedback. At the end tell me my score and, if I agree, record it with update_strategy_progress."); }
    function example() { ask("Find real examples of the \"" + doc.title + "\" setup (document id " + doc.id + ") on the " + app.symbol + " chart. Use apply_strategy_chart to set the chart up, read the data with the chart tools, mark what you find with mark_bar and lines, and explain each one. If the setup is not actually present, say so plainly instead of forcing an example."); }
    function plan() { ask("Walk me through a practice trade plan using the \"" + doc.title + "\" strategy (document id " + doc.id + ") on " + app.symbol + ". Check whether the setup is actually present. If it is, use get_risk_rules and calculate_position_size for the size and explain the stop, target and what would prove it wrong; prepare an order ticket only if I agree. If it is not present, tell me what you would wait for."); }
    function simple() { ask("Explain the \"" + doc.title + "\" strategy (document id " + doc.id + ") in plain words as if I am completely new, using a real example from the " + app.symbol + " chart."); }
    function write() { ask("Help me write my own strategy document. Ask me a few questions about the idea (what I look for, entry, stop, target, timeframe), then create it with create_strategy_doc including a chart setup and a few check-yourself questions."); }

    // ---------------------------------------------------------------- saving, copying, deleting
    function save(d) {
        if (editing && editing.doc) {
            api.invoke("strat:update", [editing.doc.id, d], function () { app.toast("success", "Strategy saved"); editing = null; load(); });
        } else {
            api.invoke("strat:create", [Object.assign({}, d, { source: "user" })], function (c) {
                if (c && c.id) sel = c.id;
                app.toast("success", "Strategy created");
                editing = null;
                load();
            });
        }
    }
    function duplicate() {
        const d = doc;
        api.invoke("strat:create", [{ title: "My version of " + d.title, category: d.category, level: d.level, minutes: d.minutes, summary: d.summary, tags: d.tags,
                                      body: d.body, quiz: d.quiz, chartSetup: d.chartSetup, source: "user" }], function (c) {
            if (c && c.id) { sel = c.id; editing = { doc: c }; }
            load();
        });
    }
    function del() {
        const d = doc;
        app.confirm("Delete “" + d.title + "”? This cannot be undone.", function () {
            api.invoke("strat:delete", [d.id], function () {
                sel = builtin.length ? builtin[0].id : "";
                app.toast("info", "Strategy deleted");
                load();
            });
        });
    }
    function setStatus(s) { api.invoke("strat:setProgress", [doc.id, { status: s }]); }

    // ---------------------------------------------------------------- layout
    DocTitleBar {
        id: titleBar
        width: parent.width
        title: "Trading Strategies"
        Button95 { text: "Ask Claude to write one"; onClicked: view.write() }
        Button95 { text: "New strategy"; onClicked: view.editing = { doc: null } }
    }

    // the list
    Item {
        id: listPane
        y: titleBar.height + 4
        width: 250
        height: parent.height - y
        Column {
            id: listTop
            width: parent.width
            spacing: 4
            Label95 {
                width: parent.width
                textFormat: Text.StyledText
                text: "<b>" + view.done + "</b> of " + view.all.length + " practiced or confident"
            }
            ProgressBar95 { width: parent.width; height: 14; value: view.all.length ? Math.round(view.done / view.all.length * 100) : 0 }
            DocField { width: parent.width; placeholder: "Search strategies…"; onTextChanged: view.q = text }
            Flow {
                width: parent.width
                spacing: 3
                Repeater {
                    model: ["all"].concat(view.meta.levels || [])
                    DocChip {
                        required property string modelData
                        text: modelData === "all" ? "All levels" : modelData
                        checked: view.level === modelData
                        onClicked: view.level = modelData
                    }
                }
            }
        }
        DocRows {
            id: docList
            y: listTop.height + 6
            width: parent.width
            height: parent.height - y
            model: view.rows
            emptyText: view.shown.length === 0 && view.all.length ? "No strategies match." : ""
            delegate: Item {
                id: row
                required property var modelData
                readonly property bool isHeader: modelData.header !== undefined
                readonly property bool selected: !isHeader && view.doc && modelData.doc.id === view.doc.id
                width: ListView.view.width
                height: isHeader ? 22 : 36
                Rectangle {
                    visible: row.isHeader
                    anchors.fill: parent
                    color: Theme.face
                    Text {
                        x: 6
                        anchors.verticalCenter: parent.verticalCenter
                        text: row.isHeader ? row.modelData.header : ""
                        textFormat: Text.PlainText
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.fontSize
                        font.bold: true
                        color: Theme.text
                    }
                }
                Rectangle {
                    visible: !row.isHeader
                    anchors.fill: parent
                    color: row.selected ? Theme.selection : "transparent"
                }
                Rectangle {
                    visible: !row.isHeader
                    x: 7
                    y: 7
                    width: 8
                    height: 8
                    radius: 4
                    color: row.isHeader ? "transparent" : view.dotColor[view.status(row.modelData.doc.id)]
                    border.color: Theme.shadow
                    ToolTipArea { text: row.isHeader ? "" : view.meta.progressLabel[view.status(row.modelData.doc.id)] || "" }
                }
                Text {
                    visible: !row.isHeader
                    x: 20
                    y: 3
                    width: parent.width - x - 6
                    elide: Text.ElideRight
                    textFormat: Text.PlainText
                    text: row.isHeader ? "" : row.modelData.doc.title + (row.modelData.doc.source === "claude" ? "  ✦" : row.modelData.doc.source === "user" ? "  ✎" : "")
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontSize
                    font.bold: true
                    color: row.selected ? Theme.selectionText : Theme.text
                }
                Text {
                    visible: !row.isHeader
                    x: 20
                    y: 19
                    textFormat: Text.PlainText
                    text: row.isHeader ? "" : row.modelData.doc.level + " · " + row.modelData.doc.minutes + " min"
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontSize - 1
                    color: row.selected ? Theme.selectionText : Theme.shadow
                }
                MouseArea {
                    anchors.fill: parent
                    enabled: !row.isHeader
                    onClicked: { view.sel = row.modelData.doc.id; view.editing = null; page.scrollTo(0); }
                }
            }
        }
    }

    // the reader / the editor
    Item {
        id: pane
        x: listPane.width + 6
        y: listPane.y
        width: parent.width - x
        height: listPane.height

        DocScroll {
            id: page
            anchors.fill: parent
            visible: !view.editing && !!view.doc
            padding: 12

            Heading95 { level: 1; width: page.availableWidth; wrapMode: Text.Wrap; text: view.doc ? view.doc.title : "" }
            Flow {
                width: page.availableWidth
                spacing: 6
                DocBadge { text: view.doc ? view.doc.level : ""; tone: view.doc ? view.levelTone(view.doc.level) : "grey" }
                Label95 { dim: true; text: view.doc ? view.doc.category + " · " + view.doc.minutes + " min read" : "" }
                DocBadge { visible: !!view.doc && view.doc.source === "claude"; text: "✦ written by Claude"; tone: "purple" }
                DocBadge { visible: !!view.doc && view.doc.source === "user"; text: "my strategy"; tone: "grey" }
            }
            Label95 { width: page.availableWidth; wrapMode: Text.Wrap; font.pixelSize: Theme.fontSize + 2; text: view.doc ? view.doc.summary : "" }
            Flow {
                width: page.availableWidth
                spacing: 3
                Label95 { height: 20; verticalAlignment: Text.AlignVCenter; dim: true; text: "My progress  " }
                Repeater {
                    model: view.meta.progressStates || []
                    DocChip {
                        required property string modelData
                        text: view.meta.progressLabel[modelData] || modelData
                        checked: !!view.doc && view.status(view.doc.id) === modelData
                        onClicked: view.setStatus(modelData)
                    }
                }
                Label95 {
                    readonly property var p: view.doc ? view.progress[view.doc.id] : null
                    visible: !!p && p.quiz_score !== null && p.quiz_score !== undefined
                    height: 20
                    verticalAlignment: Text.AlignVCenter
                    textFormat: Text.StyledText
                    text: p ? "   last quiz: <b>" + p.quiz_score + "%</b>" : ""
                    color: Theme.shadow
                }
            }
            Flow {
                width: page.availableWidth
                spacing: 4
                readonly property bool hasSetup: !!view.doc && !!view.doc.chartSetup
                readonly property bool blocked: hasSetup && view.isBlocked(view.doc.chartSetup.interval)
                Button95 {
                    visible: parent.hasSetup && !parent.blocked
                    isDefault: true
                    text: "Set up my chart"
                    onClicked: view.app.applyChartSetup(view.doc.chartSetup, view.doc.title)
                }
                Label95 {
                    visible: parent.blocked
                    height: 23
                    verticalAlignment: Text.AlignVCenter
                    dim: true
                    text: parent.blocked ? "This guide needs " + view.intervalLabel(view.doc.chartSetup.interval) + " bars, which your FMP plan does not include." : ""
                }
                Button95 { text: "Teach me this"; onClicked: view.teach() }
                Button95 { text: "Quiz me"; onClicked: view.quiz() }
                Button95 { text: "Find an example"; onClicked: view.example() }
                Button95 { text: "Practice plan"; onClicked: view.plan() }
                Button95 { text: "Explain simply"; onClicked: view.simple() }
            }
            Loader {
                id: patternsLoader
                width: page.availableWidth
                active: !!view.doc && view.doc.id === "candlestick-patterns"
                visible: active
                sourceComponent: DocCandlePatterns {
                    width: page.availableWidth
                    app: view.app
                    onAskClaude: (text) => view.ask(text)
                    onWantTop: Qt.callLater(function () { page.scrollToItem(patternsLoader); })
                }
            }
            DocMarkdown {
                id: reader
                width: page.availableWidth
                text: view.doc ? view.doc.body : ""
                onLinkActivated: (link) => api.invoke("app:openExternal", [link])
                // where the first drawn diagram sits in the page (for the screenshot test)
                function diagramY() {
                    for (let i = 0; i < children.length; ++i) {
                        const c = children[i];
                        if (c.item && c.item.diagram !== undefined) return c.mapToItem(page.column, 0, 0).y;
                    }
                    return -1;
                }
            }
            Column {
                visible: !!view.doc && !!view.doc.chartSetup
                width: page.availableWidth
                spacing: 4
                Heading95 { level: 3; text: "Chart setup" }
                Loader {
                    active: !!view.doc && !!view.doc.chartSetup
                    sourceComponent: SetupCard { setup: view.doc.chartSetup }
                }
            }
            Column {
                visible: !!view.doc && view.doc.quiz.length > 0
                width: page.availableWidth
                spacing: 6
                Heading95 { level: 3; text: "Check yourself" }
                Repeater {
                    id: quizRepeater
                    model: view.doc ? view.doc.quiz : []
                    Column {
                        id: qa
                        required property var modelData
                        required property int index
                        property bool open: false
                        width: page.availableWidth
                        spacing: 3
                        Connections { target: view; function onDocChanged() { qa.open = false; } }
                        Text {
                            width: parent.width
                            wrapMode: Text.Wrap
                            textFormat: Text.StyledText
                            text: "<b>" + (qa.index + 1) + ".</b> " + Util.plain(qa.modelData.q)
                            color: Theme.text
                            font.family: Theme.fontFamily
                            font.pixelSize: Theme.fontSize + 1
                        }
                        Label95 { visible: qa.open; width: parent.width; leftPadding: 16; wrapMode: Text.Wrap; color: "#004080"; font.pixelSize: Theme.fontSize + 1; text: qa.modelData.a }
                        Button95 { visible: !qa.open; text: "Show answer"; height: 21; onClicked: qa.open = true }
                    }
                }
            }
            Column {
                width: page.availableWidth
                spacing: 4
                Heading95 { level: 3; text: "My notes" }
                TextArea95 {
                    id: noteArea
                    width: parent.width
                    height: 80
                    placeholder: "What clicked, what confused you, examples you found…"
                    property bool loading: true     // until reload(): the text box reports changes while it is built
                    property bool dirty: false
                    Component.onCompleted: reload()
                    onTextChanged: if (!loading) { dirty = true; noteTimer.restart(); }
                    Timer {
                        id: noteTimer
                        interval: 700
                        onTriggered: {
                            if (!view.doc || !noteArea.dirty) return;
                            api.invoke("strat:setProgress", [view.doc.id, { note: noteArea.text }]);
                            noteArea.dirty = false;
                        }
                    }
                    function reload() {
                        const p = view.doc ? view.progress[view.doc.id] : null;
                        loading = true;
                        text = p && p.note ? p.note : "";
                        Qt.callLater(function () { noteArea.loading = false; });
                        dirty = false;
                    }
                    Connections {
                        target: view
                        function onDocChanged() { noteTimer.stop(); noteArea.reload(); }
                        function onProgressChanged() { if (!noteArea.dirty) noteArea.reload(); }
                    }
                }
            }
            Row {
                spacing: 4
                Button95 { visible: !!view.doc && view.doc.source === "builtin"; text: "Copy to edit"; onClicked: view.duplicate() }
                Button95 { visible: !!view.doc && view.doc.source !== "builtin"; text: "Edit"; onClicked: view.editing = { doc: view.doc } }
                Button95 { visible: !!view.doc && view.doc.source !== "builtin"; text: "Delete"; onClicked: view.del() }
            }
        }

        Loader {
            id: editorLoader
            anchors.fill: parent
            active: false
            sourceComponent: Editor {
                host: view
                popup: menu
                initial: view.editing ? view.editing.doc : null
            }
        }
    }

    // ---------------------------------------------------------------- the timeframe and studies a guide sets up
    component SetupCard: Rectangle {
        id: card
        property var setup
        readonly property var d: setup ? core.describeSetup(setup) : ({ range: "", interval: "", studies: [] })
        width: cardCol.implicitWidth + 16
        height: cardCol.implicitHeight + 10
        color: Theme.window
        border.color: Theme.shadow
        Column {
            id: cardCol
            x: 8
            y: 5
            spacing: 2
            Text {
                textFormat: Text.StyledText
                text: "<font color=\"#808080\">Timeframe</font>  <b>" + Util.plain(card.d.range) + " · " + Util.plain(card.d.interval) + " bars</b>"
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontSize
                color: Theme.text
            }
            Text {
                textFormat: Text.StyledText
                text: "<font color=\"#808080\">Studies</font>  <b>" + Util.plain((card.d.studies || []).join(" · ") || "none") + "</b>"
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontSize
                color: Theme.text
            }
        }
    }

    // ---------------------------------------------------------------- the editor for own documents
    component Editor: DocScroll {
        id: ed
        property var initial: null
        property var host
        property Menu95 popup
        property var chartSetup: initial ? initial.chartSetup : null
        property var quizItems: initial ? JSON.parse(JSON.stringify(initial.quiz || [])) : []
        padding: 12
        well: false
        readonly property real half: (availableWidth - 10) / 2

        function collect() {
            return {
                title: titleField.text, category: categoryBox.currentText, level: levelBox.currentText, minutes: minutesBox.value,
                summary: summaryField.text, tags: tagsField.text.split(",").map(function (t) { return t.trim(); }).filter(Boolean),
                body: bodyArea.text, quiz: quizItems, chartSetup: chartSetup
            };
        }
        Component.onCompleted: {
            titleField.text = initial ? initial.title : "";
            categoryBox.currentIndex = Math.max(0, (ed.host.meta.categories || []).indexOf(initial ? initial.category : ed.host.meta.newDoc.category || "My strategies"));
            levelBox.currentIndex = Math.max(0, (ed.host.meta.levels || []).indexOf(initial ? initial.level : "beginner"));
            minutesBox.value = initial ? initial.minutes : 10;
            tagsField.text = initial ? (initial.tags || []).join(", ") : "";
            summaryField.text = initial ? initial.summary : "";
            bodyArea.text = initial ? initial.body : (ed.host.meta.newDoc.body || "");
        }

        Heading95 { level: 2; text: ed.initial ? "Edit strategy" : "New strategy" }
        EdLabel { text: "Title" }
        DocField { id: titleField; width: ed.availableWidth; placeholder: "e.g. Gap fade on large caps" }
        Row {
            spacing: 10
            Column {
                width: ed.half
                spacing: 2
                EdLabel { text: "Category" }
                ComboBox95 { id: categoryBox; width: parent.width; popupMenu: ed.popup; model: ed.host.meta.categories || [] }
            }
            Column {
                width: ed.half
                spacing: 2
                EdLabel { text: "Level" }
                ComboBox95 { id: levelBox; width: parent.width; popupMenu: ed.popup; model: ed.host.meta.levels || [] }
            }
        }
        Row {
            spacing: 10
            Column {
                width: ed.half
                spacing: 2
                EdLabel { text: "Minutes to read" }
                SpinBox95 { id: minutesBox; width: 80; from: 1; to: 600 }
            }
            Column {
                width: ed.half
                spacing: 2
                EdLabel { text: "Tags" }
                DocField { id: tagsField; width: parent.width; placeholder: "comma separated" }
            }
        }
        EdLabel { text: "One-line summary" }
        DocField { id: summaryField; width: ed.availableWidth }
        EdLabel { text: "Document (Markdown)" }
        TextArea95 { id: bodyArea; width: ed.availableWidth; height: 260; monospace: true }
        EdLabel { text: "Chart setup" }
        Flow {
            width: ed.availableWidth
            spacing: 6
            Loader { active: !!ed.chartSetup; visible: active; sourceComponent: SetupCard { setup: ed.chartSetup } }
            Label95 { visible: !ed.chartSetup; height: 23; verticalAlignment: Text.AlignVCenter; dim: true; text: "None. The \"Set up my chart\" button is hidden." }
            Button95 { text: "Use my current chart"; onClicked: ed.chartSetup = core.setupFromSettings(ed.host.app.chartSettings) }
            Button95 { visible: !!ed.chartSetup; text: "Clear"; onClicked: ed.chartSetup = null }
        }
        EdLabel { text: "Check-yourself questions" }
        Repeater {
            model: ed.quizItems.length
            Row {
                id: qRow
                required property int index
                spacing: 4
                DocField {
                    width: (ed.availableWidth - 90) / 2
                    placeholder: "Question"
                    Component.onCompleted: text = ed.quizItems[qRow.index].q
                    onTextChanged: if (ed.quizItems[qRow.index]) ed.quizItems[qRow.index].q = text
                }
                DocField {
                    width: (ed.availableWidth - 90) / 2
                    placeholder: "Answer"
                    Component.onCompleted: text = ed.quizItems[qRow.index].a
                    onTextChanged: if (ed.quizItems[qRow.index]) ed.quizItems[qRow.index].a = text
                }
                Button95 {
                    width: 75
                    height: 21
                    text: "Remove"
                    onClicked: ed.quizItems = ed.quizItems.filter(function (x, j) { return j !== qRow.index; })
                }
            }
        }
        Button95 { text: "Add question"; onClicked: ed.quizItems = ed.quizItems.concat([{ q: "", a: "" }]) }
        Row {
            spacing: 6
            Button95 { isDefault: true; text: "Save"; enabled: titleField.text.trim() !== ""; onClicked: ed.host.save(ed.collect()) }
            Button95 { text: "Cancel"; onClicked: ed.host.editing = null }
        }
    }
    component EdLabel: Label95 { bold: true }

    Menu95 { id: menu }
}
