import QtQuick
import Win95Ui
import Win95Control

// Mouse Properties: double-click speed, with a test box that only opens
// when you double-click fast enough for the speed you've chosen; and the
// touchpad (tapping, scrolling, clicking, speed), which the desktop sets up
// on the touchpad itself (src/touchpad.cpp).
PropertySheet {
    id: sheet

    title: "Mouse Properties"
    titles: touchpadNames.length ? ["Buttons", "Touchpad"] : ["Buttons"]

    property int interval: Wayland95Settings.doubleClickInterval
    // edited copy of Wayland95Settings.touchpad
    property var touchpad: Wayland95Settings.touchpad
    function setTouchpad(key, value) {
        var copy = Object.assign({}, touchpad);
        copy[key] = value;
        touchpad = copy;
        dirty = true;
    }
    readonly property var touchpadNames: Hardware.touchpads()

    onApplyRequested: {
        Wayland95Settings.doubleClickInterval = interval;
        Wayland95Settings.setTouchpad(touchpad);
    }

    component Label: Text {
        height: 21
        verticalAlignment: Text.AlignVCenter
        font.family: Theme.fontFamily
        font.pixelSize: Theme.fontSize
    }

    Item {
        GroupBox95 {
            id: speedGroup
            width: parent.width
            height: 200
            title: "<u>D</u>ouble-click speed"

            Row {
                y: 8
                spacing: 8
                Text { y: 6; text: "Slow"; font.family: Theme.fontFamily; font.pixelSize: Theme.fontSize }
                Slider95 {
                    id: speed
                    width: 200
                    // left = slow (long interval), right = fast (short)
                    from: 0; to: 10; stepSize: 1; tickCount: 11
                    value: Math.round((900 - sheet.interval) / 70)
                    onMoved: {
                        sheet.interval = 900 - value * 70;
                        sheet.dirty = true;
                    }
                }
                Text { y: 6; text: "Fast"; font.family: Theme.fontFamily; font.pixelSize: Theme.fontSize }
            }

            Text {
                y: 52
                text: "Test area:"
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontSize
            }

            // the test box: a lid that pops open on a quick enough
            // double-click, timed with the slider's (not yet applied) speed
            Bevel {
                id: testArea
                x: (parent.width - width) / 2
                y: 70
                width: 110
                height: 96
                style: "sunken"
                face: Theme.window

                property bool open: false
                property double lastPress: 0

                Item {
                    anchors.horizontalCenter: parent.horizontalCenter
                    y: 6
                    width: 56
                    height: 84

                    // the box
                    Rectangle { x: 4; y: 44; width: 48; height: 36; color: "#c00000"; border.color: "#000000" }
                    Rectangle { x: 18; y: 44; width: 6; height: 36; color: "#ffff00" }
                    Rectangle { x: 32; y: 44; width: 6; height: 36; color: "#ffff00" }
                    // the lid: shut on the box, or flipped up
                    Rectangle {
                        x: testArea.open ? -6 : 2
                        y: testArea.open ? 18 : 38
                        width: 52
                        height: 8
                        rotation: testArea.open ? -60 : 0
                        color: "#a00000"
                        border.color: "#000000"
                        Behavior on y { NumberAnimation { duration: 80 } }
                        Behavior on rotation { NumberAnimation { duration: 80 } }
                    }
                    // what's inside: a spring and a smiling ball
                    Column {
                        visible: testArea.open
                        x: 18
                        y: 0
                        Rectangle {
                            width: 20; height: 20; radius: 10
                            color: "#ffff00"; border.color: "#000000"
                            Text { anchors.centerIn: parent; text: "☺"; font.pixelSize: 16 }
                        }
                        Repeater {
                            model: 4
                            Rectangle { x: index % 2 ? 2 : 10; width: 8; height: 5; color: "transparent"; border.color: "#000000" }
                        }
                    }
                }

                MouseArea {
                    anchors.fill: parent
                    onPressed: {
                        var now = Date.now();
                        if (now - testArea.lastPress <= sheet.interval) {
                            testArea.open = !testArea.open;
                            testArea.lastPress = 0;
                        } else {
                            testArea.lastPress = now;
                        }
                    }
                }
            }
        }

        Text {
            y: speedGroup.height + 10
            width: parent.width
            wrapMode: Text.Wrap
            text: "Double-click the box to test the speed. It applies to Wayland 95 and its programs after you click Apply."
            color: Theme.shadow
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontSize
        }
    }

    // -------------------------------------------------------------- Touchpad
    Item {
        Text {
            id: deviceName
            width: parent.width
            elide: Text.ElideRight
            // inside another desktop, that desktop runs the touchpad
            text: "Touchpad: " + sheet.touchpadNames.join(", ")
                  + (Power.primarySession ? "" : "  (applies when Wayland 95 is your desktop)")
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontSize
        }

        GroupBox95 {
            id: tapGroup
            y: 22
            width: parent.width
            height: 64
            title: "Clicking"
            Column {
                spacing: 4
                CheckBox95 {
                    text: "<u>T</u>ap to click (two fingers: right-click, three: middle)"
                    checked: sheet.touchpad.tapToClick
                    onToggled: (on) => sheet.setTouchpad("tapToClick", on)
                }
                Row {
                    spacing: 8
                    Label { text: "Pressing <u>d</u>own:" }
                    ComboBox95 {
                        width: 220
                        popupMenu: sheet.popupMenu
                        readonly property var methods: ["default", "areas", "fingers"]
                        model: ["As the touchpad does by itself", "Right corner is the right button",
                                "Two fingers right-click"]
                        currentIndex: Math.max(0, methods.indexOf(sheet.touchpad.clickMethod))
                        onActivated: (i) => sheet.setTouchpad("clickMethod", methods[i])
                    }
                }
            }
        }

        GroupBox95 {
            id: scrollGroup
            y: tapGroup.y + tapGroup.height + 6
            width: parent.width
            height: 108
            title: "Scrolling"
            Column {
                spacing: 4
                Row {
                    spacing: 12
                    RadioButton95 { text: "T<u>w</u>o fingers"; checked: sheet.touchpad.scrollMethod === "twofinger"; onClicked: sheet.setTouchpad("scrollMethod", "twofinger") }
                    RadioButton95 { text: "<u>E</u>dge"; checked: sheet.touchpad.scrollMethod === "edge"; onClicked: sheet.setTouchpad("scrollMethod", "edge") }
                    RadioButton95 { text: "<u>O</u>ff"; checked: sheet.touchpad.scrollMethod === "none"; onClicked: sheet.setTouchpad("scrollMethod", "none") }
                }
                CheckBox95 {
                    text: "<u>N</u>atural scrolling (the page follows your fingers)"
                    checked: sheet.touchpad.naturalScroll
                    onToggled: (on) => sheet.setTouchpad("naturalScroll", on)
                }
                Row {
                    spacing: 8
                    Label { text: "Scroll speed:  Slow" }
                    Slider95 {
                        width: 150
                        from: 1; to: 20; stepSize: 1; tickCount: 11
                        value: sheet.touchpad.scrollSpeed
                        onMoved: sheet.setTouchpad("scrollSpeed", value)
                    }
                    Label { text: "Fast" }
                }
            }
        }

        GroupBox95 {
            id: speedGroup2
            y: scrollGroup.y + scrollGroup.height + 6
            width: parent.width
            height: 82
            title: "Pointer speed"
            Column {
                spacing: 4
                Row {
                    spacing: 8
                    Label { text: "Slow" }
                    Slider95 {
                        width: 200
                        from: -10; to: 10; stepSize: 1; tickCount: 11
                        value: sheet.touchpad.speed
                        onMoved: sheet.setTouchpad("speed", value)
                    }
                    Label { text: "Fast" }
                }
                CheckBox95 {
                    text: "<u>A</u>ccelerate: faster movements go further"
                    checked: sheet.touchpad.acceleration
                    onToggled: (on) => sheet.setTouchpad("acceleration", on)
                }
            }
        }

        Column {
            y: speedGroup2.y + speedGroup2.height + 6
            spacing: 4
            CheckBox95 {
                text: "<u>I</u>gnore the touchpad while typing"
                checked: sheet.touchpad.disableWhileTyping
                onToggled: (on) => sheet.setTouchpad("disableWhileTyping", on)
            }
            CheckBox95 {
                text: "Turn it off when a <u>m</u>ouse is plugged in"
                checked: sheet.touchpad.disableWithMouse
                onToggled: (on) => sheet.setTouchpad("disableWithMouse", on)
            }
        }

    }
}
