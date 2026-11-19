import QtQuick
import Win95Ui
import Win95Control

// Keyboard Properties: key repeat delay and rate, and the keyboard layout.
// The compositor applies these to every program (see SeatSettings).
PropertySheet {
    id: sheet

    title: "Keyboard Properties"
    titles: ["Speed", "Language"]

    property int delay: Wayland95Settings.keyRepeatDelay
    property int rate: Wayland95Settings.keyRepeatRate
    property string layout: Wayland95Settings.keyboardLayout
    property int flashTime: Wayland95Settings.cursorFlashTime

    // the four repeat delays Win95 offered, long to short
    readonly property var delays: [1000, 750, 500, 250]
    function delayIndex(ms) {
        var best = 0;
        for (var i = 1; i < delays.length; ++i)
            if (Math.abs(delays[i] - ms) < Math.abs(delays[best] - ms))
                best = i;
        return best;
    }

    readonly property var layouts: [{ code: "", name: "(Default)" }].concat(SystemInfo.keyboardLayouts())
    function layoutIndex(code) {
        for (var i = 0; i < layouts.length; ++i)
            if (layouts[i].code === code)
                return i;
        return 0;
    }

    onApplyRequested: {
        Wayland95Settings.keyRepeatDelay = delay;
        Wayland95Settings.keyRepeatRate = rate;
        Wayland95Settings.keyboardLayout = layout;
        Wayland95Settings.cursorFlashTime = flashTime;
    }

    // ------------------------------------------------------------ Speed tab
    Item {
        GroupBox95 {
            id: repeatGroup
            width: parent.width
            height: 150
            title: "Character repeat"

            Column {
                spacing: 10
                width: parent.width

                Text {
                    textFormat: Text.StyledText
                    text: "Repeat <u>d</u>elay:"
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontSize
                }
                Row {
                    spacing: 8
                    Text { y: 6; text: "Long"; font.family: Theme.fontFamily; font.pixelSize: Theme.fontSize }
                    Slider95 {
                        width: 220
                        from: 0; to: 3; stepSize: 1; tickCount: 4
                        value: sheet.delayIndex(sheet.delay)
                        onMoved: { sheet.delay = sheet.delays[value]; sheet.dirty = true; }
                    }
                    Text { y: 6; text: "Short"; font.family: Theme.fontFamily; font.pixelSize: Theme.fontSize }
                }
                Text {
                    textFormat: Text.StyledText
                    text: "Repeat <u>r</u>ate:"
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontSize
                }
                Row {
                    spacing: 8
                    Text { y: 6; text: "Slow"; font.family: Theme.fontFamily; font.pixelSize: Theme.fontSize }
                    Slider95 {
                        width: 220
                        from: 2; to: 40; stepSize: 1; tickCount: 11
                        value: sheet.rate
                        onMoved: { sheet.rate = value; sheet.dirty = true; }
                    }
                    Text { y: 6; text: "Fast"; font.family: Theme.fontFamily; font.pixelSize: Theme.fontSize }
                }
            }
        }

        Column {
            id: testColumn
            y: repeatGroup.height + 10
            width: parent.width
            spacing: 4
            Text {
                textFormat: Text.StyledText
                text: "Click here and hold down a key to <u>t</u>est repeat rate:"
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontSize
            }
            TextField95 { width: parent.width }
        }

        GroupBox95 {
            id: blinkGroup
            y: testColumn.y + testColumn.height + 10
            width: parent.width
            height: 62
            title: "Cursor <u>b</u>link rate"

            Row {
                y: 4
                spacing: 8
                // a preview cursor blinking at the slider's speed
                Rectangle {
                    y: 2
                    width: 2
                    height: 18
                    color: Theme.text
                    Timer {
                        interval: Math.max(100, sheet.flashTime / 2)
                        running: sheet.visible
                        repeat: true
                        onTriggered: parent.visible = !parent.visible
                    }
                }
                Item { width: 6; height: 1 }
                Text { y: 6; text: "Slow"; font.family: Theme.fontFamily; font.pixelSize: Theme.fontSize }
                Slider95 {
                    width: 200
                    from: 0; to: 10; stepSize: 1; tickCount: 11
                    // one blink cycle: 2000 ms (slow) to 300 ms (fast)
                    value: Math.round((2000 - sheet.flashTime) / 170)
                    onMoved: { sheet.flashTime = 2000 - value * 170; sheet.dirty = true; }
                }
                Text { y: 6; text: "Fast"; font.family: Theme.fontFamily; font.pixelSize: Theme.fontSize }
            }
        }

        Text {
            anchors.bottom: parent.bottom
            width: parent.width
            wrapMode: Text.Wrap
            text: "New settings take effect when you click Apply."
            color: Theme.shadow
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontSize
        }
    }

    // --------------------------------------------------------- Language tab
    Item {
        Text {
            id: layoutLabel
            textFormat: Text.StyledText
            text: "Keyboard <u>l</u>ayout:"
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontSize
        }
        Bevel {
            y: layoutLabel.height + 6
            width: parent.width
            height: parent.height - y - 40
            style: "sunken"
            face: Theme.window

            ListView {
                id: layoutList
                x: 2; y: 2
                width: parent.width - 4 - (layoutBar.visible ? layoutBar.width : 0)
                height: parent.height - 4
                clip: true
                interactive: false
                model: sheet.layouts
                currentIndex: sheet.layoutIndex(sheet.layout)
                keyNavigationEnabled: false
                activeFocusOnTab: true
                Component.onCompleted: positionViewAtIndex(currentIndex, ListView.Center)
                onCurrentIndexChanged: positionViewAtIndex(currentIndex, ListView.Contain)
                function pick(i) {
                    if (i < 0 || i >= sheet.layouts.length)
                        return;
                    sheet.layout = sheet.layouts[i].code;
                    sheet.dirty = true;
                }
                Keys.onUpPressed: pick(currentIndex - 1)
                Keys.onDownPressed: pick(currentIndex + 1)

                delegate: Rectangle {
                    required property var modelData
                    required property int index
                    width: layoutList.width
                    height: 16
                    color: ListView.isCurrentItem ? Theme.selection : "transparent"
                    Text {
                        x: 3
                        width: parent.width - 6
                        anchors.verticalCenter: parent.verticalCenter
                        text: parent.modelData.code
                              ? parent.modelData.name + "  (" + parent.modelData.code + ")"
                              : parent.modelData.name
                        elide: Text.ElideRight
                        color: parent.ListView.isCurrentItem ? Theme.selectionText : Theme.text
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.fontSize
                    }
                    MouseArea {
                        anchors.fill: parent
                        onPressed: {
                            layoutList.forceActiveFocus();
                            layoutList.pick(parent.index);
                        }
                    }
                }

                WheelHandler { acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad; onWheel: (event) => layoutBar.scrollBy(-event.angleDelta.y / 120 * 48) }
            }
            ScrollBar95 {
                id: layoutBar
                anchors.right: parent.right
                anchors.rightMargin: 2
                y: 2
                height: parent.height - 4
                flickable: layoutList
                stepSize: 16
            }
        }
        Text {
            anchors.bottom: parent.bottom
            width: parent.width
            wrapMode: Text.Wrap
            text: "(Default) keeps the layout Wayland 95 started with. The layout applies to every program."
            color: Theme.shadow
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontSize
        }
    }
}
