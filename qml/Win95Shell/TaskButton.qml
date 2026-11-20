import QtQuick

Item {
    id: taskButton
    width: 150

    property string text: ""
    property bool active: false
    // the program's (Wayland app id, or X11 class): its icon
    property string appId: ""
    // Wayland 95's own programs have drawn icons (MenuIcon); others the
    // icon their .desktop file names, or a plain window
    readonly property var ownIcons: ({
        win95notepad: "notepad", win95files: "files", win95terminal: "terminal", win95control: "control",
        win95sysmon: "sysmon", win95volume: "volume", win95imagewriter: "imagewriter",
        win95solitaire: "solitaire", win95mines: "mines", win95find: "find", win95repos: "repos", win95markdown: "markdown", win95photos: "photos", win95archive: "archive", "win95archive-progress": "archive", win95calc: "calc", win95email: "email", win95web: "web", win95studio: "studio",
    })
    readonly property string iconUrl: ownIcons[appId] || !appId ? "" : ProgramMenu.iconFor(appId)
    signal clicked()
    // right-click: the window menu, at x, y in the button
    signal menuRequested(real x, real y)

    Bevel { anchors.fill: parent; raised: !taskButton.active }

    // (pressed in, the icon and text move with the button face)
    readonly property int shift: taskButton.active ? 1 : 0
    Image {
        id: appIcon
        x: 4 + taskButton.shift
        anchors.verticalCenter: parent.verticalCenter
        anchors.verticalCenterOffset: taskButton.shift
        width: 16
        height: 16
        sourceSize: Qt.size(16, 16)
        source: taskButton.iconUrl
        visible: status === Image.Ready
        fillMode: Image.PreserveAspectFit
        smooth: true
    }
    MenuIcon {
        x: 4 + taskButton.shift
        anchors.verticalCenter: parent.verticalCenter
        anchors.verticalCenterOffset: taskButton.shift
        size: 16
        visible: !appIcon.visible
        kind: taskButton.ownIcons[taskButton.appId] || "app"
    }

    Text {
        anchors.left: parent.left
        anchors.leftMargin: 24 + taskButton.shift
        anchors.right: parent.right
        anchors.rightMargin: 4
        anchors.verticalCenter: parent.verticalCenter
        anchors.verticalCenterOffset: taskButton.shift
        text: taskButton.text
        elide: Text.ElideRight
        font.family: Theme.fontFamily
        font.pixelSize: Theme.fontSize
        font.bold: taskButton.active
    }

    MouseArea {
        anchors.fill: parent
        acceptedButtons: Qt.LeftButton | Qt.RightButton
        onClicked: (mouse) => {
            if (mouse.button === Qt.RightButton)
                taskButton.menuRequested(mouse.x, mouse.y);
            else
                taskButton.clicked();
        }
    }
}
