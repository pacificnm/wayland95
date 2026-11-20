import QtQuick
import Win95Ui

// "Extracting..." with the file it's on, a progress bar and Cancel, while
// the archive's job runs. (The whole window, when Archive Manager was run
// for the file manager's Extract Here / Add to.)
Dialog95 {
    id: dlg

    property string heading: ""
    property string file: ""
    property int percent: -1
    signal cancelRequested()

    title: heading
    dialogWidth: 380
    dialogHeight: 130

    ArchiveIcon {
        x: 12
        y: 12
        name: dlg.heading.indexOf("Add") === 0 ? "add" : dlg.heading.indexOf("Test") === 0 ? "test"
            : dlg.heading.indexOf("Delet") === 0 ? "delete" : "extract"
    }
    Text {
        x: 54
        y: 12
        width: parent.width - 66
        text: dlg.heading + "..."
        color: Theme.text
        font.family: Theme.fontFamily
        font.pixelSize: Theme.fontSize
    }
    Text {
        x: 54
        y: 30
        width: parent.width - 66
        elide: Text.ElideMiddle
        text: dlg.file
        color: Theme.text
        font.family: Theme.fontFamily
        font.pixelSize: Theme.fontSize
    }
    ProgressBar95 {
        x: 12
        y: 54
        width: parent.width - 24
        height: 18
        value: dlg.percent
    }
    Button95 {
        anchors.right: parent.right
        anchors.rightMargin: 12
        anchors.bottom: parent.bottom
        anchors.bottomMargin: 10
        text: "Cancel"
        isDefault: true
        onClicked: dlg.close()
    }
    // Escape and the close box close it: that's Cancel too (a finished
    // job closing it has nothing left to cancel)
    onClosed: cancelRequested()
}
