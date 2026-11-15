import QtQuick

Item {
    id: taskButton
    width: 150

    property string text: ""
    property bool active: false
    signal clicked()
    // right-click: the window menu, at x, y in the button
    signal menuRequested(real x, real y)

    Bevel { anchors.fill: parent; raised: !taskButton.active }

    Text {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.verticalCenter: parent.verticalCenter
        anchors.margins: 6
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
