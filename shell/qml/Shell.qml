import QtQuick
import QtQuick.Window
import Win95Ui as Ui
import Win95Shell

// win95shell: Wayland 95's desktop, as panels (layer-shell surfaces) of the
// wlroots compositor (docs/architecture.md). The QML is the Qt
// compositor's, shared: Taskbar.qml, StartMenu.qml, the tray, the desktop
// icons. What's different is only where each part lives:
//
//   desktop      background layer: the backdrop and the icons
//   taskbar      top layer, along the bottom; windows stay clear of it
//   popupLayer   overlay, the whole screen, shown only while the Start
//                menu, a tray pop-up or the taskbar's window menu is open;
//                it has the keyboard then, and a click outside closes them
//   tipLayer     overlay, the whole screen, never takes clicks: tooltips
QtObject {
    id: shell

    property Window desktop: Window {
        LayerShell.layer: LayerShell.Background
        LayerShell.anchors: LayerShell.AnchorTop | LayerShell.AnchorBottom | LayerShell.AnchorLeft | LayerShell.AnchorRight
        LayerShell.exclusiveZone: 0 // the area above the taskbar
        LayerShell.keyboard: LayerShell.KeyboardOnDemand
        LayerShell.scope: "desktop"
        title: "Desktop"
        color: Ui.Wayland95Settings.backgroundColor
        width: 640; height: 480 // (the compositor gives the real size)
        visible: true

        DesktopBackground {
            anchors.fill: parent
        }
        DesktopIcons {
            anchors.top: parent.top
            anchors.left: parent.left
            anchors.bottom: parent.bottom
            width: 100
        }
    }

    property Window taskbarWindow: Window {
        LayerShell.layer: LayerShell.Top
        LayerShell.anchors: LayerShell.AnchorBottom | LayerShell.AnchorLeft | LayerShell.AnchorRight
        LayerShell.exclusiveZone: height
        LayerShell.keyboard: LayerShell.KeyboardNone
        LayerShell.scope: "taskbar"
        title: "Taskbar"
        color: Theme.face
        width: 640
        height: Theme.taskbarHeight
        visible: true

        Taskbar {
            id: taskbar
            anchors.fill: parent
            popupParent: shell.popupArea
            tipParent: shell.tipArea
            onRunRequested: runDialog.open()
            onShutDownRequested: shutDownDialog.open()
            onHelpRequested: shellMessage.show("Wayland 95",
                "Wayland 95\n\nA Windows 95-style desktop for Wayland, built with Qt.\n\n"
                + "Programs started from the Start menu connect to WAYLAND_DISPLAY=" + ShellSession.socketName
                + (ShellSession.x11Display ? ", and X11 programs to DISPLAY=" + ShellSession.x11Display : "")
                + ".", "info", ["OK"])
        }
    }

    property Window popupLayer: Window {
        LayerShell.layer: LayerShell.Overlay
        LayerShell.anchors: LayerShell.AnchorTop | LayerShell.AnchorBottom | LayerShell.AnchorLeft | LayerShell.AnchorRight
        LayerShell.exclusiveZone: -1 // over the taskbar too
        LayerShell.keyboard: LayerShell.KeyboardExclusive
        LayerShell.scope: "popups"
        title: "Pop-ups"
        color: "transparent"
        width: 640; height: 480
        visible: taskbar.anyPopupVisible

        // the screen-sized parent of the pop-ups: sized before the layer is
        // first shown (its window's own content item isn't), since the tray
        // and menus work out where to go when they open
        Item {
            id: popupArea
            width: Screen.width
            height: Screen.height
            // a click outside the pop-ups closes them (as on the Qt compositor)
            MouseArea {
                anchors.fill: parent
                z: -1
                acceptedButtons: Qt.AllButtons
                onPressed: taskbar.closePopups()
            }
        }
    }
    readonly property Item popupArea: popupArea

    property Window tipLayer: Window {
        LayerShell.layer: LayerShell.Overlay
        LayerShell.anchors: LayerShell.AnchorTop | LayerShell.AnchorBottom | LayerShell.AnchorLeft | LayerShell.AnchorRight
        LayerShell.exclusiveZone: -1
        LayerShell.keyboard: LayerShell.KeyboardNone
        LayerShell.scope: "tooltips"
        title: "Tooltips"
        color: "transparent"
        flags: Qt.WindowTransparentForInput
        width: 640; height: 480
        // shown only while a tooltip is (an empty overlay over the whole
        // screen would cost every frame of every window a blend)
        visible: taskbar.tipShowing
        Item {
            id: tipArea
            width: Screen.width
            height: Screen.height
        }
    }
    readonly property Item tipArea: tipArea

    // --- the Start menu's dialogs (as on the Qt compositor: Main.qml) -------
    property Window runLayer: DialogLayer {
        showing: runDialog.visible
        RunDialog {
            id: runDialog
            onFailed: (command, message) => shellMessage.show(command, message, "error", ["OK"],
                                                              runDialog.refocus)
        }
    }
    property Window shutDownLayer: DialogLayer {
        showing: shutDownDialog.visible
        ShutDownDialog {
            id: shutDownDialog
            onChosen: (option) => {
                console.info("win95shell: Shut Down:", option);
                if (option === "standby") {
                    Ui.Power.suspend();
                } else if (option === "hibernate") {
                    Ui.Power.hibernate();
                } else {
                    Ui.Wayland95Settings.playSound("exit");
                    if (option === "shutdown")
                        Ui.Power.powerOff();
                    else if (option === "restart")
                        Ui.Power.reboot();
                    else
                        ShellSession.exitDesktop();
                }
            }
        }
    }
    // (created after the others: a message over the Run dialog is on top)
    property Window messageLayer: DialogLayer {
        showing: shellMessage.visible
        Ui.MessageBox95 { id: shellMessage }
    }

    // the "Start Wayland 95" sound
    Component.onCompleted: Ui.Wayland95Settings.playSound("startup")
}
