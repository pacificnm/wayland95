pragma Singleton
import QtQuick

// Shared Win95 palette and metrics for the bundled apps. The colors come
// from Control Panel > Display > Appearance (Wayland95Settings), so every
// app follows a scheme change live.
QtObject {
    readonly property var c: Wayland95Settings.colors

    readonly property color face: c.face                // 3D objects: buttons, dialogs
    readonly property color light: c.light              // inner highlight
    readonly property color highlight: c.highlight      // outer highlight
    readonly property color shadow: c.shadow            // inner shadow
    readonly property color dark: c.dark                // outer shadow / frame

    readonly property color window: c.window            // list / field background
    readonly property color text: c.windowText
    readonly property color disabledText: c.shadow
    readonly property color selection: c.selection
    readonly property color selectionText: c.selectionText
    readonly property color menu: c.menu
    readonly property color menuText: c.menuText

    readonly property color titleActiveStart: c.activeTitle
    readonly property color titleActiveEnd: c.activeTitle2
    readonly property color titleActiveText: c.activeTitleText
    readonly property color titleInactiveStart: c.inactiveTitle
    readonly property color titleInactiveEnd: c.inactiveTitle2
    readonly property color titleInactiveText: c.inactiveTitleText

    // generic stand-ins for MS Sans Serif / Fixedsys
    readonly property string fontFamily: "Liberation Sans"
    readonly property int fontSize: 11
    readonly property string monoFamily: "DejaVu Sans Mono"
}
