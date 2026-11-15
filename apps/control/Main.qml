import QtQuick
import QtQuick.Window
import Win95Ui
import Win95Control

// The Control Panel folder: one icon per applet. Double-click (or Enter)
// opens the applet's property sheet in its own window. Started with
// --applet NAME, only that applet opens.
Window {
    id: panel

    width: 460
    height: 320
    minimumWidth: 260
    minimumHeight: 180
    visible: false // shown in Component.onCompleted unless an applet was asked for
    color: Theme.face
    title: "Control Panel"

    // alphabetical, like Win95's
    readonly property var applets: [
        { id: "accessibility", name: "Accessibility Options", file: "AccessibilityApplet.qml",
          description: "Changes accessibility options: StickyKeys, FilterKeys, MouseKeys, High Contrast and more." },
        { id: "programs", name: "Add/Remove Programs", file: "SoftwareApplet.qml",
          description: "Installs and removes programs, and keeps them up to date." },
        { id: "bluetooth", name: "Bluetooth", file: "BluetoothApplet.qml",
          description: "Turns Bluetooth on or off, and pairs and connects Bluetooth devices." },
        { id: "datetime", name: "Date/Time", file: "DateTimeApplet.qml",
          description: "Shows the date, time and time zone." },
        { id: "display", name: "Display", file: "DisplayApplet.qml",
          description: "Changes the wallpaper, background and window colors." },
        { id: "fonts", name: "Fonts", file: "FontsApplet.qml",
          description: "Shows the installed fonts, previews them, and installs new ones." },
        { id: "hardware", name: "Hardware", file: "HardwareApplet.qml",
          description: "Lists the computer's devices and shows whether each one is working." },
        { id: "keyboard", name: "Keyboard", file: "KeyboardApplet.qml",
          description: "Changes key repeat, cursor blink and the keyboard layout." },
        { id: "mouse", name: "Mouse", file: "MouseApplet.qml",
          description: "Changes the double-click speed." },
        { id: "network", name: "Network", file: "NetworkApplet.qml",
          description: "Shows network adapters, connects to Wi-Fi and shows this computer's network name." },
        { id: "passwords", name: "Passwords", file: "PasswordsApplet.qml",
          description: "Changes your password, and shows who can approve changes to the computer." },
        { id: "power", name: "Power", file: "PowerApplet.qml",
          description: "Turns off the display and sleeps when idle, and sets brightness, the power mode and what the lid does." },
        { id: "printers", name: "Printers", file: "PrintersApplet.qml",
          description: "Adds printers, and shows and changes what they're printing." },
        { id: "regional", name: "Regional Settings", file: "RegionalApplet.qml",
          description: "Changes how numbers, currency, times and dates are shown." },
        { id: "sounds", name: "Sounds", file: "SoundsApplet.qml",
          description: "Changes the sounds played for desktop events." },
        { id: "system", name: "System", file: "SystemApplet.qml",
          description: "Shows information about your computer and how it's running." },
        { id: "taskbar", name: "Taskbar", file: "TaskbarApplet.qml",
          description: "Changes the taskbar clock and which icons show next to it." },
    ]

    property var openWindows: ({})
    property int selected: -1

    function appletIndex(id) {
        for (var i = 0; i < applets.length; ++i)
            if (applets[i].id === id)
                return i;
        return -1;
    }

    function openApplet(i) {
        var a = applets[i];
        if (!a)
            return;
        var existing = openWindows[a.id];
        if (existing) {
            existing.raise();
            existing.requestActivate();
            return;
        }
        var component = Qt.createComponent(a.file);
        if (component.status !== Component.Ready) {
            console.warn("Control Panel: cannot load", a.file, component.errorString());
            return;
        }
        // no parent window: a window parented to the (possibly hidden)
        // Control Panel isn't shown when only one applet was asked for.
        // openWindows keeps the reference that stops it being collected.
        var w = component.createObject(null);
        openWindows[a.id] = w;
        w.closing.connect(function () {
            delete panel.openWindows[a.id];
            Qt.callLater(w.destroy);
        });
        w.requestActivate();
    }

    Component.onCompleted: {
        var i = initialApplet ? appletIndex(initialApplet) : -1;
        if (i >= 0) {
            // before the tab opens, which would otherwise look in the old lists
            if (initialCheck)
                Software.checkUpdates(true);
            openApplet(i);
            var w = openWindows[applets[i].id];
            if (w && initialTab > 0 && initialTab < w.titles.length)
                w.currentTab = initialTab;
        }
        else
            panel.visible = true;
        grid.forceActiveFocus();
    }

    function helpMenu() {
        return [
            { text: "About Control Panel", action: function () {
                msgBox.show("About Control Panel",
                            "Control Panel\n\nSettings for the Wayland 95 desktop. They are saved in\n"
                            + Wayland95Settings.filePath(), "info", ["OK"], function () { grid.forceActiveFocus(); });
            } },
        ];
    }

    MenuBar95 {
        id: menuBar
        x: 2
        y: 2
        width: parent.width - 4
        height: 20
        popupMenu: popup
        menus: [
            { title: "File", items: function () {
                return [
                    { text: "Open", shortcut: "Enter", enabled: panel.selected >= 0,
                      action: function () { panel.openApplet(panel.selected); } },
                    { separator: true },
                    { text: "Close", shortcut: "Ctrl+W", action: panel.close },
                ];
            } },
            { title: "Help", items: panel.helpMenu },
        ]
    }

    Bevel {
        id: well
        anchors.top: menuBar.bottom
        anchors.topMargin: 3
        anchors.bottom: statusBar.top
        anchors.bottomMargin: 3
        x: 2
        width: parent.width - 4
        style: "sunken"
        face: Theme.window

        MouseArea {
            anchors.fill: parent
            onPressed: {
                panel.selected = -1;
                grid.forceActiveFocus();
            }
        }

        GridView {
            id: grid
            x: 4
            y: 6
            width: parent.width - 8
            height: parent.height - 10
            cellWidth: 82
            cellHeight: 76
            interactive: false
            model: panel.applets
            focus: true

            Keys.onPressed: (event) => {
                var n = panel.applets.length;
                var cols = Math.max(1, Math.floor(grid.width / grid.cellWidth));
                var i = panel.selected < 0 ? 0 : panel.selected;
                switch (event.key) {
                case Qt.Key_Left: i = Math.max(0, i - 1); break;
                case Qt.Key_Right: i = Math.min(n - 1, i + 1); break;
                case Qt.Key_Up: i = Math.max(0, i - cols); break;
                case Qt.Key_Down: i = Math.min(n - 1, i + cols); break;
                case Qt.Key_Return:
                case Qt.Key_Enter:
                    panel.openApplet(panel.selected);
                    event.accepted = true;
                    return;
                default:
                    if ((event.modifiers & Qt.ControlModifier) && event.key === Qt.Key_W) {
                        panel.close();
                        event.accepted = true;
                    }
                    return;
                }
                panel.selected = i;
                event.accepted = true;
            }

            delegate: Item {
                id: cell
                required property var modelData
                required property int index
                readonly property bool isSelected: panel.selected === index
                width: grid.cellWidth
                height: grid.cellHeight

                AppletIcon {
                    id: cellIcon
                    anchors.horizontalCenter: parent.horizontalCenter
                    y: 2
                    kind: cell.modelData.id
                    Rectangle {
                        anchors.fill: parent
                        visible: cell.isSelected
                        color: Theme.selection
                        opacity: 0.45
                    }
                }
                Rectangle {
                    id: labelBox
                    anchors.horizontalCenter: parent.horizontalCenter
                    y: cellIcon.y + cellIcon.height + 3
                    width: Math.min(label.implicitWidth, cell.width - 6) + 4
                    height: label.height + 2
                    color: cell.isSelected ? Theme.selection : "transparent"
                    // long names wrap onto a second line, like Win95's
                    Text {
                        id: label
                        anchors.centerIn: parent
                        width: Math.min(implicitWidth, cell.width - 6)
                        wrapMode: Text.Wrap
                        horizontalAlignment: Text.AlignHCenter
                        text: cell.modelData.name
                        color: cell.isSelected ? Theme.selectionText : Theme.text
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.fontSize
                    }
                    Loader {
                        anchors.fill: parent
                        active: cell.isSelected && grid.activeFocus
                        sourceComponent: FocusRect95 { color: "#ffff80" }
                    }
                }
                MouseArea {
                    x: Math.min(cellIcon.x, labelBox.x)
                    width: Math.max(cellIcon.width, labelBox.width)
                    height: labelBox.y + labelBox.height
                    onPressed: {
                        panel.selected = cell.index;
                        grid.forceActiveFocus();
                    }
                    onDoubleClicked: panel.openApplet(cell.index)
                }
            }
        }
    }

    Bevel {
        id: statusBar
        anchors.bottom: parent.bottom
        anchors.bottomMargin: 2
        x: 2
        width: parent.width - 4
        height: 18
        style: "thinSunken"
        Text {
            x: 4
            width: parent.width - 20
            anchors.verticalCenter: parent.verticalCenter
            elide: Text.ElideRight
            text: panel.selected >= 0 ? panel.applets[panel.selected].description
                                      : panel.applets.length + " object(s)"
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontSize
        }
        SizeGrip95 {
            anchors.right: parent.right
            anchors.rightMargin: 1
            anchors.bottom: parent.bottom
            anchors.bottomMargin: 1
        }
    }

    Menu95 {
        id: popup
        onClosed: if (!msgBox.visible) grid.forceActiveFocus()
    }
    MessageBox95 { id: msgBox }
}
