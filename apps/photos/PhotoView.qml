import QtQuick
import Win95Ui

// One photo, fitted to the view or zoomed (Ctrl+wheel, about the mouse;
// drag to look around; double-click: fit / actual size). The wheel alone
// scrolls a zoomed photo, and steps to the next or previous one otherwise.
//
// cropping: a crop frame over the photo, dragged out, moved and sized with
// its edges and corners; cropRect is in the picture's own pixels.
Item {
    id: view

    property string source: ""
    // 0: fit (never larger than actual size); else the scale
    property real zoom: 0
    // the picture's real size, when what's shown is a smaller copy (the
    // editor's preview); 0: the shown image's own
    property int logicalWidth: 0
    property int logicalHeight: 0
    readonly property int imageWidth: logicalWidth > 0 ? logicalWidth : img.implicitWidth
    readonly property int imageHeight: logicalHeight > 0 ? logicalHeight : img.implicitHeight
    readonly property real fitScale: imageWidth > 0
                                     ? Math.min(1, (width - 8) / imageWidth, (height - 8) / imageHeight) : 1
    readonly property real scale: zoom > 0 ? zoom : fitScale
    readonly property bool ready: img.status === Image.Ready
    readonly property int status: img.status

    property bool cropping: false
    property rect cropRect: Qt.rect(0, 0, 0, 0)
    // Free, Original, Square, 4:3, 3:2, 16:9 (turned upright for a portrait photo)
    property int aspectChoice: 0
    readonly property var aspectNames: ["Free", "Original", "Square", "4:3", "3:2", "16:9"]
    readonly property real cropAspect: {
        if (imageWidth <= 0 || imageHeight <= 0)
            return 0;
        var a = [0, imageWidth / imageHeight, 1, 4 / 3, 3 / 2, 16 / 9][aspectChoice];
        return aspectChoice >= 3 && imageHeight > imageWidth ? 1 / a : a;
    }

    signal wheelStep(int direction)
    signal contextMenu(real x, real y)

    readonly property var zoomSteps: [0.05, 0.1, 0.125, 0.167, 0.25, 0.333, 0.5, 0.667, 0.75, 1, 1.5, 2, 3, 4, 6, 8, 12, 16]

    function zoomIn() { zoomAbout(nextStep(1), flick.width / 2, flick.height / 2); }
    function zoomOut() { zoomAbout(nextStep(-1), flick.width / 2, flick.height / 2); }
    function fit() { zoom = 0; }
    function actualSize() { zoomAbout(1, flick.width / 2, flick.height / 2); }

    function nextStep(dir) {
        var s = scale;
        if (dir > 0) {
            for (var i = 0; i < zoomSteps.length; ++i)
                if (zoomSteps[i] > s * 1.01)
                    return zoomSteps[i];
            return zoomSteps[zoomSteps.length - 1];
        }
        for (var j = zoomSteps.length - 1; j >= 0; --j)
            if (zoomSteps[j] < s * 0.99)
                return zoomSteps[j];
        return zoomSteps[0];
    }

    // to scale `z`, keeping the picture's point at (px, py) of the view where it is
    function zoomAbout(z, px, py) {
        var old = scale;
        var ix = (flick.contentX + px - img.x) / old;
        var iy = (flick.contentY + py - img.y) / old;
        // (back to fit when that's where it lands)
        zoom = Math.abs(z - fitScale) < 0.001 ? 0 : Math.max(0.02, Math.min(16, z));
        flick.contentX = Math.max(0, Math.min(flick.contentWidth - flick.width, img.x + ix * scale - px));
        flick.contentY = Math.max(0, Math.min(flick.contentHeight - flick.height, img.y + iy * scale - py));
    }

    // the crop frame: the largest of the chosen shape, centred
    function resetCrop() {
        var w = imageWidth, h = imageHeight, a = cropAspect;
        if (a > 0) {
            if (w / h > a)
                w = h * a;
            else
                h = w / a;
        }
        cropRect = Qt.rect((imageWidth - w) / 2, (imageHeight - h) / 2, w, h);
    }
    onAspectChoiceChanged: if (cropping) resetCrop()
    onCroppingChanged: if (cropping) { zoom = 0; resetCrop(); }

    Rectangle {
        anchors.fill: parent
        color: "#404040"
    }

    Flickable {
        id: flick
        anchors.fill: parent
        clip: true
        contentWidth: Math.max(width, img.width)
        contentHeight: Math.max(height, img.height)
        boundsBehavior: Flickable.StopAtBounds
        interactive: !view.cropping && (contentWidth > width || contentHeight > height)
        flickableDirection: Flickable.HorizontalAndVerticalFlick
        pixelAligned: true

        Image {
            id: img
            x: Math.round((flick.contentWidth - width) / 2)
            y: Math.round((flick.contentHeight - height) / 2)
            width: Math.round(view.imageWidth * view.scale)
            height: Math.round(view.imageHeight * view.scale)
            source: view.source
            // (the editor's picture comes straight from memory; files load
            // in the background)
            asynchronous: view.source.indexOf("image://view/") === 0
            cache: false
            smooth: true
            mipmap: view.scale < 0.5
            fillMode: Image.Stretch

            // ---- the crop frame
            Item {
                id: cropLayer
                anchors.fill: parent
                visible: view.cropping
                readonly property real s: view.scale
                readonly property real l: view.cropRect.x * s
                readonly property real t: view.cropRect.y * s
                readonly property real r: (view.cropRect.x + view.cropRect.width) * s
                readonly property real b: (view.cropRect.y + view.cropRect.height) * s

                // what's cropped away, dimmed
                Rectangle { color: "#a0000000"; x: 0; y: 0; width: parent.width; height: cropLayer.t }
                Rectangle { color: "#a0000000"; x: 0; y: cropLayer.b; width: parent.width; height: parent.height - cropLayer.b }
                Rectangle { color: "#a0000000"; x: 0; y: cropLayer.t; width: cropLayer.l; height: cropLayer.b - cropLayer.t }
                Rectangle { color: "#a0000000"; x: cropLayer.r; y: cropLayer.t; width: parent.width - cropLayer.r; height: cropLayer.b - cropLayer.t }
                // the frame (black and white, to show on anything) and thirds
                Rectangle {
                    x: cropLayer.l - 1; y: cropLayer.t - 1
                    width: cropLayer.r - cropLayer.l + 2; height: cropLayer.b - cropLayer.t + 2
                    color: "transparent"
                    border.color: "#000000"
                    Rectangle { anchors.fill: parent; anchors.margins: 1; color: "transparent"; border.color: "#ffffff" }
                    Repeater {
                        model: 2
                        Rectangle { required property int index; x: parent.width * (index + 1) / 3; y: 1; width: 1; height: parent.height - 2; color: "#80ffffff" }
                    }
                    Repeater {
                        model: 2
                        Rectangle { required property int index; y: parent.height * (index + 1) / 3; x: 1; height: 1; width: parent.width - 2; color: "#80ffffff" }
                    }
                }
                // corner handles
                Repeater {
                    model: [[0, 0], [1, 0], [0, 1], [1, 1]]
                    Rectangle {
                        required property var modelData
                        width: 7; height: 7
                        x: (modelData[0] ? cropLayer.r : cropLayer.l) - 3
                        y: (modelData[1] ? cropLayer.b : cropLayer.t) - 3
                        color: "#ffffff"
                        border.color: "#000000"
                    }
                }

                MouseArea {
                    id: cropMouse
                    anchors.fill: parent
                    anchors.margins: -8 // (the frame's handles at the picture's very edge)
                    enabled: view.cropping
                    hoverEnabled: true
                    preventStealing: true
                    cursorShape: {
                        var m = hit(mouseX - 8, mouseY - 8);
                        return m === "move" ? Qt.SizeAllCursor
                             : m === "nw" || m === "se" ? Qt.SizeFDiagCursor
                             : m === "ne" || m === "sw" ? Qt.SizeBDiagCursor
                             : m === "n" || m === "s" ? Qt.SizeVerCursor
                             : m === "w" || m === "e" ? Qt.SizeHorCursor : Qt.CrossCursor;
                    }

                    property string mode: ""
                    property point fixed
                    property point grab
                    property rect start

                    // what a press at (mx, my) (picture display coordinates) takes hold of
                    function hit(mx, my) {
                        var L = cropLayer.l, T = cropLayer.t, R = cropLayer.r, B = cropLayer.b, m = 6;
                        if (view.cropRect.width <= 0)
                            return "new";
                        var inside = mx > L && mx < R && my > T && my < B;
                        var inX = mx > L - m && mx < R + m, inY = my > T - m && my < B + m;
                        var v = Math.abs(my - T) <= m && inX ? "n" : Math.abs(my - B) <= m && inX ? "s" : "";
                        var h = Math.abs(mx - L) <= m && inY ? "w" : Math.abs(mx - R) <= m && inY ? "e" : "";
                        if (v && h)
                            return v + h;
                        if ((v || h) && view.cropAspect <= 0)
                            return v || h;
                        return inside ? "move" : "new";
                    }
                    function toImage(mx, my) {
                        return Qt.point(Math.max(0, Math.min(view.imageWidth, (mx - 8) / view.scale)),
                                        Math.max(0, Math.min(view.imageHeight, (my - 8) / view.scale)));
                    }

                    onPressed: (mouse) => {
                        mode = hit(mouse.x - 8, mouse.y - 8);
                        var p = toImage(mouse.x, mouse.y);
                        var r = view.cropRect;
                        start = r;
                        grab = p;
                        if (mode === "new") {
                            fixed = p;
                            view.cropRect = Qt.rect(p.x, p.y, 0, 0);
                        } else {
                            fixed = Qt.point(mode.indexOf("w") >= 0 ? r.x + r.width : r.x,
                                             mode.indexOf("n") >= 0 ? r.y + r.height : r.y);
                        }
                    }
                    onPositionChanged: (mouse) => {
                        if (pressed)
                            dragTo(toImage(mouse.x, mouse.y));
                    }
                    onReleased: {
                        // a click, not a drag: back to the whole picture
                        if (view.cropRect.width < 2 || view.cropRect.height < 2)
                            view.resetCrop();
                        mode = "";
                    }

                    function dragTo(p) {
                        var iw = view.imageWidth, ih = view.imageHeight, s = start;
                        if (mode === "move") {
                            var nx = Math.max(0, Math.min(iw - s.width, s.x + p.x - grab.x));
                            var ny = Math.max(0, Math.min(ih - s.height, s.y + p.y - grab.y));
                            view.cropRect = Qt.rect(nx, ny, s.width, s.height);
                            return;
                        }
                        if (mode.length === 1) {
                            // an edge (free shape only)
                            var L = s.x, T = s.y, R = s.x + s.width, B = s.y + s.height;
                            if (mode === "w") L = p.x;
                            else if (mode === "e") R = p.x;
                            else if (mode === "n") T = p.y;
                            else B = p.y;
                            view.cropRect = Qt.rect(Math.min(L, R), Math.min(T, B), Math.abs(R - L), Math.abs(B - T));
                            return;
                        }
                        // a corner, or a new frame: from the corner that stays put
                        var dx = p.x - fixed.x, dy = p.y - fixed.y;
                        var w = Math.abs(dx), h = Math.abs(dy), a = view.cropAspect;
                        if (a > 0) {
                            if (w / Math.max(h, 1e-6) > a)
                                h = w / a;
                            else
                                w = h * a;
                            var maxW = dx >= 0 ? iw - fixed.x : fixed.x;
                            var maxH = dy >= 0 ? ih - fixed.y : fixed.y;
                            if (w > maxW) { w = maxW; h = w / a; }
                            if (h > maxH) { h = maxH; w = h * a; }
                        }
                        view.cropRect = Qt.rect(dx >= 0 ? fixed.x : fixed.x - w, dy >= 0 ? fixed.y : fixed.y - h, w, h);
                    }
                }
            }
        }

        TapHandler {
            enabled: !view.cropping
            onDoubleTapped: {
                if (view.zoom > 0 || view.fitScale >= 1)
                    view.fit();
                else
                    view.zoomAbout(1, point.position.x - flick.contentX, point.position.y - flick.contentY);
            }
        }
        TapHandler {
            acceptedButtons: Qt.RightButton
            onTapped: (point) => view.contextMenu(point.position.x - flick.contentX, point.position.y - flick.contentY)
        }
    }

    Text {
        anchors.centerIn: parent
        visible: img.status === Image.Loading || img.status === Image.Error
        text: img.status === Image.Error ? "This picture can't be shown." : "Loading..."
        color: "#ffffff"
        font.family: Theme.fontFamily
        font.pixelSize: Theme.fontSize
    }

    // the wheel: zoom (Ctrl), scroll a zoomed photo, or step photos
    property real wheelRest: 0
    WheelHandler {
        acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad
        onWheel: (event) => {
            var dy = event.angleDelta.y, dx = event.angleDelta.x;
            if (event.modifiers & Qt.ControlModifier) {
                if (dy !== 0 && !view.cropping)
                    view.zoomAbout(view.scale * Math.pow(1.0015, dy), event.x, event.y);
                return;
            }
            if (flick.interactive) {
                flick.contentY = Math.max(0, Math.min(flick.contentHeight - flick.height, flick.contentY - dy));
                flick.contentX = Math.max(0, Math.min(flick.contentWidth - flick.width, flick.contentX - dx));
                return;
            }
            if (view.cropping)
                return;
            // a notch at a time (a touchpad sends many small ones)
            view.wheelRest += dy;
            if (Math.abs(view.wheelRest) >= 120) {
                view.wheelStep(view.wheelRest > 0 ? -1 : 1);
                view.wheelRest = 0;
            }
        }
    }

    // the arrow keys look around a zoomed photo (Main.qml steps photos otherwise)
    function pan(dx, dy) {
        if (!flick.interactive)
            return false;
        flick.contentX = Math.max(0, Math.min(flick.contentWidth - flick.width, flick.contentX + dx));
        flick.contentY = Math.max(0, Math.min(flick.contentHeight - flick.height, flick.contentY + dy));
        return true;
    }
}
