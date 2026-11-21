import QtQuick
import Win95Ui

// One highlight: the passage in its colour, its note (written here, kept
// when the box is left), and what can be done with it. In an article's
// Notes panel and on the Notes page.
Rectangle {
    id: card
    required property var highlight  // Notes.forArticle() / Notes.all() item
    property bool showTitle: false
    signal goTo()
    signal addToProject()

    implicitHeight: col.implicitHeight + 12
    color: Theme.window
    border.color: Theme.shadow

    Rectangle {
        x: 1
        y: 1
        width: 5
        height: parent.height - 2
        color: Notes.swatch(card.highlight.color)
    }
    Column {
        id: col
        x: 12
        y: 6
        width: parent.width - 18
        spacing: 4
        Label95 {
            visible: card.showTitle
            width: parent.width
            bold: true
            elide: Text.ElideRight
            text: card.highlight.title || ""
        }
        Rectangle {
            width: parent.width
            height: passage.implicitHeight + 4
            color: Theme.window
            Rectangle { anchors.fill: parent; color: Notes.swatch(card.highlight.color); opacity: 0.3 }
            Text {
                id: passage
                x: 2
                y: 2
                width: parent.width - 4
                textFormat: Text.PlainText
                wrapMode: Text.Wrap
                maximumLineCount: 8
                elide: Text.ElideRight
                text: card.highlight.text
                color: Theme.text
                font.family: "Liberation Serif"
                font.pixelSize: 14
            }
        }
        TextArea95 {
            id: noteField
            width: parent.width
            height: Math.max(40, Math.min(120, edit.implicitHeight + 6))
            placeholder: "Add a note..."
            text: card.highlight.note
            function save() {
                if (text.trim() !== card.highlight.note)
                    Notes.setNote(card.highlight.id, text);
            }
            edit.onActiveFocusChanged: if (!edit.activeFocus) save()
            edit.Keys.onEscapePressed: { text = card.highlight.note; edit.focus = false; }
        }
        Row {
            spacing: 3
            Repeater {
                model: Notes.colors
                Rectangle {
                    required property string modelData
                    width: 18
                    height: 18
                    color: Notes.swatch(modelData)
                    border.color: card.highlight.color === modelData ? "#000000" : Theme.shadow
                    border.width: card.highlight.color === modelData ? 2 : 1
                    MouseArea { anchors.fill: parent; onClicked: Notes.setColor(card.highlight.id, parent.modelData) }
                }
            }
            Item { width: 8; height: 1 }
            IconButton { icon: "find"; text: card.width > 360 ? "Show" : ""; height: 20; onClicked: card.goTo() }
            IconButton { icon: "project"; text: card.width > 360 ? "Project..." : ""; height: 20; onClicked: { noteField.save(); card.addToProject(); } }
            IconButton {
                icon: "delete"
                height: 20
                // no question asked: it can be put back
                onClicked: {
                    var h = card.highlight;
                    if (Notes.remove(h.id))
                        window.notify("Highlight removed.", false, "Undo", function () { Notes.add(h.libraryId, h.path, h.title, h.text, h.color, h.note); });
                }
            }
        }
    }
}
