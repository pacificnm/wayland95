import QtQuick
import Win95Ui

// The Claude panel on the right: a conversation with Claude about what the user has open, with the model and effort
// pickers, streamed answers (Markdown), the thought process, tool calls as they run, Stop, new chat and chat history.
// Port of components/ChatPanel.tsx.
//
// The agent loop runs in C++ (chat:send); it streams `chat:event`s back ({requestId, type: text | thinking |
// tool_start | tool_end | done | error}). Tools run in ToolRunner. The pickers are session-only: the default comes
// from the "chatDefaults" setting (Settings screen), normalised as shared/chat.ts normalizePrefs does.
Item {
    id: panel
    property var app

    // ------------------------------------------------------------------ state
    // messages: [{role: "user"|"assistant", parts: [{type:"text"|"thinking", text} | {type:"tool", id, name, input, status, result?}],
    //             meta?, error?, authError?, streaming?}]
    property var messages: []
    property string chatId: core.uuid()
    property string requestId: ""
    property bool busy: false
    property var prefs: ({ model: "claude-sonnet-5-5", effort: "medium" })
    property var history: null          // the open history list, or null
    property var keyStatus: null        // {source: stored | env | none}
    property bool stick: true           // keep the newest text in view
    property var expanded: ({})         // "message:part" -> true for opened thinking / tool details

    // content/chat.json and the tool labels
    property var chatInfo: ({ models: [], efforts: [], defaultPrefs: { model: "claude-sonnet-5-5", effort: "medium" } })
    property var toolLabels: ({})
    function modelInfo(id) {
        const ms = chatInfo.models;
        return ms.find(function (m) { return m.id === id; }) || ms[0] || { id: id, label: id, reasoning: true, defaultEffort: "medium" };
    }
    // Reads a stored default, falling back per field so an old or hand-edited value can never select a model that is gone.
    function normalizePrefs(p) {
        const o = p && typeof p === "object" ? p : {};
        const dflt = chatInfo.defaultPrefs;
        const model = chatInfo.models.some(function (m) { return m.id === o.model; }) ? o.model : dflt.model;
        const effort = chatInfo.efforts.some(function (e) { return e.id === o.effort; }) ? o.effort : model === dflt.model ? dflt.effort : modelInfo(model).defaultEffort;
        return { model: model, effort: effort };
    }
    // Util.markdown, with _emphasis_ written as *emphasis* (Qt's importer underlines the underscore form)
    function markdown(text) { return Util.markdown(String(text ?? "").replace(/(^|[^\w_])_([^_\n]+)_(?![\w_])/g, "$1*$2*")); }
    function toolLabel(name) { return toolLabels[name] || name; }
    function toolArg(input) {
        if (!input || typeof input !== "object") return "";
        const o = input;
        return [o.symbol, o.study, o.label, o.price !== undefined ? "@ " + o.price : null, o.range, o.interval, o.count !== undefined ? o.count + " bars" : null]
            .filter(function (x) { return x !== undefined && x !== null; }).join(" ");
    }

    function loadDefaults() {
        // the default comes from Settings; the pickers below override it for this session only
        api.invoke("settings:get", ["chatDefaults"], function (p) { panel.prefs = panel.normalizePrefs(p); });
    }
    function loadKeyStatus() { api.invoke("chat:key:status", [], function (k) { panel.keyStatus = k; }); }
    Component.onCompleted: {
        api.invoke("content:get", ["chat.json"], function (c) {
            if (c && c.models) panel.chatInfo = c;
            panel.loadDefaults();
        });
        api.invoke("content:get", ["tools.json"], function (t) {
            const m = {};
            (t || []).forEach(function (x) { m[x.name] = x.label; });
            panel.toolLabels = m;
        });
        loadKeyStatus();
        if (typeof testScript !== "undefined" && testScript === "chat") testTimer.start();
    }
    // Settings saved new defaults (upstream's chat-defaults-changed window event): they replace the session's choice
    Connections {
        target: panel.app
        ignoreUnknownSignals: true
        function onChatDefaultsChanged(prefs) { panel.prefs = panel.normalizePrefs(prefs); }
    }
    // the key may have been added in Settings: read its status again when the user comes back from there
    readonly property string appView: app ? app.view : ""
    onAppViewChanged: if (appView !== "" && appView !== "settings") loadKeyStatus();
    // another part of the app (e.g. the journal's "Ask Claude to review") can send a message for the user
    readonly property var chatPrompt: app ? app.chatPrompt : null
    onChatPromptChanged: {
        if (!chatPrompt) return;
        app.chatVisible = true;
        if (busy) app.toast("warning", "Claude is still answering. Try again in a moment.");
        else send(chatPrompt.text);
    }

    // ------------------------------------------------------------------ the conversation
    function patchLast(fn) {
        const ms = messages.slice();
        if (!ms.length) return;
        ms[ms.length - 1] = fn(Object.assign({}, ms[ms.length - 1]));
        messages = ms;
    }
    // Appends streamed text to the trailing part of the same kind, or starts a new one.
    function addStreamed(parts, type, text) {
        const last = parts[parts.length - 1];
        if (last && last.type === type) return parts.slice(0, -1).concat([{ type: type, text: last.text + text }]);
        return parts.concat([{ type: type, text: text }]);
    }
    function handleEvent(e) {
        if (!e || e.requestId !== requestId) return;
        if (e.type === "text") patchLast(function (m) { m.parts = addStreamed(m.parts, "text", e.text); return m; });
        else if (e.type === "thinking") patchLast(function (m) { m.parts = addStreamed(m.parts, "thinking", e.text); return m; });
        else if (e.type === "tool_start") patchLast(function (m) { m.parts = m.parts.concat([{ type: "tool", id: e.id, name: e.name, input: e.input, status: "running" }]); return m; });
        else if (e.type === "tool_end") patchLast(function (m) {
            m.parts = m.parts.map(function (p) { return p.type === "tool" && p.id === e.id ? Object.assign({}, p, { status: e.ok ? "ok" : "error", result: e.result }) : p; });
            return m;
        });
        else if (e.type === "error") {
            patchLast(function (m) { m.streaming = false; m.error = e.message; m.authError = e.code === "auth"; return m; });
            busy = false;
            loadKeyStatus();
        } else if (e.type === "done") {
            const note = e.stopReason === "refusal" ? " · declined" : e.stopReason === "max_tokens" ? " · cut off (length limit)" : e.stopReason === "stopped" ? " · stopped" : "";
            const served = (chatInfo.models.find(function (m) { return m.id === e.model; }) || { label: e.model }).label;
            patchLast(function (m) {
                m.streaming = false;
                m.parts = m.parts.map(function (p) { return p.type === "tool" && p.status === "running" ? Object.assign({}, p, { status: "error", result: "Stopped" }) : p; });
                m.meta = served + (e.outputTokens ? " · " + core.grouped(e.outputTokens, 0) + " tokens" : "") + note;
                return m;
            });
            busy = false;
        }
    }
    Connections {
        target: api
        function onEvent(channel, payload) { if (channel === "chat:event") panel.handleEvent(payload); }
    }

    function send(text) {
        text = String(text === undefined ? inputEdit.text : text).trim();
        if (!text || busy) return;
        inputEdit.text = "";
        stick = true;
        const id = core.uuid();
        requestId = id;
        busy = true;
        messages = messages.concat([{ role: "user", parts: [{ type: "text", text: text }] }, { role: "assistant", parts: [], streaming: true }]);
        api.invoke("chat:send", [{ requestId: id, chatId: chatId, model: prefs.model, effort: prefs.effort, text: text, context: app ? app.chatContext : null }]);
    }
    function stop() { if (requestId) api.invoke("chat:stop", [requestId]); }
    function newChat() {
        if (busy) stop();
        requestId = "";
        busy = false;
        chatId = core.uuid();
        messages = [];
        expanded = {};
        history = null;
    }
    function openHistory() {
        if (history) { history = null; return; }
        api.invoke("chat:list", [], function (l) { panel.history = l || []; });
    }
    function loadChat(c) {
        if (busy) stop();
        requestId = "";
        busy = false;
        api.invoke("chat:get", [c.id], function (rows) {
            panel.chatId = c.id;
            panel.expanded = {};
            panel.messages = (rows || []).map(function (r) { return { role: r.role, parts: [{ type: "text", text: r.content }] }; });
            panel.stick = true;
            panel.history = null;
        });
    }
    function deleteChat(c) {
        api.invoke("chat:delete", [c.id], function () {
            if (panel.history) panel.history = panel.history.filter(function (h) { return h.id !== c.id; });
            if (c.id === panel.chatId) {
                const keep = panel.history;
                panel.newChat();
                panel.history = keep;
            }
        });
    }
    function toggle(key) {
        const e = Object.assign({}, expanded);
        if (e[key]) delete e[key]; else e[key] = true;
        expanded = e;
    }

    readonly property var info: modelInfo(prefs.model)
    readonly property var suggestions: [
        "Walk me through the " + (app ? app.symbol : "") + " chart setup",
        "What do my active studies say right now?",
        "Explain RSI and how to read it",
        "Help me size a paper trade with proper risk"
    ]

    // ------------------------------------------------------------------ layout: title bar
    Bevel {
        id: titleBar
        width: parent.width
        height: 26
        style: "raised"
        Text {
            id: sparkle
            x: 6
            anchors.verticalCenter: parent.verticalCenter
            text: "✦"
            color: "#c15f3c"
            font.pixelSize: 14
        }
        Text {
            x: sparkle.x + sparkle.width + 5
            anchors.verticalCenter: parent.verticalCenter
            text: "Claude"
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontSize
            font.bold: true
            color: Theme.text
        }
        Row {
            anchors.right: parent.right
            anchors.rightMargin: 3
            anchors.verticalCenter: parent.verticalCenter
            spacing: 2
            ChatIconButton { icon: "new"; tip: "New chat"; onClicked: panel.newChat() }
            ChatIconButton { id: historyButton; icon: "clock"; tip: "Chat history"; checked: panel.history !== null; onClicked: panel.openHistory() }
        }
    }

    // ------------------------------------------------------------------ the conversation well
    Bevel {
        id: well
        y: titleBar.height + 3
        width: parent.width
        height: inputBox.y - y - 3
        style: "sunken"
        face: Theme.window

        Flickable {
            id: flick
            x: 2
            y: 2
            width: parent.width - 4 - (bar.visible ? bar.width : 0)
            height: parent.height - 4
            clip: true
            interactive: false
            boundsBehavior: Flickable.StopAtBounds
            contentWidth: width
            contentHeight: column.implicitHeight + 16
            function toEnd() { contentY = Math.max(0, contentHeight - height); }
            onContentHeightChanged: if (panel.stick) Qt.callLater(toEnd)
            onHeightChanged: if (panel.stick) Qt.callLater(toEnd)

            Column {
                id: column
                x: 8
                y: 8
                width: flick.width - 16
                spacing: 12
                Repeater {
                    model: panel.messages.length
                    delegate: ChatMessage {
                        required property int index
                        width: column.width
                        msgIndex: index
                        msg: panel.messages[index]
                    }
                }
            }
            WheelHandler {
                onWheel: (event) => {
                    flick.contentY = Math.max(0, Math.min(flick.contentHeight - flick.height, flick.contentY - event.angleDelta.y / 120 * 54));
                    panel.stick = flick.contentHeight - flick.contentY - flick.height < 40;
                }
            }
        }
        ScrollBar95 {
            id: bar
            anchors.right: parent.right
            anchors.rightMargin: 2
            y: 2
            height: parent.height - 4
            flickable: flick
            stepSize: 18
            visible: needed
        }

        // nothing said yet: what Claude can do, the missing key, some questions to start with
        Column {
            visible: panel.messages.length === 0
            anchors.centerIn: parent
            width: Math.min(parent.width - 24, 300)
            spacing: 8
            Text {
                anchors.horizontalCenter: parent.horizontalCenter
                text: "✦"
                color: "#c15f3c"
                font.pixelSize: 30
            }
            Heading95 {
                width: parent.width
                horizontalAlignment: Text.AlignHCenter
                level: 3
                text: "Ask Claude"
            }
            Label95 {
                width: parent.width
                horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.Wrap
                dim: true
                text: "About charts, indicators, trade ideas and risk. Claude sees what you have open."
            }
            Notice {
                width: parent.width
                kind: "warning"
                text: panel.keyStatus && panel.keyStatus.source === "none" ? "Add your Anthropic API key to start chatting." : ""
                buttons: [{ text: "Settings", action: function () { panel.app.setView("settings"); } }]
            }
            Repeater {
                model: panel.suggestions
                delegate: Item {
                    id: chip
                    required property string modelData
                    width: parent.width
                    height: chipText.implicitHeight + 10
                    Bevel { anchors.fill: parent; style: chipMouse.pressed ? "pressed" : "raised" }
                    Text {
                        id: chipText
                        x: 8
                        y: 5 + (chipMouse.pressed ? 1 : 0)
                        width: parent.width - 16
                        wrapMode: Text.Wrap
                        text: chip.modelData
                        textFormat: Text.PlainText
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.fontSize
                        color: Theme.text
                    }
                    MouseArea { id: chipMouse; anchors.fill: parent; onClicked: panel.send(chip.modelData) }
                }
            }
        }
    }

    // ------------------------------------------------------------------ the question box and the pickers
    Item {
        id: inputBox
        anchors.bottom: parent.bottom
        width: parent.width
        height: inputWell.height + 4 + toolsRow.height

        Bevel {
            id: inputWell
            width: parent.width
            height: Math.min(110, Math.max(44, inputEdit.implicitHeight + 4))
            style: "sunken"
            face: Theme.window
            Flickable {
                id: inputFlick
                x: 2
                y: 2
                width: parent.width - 4
                height: parent.height - 4
                clip: true
                interactive: false
                contentWidth: width
                contentHeight: inputEdit.implicitHeight
                function ensureVisible(r) {
                    if (contentY >= r.y) contentY = r.y;
                    else if (contentY + height <= r.y + r.height) contentY = r.y + r.height - height;
                }
                TextEdit {
                    id: inputEdit
                    width: inputFlick.width
                    padding: 3
                    focus: true
                    selectByMouse: true
                    wrapMode: TextEdit.Wrap
                    textFormat: TextEdit.PlainText
                    color: Theme.text
                    selectionColor: Theme.selection
                    selectedTextColor: Theme.selectionText
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontSize + 1
                    onCursorRectangleChanged: if (activeFocus) inputFlick.ensureVisible(cursorRectangle)
                    Keys.onPressed: (event) => {
                        // Enter sends, Shift+Enter starts a new line
                        if ((event.key === Qt.Key_Return || event.key === Qt.Key_Enter) && !(event.modifiers & Qt.ShiftModifier)) {
                            panel.send();
                            event.accepted = true;
                        }
                    }
                    Text {
                        visible: inputEdit.text === ""
                        x: inputEdit.leftPadding
                        y: inputEdit.topPadding
                        width: inputEdit.width - 6
                        elide: Text.ElideRight
                        text: "Ask Claude…  (Enter to send, Shift+Enter for a new line)"
                        color: Theme.shadow
                        font: inputEdit.font
                    }
                }
            }
        }

        Item {
            id: toolsRow
            y: inputWell.height + 4
            width: parent.width
            height: 23
            ComboBox95 {
                id: modelPicker
                width: Math.min(150, (parent.width - sendButton.width - 8) * 0.55)
                height: 23
                popupMenu: pickerMenu
                model: panel.chatInfo.models.map(function (m) { return m.label; })
                currentIndex: panel.chatInfo.models.findIndex(function (m) { return m.id === panel.prefs.model; })
                onActivated: (i) => {
                    const m = panel.chatInfo.models[i];
                    panel.prefs = { model: m.id, effort: m.defaultEffort };
                }
                ToolTipArea { anchors.fill: parent; text: "Model: " + (panel.info.desc || "") }
            }
            ComboBox95 {
                id: effortPicker
                x: modelPicker.width + 4
                width: parent.width - x - sendButton.width - 4
                height: 23
                enabled: panel.info.reasoning !== false
                opacity: enabled ? 1 : 0.5
                popupMenu: pickerMenu
                model: panel.chatInfo.efforts.map(function (e) { return "Effort: " + e.label; })
                currentIndex: panel.chatInfo.efforts.findIndex(function (e) { return e.id === panel.prefs.effort; })
                onActivated: (i) => { panel.prefs = { model: panel.prefs.model, effort: panel.chatInfo.efforts[i].id }; }
                ToolTipArea {
                    anchors.fill: parent
                    text: {
                        const e = panel.chatInfo.efforts.find(function (x) { return x.id === panel.prefs.effort; });
                        return "Effort: how much Claude thinks before answering" + (e ? " (" + e.label + ": " + e.desc + ")" : "");
                    }
                }
            }
            Button95 {
                id: sendButton
                anchors.right: parent.right
                width: 64
                height: 23
                isDefault: !panel.busy
                text: panel.busy ? "Stop" : "Send"
                enabled: panel.busy || inputEdit.text.trim() !== ""
                onClicked: panel.busy ? panel.stop() : panel.send()
            }
        }
    }

    // ------------------------------------------------------------------ chat history drop-down
    MouseArea {
        anchors.fill: parent
        visible: panel.history !== null
        onPressed: panel.history = null
    }
    Bevel {
        id: historyBox
        visible: panel.history !== null
        anchors.right: parent.right
        anchors.rightMargin: 2
        y: titleBar.height - 1
        width: Math.min(parent.width - 8, 300)
        height: Math.min(histColumn.implicitHeight + 6, panel.height * 0.6)
        style: "raised"
        Flickable {
            id: histFlick
            x: 3
            y: 3
            width: parent.width - 6
            height: parent.height - 6
            clip: true
            interactive: false
            contentHeight: histColumn.implicitHeight
            WheelHandler {
                onWheel: (event) => { histFlick.contentY = Math.max(0, Math.min(histFlick.contentHeight - histFlick.height, histFlick.contentY - event.angleDelta.y / 120 * 40)); }
            }
            Column {
                id: histColumn
                width: histFlick.width
                Label95 {
                    visible: panel.history !== null && panel.history.length === 0
                    padding: 6
                    dim: true
                    text: "No saved chats yet"
                }
                Repeater {
                    model: panel.history || []
                    delegate: Rectangle {
                        id: histRow
                        required property var modelData
                        readonly property bool current: modelData.id === panel.chatId
                        width: histColumn.width
                        height: 22
                        color: rowMouse.containsMouse ? Theme.selection : "transparent"
                        Text {
                            x: 6
                            width: parent.width - 34
                            anchors.verticalCenter: parent.verticalCenter
                            elide: Text.ElideRight
                            textFormat: Text.PlainText
                            text: histRow.modelData.title
                            font.family: Theme.fontFamily
                            font.pixelSize: Theme.fontSize
                            font.bold: histRow.current
                            color: rowMouse.containsMouse ? Theme.selectionText : Theme.text
                        }
                        MouseArea {
                            id: rowMouse
                            anchors.fill: parent
                            hoverEnabled: true
                            onClicked: panel.loadChat(histRow.modelData)
                        }
                        ChatIconButton {
                            anchors.right: parent.right
                            anchors.rightMargin: 2
                            anchors.verticalCenter: parent.verticalCenter
                            icon: "delete"
                            tip: "Delete"
                            onClicked: panel.deleteChat(histRow.modelData)
                        }
                    }
                }
            }
        }
    }

    // the pickers' drop-down lists open in a menu over the whole window
    Menu95 { id: pickerMenu; parent: panel.app ? panel.app.contentItem : panel }

    // ------------------------------------------------------------------ components
    // A flat toolbar button with an Icon95 and a tooltip.
    component ChatIconButton: Item {
        id: ib
        property string icon: ""
        property string tip: ""
        property bool checked: false
        signal clicked()
        width: 22
        height: 20
        Bevel {
            anchors.fill: parent
            visible: ibMouse.containsMouse || ib.checked
            style: (ibMouse.pressed && ibMouse.containsMouse) || ib.checked ? "thinSunken" : "thinRaised"
        }
        Icon95 { anchors.centerIn: parent; name: ib.icon }
        MouseArea { id: ibMouse; anchors.fill: parent; hoverEnabled: true; onClicked: ib.clicked() }
        ToolTipArea { anchors.fill: parent; text: ib.tip }
    }

    // One message: who said it, then its text, thinking and tool calls in order, an error and the token count.
    component ChatMessage: Column {
        id: m
        property int msgIndex: 0
        property var msg: ({ role: "user", parts: [] })
        readonly property bool isUser: msg.role === "user"
        readonly property var parts: msg.parts || []
        spacing: 4

        Row {
            spacing: 5
            Item {
                width: 14
                height: 14
                Icon95 { visible: m.isUser; anchors.centerIn: parent; name: "person"; size: 14 }
                Text { visible: !m.isUser; anchors.centerIn: parent; text: "✦"; color: "#c15f3c"; font.pixelSize: 12 }
            }
            Text {
                text: m.isUser ? "You" : "Claude"
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontSize
                font.bold: true
                color: Theme.text
            }
        }
        // the question, as typed
        Bevel {
            visible: m.isUser
            width: parent.width
            height: visible ? userText.implicitHeight + 10 : 0
            style: "thinRaised"
            face: "#ffffe1"
            TextEdit {
                id: userText
                x: 6
                y: 5
                width: parent.width - 12
                readOnly: true
                selectByMouse: true
                wrapMode: TextEdit.Wrap
                textFormat: TextEdit.PlainText
                text: m.isUser ? m.parts.map(function (p) { return p.type === "text" ? p.text : ""; }).join("") : ""
                color: "#000000"
                selectionColor: Theme.selection
                selectedTextColor: Theme.selectionText
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontSize + 1
            }
        }
        // the answer's parts
        Repeater {
            model: m.isUser ? 0 : m.parts.length
            delegate: ChatPart {
                required property int index
                width: m.width
                part: m.parts[index] || { type: "text", text: "" }
                partKey: m.msgIndex + ":" + index
                last: index === m.parts.length - 1
                streaming: !!m.msg.streaming
            }
        }
        Label95 {
            visible: !m.isUser && !!m.msg.streaming && (m.parts.length === 0
                     || (m.parts[m.parts.length - 1].type === "tool" && m.parts[m.parts.length - 1].status !== "running"))
            dim: true
            font.italic: true
            text: "Working…"
        }
        Notice {
            width: parent.width
            kind: "error"
            text: m.msg.error ? Util.plain(m.msg.error) : ""
            buttons: m.msg.authError ? [{ text: "Open Settings", action: function () { panel.app.setView("settings"); } }] : []
        }
        Label95 {
            visible: !!m.msg.meta
            small: true
            dim: true
            text: m.msg.meta || ""
        }
    }

    // A part of an answer: Markdown text, the collapsible thought process, or a tool call with its input and result.
    component ChatPart: Column {
        id: cp
        property var part: ({ type: "text", text: "" })
        property string partKey: ""
        property bool last: false
        property bool streaming: false
        readonly property bool open: cp.part.type === "thinking" ? (panel.expanded[cp.partKey] === true) !== (cp.streaming && cp.last)
                                                                 : panel.expanded[cp.partKey] === true
        spacing: 2

        TextEdit {
            visible: cp.part.type === "text"
            width: parent.width
            text: cp.part.type === "text" ? panel.markdown(cp.part.text) + (cp.streaming && cp.last ? " ▍" : "") : ""
            textFormat: TextEdit.MarkdownText
            wrapMode: TextEdit.Wrap
            readOnly: true
            selectByMouse: true
            color: Theme.text
            selectionColor: Theme.selection
            selectedTextColor: Theme.selectionText
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontSize + 1
            onLinkActivated: (link) => { if (/^https?:\/\//.test(link)) api.invoke("app:openExternal", [link]); }
        }

        // the header line of thinking and tool parts; click to open
        Item {
            visible: cp.part.type !== "text"
            width: parent.width
            height: visible ? 18 : 0
            Text {
                id: arrow
                anchors.verticalCenter: parent.verticalCenter
                text: cp.open ? "▾" : "▸"
                font.pixelSize: 10
                color: Theme.shadow
            }
            Text {
                id: statusMark
                x: arrow.width + 4
                visible: cp.part.type === "tool"
                anchors.verticalCenter: parent.verticalCenter
                width: visible ? implicitWidth : 0
                text: cp.part.status === "running" ? "⌛" : cp.part.status === "ok" ? "✔" : "✖"
                color: cp.part.status === "ok" ? "#007000" : cp.part.status === "error" ? "#b00000" : Theme.text
                font.pixelSize: 11
            }
            Text {
                x: statusMark.x + statusMark.width + (statusMark.visible ? 4 : 0)
                width: parent.width - x
                anchors.verticalCenter: parent.verticalCenter
                elide: Text.ElideRight
                textFormat: Text.StyledText
                text: cp.part.type === "thinking"
                      ? "<i>" + (cp.streaming && cp.last ? "Thinking…" : "Thought process") + "</i>"
                      : "<b>" + Util.plain(panel.toolLabel(cp.part.name)) + "</b>  <font color=\"#606060\">" + Util.plain(panel.toolArg(cp.part.input)) + "</font>"
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontSize
                color: Theme.shadow
            }
            MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: panel.toggle(cp.partKey) }
        }
        // the thought process
        Rectangle {
            visible: cp.part.type === "thinking" && cp.open
            width: parent.width
            height: visible ? thinkText.implicitHeight + 8 : 0
            color: "#f4f4f4"
            border.color: Theme.light
            TextEdit {
                id: thinkText
                x: 6
                y: 4
                width: parent.width - 12
                readOnly: true
                selectByMouse: true
                wrapMode: TextEdit.Wrap
                textFormat: TextEdit.PlainText
                text: cp.part.type === "thinking" ? cp.part.text : ""
                color: "#505050"
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontSize
                font.italic: true
            }
        }
        // a tool call's input and result
        Rectangle {
            visible: cp.part.type === "tool" && cp.open
            width: parent.width
            height: visible ? toolText.implicitHeight + 8 : 0
            color: "#f4f4f4"
            border.color: Theme.light
            TextEdit {
                id: toolText
                x: 6
                y: 4
                width: parent.width - 12
                readOnly: true
                selectByMouse: true
                wrapMode: TextEdit.WrapAnywhere
                textFormat: TextEdit.PlainText
                text: cp.part.type === "tool" ? JSON.stringify(cp.part.input, null, 1) + (cp.part.result ? "\n\n" + cp.part.result : "") : ""
                color: Theme.text
                font.family: Theme.monoFamily
                font.pixelSize: 11
            }
        }
        // a short result line under a finished tool, so the user sees what came back without opening it
        Text {
            visible: cp.part.type === "tool" && !cp.open && !!cp.part.result
            x: 14
            width: parent.width - 14
            elide: Text.ElideRight
            maximumLineCount: 1
            textFormat: Text.PlainText
            text: cp.part.type === "tool" && cp.part.result ? String(cp.part.result).replace(/\s+/g, " ").slice(0, 200) : ""
            color: cp.part.status === "error" ? "#b00000" : "#606060"
            font.family: Theme.monoFamily
            font.pixelSize: 10
        }
    }

    // ------------------------------------------------------------------ W95_TRADINGLAB_TEST=chat: a conversation for the screenshot,
    // made of chat:event-like data injected here (no API call)
    Timer {
        id: testTimer
        interval: 1500
        onTriggered: {
            const id = "test-request";
            panel.requestId = id;
            panel.busy = true;
            panel.messages = [
                { role: "user", parts: [{ type: "text", text: "Where is support on the AAPL chart? Draw it for me." }] },
                { role: "assistant", parts: [], streaming: true }
            ];
            const ev = function (o) { panel.handleEvent(Object.assign({ requestId: id }, o)); };
            ev({ type: "thinking", text: "The user wants support levels. I should read the chart first: swings, then the recent candles, " });
            ev({ type: "thinking", text: "and anchor a line on a real swing low." });
            ev({ type: "text", text: "Let me look at the chart before drawing anything." });
            ev({ type: "tool_start", id: "t1", name: "get_chart_state", input: {} });
            ev({ type: "tool_end", id: "t1", ok: true, result: "{\n \"symbol\": \"AAPL\",\n \"range\": \"6M\",\n \"interval\": \"1day\",\n \"bars\": { \"loaded\": 126, \"lastClose\": 182.41 }\n}" });
            ev({ type: "tool_start", id: "t2", name: "find_swings", input: { left: 5, right: 5, max: 6 } });
            ev({ type: "tool_end", id: "t2", ok: true, result: "Swing points (most recent first):\nswing low: 2026-11-03 @ 176.20\nswing high: 2026-10-21 @ 191.85\nswing low: 2026-09-29 @ 171.10" });
            ev({ type: "tool_start", id: "t3", name: "draw_horizontal_line", input: { price: 176.2, label: "Support" } });
            ev({ type: "tool_end", id: "t3", ok: true, result: "Drew horizontal line at 176.20 labelled \"Support\" (id 3f2a…)." });
            ev({ type: "text", text: "\n\nI drew **support at 176.20**, the most recent swing low (3 Nov). Here is how the levels line up:\n\n"
                                     + "| Level | Price | Why |\n|---|---|---|\n| Resistance | 191.85 | swing high, 21 Oct |\n| Support | 176.20 | swing low, 3 Nov |\n| Deeper support | 171.10 | swing low, 29 Sep |\n\n"
                                     + "- Price closed at 182.41, about **3.5% above** support.\n- A daily close below 176.20 would break the pattern of higher lows.\n\n"
                                     + "_This is a reading of past prices, not a prediction or advice._" });
            ev({ type: "done", model: "claude-sonnet-5-5", stopReason: "end_turn", inputTokens: 18234, outputTokens: 1462 });
            panel.toggle("1:3");
        }
    }
}
