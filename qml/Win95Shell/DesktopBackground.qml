import QtQuick
import Win95Ui as Ui

// The desktop backdrop: a solid color (Win95 teal by default) and an
// optional wallpaper, centered, tiled or stretched - all from the Display
// applet in the Control Panel.
Rectangle {
    id: background
    color: Ui.Wayland95Settings.backgroundColor

    readonly property string mode: Ui.Wayland95Settings.wallpaperMode

    Image {
        anchors.fill: parent
        visible: status === Image.Ready
        source: Ui.Wayland95Settings.wallpaper ? Ui.Win95Fs.toUrl(Ui.Wayland95Settings.wallpaper) : ""
        asynchronous: true
        cache: false
        fillMode: background.mode === "tile" ? Image.Tile
                : background.mode === "stretch" ? Image.Stretch
                : Image.Pad // center
        horizontalAlignment: Image.AlignHCenter
        verticalAlignment: Image.AlignVCenter
        // don't keep a huge photo at full resolution just to stretch it
        sourceSize.width: background.mode === "stretch" ? background.width : 0
        sourceSize.height: background.mode === "stretch" ? background.height : 0
    }
}
