import QtQuick
import Win95Ui

// Every highlight and note, by article: search them, show one colour,
// export them as Markdown or turn them into a project document.
Page95 {
    id: page
    title: "Notes"
    property string color: ""  // "" for every colour
    property var highlights: []
    readonly property var shownIds: highlights.map(function (h) { return h.id; })
    actions: [
        { text: "New Project &Document...", enabled: highlights.length > 0, action: toProject },
        { text: "&Export...", enabled: highlights.length > 0, action: exportNotes },
    ]
    function refresh() { page.highlights = Notes.all(page.color, search.text); }
    Component.onCompleted: refresh()
    Connections {
        target: Notes
        function onChanged() { refreshTimer.restart(); }
    }
    Timer { id: refreshTimer; interval: 150; onTriggered: page.refresh() }

    function toProject() {
        var ids = shownIds;
        window.pickProject("Notes to a Project Document", "", false, [], function (projectId, projectTitle) {
            var documentId = Notes.toProject(projectId, "Notes", ids);
            if (documentId > 0)
                window.notify("A document of your notes is in “" + projectTitle + "”", false, "Open", function () { window.openProject(projectId); });
        });
    }
    function exportNotes() {
        var ids = shownIds;
        window.chooseFile("save", [{ label: "Markdown (*.md)", patterns: ["*.md"] }], "md", "notes.md", function (path) {
            var error = Notes.exportTo("file://" + path, ids);
            window.notify(error !== "" ? error : "Exported to " + path, error !== "");
        });
    }

    Row {
        id: top
        spacing: 6
        height: 24
        Label95 { height: 22; verticalAlignment: Text.AlignVCenter; text: "Search:" }
        TextField95 {
            id: search
            width: Math.max(140, page.width - 470)
            height: 22
            onTextChanged: refreshTimer.restart()
        }
        Label95 { height: 22; verticalAlignment: Text.AlignVCenter; text: "Colour:" }
        Repeater {
            model: [""].concat(Notes.colors)
            ToolButton95 {
                required property string modelData
                width: modelData === "" ? 34 : 24
                height: 22
                text: modelData === "" ? "All" : ""
                checked: page.color === modelData
                onClicked: { page.color = modelData; page.refresh(); }
                Rectangle {
                    visible: parent.modelData !== ""
                    anchors.centerIn: parent
                    width: 12
                    height: 12
                    color: parent.modelData !== "" ? Notes.swatch(parent.modelData) : "transparent"
                    border.color: "#000000"
                }
            }
        }
    }
    Bevel {
        y: top.height + 4
        width: parent.width
        height: parent.height - y - 30
        style: "sunken"
        face: Theme.face
        Flickable {
            id: flick
            x: 2
            y: 2
            width: parent.width - 4 - (bar.visible ? bar.width : 0)
            height: parent.height - 4
            clip: true
            interactive: false
            contentHeight: col.implicitHeight + 12
            Column {
                id: col
                x: 8
                y: 6
                width: parent.width - 16
                spacing: 6
                Repeater {
                    model: page.highlights
                    Column {
                        id: entry
                        required property var modelData
                        required property int index
                        readonly property bool first: index === 0 || page.highlights[index - 1].articleKey !== modelData.articleKey
                        width: col.width
                        spacing: 4
                        // the article's heading, before its first highlight
                        Item {
                            visible: entry.first
                            width: parent.width
                            height: visible ? 24 : 0
                            AthenaeumIcon { y: 5; name: "article" }
                            Text {
                                x: 22
                                y: 3
                                width: parent.width - 22
                                elide: Text.ElideRight
                                textFormat: Text.PlainText
                                text: entry.modelData.title || entry.modelData.path || ""
                                color: artMouse.containsMouse ? "#0000c0" : Theme.text
                                font.underline: artMouse.containsMouse
                                font.family: Theme.fontFamily
                                font.pixelSize: Theme.fontSize + 2
                                font.bold: true
                                MouseArea {
                                    id: artMouse
                                    anchors.fill: parent
                                    hoverEnabled: true
                                    cursorShape: Qt.PointingHandCursor
                                    onClicked: window.openArticle(entry.modelData.libraryId, entry.modelData.path, "")
                                }
                            }
                        }
                        HighlightCard {
                            width: parent.width
                            highlight: entry.modelData
                            onGoTo: window.openArticle(entry.modelData.libraryId, entry.modelData.path, "hl-" + entry.modelData.id)
                            onAddToProject: {
                                var id = entry.modelData.id;
                                window.pickProject("Save Highlight to Project", entry.modelData.text, false, [], function (projectId, projectTitle) {
                                    if (Notes.addToProject(projectId, id))
                                        window.notify("Saved to “" + projectTitle + "”");
                                });
                            }
                        }
                    }
                }
            }
            WheelHandler {
                onWheel: (event) => {
                    flick.contentY = Math.max(0, Math.min(flick.contentHeight - flick.height, flick.contentY - event.angleDelta.y / 120 * 60));
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
            stepSize: 20
        }
        Label95 {
            visible: page.highlights.length === 0
            anchors.centerIn: parent
            width: parent.width - 60
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.Wrap
            dim: true
            text: search.text !== "" || page.color !== "" ? "No notes match."
                : "No notes yet.\n\nWhile reading, select a passage, right-click it and choose Highlight. Your highlights and the notes you write on them are gathered here, by article."
        }
    }
    Label95 {
        anchors.bottom: parent.bottom
        height: 23
        verticalAlignment: Text.AlignVCenter
        dim: true
        text: page.highlights.length === 1 ? "1 highlight" : page.highlights.length + " highlights"
    }
    Row {
        anchors.bottom: parent.bottom
        anchors.right: parent.right
        spacing: 6
        Button95 { width: 170; text: "New Project Document..."; enabled: page.highlights.length > 0; onClicked: page.toProject() }
        Button95 { width: 80; text: "Export..."; enabled: page.highlights.length > 0; onClicked: page.exportNotes() }
    }
}
