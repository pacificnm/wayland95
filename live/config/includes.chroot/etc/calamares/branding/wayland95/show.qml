/* Shown while Wayland 95 is copied to the disk: a few slides about what's
 * in it, each a little Win95 window on the teal desktop.
 */
import QtQuick 2.15
import calamares.slideshow 1.0

Presentation {
    id: presentation

    readonly property string fontFamily: "Liberation Sans"

    // the teal desktop behind every slide
    Rectangle {
        parent: presentation
        anchors.fill: parent
        z: -1
        color: "#008080"
    }

    function onActivate() { advance.running = true }
    function onLeave() { advance.running = false }

    Timer {
        id: advance
        interval: 12000
        repeat: true
        onTriggered: presentation.goToNextSlide()
    }

    // a slide: a window with a title bar, a picture and some text
    component Window95: Rectangle {
        id: win
        property string title
        property string body
        property url picture

        anchors.centerIn: parent
        width: Math.min(parent.width - 40, 520)
        height: content.implicitHeight + 46
        color: "#c0c0c0"

        // raised outer edge
        Rectangle { width: parent.width; height: 1; color: "#dfdfdf" }
        Rectangle { width: 1; height: parent.height; color: "#dfdfdf" }
        Rectangle { y: parent.height - 1; width: parent.width; height: 1; color: "#000000" }
        Rectangle { x: parent.width - 1; width: 1; height: parent.height; color: "#000000" }
        Rectangle { x: 1; y: 1; width: parent.width - 2; height: 1; color: "#ffffff" }
        Rectangle { x: 1; y: 1; width: 1; height: parent.height - 2; color: "#ffffff" }
        Rectangle { x: 1; y: parent.height - 2; width: parent.width - 2; height: 1; color: "#808080" }
        Rectangle { x: parent.width - 2; y: 1; width: 1; height: parent.height - 2; color: "#808080" }

        Rectangle {
            id: titleBar
            x: 3; y: 3
            width: parent.width - 6
            height: 18
            gradient: Gradient {
                orientation: Gradient.Horizontal
                GradientStop { position: 0.0; color: "#000080" }
                GradientStop { position: 1.0; color: "#1084d0" }
            }
            Text {
                x: 4
                anchors.verticalCenter: parent.verticalCenter
                text: win.title
                color: "#ffffff"
                font.family: presentation.fontFamily
                font.pixelSize: 11
                font.bold: true
            }
        }

        Row {
            id: content
            x: 16
            y: titleBar.y + titleBar.height + 14
            spacing: 16
            Image {
                id: pictureImage
                source: win.picture
                visible: win.picture != ""
                // welcome.png is drawn at 2x; show it pixel for pixel
                width: 150
                height: 116
                smooth: false
            }
            Text {
                width: win.width - 32 - (pictureImage.visible ? pictureImage.width + content.spacing : 0)
                text: win.body
                wrapMode: Text.WordWrap
                textFormat: Text.StyledText
                color: "#000000"
                font.family: presentation.fontFamily
                font.pixelSize: 12
                lineHeight: 1.15
            }
        }
    }

    Slide {
        Window95 {
            title: qsTr("Welcome to Wayland 95")
            picture: "welcome.png"
            body: qsTr("<b>Wayland 95 is being installed on your computer.</b><br><br>"
                       + "This takes a few minutes. When it's done, restart your computer, "
                       + "remove the installation media and log on with the account you just made.")
        }
    }
    Slide {
        Window95 {
            title: qsTr("The Start menu")
            body: qsTr("<b>Everything starts with the Start button.</b><br><br>"
                       + "Programs lists every program on the computer, Documents your recent files, "
                       + "and Settings opens Control Panel. Find, Help and Run... are there too, "
                       + "and Shut Down... when you're finished.")
        }
    }
    Slide {
        Window95 {
            title: qsTr("Programs included")
            body: qsTr("<b>Wayland 95 has its own programs:</b><br><br>"
                       + "File Manager, Notepad, Terminal, System Monitor, the Image Writer and "
                       + "Control Panel, all in the same style. Programs made for other desktops "
                       + "work too, including X11 programs.")
        }
    }
    Slide {
        Window95 {
            title: qsTr("Adding programs")
            body: qsTr("<b>Get more programs with Add/Remove Programs.</b><br><br>"
                       + "Open Control Panel > Add/Remove Programs to install from Debian's "
                       + "collection or from Flathub. Its Updates tab keeps the system and "
                       + "Wayland 95 itself up to date, and Wayland 95 tells you when updates are ready.")
        }
    }
}
