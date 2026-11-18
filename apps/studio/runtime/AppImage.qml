import QtQuick
import Win95Ui

// Image: a picture. The compiler puts the file inside the app.
AppElement {
    id: image

    property string picture: ""     // a URL: the Studio and the compiler fill it in
    property string sizeMode: "Fit"
    property string borderStyle: "None"
    signal clicked()

    readonly property int inset: borderStyle === "None" ? 0 : borderStyle === "Single" ? 1 : 2

    Rectangle {
        anchors.fill: parent
        color: image.backColor !== "" ? image.backColor : "transparent"
        border.color: Theme.dark
        border.width: image.borderStyle === "Single" ? 1 : 0
    }
    Bevel {
        visible: image.borderStyle === "Sunken"
        anchors.fill: parent
        style: "sunken"
        filled: false
    }

    Image {
        anchors.fill: parent
        anchors.margins: image.inset
        source: image.picture
        clip: true
        smooth: true
        fillMode: image.sizeMode === "Stretch" ? Image.Stretch
                : image.sizeMode === "Center" ? Image.Pad
                : image.sizeMode === "Tile" ? Image.Tile : Image.PreserveAspectFit
    }

    // on the canvas, an empty one still has to be seen
    Rectangle {
        visible: image.designMode && image.picture === ""
        anchors.fill: parent
        color: "transparent"
        border.color: Theme.shadow
        border.width: 1
        Text {
            anchors.centerIn: parent
            text: "Image"
            color: Theme.shadow
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontSize
        }
    }

    MouseArea {
        anchors.fill: parent
        enabled: !image.designMode
        onClicked: image.clicked()
    }
}
