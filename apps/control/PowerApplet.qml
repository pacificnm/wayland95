import QtQuick
import Win95Ui
import Win95Control

// Power: power schemes (turning off the display and sleeping when idle,
// separately on mains and on battery), screen brightness and the power
// mode, the battery, and what the lid and power buttons do.
PropertySheet {
    id: sheet

    title: "Power Properties"
    titles: ["Power Schemes", "Battery", "Advanced"]

    // edited copy of Wayland95Settings.power, written on Apply
    property var settings: Wayland95Settings.power
    function set(key, value) {
        var copy = Object.assign({}, settings);
        copy[key] = value;
        settings = copy;
        dirty = true;
    }
    property bool showMeter: Wayland95Settings.showBattery
    onApplyRequested: {
        Wayland95Settings.setPower(settings);
        Wayland95Settings.showBattery = showMeter;
    }

    readonly property var timeouts: [0, 1, 2, 3, 5, 10, 15, 20, 25, 30, 45, 60, 120, 180, 300]
    function timeoutLabel(m) {
        if (m === 0) return "Never";
        if (m < 60) return "After " + m + (m === 1 ? " min" : " mins");
        return "After " + (m / 60) + (m === 60 ? " hour" : " hours");
    }
    function timeoutIndex(m) {
        var i = timeouts.indexOf(m);
        return i >= 0 ? i : 0;
    }

    // the actions on offer; hibernate only where the computer can
    function actions(ids) {
        var names = { nothing: "Do nothing", ask: "Ask me what to do", display: "Turn off the display",
                      sleep: "Stand by", hibernate: "Hibernate", shutdown: "Shut down" };
        return ids.filter(function (id) {
            return (id !== "hibernate" || Power.canHibernate) && (id !== "sleep" || Power.canSuspend);
        }).map(function (id) { return { id: id, name: names[id] }; });
    }
    readonly property var profileNames: ({ "power-saver": "Power saver", "balanced": "Balanced",
                                           "performance": "High performance" })

    component Label: Text {
        height: 21
        verticalAlignment: Text.AlignVCenter
        font.family: Theme.fontFamily
        font.pixelSize: Theme.fontSize
    }
    component Note: Text {
        wrapMode: Text.Wrap
        color: Theme.shadow
        font.family: Theme.fontFamily
        font.pixelSize: Theme.fontSize
    }
    // a drop-down of choices [{ id, name }] bound to settings[key]
    component Choice: ComboBox95 {
        property string key
        property var choices: []
        width: 170
        popupMenu: sheet.popupMenu
        model: choices.map(function (c) { return c.name; })
        currentIndex: Math.max(0, choices.findIndex(function (c) { return c.id === sheet.settings[key]; }))
        onActivated: (i) => sheet.set(key, choices[i].id)
    }
    component Timeout: ComboBox95 {
        property string key
        width: 120
        popupMenu: sheet.popupMenu
        model: sheet.timeouts.map(sheet.timeoutLabel)
        currentIndex: sheet.timeoutIndex(sheet.settings[key])
        onActivated: (i) => sheet.set(key, sheet.timeouts[i])
    }

    // ------------------------------------------------------- Power Schemes
    Item {
        GroupBox95 {
            id: displayGroup
            width: parent.width
            height: Power.hasBacklight || Power.powerProfiles.length > 0 ? 90 : 0
            visible: height > 0
            title: "Display and performance"

            Grid {
                columns: 2
                columnSpacing: 10
                rowSpacing: 8
                verticalItemAlignment: Grid.AlignVCenter

                Label { text: "Screen <u>b</u>rightness:"; visible: Power.hasBacklight }
                Row {
                    visible: Power.hasBacklight
                    spacing: 6
                    Slider95 {
                        id: brightnessSlider
                        width: 170
                        from: 5
                        to: 100
                        stepSize: 5
                        tickCount: 11
                        // changes right away, like the brightness keys
                        onMoved: Power.setBrightness(value)
                    }
                    Label { text: Power.brightness + "%" }
                }
                Label { text: "Power <u>m</u>ode:"; visible: Power.powerProfiles.length > 0 }
                ComboBox95 {
                    visible: Power.powerProfiles.length > 0
                    width: 170
                    popupMenu: sheet.popupMenu
                    model: Power.powerProfiles.map(function (p) { return sheet.profileNames[p] || p; })
                    currentIndex: Math.max(0, Power.powerProfiles.indexOf(Power.powerProfile))
                    onActivated: (i) => Power.setPowerProfile(Power.powerProfiles[i])
                }
            }
            Binding {
                target: brightnessSlider
                property: "value"
                value: Power.brightness
                when: !brightnessSlider.activeFocus
            }
        }

        GroupBox95 {
            id: schemeGroup
            y: displayGroup.visible ? displayGroup.height + 8 : 0
            width: parent.width
            height: 108
            title: "When the computer isn't being used"

            Grid {
                columns: Battery.present ? 3 : 2
                columnSpacing: 10
                rowSpacing: 8
                verticalItemAlignment: Grid.AlignVCenter

                Item { width: 1; height: 16 }
                Text {
                    text: "Plugged in"
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontSize
                    font.bold: true
                }
                Text {
                    visible: Battery.present
                    text: "On batteries"
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontSize
                    font.bold: true
                }

                Label { text: "Turn off <u>m</u>onitor:" }
                Timeout { key: "displayOffAC" }
                Timeout { key: "displayOffBattery"; visible: Battery.present }

                Label { text: "<u>S</u>tand by:" }
                Timeout { key: "sleepAC"; enabled: Power.canSuspend }
                Timeout { key: "sleepBattery"; visible: Battery.present; enabled: Power.canSuspend }
            }
        }

        Note {
            anchors.bottom: parent.bottom
            width: parent.width
            text: Power.primarySession
                  ? "Programs playing video keep the monitor on and the computer awake."
                  : "Wayland 95 is running inside another desktop, which manages power for now. These "
                    + "settings take effect when Wayland 95 is your desktop."
        }
    }

    // ------------------------------------------------------------- Battery
    Item {
        // a big battery
        Item {
            id: meter
            width: 70
            height: 120
            visible: Battery.present
            Rectangle { x: 22; y: 0; width: 26; height: 8; color: "#000000" }
            Rectangle {
                y: 6
                width: parent.width
                height: parent.height - 6
                color: "#ffffff"
                border.color: "#000000"
                border.width: 2
                Rectangle {
                    x: 4
                    width: parent.width - 8
                    anchors.bottom: parent.bottom
                    anchors.bottomMargin: 4
                    height: (parent.height - 8) * Battery.percent / 100
                    color: Battery.percent <= 20 && !Battery.pluggedIn ? "#ff0000"
                         : Battery.percent <= 40 ? "#ffd000" : "#00a000"
                }
            }
        }
        Column {
            x: meter.width + 20
            spacing: 6
            visible: Battery.present
            Text {
                text: Battery.percent + "%"
                font.family: Theme.fontFamily
                font.pixelSize: 28
                font.bold: true
            }
            Label {
                text: Battery.charging ? "Charging"
                    : Battery.pluggedIn ? "Plugged in, not charging"
                    : "Running on batteries"
            }
            Label {
                visible: Battery.minutesLeft >= 0
                text: {
                    var h = Math.floor(Battery.minutesLeft / 60), m = Battery.minutesLeft % 60;
                    var t = (h > 0 ? h + " hr " : "") + m + " min";
                    return Battery.charging ? t + " until full" : t + " remaining";
                }
            }
        }

        GroupBox95 {
            y: meter.height + 12
            width: parent.width
            height: 96
            visible: Battery.present
            title: "Battery details"
            Grid {
                columns: 2
                columnSpacing: 10
                rowSpacing: 2
                Label { text: "Battery:" }
                Label { text: Battery.model || "(not reported)" }
                Label { text: "Holds when full:" }
                Label { text: Battery.fullWh > 0 ? Battery.fullWh.toFixed(1) + " Wh" : "(not reported)" }
                Label { text: "Health:" }
                Label {
                    text: Battery.designWh > 0 && Battery.fullWh > 0
                          ? Math.round(100 * Battery.fullWh / Battery.designWh) + "% of its capacity when new ("
                            + Battery.designWh.toFixed(1) + " Wh)"
                          : "(not reported)"
                }
            }
        }

        Label {
            visible: !Battery.present
            width: parent.width
            wrapMode: Text.Wrap
            text: "This computer doesn't have a battery."
        }

        CheckBox95 {
            anchors.bottom: parent.bottom
            visible: Battery.present
            text: "Show battery <u>m</u>eter on the taskbar"
            checked: sheet.showMeter
            onToggled: (on) => { sheet.showMeter = on; sheet.dirty = true; }
        }
    }

    // ------------------------------------------------------------ Advanced
    Item {
        Grid {
            columns: 2
            columnSpacing: 10
            rowSpacing: 10
            verticalItemAlignment: Grid.AlignVCenter

            Label { text: "When I close the <u>l</u>id:"; visible: Power.hasLid }
            Choice {
                visible: Power.hasLid
                key: "lidAction"
                choices: sheet.actions(["nothing", "display", "sleep", "hibernate", "shutdown"])
            }
            Label { text: "When I press the <u>p</u>ower button:" }
            Choice {
                key: "powerButton"
                choices: sheet.actions(["ask", "sleep", "hibernate", "shutdown", "nothing"])
            }
            Label { text: "When I press the sl<u>e</u>ep button:" }
            Choice {
                key: "sleepButton"
                choices: sheet.actions(["sleep", "hibernate", "display", "nothing"])
            }
            Label { text: "When the battery is <u>c</u>ritically low:"; visible: Battery.present }
            Choice {
                visible: Battery.present
                key: "criticalBattery"
                choices: sheet.actions(["hibernate", "sleep", "shutdown", "nothing"])
            }
        }

        Note {
            anchors.bottom: parent.bottom
            width: parent.width
            text: (Battery.present ? "Critically low is 5%. You get a warning and a minute to plug in first. " : "")
                  + (Power.canHibernate ? "Hibernate saves everything to disk and turns the computer off; it starts up where you left it."
                                        : "This computer can't hibernate (it needs a swap area as big as its memory).")
        }
    }
}
