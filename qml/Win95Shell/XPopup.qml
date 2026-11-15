import QtQuick
import QtWayland.Compositor

// An X11 override-redirect window: a menu, tooltip, drop-down list or drag
// icon. The client positions these itself in X root coordinates, so this
// just shows the surface there, with no frame.
WaylandQuickItem {
    id: popup

    required property var xwindow   // XWaylandWindow

    surface: xwindow ? xwindow.surface : null
    // keyboard focus stays with the window the menu belongs to
    focusOnClick: false

    function place() {
        if (!popup.xwindow)
            return;
        var p = popup.mapFromSurface(Qt.point(popup.xwindow.geometry.x, popup.xwindow.geometry.y));
        popup.x = Math.round(p.x);
        popup.y = Math.round(p.y);
    }

    Component.onCompleted: place()
    // the surface-to-item scale is only exact once there's a buffer
    onWidthChanged: place()

    Connections {
        target: popup.xwindow
        function onGeometryChanged() { popup.place(); }
        function onUnmapped() { popup.destroy(); }
    }
}
