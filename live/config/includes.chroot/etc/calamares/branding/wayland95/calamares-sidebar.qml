/* The installer's left panel, Wayland 95 Setup style: the logo, then the
 * steps with a ► at the current one (done steps plain, later ones dimmed).
 */
import io.calamares.ui 1.0
import io.calamares.core 1.0
import QtQuick 2.15

Rectangle {
    id: sideBar
    // Calamares makes this 190px wide, or 100px on screens under 1024px
    anchors.fill: parent
    readonly property bool narrow: width < 150
    // the Start menu banner's navy, fading to the title bar blue
    gradient: Gradient {
        GradientStop { position: 0.0; color: "#000080" }
        GradientStop { position: 1.0; color: "#1084d0" }
    }

    readonly property string fontFamily: "Liberation Sans"

    // a 2px sunken edge where it meets the page, like Win95 wizard pictures
    Rectangle { anchors.right: parent.right; width: 1; height: parent.height; color: "#ffffff" }
    Rectangle { anchors.right: parent.right; anchors.rightMargin: 1; width: 1; height: parent.height; color: "#808080" }

    Column {
        id: header
        x: sideBar.narrow ? 8 : 14
        y: 14
        width: parent.width - 2 * x
        spacing: 4

        // the four-color logo, as on the Start button
        Rectangle {
            width: 26; height: 26
            color: "#000000"
            Grid {
                x: 1; y: 1
                columns: 2
                spacing: 1
                Rectangle { width: 11; height: 11; color: "#ff0000" }
                Rectangle { width: 11; height: 11; color: "#00a000" }
                Rectangle { width: 11; height: 11; color: "#0000ff" }
                Rectangle { width: 11; height: 11; color: "#ffcc00" }
            }
        }
        Text {
            text: "Wayland 95"
            color: "#ffffff"
            font.family: sideBar.fontFamily
            font.pixelSize: sideBar.narrow ? 13 : 16
            font.bold: true
        }
        Text {
            text: qsTr("Setup")
            color: "#c0c0ff"
            font.family: sideBar.fontFamily
            font.pixelSize: 11
        }

        // an etched rule under the title
        Item {
            width: parent.width; height: 8
            Rectangle { y: 6; width: parent.width; height: 1; color: "#000040" }
            Rectangle { y: 7; width: parent.width; height: 1; color: "#4060c0" }
        }
    }

    Column {
        anchors.top: header.bottom
        anchors.topMargin: 14
        x: sideBar.narrow ? 4 : 10
        width: parent.width - x - 4
        spacing: 2

        Repeater {
            model: ViewManager
            Item {
                id: step
                readonly property bool current: index === ViewManager.currentStepIndex
                readonly property bool done: index < ViewManager.currentStepIndex
                width: parent.width
                height: Math.max(22, label.implicitHeight + 6)

                Text {
                    id: arrow
                    x: 0
                    anchors.verticalCenter: parent.verticalCenter
                    text: "►"
                    color: "#ffffff"
                    visible: step.current
                    font.family: sideBar.fontFamily
                    font.pixelSize: 11
                }
                Text {
                    id: label
                    x: sideBar.narrow ? 12 : 16
                    width: parent.width - x
                    anchors.verticalCenter: parent.verticalCenter
                    text: display
                    wrapMode: Text.WordWrap
                    color: step.current ? "#ffffff" : step.done ? "#e0e0e0" : "#a0b0e0"
                    font.family: sideBar.fontFamily
                    font.pixelSize: sideBar.narrow ? 11 : 12
                    font.bold: step.current
                }
            }
        }
    }
}
