import QtQuick

// Shows one of the Wayland 95 screen savers at whatever size it's given:
// full screen in the desktop, or in the Display applet's little monitor.
//
// saver: "none", "blank", "starfield", "polygons", "curves", "marquee"
// options: the saver's settings (Wayland95Settings.screenSaverOptions)
Rectangle {
    id: view

    property string saver: "starfield"
    property var options: ({})
    property bool running: true

    function option(key, fallback) {
        return options && options[key] !== undefined ? options[key] : fallback;
    }

    color: "black"
    clip: true

    Loader {
        anchors.fill: parent
        active: view.saver !== "none" && view.saver !== "blank"
        source: ({ starfield: "SaverStarfield.qml", polygons: "SaverPolygons.qml",
                   curves: "SaverCurves.qml", marquee: "SaverMarquee.qml" })[view.saver] || ""
        onLoaded: {
            item.view = view;
        }
    }

    // the saver list, for the Display applet
    readonly property var savers: [
        { id: "none", name: "(None)", hasSettings: false },
        { id: "blank", name: "Blank Screen", hasSettings: false },
        { id: "curves", name: "Curves", hasSettings: true },
        { id: "marquee", name: "Scrolling Marquee", hasSettings: true },
        { id: "polygons", name: "Polygons", hasSettings: true },
        { id: "starfield", name: "Starfield", hasSettings: true },
    ]
}
