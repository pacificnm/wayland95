import QtQuick
import QtQuick.Window
import Win95Shell

// The "Starting Wayland 95..." screen at login: shown by main.cpp before the
// desktop's QML loads (which takes a few seconds), so the screen isn't left
// empty, and faded out once the desktop has drawn itself. Kept small and
// plain (no Win95Ui) so it's up quickly.
//
// The band along the bottom moves with an Animator, which runs on the
// render thread: it keeps going while the desktop loads and the shell's own
// thread is busy (with a software renderer it may stand still instead).
Window {
    id: splash
    screen: ShellSession.mainScreen(Qt.application.screens, ShellSession.primaryOutput)
    LayerShell.layer: LayerShell.Overlay
    LayerShell.anchors: LayerShell.AnchorTop | LayerShell.AnchorBottom | LayerShell.AnchorLeft | LayerShell.AnchorRight
    LayerShell.exclusiveZone: -1
    LayerShell.keyboard: LayerShell.KeyboardNone
    LayerShell.scope: "splash"
    title: "Starting Wayland 95"
    color: "transparent"
    width: 640; height: 480 // (the compositor gives the real size)
    visible: true

    // the desktop has drawn itself: fade out, then go
    function finish() {
        if (!fade.running && content.opacity > 0)
            fade.start();
    }
    signal finished()

    Rectangle {
        id: content
        anchors.fill: parent
        color: "#000000"

        Column {
            anchors.centerIn: parent
            anchors.verticalCenterOffset: -30
            spacing: 28

            Row {
                anchors.horizontalCenter: parent.horizontalCenter
                spacing: 22

                // the four-color square of the Start button, large
                Grid {
                    anchors.verticalCenter: parent.verticalCenter
                    columns: 2
                    spacing: 6
                    Rectangle { width: 36; height: 36; color: "#ff0000" }
                    Rectangle { width: 36; height: 36; color: "#00a000" }
                    Rectangle { width: 36; height: 36; color: "#0000ff" }
                    Rectangle { width: 36; height: 36; color: "#ffcc00" }
                }
                Row {
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: 10
                    Text {
                        text: "Wayland"
                        color: "#c0c0c0"
                        font.family: "Liberation Sans"
                        font.pixelSize: 64
                        font.bold: true
                    }
                    Text {
                        text: "95"
                        color: "#ffffff"
                        font.family: "Liberation Sans"
                        font.pixelSize: 64
                        font.bold: true
                    }
                }
            }

            Text {
                anchors.horizontalCenter: parent.horizontalCenter
                text: "Starting Wayland 95..."
                color: "#c0c0c0"
                font.family: "Liberation Sans"
                font.pixelSize: 18
            }
        }

        // the moving color band along the bottom
        Item {
            id: band
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            height: 10
            clip: true

            Row {
                id: strip
                height: parent.height
                // two copies side by side, moved by one: seamless
                Repeater {
                    model: 2
                    Rectangle {
                        width: band.width
                        height: band.height
                        gradient: Gradient {
                            orientation: Gradient.Horizontal
                            GradientStop { position: 0.0; color: "#000080" }
                            GradientStop { position: 0.25; color: "#1084d0" }
                            GradientStop { position: 0.5; color: "#008080" }
                            GradientStop { position: 0.75; color: "#1084d0" }
                            GradientStop { position: 1.0; color: "#000080" }
                        }
                    }
                }
            }
            XAnimator {
                id: moving
                target: strip
                from: -band.width
                to: 0
                duration: 1600
                loops: Animation.Infinite
                running: band.width > 0
            }
        }
    }

    OpacityAnimator {
        id: fade
        target: content
        from: 1
        to: 0
        duration: 300
        // still, then hidden, so it isn't drawn while it's taken apart
        onFinished: {
            moving.stop();
            Qt.callLater(() => { splash.visible = false; splash.finished(); });
        }
    }
}
