import QtQuick
import QtQuick.Window
import Win95Shell

// A shell dialog (Run, Shut Down, a message box) as a panel over
// everything: the dialogs are the Qt compositor's, which fill the screen
// (dimming it, or blocking clicks behind them, as there) and centre their
// box. Shown while its dialog is; it has the keyboard meanwhile.
//
//   DialogLayer { showing: run.visible; RunDialog { id: run } }
Window {
    id: layer
    default property alias content: area.data
    property bool showing: false

    LayerShell.layer: LayerShell.Overlay
    LayerShell.anchors: LayerShell.AnchorTop | LayerShell.AnchorBottom | LayerShell.AnchorLeft | LayerShell.AnchorRight
    LayerShell.exclusiveZone: -1
    LayerShell.keyboard: LayerShell.KeyboardExclusive
    LayerShell.scope: "dialog"
    title: "Dialog"
    color: "transparent"
    width: 640; height: 480
    visible: showing

    // screen-sized from the start, so a dialog centres itself right when it
    // opens (before the layer's own size arrives)
    Item {
        id: area
        width: Screen.width
        height: Screen.height
    }
}
