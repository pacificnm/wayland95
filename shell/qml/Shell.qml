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
            onRunRequested: console.info("win95shell: Run comes in step 4b")
            onShutDownRequested: console.info("win95shell: Shut Down comes in step 4b")
            onHelpRequested: console.info("win95shell: Help comes in step 4b")
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

    // the "Start Wayland 95" sound
    Component.onCompleted: Ui.Wayland95Settings.playSound("startup")
}
