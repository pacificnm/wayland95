import QtQuick
import Win95Ui

// A chat: what is missing to answer (Ollama, a model), the questions and
// cited answers, and the question box. The Ask page, a project and the
// article panel all show one. A citation's link opens its article at the
// section; pointing at it names the source in the status bar.
Item {
    id: view
    required property Conversation conversation
    property bool follow: true  // keep the newest text in view while an answer is written
    property bool compact: false
    signal openArticle(var libraryId, string path, string fragment)

    function setQuestion(text) {
        input.text = text;
        input.input.forceActiveFocus();
    }
    function focusInput() { input.input.forceActiveFocus(); }
    function send() {
        if (input.text.trim() === "" || view.conversation.busy)
            return;
        view.follow = true;
        view.conversation.ask(input.text);
        input.text = "";
    }
    function openCitation(row, link) {
        if (link.indexOf("cite:") === 0) {
            var s = view.conversation.source(row, parseInt(link.slice(5)));
            if (!s.libraryId)
                return;
            if (!s.available) {
                window.notify("“" + (s.libraryTitle || "That library") + "” is no longer installed.");
                return;
            }
            view.openArticle(s.libraryId, s.path, s.section ? s.section.replace(/ /g, "_") : "");
        } else if (link.indexOf("check:") === 0) {
            window.msg("Check This", view.conversation.flagReason(row, parseInt(link.slice(6))));
        }
    }
    function linkTip(row, link) {
        if (link.indexOf("cite:") === 0) {
            var s = view.conversation.source(row, parseInt(link.slice(5)));
            return s.title ? (s.section ? s.title + " – " + s.section : s.title) : "";
        }
        if (link.indexOf("check:") === 0)
            return view.conversation.flagReason(row, parseInt(link.slice(6)));
        return "";
    }

    Column {
        id: notices
        width: parent.width
        spacing: 2
        Notice {
            width: parent.width
            kind: "error"
            text: Models.checked && !Models.reachable
                  ? "Ollama is not running at " + Util.plain(Models.url) + ", so questions cannot be answered. Reading and searching libraries work without it." : ""
            buttons: [{ text: "Services...", action: function () { controlPanel.openServices(); } },
                      { text: "Check Again", action: function () { Models.refresh(); } },
                      { text: "Settings", action: function () { window.show("settings"); } }]
        }
        Notice {
            width: parent.width
            kind: "error"
            text: Models.reachable && !Models.answerModelInstalled
                  ? "The answer model “" + Util.plain(Models.answerModel) + "” is not installed. Install it in a terminal with: <b>ollama pull "
                    + Util.plain(Models.answerModel) + "</b>, or get it in Control Panel > Services > Ollama > Settings." : ""
            buttons: [{ text: "Services...", action: function () { controlPanel.openServices(); } },
                      { text: "Check Again", action: function () { Models.refresh(); } },
                      { text: "Settings", action: function () { window.show("settings"); } }]
        }
        Notice {
            width: parent.width
            text: LibraryManager.installed.count === 0 ? "No libraries are installed yet, so there is nothing to answer from." : ""
            buttons: [{ text: "Library Manager", action: function () { window.showLibraryManager(1); } }]
        }
        Notice {
            width: parent.width
            kind: "warning"
            text: Models.reachable && Models.answerModelInstalled && !Models.embeddingModelInstalled
                  ? "The embedding model “" + Util.plain(Models.embeddingModel) + "” is not installed, so sources are ranked by words only and sentences are not linked to them. Install it with: <b>ollama pull "
                    + Util.plain(Models.embeddingModel) + "</b>" : ""
            buttons: [{ text: "Check Again", action: function () { Models.refresh(); } }]
        }
    }

    Bevel {
        id: well
        y: notices.height + (notices.height > 0 ? 4 : 0)
        width: parent.width
        height: parent.height - y - inputRow.height - 6
        style: "sunken"
        face: Theme.window
        ListView {
            id: list
            x: 2
            y: 2
            width: parent.width - 4 - (bar.visible ? bar.width : 0)
            height: parent.height - 4
            clip: true
            interactive: false
            boundsBehavior: Flickable.StopAtBounds
            model: view.conversation
            spacing: 10
            topMargin: 8
            bottomMargin: 8
            leftMargin: 10
            rightMargin: 10
            onCountChanged: if (view.follow) Qt.callLater(positionViewAtEnd)
            onContentHeightChanged: if (view.follow && view.conversation.busy) Qt.callLater(positionViewAtEnd)
            delegate: Column {
                id: message
                required property int index
                required property string role
                required property string text
                required property string status
                required property string phase
                required property string error
                required property string note
                required property var sources
                required property string stats
                required property string content
                width: list.width - 20
                spacing: 4

                // ---- a question: a raised box on the right, like a note handed over
                Item {
                    visible: message.role === "user"
                    width: parent.width
                    height: visible ? qbox.height : 0
                    Bevel {
                        id: qbox
                        anchors.right: parent.right
                        width: Math.min(question.implicitWidth + 16, parent.width * 0.85)
                        height: question.implicitHeight + 10
                        style: "raised"
                        face: "#ffffe1"
                        Text {
                            id: question
                            x: 8
                            y: 5
                            width: parent.width - 16
                            textFormat: Text.PlainText
                            wrapMode: Text.Wrap
                            text: message.text
                            color: "#000000"
                            font.family: Theme.fontFamily
                            font.pixelSize: Theme.fontSize + 1
                        }
                    }
                }
                // ---- an answer
                Label95 {
                    visible: message.role === "assistant" && message.status === "working" && message.phase !== ""
                    dim: true
                    font.italic: true
                    text: "⌛ " + message.phase
                }
                TextEdit {
                    id: answer
                    visible: message.role === "assistant" && message.text !== ""
                    width: parent.width
                    text: Util.markdown(message.text)
                    textFormat: TextEdit.MarkdownText
                    wrapMode: TextEdit.Wrap
                    readOnly: true
                    selectByMouse: true
                    color: Theme.text
                    selectionColor: Theme.selection
                    selectedTextColor: Theme.selectionText
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontSize + 1
                    onLinkActivated: (link) => view.openCitation(message.index, link)
                    onHoveredLinkChanged: window.hoverText = hoveredLink !== "" ? Util.plain(view.linkTip(message.index, hoveredLink)) : ""
                    HoverHandler { cursorShape: answer.hoveredLink !== "" ? Qt.PointingHandCursor : Qt.IBeamCursor }
                }
                Label95 {
                    visible: message.role === "assistant" && message.status === "stopped"
                    dim: true
                    font.italic: true
                    text: message.text === "" ? "Stopped before the answer began." : "Stopped."
                }
                Notice { width: parent.width; kind: "error"; text: Util.plain(message.error) }
                Notice { width: parent.width; kind: "warning"; text: message.error === "" ? Util.plain(message.note) : "" }
                // the sources the model was shown
                Label95 {
                    visible: message.role === "assistant" && message.sources.length > 0
                    topPadding: 4
                    bold: true
                    text: "Sources"
                }
                Repeater {
                    model: message.role === "assistant" ? message.sources : []
                    Item {
                        id: sourceRow
                        required property var modelData
                        width: message.width
                        height: 18
                        Text {
                            id: num
                            width: 28
                            height: parent.height
                            verticalAlignment: Text.AlignVCenter
                            text: "[" + sourceRow.modelData.n + "]"
                            color: "#0000c0"
                            font.family: Theme.fontFamily
                            font.pixelSize: Theme.fontSize
                        }
                        Text {
                            x: num.width
                            width: parent.width - x
                            height: parent.height
                            verticalAlignment: Text.AlignVCenter
                            elide: Text.ElideRight
                            textFormat: Text.PlainText
                            text: {
                                var s = sourceRow.modelData;
                                var t = view.conversation.articlePath !== "" ? (s.section || "Introduction")
                                      : s.section ? s.title + " – " + s.section : s.title;
                                if (view.conversation.articlePath === "" && s.libraryTitle)
                                    t += s.kind === "library" ? "  (" + s.libraryTitle + ", outside the project)" : "  (" + s.libraryTitle + ")";
                                return t;
                            }
                            color: srcMouse.containsMouse ? "#0000c0" : Theme.text
                            font.underline: srcMouse.containsMouse
                            font.family: Theme.fontFamily
                            font.pixelSize: Theme.fontSize
                        }
                        MouseArea {
                            id: srcMouse
                            anchors.fill: parent
                            hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            onClicked: view.openCitation(message.index, "cite:" + sourceRow.modelData.n)
                            onContainsMouseChanged: window.hoverText = containsMouse && sourceRow.modelData.snippet ? Util.plain(sourceRow.modelData.snippet + "…") : ""
                        }
                    }
                }
                Button95 {
                    visible: message.role === "assistant" && message.status === "done" && message.content !== "" && message.sources.length > 0
                    width: 130
                    height: 21
                    text: "Save to Project..."
                    onClicked: {
                        var src = message.sources, text = message.content;
                        window.pickProject("Save Answer to Project", text, true, [], function (projectId, projectTitle, note) {
                            if (projectList.addAnswer(projectId, text, src, note))
                                window.notify("Saved to “" + projectTitle + "”");
                        });
                    }
                }
                Label95 {
                    visible: message.stats !== ""
                    width: parent.width
                    small: true
                    dim: true
                    wrapMode: Text.Wrap
                    text: message.stats
                }
            }
            WheelHandler {
                onWheel: (event) => {
                    list.contentY = Math.max(list.originY - list.topMargin,
                                             Math.min(list.originY + list.contentHeight + list.bottomMargin - list.height, list.contentY - event.angleDelta.y / 120 * 54));
                    view.follow = list.atYEnd;
                }
            }
        }
        ScrollBar95 {
            id: bar
            anchors.right: parent.right
            anchors.rightMargin: 2
            y: 2
            height: parent.height - 4
            flickable: list
            stepSize: 18
        }
        Column {
            visible: list.count === 0
            anchors.centerIn: parent
            width: parent.width - 60
            spacing: 8
            AthenaeumIcon { anchors.horizontalCenter: parent.horizontalCenter; size: 32; name: "chat" }
            Heading95 {
                width: parent.width
                horizontalAlignment: Text.AlignHCenter
                level: 3
                text: view.conversation.articlePath !== "" ? "Ask about “" + view.conversation.articleTitle + "”"
                    : view.conversation.projectId > 0 ? "Ask about this project" : "Ask a question"
            }
            Label95 {
                width: parent.width
                horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.Wrap
                dim: true
                text: view.conversation.articlePath !== ""
                    ? "Answers come from this article alone. Each sentence links to the section it comes from."
                    : view.conversation.projectId > 0
                    ? "Answers come from the project's articles and saved excerpts, written with its goal in mind. Tick “Also search the whole library” to look further."
                    : "A local model answers from your libraries. Each sentence links to the article it comes from; sentences no source backs are marked for checking."
            }
        }
    }

    Row {
        id: inputRow
        anchors.bottom: parent.bottom
        width: parent.width
        height: 23
        spacing: 4
        TextField95 {
            id: input
            width: parent.width - 84
            height: 23
            input.maximumLength: 2000 // a pasted page would push the sources out of the model's context
            onAccepted: view.send()
            Text {
                visible: input.text === "" && !input.input.activeFocus
                x: 4
                anchors.verticalCenter: parent.verticalCenter
                text: list.count > 0 ? "Ask a follow-up question..."
                    : view.conversation.articlePath !== "" ? "Ask about this article..."
                    : view.conversation.projectId > 0 ? "Ask about this project..." : "Ask a question about your libraries..."
                color: Theme.shadow
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontSize
            }
        }
        Button95 {
            width: 80
            height: 23
            visible: !view.conversation.busy
            isDefault: true
            text: "Ask"
            enabled: input.text.trim() !== ""
            onClicked: view.send()
        }
        Button95 {
            width: 80
            height: 23
            visible: view.conversation.busy
            text: "Stop"
            onClicked: view.conversation.stop()
        }
    }
}
