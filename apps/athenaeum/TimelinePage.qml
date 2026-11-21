import QtQuick
import Win95Ui

// An article's or a project's dated sentences in date order, grouped by
// period, each opening its article at the section; read from the text,
// not written by the model.
Page95 {
    id: page
    property var libraryId: 0
    property string path: ""
    property string articleTitle: ""
    property var projectId: 0
    property int current: -1
    title: "Timeline: " + Util.pageTitle(timeline.title)
    actions: [
        { text: page.projectId > 0 ? "Add to This &Project" : "Add to &Project...", enabled: timeline.events.length > 0, action: toProject },
        { text: "&Export...", enabled: timeline.events.length > 0, action: exportTimeline },
    ]
    Component.onCompleted: page.projectId > 0 ? timeline.loadProject(page.projectId) : timeline.loadArticle(page.libraryId, page.path, page.articleTitle)

    TimelineModel { id: timeline }

    function toProject() {
        if (page.projectId > 0) {
            if (timeline.toProject(page.projectId) > 0)
                window.notify("The timeline is a document of the project now");
            return;
        }
        window.pickProject("Add the Timeline to a Project", "", false, [], function (projectId, projectTitle) {
            if (timeline.toProject(projectId) > 0)
                window.notify("The timeline is a document of “" + projectTitle + "” now");
        });
    }
    function exportTimeline() {
        window.chooseFile("save", [{ label: "Markdown (*.md)", patterns: ["*.md"] }], "md", timeline.exportName(), function (path) {
            var error = timeline.exportTo("file://" + path);
            window.notify(error !== "" ? error : "Exported to " + path, error !== "");
        });
    }
    function open(e) { window.openArticle(e.libraryId, e.path, e.section.replace(/ /g, "_")); }

    Row {
        id: top
        spacing: 6
        height: 24
        Label95 { height: 22; verticalAlignment: Text.AlignVCenter; text: "Find:" }
        TextField95 { width: 200; height: 22; onTextChanged: timeline.query = text }
        ComboBox95 {
            id: articleBox
            visible: timeline.articles.length > 1
            width: 240
            height: 22
            popupMenu: popup
            readonly property var values: [{ key: "", label: "All articles (" + timeline.total + ")" }]
                .concat(timeline.articles.map(function (a) { return { key: a.key, label: Util.plain(a.title) + " (" + a.count + ")" }; }))
            model: values.map(function (v) { return v.label; })
            currentIndex: Math.max(0, values.findIndex(function (v) { return v.key === timeline.article; }))
            onActivated: (i) => timeline.article = values[i].key
        }
        Label95 {
            height: 22
            verticalAlignment: Text.AlignVCenter
            visible: !timeline.loading
            dim: true
            text: timeline.events.length === timeline.total ? timeline.total + (timeline.total === 1 ? " event" : " events")
                                                            : timeline.events.length + " of " + timeline.total + " events"
        }
    }
    Bevel {
        y: top.height + 4
        width: parent.width
        height: parent.height - y - 30
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
            model: timeline.events
            section.property: "period"
            section.delegate: Item {
                required property string section
                width: list.width
                height: 26
                Rectangle { x: 4; y: 4; width: parent.width - 8; height: 20; color: Theme.face }
                Text {
                    x: 10
                    y: 4
                    height: 20
                    verticalAlignment: Text.AlignVCenter
                    textFormat: Text.PlainText
                    text: parent.section
                    color: Theme.text
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontSize + 1
                    font.bold: true
                }
            }
            delegate: Item {
                id: row
                required property var modelData
                required property int index
                readonly property bool selected: index === page.current
                width: list.width
                height: Math.max(text.implicitHeight + where.implicitHeight + 10, 22)
                Rectangle { anchors.fill: parent; color: row.selected ? Theme.selection : "transparent" }
                Text {
                    id: date
                    x: 12
                    y: 4
                    width: 110
                    textFormat: Text.PlainText
                    wrapMode: Text.Wrap
                    text: row.modelData.label
                    font.italic: row.modelData.approximate
                    font.bold: !row.modelData.approximate
                    color: row.selected ? Theme.selectionText : "#000080"
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontSize
                }
                Text {
                    id: text
                    x: 130
                    y: 4
                    width: parent.width - x - 10
                    textFormat: Text.PlainText
                    wrapMode: Text.Wrap
                    text: row.modelData.text
                    color: row.selected ? Theme.selectionText : Theme.text
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontSize
                }
                Text {
                    id: where
                    x: 130
                    y: text.y + text.implicitHeight + 1
                    width: parent.width - x - 10
                    elide: Text.ElideRight
                    textFormat: Text.PlainText
                    text: row.modelData.section !== "" ? row.modelData.articleTitle + " · " + row.modelData.section : row.modelData.articleTitle
                    color: row.selected ? Theme.selectionText : Theme.shadow
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontSize - 1
                }
                MouseArea {
                    anchors.fill: parent
                    onPressed: page.current = row.index
                    onDoubleClicked: page.open(row.modelData)
                }
            }
            WheelHandler {
                onWheel: (event) => {
                    list.contentY = Math.max(list.originY, Math.min(list.originY + list.contentHeight - list.height, list.contentY - event.angleDelta.y / 120 * 60));
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
            stepSize: 20
        }
        Label95 {
            visible: timeline.loading || list.count === 0
            anchors.centerIn: parent
            width: parent.width - 60
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.Wrap
            dim: true
            text: timeline.loading ? "Reading the dates..."
                : timeline.total === 0 ? (page.projectId > 0 ? "No dates found. The project's articles give no dates. Add articles about events, people or places."
                                                             : "No dates found. This article gives no dates in its text.")
                : "No events match."
        }
    }
    Label95 {
        anchors.bottom: parent.bottom
        height: 23
        verticalAlignment: Text.AlignVCenter
        dim: true
        text: "Double-click an event to read it in its article."
    }
    Row {
        anchors.bottom: parent.bottom
        anchors.right: parent.right
        spacing: 6
        Button95 { width: 130; text: page.projectId > 0 ? "Add to This Project" : "Add to Project..."; enabled: timeline.events.length > 0; onClicked: page.toProject() }
        Button95 { width: 80; text: "Export..."; enabled: timeline.events.length > 0; onClicked: page.exportTimeline() }
    }
}
