import QtQuick

// Generic Win95-flavored file icons, drawn from rectangles (no artwork).
// Designed on a 32x32 grid and scaled for the 16px details view.
Item {
    id: icon

    property string kind: "file" // folder home drive removable file text image audio video archive pdf exec
    property bool symlink: false
    property bool selected: false
    property int size: 32

    width: size
    height: size

    Item {
        id: art
        width: 32
        height: 32
        scale: icon.size / 32
        transformOrigin: Item.TopLeft

        readonly property bool isPage: ["file", "text", "image", "audio", "video", "archive", "pdf"].indexOf(icon.kind) >= 0

        // --- folder / home -------------------------------------------------
        Item {
            visible: icon.kind === "folder" || icon.kind === "home"
            anchors.fill: parent
            Rectangle { x: 2; y: 6; width: 12; height: 5; color: "#e8c048"; border.color: "#806000" }
            Rectangle { x: 2; y: 9; width: 28; height: 19; color: "#f8d878"; border.color: "#806000" }
            Rectangle { x: 3; y: 10; width: 26; height: 1; color: "#fff0b0" }
            Text {
                visible: icon.kind === "home"
                anchors.horizontalCenter: parent.horizontalCenter
                y: 9
                text: "⌂"
                color: "#004080"
                font.pixelSize: 17
                font.bold: true
            }
        }

        // --- drive -----------------------------------------------------------
        Item {
            visible: icon.kind === "drive"
            anchors.fill: parent
            Rectangle { x: 1; y: 11; width: 30; height: 13; color: "#c0c0c0"; border.color: "#000000" }
            Rectangle { x: 2; y: 12; width: 28; height: 1; color: "#ffffff" }
            Rectangle { x: 4; y: 19; width: 16; height: 2; color: "#808080" }
            Rectangle { x: 25; y: 18; width: 3; height: 3; color: "#00e000"; border.color: "#006000" }
        }

        // --- removable: a USB stick ------------------------------------------
        Item {
            visible: icon.kind === "removable"
            anchors.fill: parent
            Rectangle { x: 22; y: 12; width: 8; height: 8; color: "#c0c0c0"; border.color: "#000000" }
            Rectangle { x: 25; y: 14; width: 2; height: 1; color: "#000000" }
            Rectangle { x: 25; y: 17; width: 2; height: 1; color: "#000000" }
            Rectangle { x: 2; y: 9; width: 21; height: 14; radius: 2; color: "#2050b0"; border.color: "#000000" }
            Rectangle { x: 4; y: 11; width: 17; height: 2; color: "#5080e0" }
            Rectangle { x: 5; y: 16; width: 10; height: 3; color: "#ffffff"; border.color: "#000040" }
            Rectangle { x: 18; y: 16; width: 2; height: 2; color: "#00e000" }
        }

        // --- executable: a little application window -------------------------
        Item {
            visible: icon.kind === "exec"
            anchors.fill: parent
            Rectangle { x: 2; y: 5; width: 28; height: 22; color: "#ffffff"; border.color: "#000000" }
            Rectangle { x: 3; y: 6; width: 26; height: 5; color: "#000080" }
            Rectangle { x: 25; y: 7; width: 3; height: 3; color: "#c0c0c0" }
            Rectangle { x: 6; y: 15; width: 20; height: 2; color: "#c0c0c0" }
            Rectangle { x: 6; y: 20; width: 14; height: 2; color: "#c0c0c0" }
        }

        // --- documents: page with a folded corner ----------------------------
        Item {
            visible: art.isPage
            anchors.fill: parent

            Rectangle { x: 6; y: 1; width: 21; height: 30; color: "#ffffff"; border.color: "#000000" }
            // folded corner
            Rectangle { x: 21; y: 1; width: 6; height: 6; color: "#c0c0c0"; border.color: "#000000" }

            // text lines
            Column {
                visible: icon.kind === "text" || icon.kind === "file"
                x: 9; y: 9
                spacing: 3
                Repeater {
                    model: icon.kind === "text" ? 6 : 0
                    Rectangle { width: index % 3 === 2 ? 9 : 15; height: 1; color: "#000080" }
                }
            }
            // image: sky, sun and a hill
            Item {
                visible: icon.kind === "image"
                x: 9; y: 10; width: 15; height: 15
                Rectangle { anchors.fill: parent; color: "#80c0ff"; border.color: "#000080" }
                Rectangle { x: 9; y: 3; width: 4; height: 4; radius: 2; color: "#ffff00" }
                Rectangle { x: 1; y: 9; width: 13; height: 5; color: "#008000" }
            }
            // audio: a note
            Text {
                visible: icon.kind === "audio"
                x: 9; y: 6
                text: "♫"
                color: "#800080"
                font.pixelSize: 18
            }
            // video: film strip
            Item {
                visible: icon.kind === "video"
                x: 9; y: 9; width: 15; height: 16
                Rectangle { anchors.fill: parent; color: "#000000" }
                Rectangle { x: 4; y: 3; width: 7; height: 10; color: "#4080c0" }
                Column {
                    x: 1; y: 1; spacing: 2
                    Repeater { model: 5; Rectangle { width: 2; height: 1; color: "#ffffff" } }
                }
                Column {
                    x: 12; y: 1; spacing: 2
                    Repeater { model: 5; Rectangle { width: 2; height: 1; color: "#ffffff" } }
                }
            }
            // archive: a zipper
            Column {
                visible: icon.kind === "archive"
                x: 15; y: 3
                Repeater {
                    model: 9
                    Rectangle { width: 3; height: 3; color: index % 2 ? "#808080" : "#404040" }
                }
            }
            // pdf: red band
            Rectangle {
                visible: icon.kind === "pdf"
                x: 7; y: 13; width: 19; height: 8
                color: "#c00000"
                Text {
                    anchors.centerIn: parent
                    text: "PDF"
                    color: "white"
                    font.pixelSize: 7
                    font.bold: true
                }
            }
        }

        // shortcut arrow overlay, bottom-left
        Rectangle {
            visible: icon.symlink
            x: 0; y: 20; width: 11; height: 11
            color: "#ffffff"
            border.color: "#000000"
            Text {
                anchors.centerIn: parent
                text: "↗"
                color: "#000000"
                font.pixelSize: 10
                font.bold: true
            }
        }
    }

    // selected icons get a navy wash, like Win95's dithered highlight
    Rectangle {
        visible: icon.selected
        anchors.fill: parent
        color: "#000080"
        opacity: 0.45
    }
}
