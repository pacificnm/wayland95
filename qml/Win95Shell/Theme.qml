pragma Singleton
import QtQuick
import Win95Ui as Ui

// The shell's palette and metrics. Colors follow Control Panel > Display >
// Appearance (Wayland95Settings), so a scheme change re-colors title bars,
// the taskbar and the Start menu live.
// Note: "MS Sans Serif"/"Tahoma" aren't freely redistributable, so a
// generic sans-serif stands in; point `fontFamily` elsewhere if you like.
QtObject {
    readonly property var c: Ui.Wayland95Settings.colors

    readonly property color face: c.face               // button/window face
    readonly property color light: c.light
    readonly property color highlight: c.highlight     // raised-edge highlight
    readonly property color shadow: c.shadow           // raised-edge shadow
    readonly property color shadowDarker: c.dark       // outer edge

    readonly property color titleActiveStart: c.activeTitle
    readonly property color titleActiveEnd: c.activeTitle2
    readonly property color titleInactiveStart: c.inactiveTitle
    readonly property color titleInactiveEnd: c.inactiveTitle2
    readonly property color titleTextActive: c.activeTitleText
    readonly property color titleTextInactive: c.inactiveTitleText

    readonly property color text: c.windowText
    readonly property color menu: c.menu
    readonly property color menuText: c.menuText
    readonly property color selectionBlue: c.selection
    readonly property color selectionText: c.selectionText

    readonly property string fontFamily: "Liberation Sans"
    readonly property int fontSize: 11

    readonly property int bevelThickness: 2
    readonly property int titleBarHeight: 22
    readonly property int taskbarHeight: 34
}
