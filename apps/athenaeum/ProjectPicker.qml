import QtQuick
import Win95Ui

// Choose a research project (or start one) for something being added to
// it: an article, a selected passage, an answer, a chat. The most recently
// used project comes first.
//   picker.pick(title, preview, withNote, already, function (projectId, projectTitle, note) { ... })
Dialog95 {
    id: dialog
    property string preview: ""
    property bool withNote: true
    property var already: []
    property var callback: null
    property var choices: []
    property int choice: 0
    property Menu95 popupMenu
    readonly property bool isNew: choices.length > 0 && choices[choice].projectId === -1
    readonly property bool canAccept: !isNew || newTitle.text.trim() !== ""
    dialogWidth: 400
    dialogHeight: 120 + (preview !== "" ? 70 : 0) + (withNote ? 80 : 0) + (isNew ? 28 : 0)

    ProjectList { id: projects }

    function pick(t, text, note, alreadyIn, cb) {
        title = t;
        preview = text || "";
        withNote = note !== false;
        already = alreadyIn || [];
        callback = cb;
        projects.refresh();
        choices = projects.list().map(function (p) { return { projectId: p.projectId, title: p.title }; })
                  .concat([{ projectId: -1, title: "New Project..." }]);
        choice = 0;
        noteArea.text = "";
        newTitle.text = "";
        show();
        if (isNew)
            newTitle.input.forceActiveFocus();
    }
    function accept() {
        if (!canAccept)
            return;
        var c = choices[choice];
        var id = c.projectId, name = c.title;
        if (id === -1) {
            name = newTitle.text.trim();
            id = projects.create(name, "");
        }
        var cb = callback, note = withNote ? noteArea.text : "";
        close();
        if (id > 0 && cb)
            cb(id, name, note);
    }
    Keys.onEscapePressed: close()

    Column {
        x: 12
        y: 12
        width: parent.width - 24
        spacing: 6
        Label95 {
            width: parent.width
            visible: dialog.preview !== ""
            height: visible ? Math.min(implicitHeight, 64) : 0
            clip: true
            wrapMode: Text.Wrap
            maximumLineCount: 4
            elide: Text.ElideRight
            font.italic: true
            text: "“" + dialog.preview + "”"
        }
        Label95 { text: "Project:" }
        ComboBox95 {
            width: parent.width
            popupMenu: dialog.popupMenu
            model: dialog.choices.map(function (c) {
                return Util.plain(c.projectId > 0 && dialog.already.indexOf(c.projectId) >= 0 ? c.title + " (already there)" : c.title);
            })
            currentIndex: dialog.choice
            onActivated: (i) => { dialog.choice = i; if (dialog.isNew) newTitle.input.forceActiveFocus(); }
        }
        TextField95 {
            id: newTitle
            width: parent.width
            visible: dialog.isNew
            onAccepted: dialog.accept()
            Text {
                visible: newTitle.text === ""
                x: 4
                anchors.verticalCenter: parent.verticalCenter
                text: "Name of the new project"
                color: Theme.shadow
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontSize
            }
        }
        Label95 { visible: dialog.withNote; text: "A note (optional):" }
        TextArea95 { id: noteArea; visible: dialog.withNote; width: parent.width; height: 56 }
    }
    Row {
        anchors.right: parent.right
        anchors.rightMargin: 12
        anchors.bottom: parent.bottom
        anchors.bottomMargin: 10
        spacing: 6
        Button95 { text: "OK"; isDefault: true; enabled: dialog.canAccept; onClicked: dialog.accept() }
        Button95 { text: "Cancel"; onClicked: dialog.close() }
    }
}
