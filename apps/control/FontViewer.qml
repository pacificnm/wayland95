import QtQuick
import QtQuick.Window
import Win95Ui
import Win95Control

// One font, shown the way Win95's font viewer did: its name and details,
// the alphabet, then a line of text at sizes from 12 to 72.
Window {
    id: viewer

    required property var font   // an entry from Fonts.fonts

    width: 600
    height: 560
    minimumWidth: 360
    minimumHeight: 260
    visible: true
    color: Theme.face
    title: font.name + " (" + font.format + ")"

    // that exact file, so the face shown is this one even when another
    // font has the same family name
    readonly property string family: Fonts.previewFamily(font.file) || font.family
    readonly property string sample: "The quick brown fox jumps over the lazy dog. 1234567890"
    readonly property var sizes: [12, 18, 24, 36, 48, 60, 72]

    Shortcut { sequence: "Esc"; onActivated: viewer.close() }

    Row {
        id: toolbar
        x: 6
        y: 6
        spacing: 6
        Button95 {
            text: "<u>D</u>one"
            isDefault: true
            onClicked: viewer.close()
        }
    }

    Bevel {
        id: page
        anchors.top: toolbar.bottom
        anchors.topMargin: 6
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        style: "sunken"
        face: "#ffffff"

        Flickable {
            id: flick
            x: 2
            y: 2
            width: parent.width - 4 - (bar.needed ? bar.width : 0)
            height: parent.height - 4
            clip: true
            interactive: false
            contentHeight: content.height + 20
            contentWidth: Math.max(width, content.width + 24)

            Column {
                id: content
                x: 12
                y: 10
                spacing: 6

                Text {
                    text: viewer.font.name + " (" + viewer.font.format + ")"
                    font.family: viewer.family
                    font.styleName: viewer.font.style
                    font.pixelSize: 28
                }
                Grid {
                    columns: 2
                    columnSpacing: 12
                    rowSpacing: 2
                    Repeater {
                        model: [
                            ["Typeface name:", viewer.font.family + (viewer.font.style ? " " + viewer.font.style : "")],
                            ["File:", viewer.font.file],
                            ["File size:", Math.max(1, Math.round(viewer.font.size / 1024)) + " KB"],
                            ["Version:", viewer.font.version || "(not given)"],
                            ["Installed:", viewer.font.mine ? "By you (in your fonts folder)" : "With the system"],
                        ].reduce(function (all, pair) { return all.concat(pair); }, [])
                        delegate: Text {
                            required property var modelData
                            text: modelData
                            font.family: Theme.fontFamily
                            font.pixelSize: Theme.fontSize
                        }
                    }
                }

                Rectangle { width: flick.width - 24; height: 1; color: "#000000" }

                Column {
                    spacing: 2
                    Repeater {
                        model: ["abcdefghijklmnopqrstuvwxyz", "ABCDEFGHIJKLMNOPQRSTUVWXYZ", "1234567890.:,;(\"*!?')"]
                        Text {
                            required property string modelData
                            text: modelData
                            textFormat: Text.PlainText
                            font.family: viewer.family
                            font.styleName: viewer.font.style
                            font.pixelSize: 20
                        }
                    }
                }

                Rectangle { width: flick.width - 24; height: 1; color: "#000000" }

                Repeater {
                    model: viewer.sizes
                    Row {
                        required property int modelData
                        spacing: 10
                        Text {
                            width: 22
                            anchors.baseline: line.baseline
                            text: parent.modelData
                            font.family: Theme.fontFamily
                            font.pixelSize: Theme.fontSize
                        }
                        Text {
                            id: line
                            text: viewer.sample
                            textFormat: Text.PlainText
                            font.family: viewer.family
                            font.styleName: viewer.font.style
                            // points, as Win95 listed them (at 96 dpi)
                            font.pixelSize: Math.round(parent.modelData * 96 / 72)
                        }
                    }
                }
            }

            WheelHandler { acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad; onWheel: (event) => bar.scrollBy(-event.angleDelta.y / 120 * 60) }
        }
        ScrollBar95 {
            id: bar
            visible: needed
            anchors.right: parent.right
            anchors.rightMargin: 2
            y: 2
            height: parent.height - 4
            flickable: flick
            stepSize: 30
        }
    }
}
