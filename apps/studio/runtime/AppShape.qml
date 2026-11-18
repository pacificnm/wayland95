import QtQuick
import Win95Ui

// Shape: a rectangle, oval or line, for decoration (BackColor fills it).
AppElement {
    id: item

    property string shape: "Rectangle"
    property string borderColor: "#000000"
    property int borderWidth: 1

    readonly property bool line: shape === "Horizontal Line" || shape === "Vertical Line"
    readonly property color stroke: borderColor !== "" ? borderColor : Theme.dark

    Rectangle {
        visible: !item.line
        anchors.fill: parent
        radius: item.shape === "Oval" ? Math.min(width, height) / 2
              : item.shape === "Rounded Rectangle" ? Math.min(12, Math.min(width, height) / 4) : 0
        color: item.backColor !== "" ? item.backColor : "transparent"
        border.color: item.stroke
        border.width: Math.max(0, item.borderWidth)
        antialiasing: item.shape !== "Rectangle"
    }
    Rectangle {
        visible: item.shape === "Horizontal Line"
        width: parent.width
        height: Math.max(1, item.borderWidth)
        anchors.verticalCenter: parent.verticalCenter
        color: item.stroke
    }
    Rectangle {
        visible: item.shape === "Vertical Line"
        width: Math.max(1, item.borderWidth)
        height: parent.height
        anchors.horizontalCenter: parent.horizontalCenter
        color: item.stroke
    }
}
