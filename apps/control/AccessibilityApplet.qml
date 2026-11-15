import QtQuick
import Win95Ui
import Win95Control

// Accessibility Options, like Win95's: Keyboard (StickyKeys, FilterKeys,
// ToggleKeys), Sound (SoundSentry, ShowSounds), Display (High Contrast),
// Mouse (MouseKeys) and General. The desktop does the work (the
// compositor sees every key and click first); see src/accessibility.cpp.
PropertySheet {
    id: sheet

    title: "Accessibility Properties"
    titles: ["Keyboard", "Sound", "Display", "Mouse", "General"]

    // edited copy, written on Apply
    property var settings: Wayland95Settings.accessibility
    function set(key, value) {
        var copy = Object.assign({}, settings);
        copy[key] = value;
        settings = copy;
        dirty = true;
    }
    onApplyRequested: {
        // High Contrast also changes the color scheme
        var copy = Object.assign({}, settings);
        var contrast = copy.highContrast;
        copy.highContrast = Wayland95Settings.accessibility.highContrast;
        copy.schemeBeforeHighContrast = Wayland95Settings.accessibility.schemeBeforeHighContrast;
        Wayland95Settings.setAccessibility(copy);
        if (contrast !== copy.highContrast || contrast)
            Wayland95Settings.setHighContrast(contrast);
    }

    component Para: Text {
        width: parent ? parent.width : 300
        wrapMode: Text.Wrap
        font.family: Theme.fontFamily
        font.pixelSize: Theme.fontSize
    }
    // one feature: a group with what it does, Use..., and Settings...
    component Feature: GroupBox95 {
        id: feature
        property string key
        property string useText
        property string about
        property Item dialog: null
        width: parent ? parent.width : 300
        height: 92
        Para {
            id: aboutText
            width: parent.width
            text: feature.about
        }
        CheckBox95 {
            anchors.bottom: parent.bottom
            text: feature.useText
            checked: !!sheet.settings[feature.key]
            onToggled: (on) => sheet.set(feature.key, on)
        }
        Button95 {
            visible: !!feature.dialog
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            text: "<u>S</u>ettings..."
            onClicked: feature.dialog.open()
        }
    }

    // -------------------------------------------------------------- Keyboard
    Column {
        spacing: 8
        Feature {
            key: "stickyKeys"
            title: "StickyKeys"
            useText: "Use <u>S</u>tickyKeys"
            about: "Use StickyKeys if you want to use Shift, Ctrl, Alt or the Windows key by pressing one key at a time."
            dialog: stickyDialog
        }
        Feature {
            key: "filterKeys"
            title: "FilterKeys"
            useText: "Use <u>F</u>ilterKeys"
            about: "Use FilterKeys if you want the computer to ignore brief or repeated keystrokes."
            dialog: filterDialog
        }
        Feature {
            key: "toggleKeys"
            title: "ToggleKeys"
            useText: "Use <u>T</u>oggleKeys"
            about: "Use ToggleKeys if you want to hear tones when pressing Caps Lock, Num Lock and Scroll Lock."
            dialog: toggleDialog
        }
    }

    // ----------------------------------------------------------------- Sound
    Column {
        spacing: 8
        Feature {
            key: "soundSentry"
            title: "SoundSentry"
            useText: "Use Sound<u>S</u>entry"
            about: "Use SoundSentry if you want the desktop to show something you can see whenever it makes a sound."
            dialog: sentryDialog
        }
        Feature {
            key: "showSounds"
            title: "ShowSounds"
            useText: "Use Show<u>S</u>ounds"
            about: "Use ShowSounds to show a caption naming each sound the desktop and its programs make."
        }
    }

    // --------------------------------------------------------------- Display
    Column {
        spacing: 8
        Feature {
            key: "highContrast"
            title: "High Contrast"
            useText: "Use <u>H</u>igh Contrast"
            about: "Use this option if you want the desktop and its programs to use colors and fonts designed for easy reading."
            dialog: contrastDialog
        }
    }

    // ----------------------------------------------------------------- Mouse
    Column {
        spacing: 8
        Feature {
            key: "mouseKeys"
            title: "MouseKeys"
            useText: "Use <u>M</u>ouseKeys"
            about: "Use MouseKeys if you want to control the mouse pointer using the numeric keypad on your keyboard."
            dialog: mouseDialog
        }
        Para {
            color: Theme.shadow
            text: "The keypad's 1-4 and 6-9 move the pointer, 5 clicks and + double-clicks. / chooses the left button, "
                  + "- the right one and * the middle one. 0 holds the button down for dragging, and . lets it go."
        }
    }

    // --------------------------------------------------------------- General
    Column {
        spacing: 8
        GroupBox95 {
            width: parent.width
            height: 62
            title: "Automatic reset"
            CheckBox95 {
                id: resetBox
                text: "<u>T</u>urn off accessibility features after idle for:"
                checked: sheet.settings.autoReset
                onToggled: (on) => sheet.set("autoReset", on)
            }
            ComboBox95 {
                y: 22
                x: 20
                width: 110
                enabled: resetBox.checked
                popupMenu: sheet.popupMenu
                readonly property var minutes: [5, 10, 15, 20, 25, 30]
                model: minutes.map(function (m) { return m + " minutes"; })
                currentIndex: Math.max(0, minutes.indexOf(sheet.settings.autoResetMinutes))
                onActivated: (i) => sheet.set("autoResetMinutes", minutes[i])
            }
        }
        GroupBox95 {
            width: parent.width
            height: 62
            title: "Notification"
            Column {
                spacing: 4
                CheckBox95 {
                    text: "Give <u>w</u>arning message when turning a feature on or off"
                    checked: sheet.settings.notifyMessage
                    onToggled: (on) => sheet.set("notifyMessage", on)
                }
                CheckBox95 {
                    text: "Make a s<u>o</u>und when turning a feature on or off"
                    checked: sheet.settings.notifySound
                    onToggled: (on) => sheet.set("notifySound", on)
                }
            }
        }
        Para {
            color: Theme.shadow
            text: "Notification covers the shortcut keys, which turn features on and off from anywhere. "
                  + "Each feature's Settings... says which keys it uses."
        }
    }

    // ------------------------------------------------------ Settings dialogs
    component SettingsDialog: Dialog95 {
        id: dlg
        dialogWidth: 360
        default property alias body: inner.data
        function open() { show(); }
        Column {
            id: inner
            x: 12
            y: 12
            width: parent.width - 24
            spacing: 8
        }
        Button95 {
            anchors.right: parent.right
            anchors.rightMargin: 10
            anchors.bottom: parent.bottom
            anchors.bottomMargin: 10
            text: "OK"
            isDefault: true
            onClicked: dlg.close()
        }
    }
    component ShortcutBox: GroupBox95 {
        id: box
        property string key
        property string keys
        property string feature
        width: parent ? parent.width : 300
        height: 70
        title: "Keyboard shortcut"
        Para { width: box.width - 16; text: "The shortcut for " + box.feature + " is: " + box.keys }
        CheckBox95 {
            anchors.bottom: parent.bottom
            text: "<u>U</u>se shortcut"
            checked: !!sheet.settings[box.key]
            onToggled: (on) => sheet.set(box.key, on)
        }
    }

    extras: [
        SettingsDialog {
            id: stickyDialog
            title: "Settings for StickyKeys"
            dialogHeight: 236
            ShortcutBox { key: "stickyShortcut"; feature: "StickyKeys"; keys: "Press Shift five times." }
            GroupBox95 {
                width: parent.width
                height: 82
                title: "Options"
                Column {
                    spacing: 4
                    CheckBox95 {
                        width: 300
                        text: "<u>P</u>ress modifier key twice to lock"
                        checked: sheet.settings.stickyLock
                        onToggled: (on) => sheet.set("stickyLock", on)
                    }
                    CheckBox95 {
                        text: "<u>M</u>ake sounds when a modifier key is pressed"
                        checked: sheet.settings.stickySound
                        onToggled: (on) => sheet.set("stickySound", on)
                    }
                }
            }
        },
        SettingsDialog {
            id: filterDialog
            title: "Settings for FilterKeys"
            dialogHeight: 290
            ShortcutBox { key: "filterShortcut"; feature: "FilterKeys"; keys: "Hold down the right Shift key for eight seconds." }
            GroupBox95 {
                id: filterGroup
                width: parent.width
                height: 128
                title: "Filter options"
                readonly property var bounceTimes: [0, 500, 700, 1000, 1500, 2000]
                readonly property var slowTimes: [0, 250, 500, 750, 1000, 1500, 2000]
                Grid {
                    columns: 2
                    columnSpacing: 8
                    rowSpacing: 6
                    verticalItemAlignment: Grid.AlignVCenter
                    Text { text: "Ignore keys pressed again within:"; font.family: Theme.fontFamily; font.pixelSize: Theme.fontSize }
                    ComboBox95 {
                        width: 96
                        popupMenu: sheet.popupMenu
                        model: filterGroup.bounceTimes.map(function (t) { return t ? (t / 1000) + " seconds" : "(off)"; })
                        currentIndex: Math.max(0, filterGroup.bounceTimes.indexOf(sheet.settings.filterBounceMs))
                        onActivated: (i) => sheet.set("filterBounceMs", filterGroup.bounceTimes[i])
                    }
                    Text { text: "Accept keys only held for:"; font.family: Theme.fontFamily; font.pixelSize: Theme.fontSize }
                    ComboBox95 {
                        width: 96
                        popupMenu: sheet.popupMenu
                        model: filterGroup.slowTimes.map(function (t) { return t ? (t / 1000) + " seconds" : "(off)"; })
                        currentIndex: Math.max(0, filterGroup.slowTimes.indexOf(sheet.settings.filterSlowMs))
                        onActivated: (i) => sheet.set("filterSlowMs", filterGroup.slowTimes[i])
                    }
                }
                Text {
                    anchors.bottom: parent.bottom
                    width: parent.width
                    wrapMode: Text.Wrap
                    color: Theme.shadow
                    text: "These take effect once you click Apply; try them in any program."
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontSize
                }
            }
        },
        SettingsDialog {
            id: toggleDialog
            title: "Settings for ToggleKeys"
            dialogHeight: 130
            ShortcutBox { key: "toggleShortcut"; feature: "ToggleKeys"; keys: "Hold down Num Lock for five seconds." }
        },
        SettingsDialog {
            id: sentryDialog
            title: "Settings for SoundSentry"
            dialogHeight: 130
            Text { text: "Warning for windowed programs:"; font.family: Theme.fontFamily; font.pixelSize: Theme.fontSize }
            ComboBox95 {
                width: 220
                popupMenu: sheet.popupMenu
                readonly property var modes: ["caption", "window", "desktop"]
                model: ["Flash active caption bar", "Flash active window", "Flash desktop"]
                currentIndex: Math.max(0, modes.indexOf(sheet.settings.soundSentryFlash))
                onActivated: (i) => sheet.set("soundSentryFlash", modes[i])
            }
        },
        SettingsDialog {
            id: contrastDialog
            title: "Settings for High Contrast"
            dialogHeight: 196
            ShortcutBox { key: "highContrastShortcut"; feature: "High Contrast"; keys: "Press Left Alt + Left Shift + Print Screen." }
            Text { text: "High contrast color scheme:"; font.family: Theme.fontFamily; font.pixelSize: Theme.fontSize }
            ComboBox95 {
                width: 220
                popupMenu: sheet.popupMenu
                readonly property var schemes: Wayland95Settings.colorSchemes().map(function (s) { return s.name; })
                    .filter(function (n) { return n.indexOf("High Contrast") === 0; })
                model: schemes
                currentIndex: Math.max(0, schemes.indexOf(sheet.settings.highContrastScheme))
                onActivated: (i) => sheet.set("highContrastScheme", schemes[i])
            }
        },
        SettingsDialog {
            id: mouseDialog
            title: "Settings for MouseKeys"
            dialogHeight: 282
            ShortcutBox { key: "mouseKeysShortcut"; feature: "MouseKeys"; keys: "Press Left Alt + Left Shift + Num Lock." }
            GroupBox95 {
                width: parent.width
                height: 74
                title: "Pointer speed"
                Grid {
                    columns: 2
                    columnSpacing: 8
                    rowSpacing: 4
                    verticalItemAlignment: Grid.AlignVCenter
                    Text { text: "Top speed:"; font.family: Theme.fontFamily; font.pixelSize: Theme.fontSize }
                    Slider95 {
                        width: 200
                        from: 1; to: 10; stepSize: 1
                        value: sheet.settings.mouseKeysSpeed
                        onMoved: sheet.set("mouseKeysSpeed", value)
                    }
                    Text { text: "Acceleration:"; font.family: Theme.fontFamily; font.pixelSize: Theme.fontSize }
                    Slider95 {
                        width: 200
                        from: 1; to: 10; stepSize: 1
                        value: sheet.settings.mouseKeysAcceleration
                        onMoved: sheet.set("mouseKeysAcceleration", value)
                    }
                }
            }
            Row {
                spacing: 12
                Text { text: "Use MouseKeys when Num Lock is:"; font.family: Theme.fontFamily; font.pixelSize: Theme.fontSize }
                RadioButton95 { text: "O<u>n</u>"; checked: sheet.settings.mouseKeysNumLockOn; onClicked: sheet.set("mouseKeysNumLockOn", true) }
                RadioButton95 { text: "O<u>f</u>f"; checked: !sheet.settings.mouseKeysNumLockOn; onClicked: sheet.set("mouseKeysNumLockOn", false) }
            }
        }
    ]
}
