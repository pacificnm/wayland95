import QtQuick
import Win95Ui
import Win95Photos

// The editing tools beside the photo: rotate and flip, crop, resize, light
// and color (previewed as the sliders move), effects. `host` is the window,
// which does the edits (it loads the photo into the editor first).
Rectangle {
    id: panel

    property var host
    property Menu95 popupMenu
    // the crop frame is out (PhotoView.cropping)
    property bool cropping: false
    property int aspectChoice: 0
    property var aspectNames: []
    readonly property bool adjusting: brightness.value !== 0 || contrast.value !== 0 || saturation.value !== 0

    signal cropRequested()
    signal cropApplied()
    signal cropCancelled()

    color: Theme.face
    width: 196

    function resetSliders() {
        brightness.value = 0;
        contrast.value = 0;
        saturation.value = 0;
        previewTimer.stop();
    }

    // a Win95 group box: an etched frame with its title on the line
    component Group: Item {
        id: group
        property string title: ""
        default property alias content: inner.data
        width: (parent ? parent.width : panel.width) - 12
        x: 6
        height: inner.childrenRect.height + 26
        Rectangle { y: 7; width: parent.width - 1; height: parent.height - 8; color: "transparent"; border.color: Theme.shadow }
        Rectangle { x: 1; y: 8; width: parent.width - 1; height: parent.height - 8; color: "transparent"; border.color: Theme.highlight }
        Rectangle {
            x: 7; y: 0
            width: label.implicitWidth + 6; height: 15
            color: Theme.face
            Text {
                id: label
                x: 3
                text: group.title
                color: Theme.text
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontSize
            }
        }
        Item { id: inner; x: 8; y: 18; width: parent.width - 16 }
    }

    component Labeled: Item {
        id: labeled
        property string text: ""
        property alias slider: s
        property alias value: s.value
        width: parent ? parent.width : 0
        height: 44
        Text {
            text: labeled.text
            color: Theme.text
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontSize
        }
        Text {
            anchors.right: parent.right
            text: (s.value > 0 ? "+" : "") + Math.round(s.value)
            color: Theme.text
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontSize
        }
        Slider95 {
            id: s
            y: 14
            width: parent.width
            from: -100
            to: 100
            stepSize: 1
            tickCount: 9
            onMoved: previewTimer.restart()
        }
    }

    Flickable {
        id: flick
        anchors.fill: parent
        anchors.rightMargin: vbar.visible ? vbar.width : 0
        contentHeight: column.height + 8
        clip: true
        interactive: false

        Column {
            id: column
            y: 6
            width: flick.width
            spacing: 8

            Group {
                title: "Rotate and Flip"
                Row {
                    spacing: 4
                    PhotoButton { icon: "rotateLeft"; tip: "Turns the photo a quarter left (Ctrl+L)."; onClicked: panel.host.edit("rotateLeft") }
                    PhotoButton { icon: "rotateRight"; tip: "Turns the photo a quarter right (Ctrl+R)."; onClicked: panel.host.edit("rotateRight") }
                    PhotoButton { icon: "flipH"; tip: "Flips the photo left to right."; onClicked: panel.host.edit("flipH") }
                    PhotoButton { icon: "flipV"; tip: "Flips the photo upside down."; onClicked: panel.host.edit("flipV") }
                }
            }

            Group {
                title: "Crop"
                Column {
                    spacing: 6
                    width: parent.width
                    Button95 {
                        visible: !panel.cropping
                        width: parent.width
                        text: "Crop..."
                        onClicked: panel.cropRequested()
                    }
                    Text {
                        visible: panel.cropping
                        width: parent.width
                        wrapMode: Text.Wrap
                        text: "Drag a frame over the part to keep."
                        color: Theme.text
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.fontSize
                    }
                    Row {
                        visible: panel.cropping
                        spacing: 6
                        Text {
                            anchors.verticalCenter: parent.verticalCenter
                            text: "Shape:"
                            color: Theme.text
                            font.family: Theme.fontFamily
                            font.pixelSize: Theme.fontSize
                        }
                        ComboBox95 {
                            width: 110
                            model: panel.aspectNames
                            popupMenu: panel.popupMenu
                            currentIndex: panel.aspectChoice
                            onActivated: (i) => panel.aspectChoice = i
                        }
                    }
                    Row {
                        visible: panel.cropping
                        spacing: 6
                        Button95 { width: 77; text: "Crop"; isDefault: true; onClicked: panel.cropApplied() }
                        Button95 { width: 77; text: "Cancel"; onClicked: panel.cropCancelled() }
                    }
                }
            }

            Group {
                title: "Size"
                Button95 {
                    width: parent.width
                    text: "Resize..."
                    enabled: !panel.cropping
                    onClicked: panel.host.showResize()
                }
            }

            Group {
                title: "Light and Color"
                Column {
                    width: parent.width
                    spacing: 2
                    Labeled { id: brightness; text: "Brightness" }
                    Labeled { id: contrast; text: "Contrast" }
                    Labeled { id: saturation; text: "Color saturation" }
                    Row {
                        spacing: 6
                        topPadding: 4
                        Button95 {
                            width: 77
                            text: "Apply"
                            enabled: panel.adjusting
                            onClicked: {
                                previewTimer.stop();
                                panel.host.adjust(Math.round(brightness.value), Math.round(contrast.value),
                                                  Math.round(saturation.value));
                                panel.resetSliders();
                            }
                        }
                        Button95 {
                            width: 77
                            text: "Reset"
                            enabled: panel.adjusting
                            onClicked: {
                                panel.resetSliders();
                                PhotoEditor.cancelPreview();
                            }
                        }
                    }
                }
            }

            Group {
                title: "Effects"
                Column {
                    width: parent.width
                    spacing: 6
                    Button95 { width: parent.width; text: "Auto-Enhance"; onClicked: panel.host.edit("enhance") }
                    Button95 { width: parent.width; text: "Black and White"; onClicked: panel.host.edit("grayscale") }
                    Button95 { width: parent.width; text: "Sepia"; onClicked: panel.host.edit("sepia") }
                }
            }

            Group {
                title: "Changes"
                Column {
                    width: parent.width
                    spacing: 6
                    Button95 {
                        width: parent.width
                        text: "Undo"
                        enabled: PhotoEditor.canUndo
                        onClicked: panel.host.undo()
                    }
                    Button95 {
                        width: parent.width
                        text: "Redo"
                        enabled: PhotoEditor.canRedo
                        onClicked: panel.host.redo()
                    }
                    Button95 {
                        width: parent.width
                        text: "Revert to Saved"
                        enabled: PhotoEditor.modified
                        onClicked: panel.host.revert()
                    }
                }
            }
        }

        WheelHandler {
            acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad
            onWheel: (event) => vbar.scrollBy(-event.angleDelta.y / 120 * 40)
        }
    }

    ScrollBar95 {
        id: vbar
        anchors.right: parent.right
        height: parent.height
        flickable: flick
        stepSize: 20
    }

    // the sliders preview on a screen-sized copy, a moment after they move
    Timer {
        id: previewTimer
        interval: 60
        onTriggered: panel.host.preview(Math.round(brightness.value), Math.round(contrast.value),
                                        Math.round(saturation.value))
    }

    // (the left edge: a raised line between the photo and the tools)
    Rectangle { width: 1; height: parent.height; color: Theme.highlight }
}
