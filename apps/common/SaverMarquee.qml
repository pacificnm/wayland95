import QtQuick

// Text scrolling across the screen, at a new height each time.
// Options: text, speed (1-10), color, background.
Rectangle {
    id: marquee

    property var view
    readonly property string message: view ? view.option("text", "Wayland 95") : "Wayland 95"
    readonly property real speed: view ? view.option("speed", 5) : 5

    color: view ? view.option("background", "#000000") : "#000000"

    Text {
        id: label
        text: marquee.message
        color: marquee.view ? marquee.view.option("color", "#ffff00") : "#ffff00"
        font.family: "Liberation Sans"
        font.bold: true
        font.pixelSize: Math.max(10, marquee.height / 8)
        x: marquee.width
        y: (marquee.height - height) / 2
    }

    // move left a little each frame; speed 5 crosses the screen in ~5 s
    Timer {
        interval: 16
        repeat: true
        running: marquee.view ? marquee.view.running : false
        onTriggered: {
            label.x -= marquee.width * 0.0008 * marquee.speed * 4;
            if (label.x < -label.width) {
                label.x = marquee.width;
                label.y = Math.random() * Math.max(0, marquee.height - label.height);
            }
        }
    }
}
