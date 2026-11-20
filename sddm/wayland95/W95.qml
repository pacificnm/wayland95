pragma Singleton
import QtQuick

// The Wayland 95 Standard colors and font. The login screen runs before
// anyone logs in, so it can't follow a user's color scheme (or load the
// desktop's own QML): this is a copy of the defaults.
QtObject {
    readonly property color face: "#c0c0c0"
    readonly property color light: "#dfdfdf"
    readonly property color highlight: "#ffffff"
    readonly property color shadow: "#808080"
    readonly property color dark: "#000000"

    readonly property color window: "#ffffff"
    readonly property color text: "#000000"
    readonly property color disabledText: "#808080"
    readonly property color selection: "#000080"
    readonly property color selectionText: "#ffffff"
    readonly property color error: "#c00000"

    readonly property color titleStart: "#000080"
    readonly property color titleEnd: "#1084d0"
    readonly property color titleText: "#ffffff"

    readonly property string fontFamily: "Liberation Sans"
    readonly property int fontSize: 11
}
