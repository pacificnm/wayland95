import QtQuick
import Win95Ui

// Trading Journal: the entries (trade ideas, taken trades, notes) with stats, filters and search, and the entry
// editor: plan, thesis, the result from the linked orders, the chart picture taken when it was recorded, the review
// (emotions, did I follow my plan, what happened, the lesson), comments (yours and Claude's), "Open in ticket" and
// "Ask Claude to review". Edits save themselves a moment after you stop typing. Port of views/JournalView.tsx.
Item {
    id: view
    property var app

    property var items: []
    property int selId: -1
    property string chip: "all"
    property string q: ""
    property var emotionList: []

    readonly property var chips: [["all", "All"], ["ideas", "Ideas"], ["open", "Open"], ["closed", "Closed"], ["skipped", "Skipped"], ["notes", "Notes"], ["claude", "By Claude"]]
    function matches(e, c) {
        switch (c) {
        case "ideas": return e.kind === "trade" && e.status === "idea";
        case "open": return e.trade.state === "working" || e.trade.state === "open";
        case "closed": return e.trade.state === "closed";
        case "skipped": return e.status === "skipped";
        case "notes": return e.kind === "note";
        case "claude": return e.source === "claude";
        default: return true;
        }
    }
    // [label, badge tone]
    function badge(e) {
        if (e.kind === "note") return ["Note", "grey"];
        if (e.status === "skipped") return ["Skipped", "grey"];
        switch (e.trade.state) {
        case "open": return ["Open", "blue"];
        case "working": return ["Order working", "blue"];
        case "closed": return ["Closed", "green"];
        case "cancelled": return ["Cancelled", "grey"];
        default: return ["Idea", "amber"];
        }
    }
    function plText(n) { return n === null || n === undefined ? "" : (n >= 0 ? "+" : "-") + core.money(Math.abs(n), 2); }
    function rText(r) { return r === null || r === undefined ? "" : (r >= 0 ? "+" : "") + r.toFixed(2) + "R"; }

    readonly property var shown: {
        const t = q.trim().toLowerCase();
        return items.filter(function (e) {
            return matches(e, chip) && (!t || [e.title, e.symbol, e.body, e.setup, e.review, e.lesson, e.tags.join(" ")].join(" ").toLowerCase().indexOf(t) >= 0);
        });
    }
    readonly property var stats: {
        const closed = items.filter(function (e) { return e.trade.state === "closed"; });
        const wins = closed.filter(function (e) { return (e.trade.realizedPl || 0) > 0; }).length;
        const rs = closed.map(function (e) { return e.trade.rMultiple; }).filter(function (r) { return r !== null && r !== undefined; });
        return { closed: closed.length, winRate: closed.length ? wins / closed.length * 100 : null,
                 net: closed.reduce(function (s, e) { return s + (e.trade.realizedPl || 0); }, 0),
                 avgR: rs.length ? rs.reduce(function (a, b) { return a + b; }, 0) / rs.length : null };
    }
    readonly property var sel: {
        for (let i = 0; i < items.length; ++i) if (items[i].id === selId) return items[i];
        return null;
    }
    onItemsChanged: if (selId < 0 && items.length) selId = items[0].id

    function load() { api.invoke("journal:list", [{ limit: 500 }], function (l) { items = l || []; }); }
    function focusOn(id) { if (id >= 0) { selId = id; chip = "all"; q = ""; searchField.text = ""; } }
    Component.onCompleted: {
        api.invoke("content:get", ["lists.json"], function (l) { if (l) emotionList = l.emotions; });
        load();
        if (testScript === "journal-entry" || testScript === "journal-comments") testEntry();
    }
    // app.openJournal(id) from another screen (or Claude)
    readonly property int focusId: app ? app.journalFocus : -1
    onFocusIdChanged: focusOn(focusId)
    Connections {
        target: api
        function onEvent(channel, payload) { if (channel === "journal:changed") view.load(); }
    }

    function create(kind) {
        api.invoke("journal:create", [{ kind: kind, symbol: kind === "trade" ? app.symbol : null, title: kind === "trade" ? app.symbol + " trade" : "New note", status: "idea" }], function (e) {
            chip = "all";
            q = "";
            searchField.text = "";
            if (e && e.id !== undefined) selId = e.id;
        });
    }

    // the "journal-entry" screenshot: one filled-in entry with a comment
    function testEntry() {
        api.invoke("journal:list", [{ limit: 5 }], function (l) {
            if (l && l.length) return;
            api.invoke("journal:create", [{ kind: "trade", symbol: "AAPL", direction: "long", title: "AAPL pullback to the 50-day", status: "idea",
                                            body: "Price pulled back to the rising 50-day average on falling volume. The idea is wrong below the last swing low.",
                                            setup: "support bounce", tags: ["pullback", "trend"], plan_entry: 187.5, plan_stop: 183, plan_target: 198, plan_qty: 20,
                                            emotions: ["Calm", "Patient"] }], function (e) {
                api.invoke("journal:comment", [e.id, { by: "claude", text: "Good plan: the stop sits **below the swing low**, and the target gives about *2.3 : 1*.\n\n- Check earnings dates first\n- Size it with the Position Calculator" }]);
                selId = e.id;
            });
        });
    }

    // ---------------------------------------------------------------- layout
    DocTitleBar {
        id: titleBar
        width: parent.width
        title: "Trading Journal"
        Button95 { text: "New entry"; onClicked: view.create("trade") }
        Button95 { text: "New note"; onClicked: view.create("note") }
    }

    Item {
        id: listPane
        y: titleBar.height + 4
        width: 270
        height: parent.height - y
        Column {
            id: listTop
            width: parent.width
            spacing: 4
            Grid {
                columns: 2
                columnSpacing: 2
                rowSpacing: 2
                Repeater {
                    model: [
                        ["Closed trades", String(view.stats.closed), Theme.text],
                        ["Win rate", view.stats.winRate !== null ? view.stats.winRate.toFixed(0) + "%" : "—", Theme.text],
                        ["Net P/L", view.stats.closed ? view.plText(view.stats.net) : "—", view.stats.closed ? (view.stats.net >= 0 ? "#007000" : "#b00000") : Theme.text],
                        ["Avg R", view.stats.avgR !== null ? view.rText(view.stats.avgR) : "—", Theme.text]
                    ]
                    Bevel {
                        required property var modelData
                        style: "thinSunken"
                        width: (listTop.width - 2) / 2
                        height: 34
                        Label95 { x: 4; y: 3; width: parent.width - 8; small: true; dim: true; elide: Text.ElideRight; text: parent.modelData[0] }
                        Label95 { x: 4; y: 17; width: parent.width - 8; bold: true; elide: Text.ElideRight; text: parent.modelData[1]; color: parent.modelData[2] }
                    }
                }
            }
            DocField { id: searchField; width: parent.width; placeholder: "Search journal…"; onTextChanged: view.q = text }
            Flow {
                width: parent.width
                spacing: 3
                Repeater {
                    model: view.chips
                    DocChip {
                        required property var modelData
                        text: modelData[1]
                        checked: view.chip === modelData[0]
                        onClicked: view.chip = modelData[0]
                    }
                }
            }
        }
        DocRows {
            y: listTop.height + 6
            width: parent.width
            height: parent.height - y
            model: view.shown
            emptyText: view.shown.length ? "" : view.items.length === 0
                       ? "Nothing here yet. Record why you take a trade with the “Record in journal” box on the order ticket, ask Claude for trade ideas, or add an entry yourself."
                       : "No entries match."
            delegate: Item {
                id: row
                required property var modelData
                readonly property bool selected: modelData.id === view.selId
                readonly property var b: view.badge(modelData)
                readonly property color fg: selected ? Theme.selectionText : Theme.text
                width: ListView.view.width
                height: 52
                Rectangle { anchors.fill: parent; color: row.selected ? Theme.selection : "transparent" }
                Rectangle { y: parent.height - 1; width: parent.width; height: 1; color: Theme.face }
                Label95 {
                    x: 6
                    y: 3
                    textFormat: Text.StyledText
                    text: "<b>" + Util.plain(row.modelData.symbol || "—") + "</b>" + (row.modelData.direction ? " " + row.modelData.direction : "")
                    color: row.fg
                }
                Row {
                    anchors.right: parent.right
                    anchors.rightMargin: 6
                    y: 3
                    spacing: 4
                    Label95 { visible: row.modelData.source === "claude"; text: "✦"; color: row.selected ? Theme.selectionText : "#5b2c9a" }
                    DocBadge { text: row.b[0]; tone: row.b[1] }
                }
                Label95 { x: 6; y: 19; width: parent.width - 12; elide: Text.ElideRight; text: row.modelData.title; color: row.fg }
                Label95 { x: 6; y: 35; small: true; text: Util.dateTime(row.modelData.created_at); color: row.selected ? Theme.selectionText : Theme.shadow }
                Label95 {
                    anchors.right: parent.right
                    anchors.rightMargin: 6
                    y: 35
                    small: true
                    visible: row.modelData.trade.realizedPl !== null && row.modelData.trade.realizedPl !== undefined
                    text: visible ? view.plText(row.modelData.trade.realizedPl) + " " + view.rText(row.modelData.trade.rMultiple) : ""
                    color: row.selected ? Theme.selectionText : row.modelData.trade.realizedPl >= 0 ? "#007000" : "#b00000"
                }
                MouseArea { anchors.fill: parent; onClicked: view.selId = row.modelData.id }
            }
        }
    }

    Item {
        id: pane
        x: listPane.width + 6
        y: listPane.y
        width: parent.width - x
        height: listPane.height
        Label95 { visible: !view.sel; x: 10; y: 10; dim: true; text: "Select an entry, or create one." }
        Loader {
            id: detailLoader
            anchors.fill: parent
            active: false
            sourceComponent: Detail { host: view; popup: menu; item: view.sel }
        }
    }
    // a fresh editor for each entry (upstream keys it by id)
    readonly property int shownId: sel ? sel.id : -1
    onShownIdChanged: { detailLoader.active = false; detailLoader.active = shownId >= 0; }

    // ---------------------------------------------------------------- one entry
    component Detail: DocScroll {
        id: d
        property var host
        property Menu95 popup
        property var item
        property bool loading: true      // until the fields hold the entry (text boxes report changes while they are built)
        property bool dirty: false
        property bool saved: false
        property string kind: "trade"
        property var emotions: []
        property string followed: ""
        property string image: ""
        property bool zoom: false
        padding: 12
        readonly property bool isTrade: kind === "trade"
        readonly property var t: item ? item.trade : ({})
        readonly property var b: item ? host.badge(item) : ["", "grey"]
        readonly property real half: (availableWidth - 10) / 2

        function n(s) { const v = parseFloat(s); return isFinite(v) ? v : null; }
        function populate() {
            if (!item) return;
            loading = true;
            titleField.text = item.title;
            kind = item.kind;
            kindBox.currentIndex = item.kind === "note" ? 1 : 0;
            symbolField.text = item.symbol || "";
            dirBox.currentIndex = item.direction === "long" ? 1 : item.direction === "short" ? 2 : 0;
            setupField.text = item.setup;
            tagsField.text = item.tags.join(", ");
            entryField.text = item.plan_entry !== null ? String(item.plan_entry) : "";
            stopField.text = item.plan_stop !== null ? String(item.plan_stop) : "";
            targetField.text = item.plan_target !== null ? String(item.plan_target) : "";
            qtyField.text = item.plan_qty !== null ? String(item.plan_qty) : "";
            bodyArea.text = item.body;
            emotions = item.emotions.slice();
            followed = item.followed_plan || "";
            reviewArea.text = item.review;
            lessonArea.text = item.lesson;
            // text boxes report a change again once they are laid out: ignore those
            Qt.callLater(function () { d.loading = false; });
        }
        function edited() { if (loading) return; dirty = true; saved = false; saveTimer.restart(); }
        Timer {
            id: saveTimer
            interval: 700
            onTriggered: {
                const id = d.item.id;
                api.invoke("journal:update", [id, {
                    title: titleField.text, symbol: symbolField.text || null, direction: ["", "long", "short"][dirBox.currentIndex] || null, kind: d.kind,
                    body: bodyArea.text, setup: setupField.text, tags: tagsField.text.split(",").map(function (s) { return s.trim(); }).filter(Boolean),
                    plan_entry: d.n(entryField.text), plan_stop: d.n(stopField.text), plan_target: d.n(targetField.text), plan_qty: d.n(qtyField.text),
                    emotions: d.emotions, followed_plan: d.followed || null, review: reviewArea.text, lesson: lessonArea.text
                }], function () { d.dirty = false; d.saved = true; });
            }
        }
        Component.onCompleted: {
            populate();
            if (item && item.has_image) api.invoke("journal:image", [item.id], function (img) {
                if (img) d.image = String(img).indexOf("data:") === 0 ? String(img) : "data:image/png;base64," + img;
            });
        }
        // the "journal-comments" screenshot: the bottom of the entry
        Timer { interval: 1500; running: testScript === "journal-comments"; onTriggered: d.scrollTo(1e6) }
        // edits made elsewhere (e.g. by Claude)
        readonly property var updatedAt: item ? item.updated_at : 0
        onUpdatedAtChanged: if (!dirty && !saveTimer.running) populate()

        readonly property var risk: n(entryField.text) !== null && n(stopField.text) !== null && n(qtyField.text) ? Math.abs(n(entryField.text) - n(stopField.text)) * n(qtyField.text) : null
        readonly property var reward: n(entryField.text) !== null && n(targetField.text) !== null && n(qtyField.text) ? Math.abs(n(targetField.text) - n(entryField.text)) * n(qtyField.text) : null

        function setStatus(s) { api.invoke("journal:update", [item.id, { status: s }]); }
        function del() {
            const e = item;
            host.app.confirm("Delete “" + e.title + "”? This cannot be undone.", function () {
                api.invoke("journal:delete", [e.id], function () { host.app.toast("info", "Journal entry deleted"); host.selId = -1; });
            });
        }
        function addComment() {
            const text = commentField.text.trim();
            if (!text) return;
            api.invoke("journal:comment", [item.id, { by: "user", text: text }], function () { commentField.text = ""; });
        }
        function review() {
            host.app.askClaude("Please review my journal entry #" + item.id + " (\"" + item.title + "\"). Read it with journal_get_entry, then give me honest feedback on my reasoning, risk management and what I can learn from it. Save your feedback as a comment on the entry with journal_add_comment.");
        }

        // the head: title, badges, buttons
        DocField {
            id: titleField
            width: d.availableWidth
            height: 26
            placeholder: "Title"
            input.font.pixelSize: Theme.fontSize + 4
            input.font.bold: true
            onTextChanged: d.edited()
        }
        Flow {
            width: d.availableWidth
            spacing: 4
            DocBadge { text: d.b[0]; tone: d.b[1] }
            DocBadge { visible: !!d.item && d.item.source === "claude"; text: "✦ Claude"; tone: "purple" }
            Label95 { height: 22; verticalAlignment: Text.AlignVCenter; dim: true; text: d.saved ? "Saved ✓" : d.dirty ? "Saving…" : "" }
            Button95 {
                visible: d.isTrade && symbolField.text !== "" && !!d.item && d.item.status !== "taken" && d.t.state === "none"
                text: "Open in ticket"
                onClicked: d.host.app.ticketFromEntry(d.item)
                ToolTipArea { text: "Open an order ticket pre-filled from this plan" }
            }
            Button95 { text: "Ask Claude to review"; onClicked: d.review() }
            Button95 {
                visible: d.isTrade && d.t.state === "none"
                text: d.item && d.item.status === "skipped" ? "Reopen" : "Skip"
                onClicked: d.setStatus(d.item.status === "skipped" ? "idea" : "skipped")
            }
            Button95 { text: "Delete"; onClicked: d.del() }
        }
        Label95 { dim: true; text: d.item ? Util.dateTime(d.item.created_at) + (d.item.source === "claude" ? " · suggested by Claude" : "") : "" }

        Row {
            spacing: 10
            Column {
                width: d.half
                spacing: 2
                Label95 { text: "Type" }
                ComboBox95 {
                    id: kindBox
                    width: parent.width
                    popupMenu: d.popup
                    model: ["Trade", "Note / lesson"]
                    onActivated: (i) => { d.kind = i === 1 ? "note" : "trade"; d.edited(); }
                }
            }
            Column {
                width: d.half
                spacing: 2
                Label95 { text: "Setup" }
                DocField { id: setupField; width: parent.width; placeholder: "e.g. support bounce, breakout"; onTextChanged: d.edited() }
            }
        }
        Row {
            visible: d.isTrade
            spacing: 10
            Column {
                width: d.half
                spacing: 2
                Row {
                    spacing: 8
                    Label95 { text: "Symbol" }
                    Label95 {
                        visible: symbolField.text !== ""
                        text: "Open chart"
                        color: "#0000c0"
                        font.underline: true
                        MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: { d.host.app.setSymbol(symbolField.text); d.host.app.setView("chart"); } }
                    }
                }
                DocField {
                    id: symbolField
                    width: parent.width
                    placeholder: "AAPL"
                    onTextChanged: { if (text !== text.toUpperCase()) { text = text.toUpperCase(); return; } d.edited(); }
                }
            }
            Column {
                width: d.half
                spacing: 2
                Label95 { text: "Direction" }
                ComboBox95 { id: dirBox; width: parent.width; popupMenu: d.popup; model: ["—", "Long", "Short"]; onActivated: d.edited() }
            }
        }
        Column {
            width: d.availableWidth
            spacing: 2
            Label95 { text: "Tags" }
            DocField { id: tagsField; width: parent.width; placeholder: "comma separated"; onTextChanged: d.edited() }
        }

        // the result from the linked orders
        GroupBox95 {
            visible: d.t.state !== "none"
            width: d.availableWidth
            height: 82
            title: "Result"
            Grid {
                columns: 6
                columnSpacing: 14
                rowSpacing: 2
                flow: Grid.TopToBottom
                rows: 2
                Repeater {
                    model: [
                        ["Status", d.b[0], Theme.text],
                        ["Shares", d.t.qty !== null && d.t.qty !== undefined ? String(d.t.qty) : "—", Theme.text],
                        ["Avg entry", d.t.entryPrice !== null && d.t.entryPrice !== undefined ? d.t.entryPrice.toFixed(2) : "—", Theme.text],
                        ["Avg exit", d.t.exitPrice !== null && d.t.exitPrice !== undefined ? d.t.exitPrice.toFixed(2) : "—", Theme.text],
                        ["Realized P/L", d.t.realizedPl !== null && d.t.realizedPl !== undefined ? d.host.plText(d.t.realizedPl) : "—", d.t.realizedPl === null || d.t.realizedPl === undefined ? Theme.text : d.t.realizedPl >= 0 ? "#007000" : "#b00000"],
                        ["R multiple", d.t.rMultiple !== null && d.t.rMultiple !== undefined ? d.host.rText(d.t.rMultiple) : "—", d.t.rMultiple === null || d.t.rMultiple === undefined ? Theme.text : d.t.rMultiple >= 0 ? "#007000" : "#b00000"]
                    ]
                    Column {
                        required property var modelData
                        Label95 { dim: true; small: true; text: parent.modelData[0] }
                        Label95 { bold: true; text: parent.modelData[1]; color: parent.modelData[2] }
                    }
                }
            }
            Label95 {
                visible: d.t.state === "open"
                y: 36
                dim: true
                text: "Still open. The result is realized P/L so far; it updates as fills come in."
            }
        }

        // the plan
        GroupBox95 {
            visible: d.isTrade
            width: d.availableWidth
            height: 84
            title: "Plan"
            Row {
                spacing: 8
                Repeater {
                    model: ["Entry", "Stop", "Target", "Shares"]
                    Label95 { required property string modelData; width: (d.availableWidth - 20 - 24) / 4; text: modelData }
                }
            }
            Row {
                y: 16
                spacing: 8
                DocField { id: entryField; width: (d.availableWidth - 44) / 4; onTextChanged: d.edited() }
                DocField { id: stopField; width: (d.availableWidth - 44) / 4; onTextChanged: d.edited() }
                DocField { id: targetField; width: (d.availableWidth - 44) / 4; onTextChanged: d.edited() }
                DocField { id: qtyField; width: (d.availableWidth - 44) / 4; onTextChanged: d.edited() }
            }
            Text {
                y: 42
                visible: d.risk !== null || d.reward !== null
                textFormat: Text.StyledText
                color: Theme.shadow
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontSize
                text: (d.risk !== null ? "Planned risk <b><font color=\"#b00000\">" + core.money(d.risk, 2) + "</font></b>" : "")
                      + (d.reward !== null ? " · reward <b><font color=\"#007000\">" + core.money(d.reward, 2) + "</font></b>" : "")
                      + (d.risk && d.reward ? " · reward:risk <b>" + (d.reward / d.risk).toFixed(2) + " : 1</b>" : "")
            }
        }

        Heading95 { level: 3; width: d.availableWidth; text: d.isTrade ? "Why this trade? (thesis, and what would prove it wrong)" : "Notes" }
        TextArea95 {
            id: bodyArea
            width: d.availableWidth
            height: 100
            placeholder: d.isTrade ? "What did you see? Why now? Where is the idea wrong?" : "Write anything worth remembering…"
            onTextChanged: d.edited()
        }

        Heading95 { level: 3; visible: !!d.item && d.item.has_image; text: "Chart when this was recorded" }
        Label95 { visible: !!d.item && d.item.has_image && d.image === ""; dim: true; text: "Loading…" }
        Bevel {
            visible: d.image !== ""
            style: "sunken"
            width: Math.min(d.availableWidth, (d.zoom ? d.availableWidth : 360))
            height: shot.implicitWidth > 0 ? (width - 4) * shot.implicitHeight / shot.implicitWidth + 4 : 0
            Image {
                id: shot
                x: 2
                y: 2
                width: parent.width - 4
                height: parent.height - 4
                source: d.image
                fillMode: Image.PreserveAspectFit
                asynchronous: true
                MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: d.zoom = !d.zoom }
                ToolTipArea { text: "Click to enlarge" }
            }
        }

        Heading95 { level: 3; text: "Review — what happened and what I learned" }
        Flow {
            width: d.availableWidth
            spacing: 3
            Label95 { height: 20; verticalAlignment: Text.AlignVCenter; dim: true; text: "How I felt:" }
            Repeater {
                model: d.host.emotionList
                DocChip {
                    required property string modelData
                    text: modelData
                    checked: d.emotions.indexOf(modelData) >= 0
                    onClicked: {
                        d.emotions = checked ? d.emotions.filter(function (x) { return x !== modelData; }) : d.emotions.concat([modelData]);
                        d.edited();
                    }
                }
            }
        }
        Flow {
            visible: d.isTrade
            width: d.availableWidth
            spacing: 3
            Label95 { height: 20; verticalAlignment: Text.AlignVCenter; dim: true; text: "Did I follow my plan?" }
            Repeater {
                model: [["yes", "Yes"], ["partly", "Partly"], ["no", "No"]]
                DocChip {
                    required property var modelData
                    text: modelData[1]
                    checked: d.followed === modelData[0]
                    onClicked: { d.followed = d.followed === modelData[0] ? "" : modelData[0]; d.edited(); }
                }
            }
        }
        TextArea95 { id: reviewArea; width: d.availableWidth; height: 70; placeholder: "What actually happened? Did price do what you expected?"; onTextChanged: d.edited() }
        TextArea95 { id: lessonArea; width: d.availableWidth; height: 54; placeholder: "Lesson: what will you do differently next time?"; onTextChanged: d.edited() }

        Heading95 { level: 3; text: "Comments" }
        Label95 {
            visible: !!d.item && d.item.comments.length === 0
            width: d.availableWidth
            wrapMode: Text.Wrap
            dim: true
            text: "No comments yet. Ask Claude to review this entry, or add your own."
        }
        Repeater {
            model: d.item ? d.item.comments : []
            Rectangle {
                id: comment
                required property var modelData
                width: d.availableWidth
                height: commentCol.implicitHeight + 10
                color: modelData.by === "claude" ? "#f3eefc" : "#f4f4f4"
                border.color: Theme.face
                Column {
                    id: commentCol
                    x: 8
                    y: 5
                    width: parent.width - 16
                    spacing: 2
                    Text {
                        textFormat: Text.StyledText
                        text: (comment.modelData.by === "claude" ? "<b>✦ Claude</b>" : "<b>You</b>") + " <font color=\"#808080\">· " + Util.dateTime(comment.modelData.at) + "</font>"
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.fontSize
                        color: Theme.text
                    }
                    DocMarkdown {
                        width: parent.width
                        text: comment.modelData.text
                        onLinkActivated: (link) => api.invoke("app:openExternal", [link])
                    }
                }
            }
        }
        Row {
            spacing: 4
            DocField { id: commentField; width: d.availableWidth - 79; placeholder: "Add a comment…"; onAccepted: d.addComment() }
            Button95 { text: "Add"; enabled: commentField.text.trim() !== ""; onClicked: d.addComment() }
        }
    }

    Menu95 { id: menu }
}
