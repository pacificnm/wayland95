import QtQuick
import Win95Ui

// Athenaeum's own icons, drawn on a 16x16 grid like Icon95's (whose names
// work here too: "book", "find", "folder"...). "athenaeum" is the program's
// icon: a Greek temple, the library of the name.
Item {
    id: icon

    property string name: ""
    property int size: 16
    property bool enabled: true
    width: size
    height: size

    readonly property var own: ["athenaeum", "chat", "highlight", "school", "download", "article", "timeline", "cite", "project"]
    readonly property bool isOwn: own.indexOf(name) >= 0

    Icon95 {
        anchors.fill: parent
        visible: !icon.isOwn
        name: icon.isOwn ? "" : icon.name
        size: icon.size
        enabled: icon.enabled
    }

    Canvas {
        id: canvas
        anchors.fill: parent
        visible: icon.isOwn
        property string drawn: icon.name + "/" + icon.size + "/" + icon.enabled
        onDrawnChanged: requestPaint()
        Component.onCompleted: requestPaint()
        onPaint: {
            var ctx = getContext("2d");
            ctx.reset();
            if (!icon.isOwn)
                return;
            var s = width / 16;
            ctx.scale(s, s);
            var grey = !icon.enabled;
            function c(col) { return grey ? "#808080" : col; }
            function rect(x, y, w, h, col) { ctx.fillStyle = c(col); ctx.fillRect(x, y, w, h); }
            function frame(x, y, w, h, fill) { rect(x, y, w, h, "#000000"); rect(x + 1, y + 1, w - 2, h - 2, fill); }
            function poly(pts, fill) {
                ctx.beginPath();
                ctx.moveTo(pts[0], pts[1]);
                for (var i = 2; i < pts.length; i += 2)
                    ctx.lineTo(pts[i], pts[i + 1]);
                ctx.closePath();
                ctx.fillStyle = c(fill);
                ctx.fill();
            }
            function page(x, y) {
                poly([x, y, x + 8, y, x + 11, y + 3, x + 11, y + 14, x, y + 14], "#000000");
                poly([x + 1, y + 1, x + 7, y + 1, x + 10, y + 4, x + 10, y + 13, x + 1, y + 13], "#ffffff");
                rect(x + 7, y + 1, 1, 3, "#000000");
                rect(x + 7, y + 3, 3, 1, "#000000");
            }
            switch (icon.name) {
            case "athenaeum":
                // the pediment
                poly([0, 5, 8, 0, 16, 5], "#000000");
                poly([2, 4, 8, 1, 14, 4], "#c0c0c0");
                rect(7, 2, 2, 2, "#000080");
                rect(1, 5, 14, 2, "#000000");
                rect(2, 5, 12, 1, "#ffffff");
                // the columns
                for (var i = 0; i < 4; ++i) {
                    rect(2 + i * 3.5, 7, 2, 6, "#ffffff");
                    rect(3.5 + i * 3.5, 7, 0.5, 6, "#808080");
                }
                // the steps
                rect(1, 13, 14, 1, "#808080");
                rect(0, 14, 16, 2, "#000000");
                rect(1, 14, 14, 1, "#c0c0c0");
                break;
            case "chat":
                poly([1, 2, 15, 2, 15, 11, 7, 11, 3, 15, 4, 11, 1, 11], "#000000");
                poly([2, 3, 14, 3, 14, 10, 6.5, 10, 4.5, 12.5, 5, 10, 2, 10], "#ffffff");
                rect(4, 5, 8, 1, "#000080");
                rect(4, 7, 6, 1, "#000080");
                break;
            case "highlight":
                page(1, 1);
                rect(2, 5, 8, 2, "#ffff00");
                rect(3, 5.5, 6, 1, "#000000");
                rect(3, 9, 6, 1, "#808080");
                rect(3, 11, 5, 1, "#808080");
                // the marker
                poly([10, 12, 15, 6, 16, 7, 11, 13], "#000000");
                poly([10.5, 11.5, 14.5, 7, 15, 7.5, 11, 12], "#ffff00");
                break;
            case "school":
                // a mortarboard
                poly([0, 6, 8, 2, 16, 6, 8, 10], "#000000");
                poly([2, 6, 8, 3, 14, 6, 8, 9], "#000080");
                poly([4, 8, 8, 10, 12, 8, 12, 12, 8, 14, 4, 12], "#000000");
                rect(13, 6, 1, 6, "#808000");
                rect(12, 12, 3, 2, "#ffff00");
                break;
            case "download":
                rect(6, 1, 4, 7, "#000000");
                rect(7, 2, 2, 6, "#008000");
                poly([3, 7, 13, 7, 8, 12], "#000000");
                poly([5, 8, 11, 8, 8, 11], "#008000");
                rect(1, 13, 14, 2, "#000000");
                rect(2, 13, 12, 1, "#c0c0c0");
                break;
            case "article":
                page(2, 1);
                for (var l = 0; l < 5; ++l)
                    rect(4, 5 + l * 2, l === 0 ? 4 : 6, 1, l === 0 ? "#000080" : "#808080");
                break;
            case "timeline":
                rect(1, 7, 14, 2, "#000000");
                for (var t = 0; t < 3; ++t) {
                    rect(2 + t * 5, 3 + (t % 2) * 7, 2, 4, "#000000");
                    rect(1 + t * 5, 2 + (t % 2) * 10, 4, 2, t === 1 ? "#ff0000" : "#000080");
                }
                break;
            case "cite":
                page(2, 1);
                ctx.fillStyle = c("#000080");
                ctx.font = "bold 9px serif";
                ctx.fillText("“”", 3.5, 12);
                break;
            case "project":
                poly([0, 3, 6, 3, 7, 5, 15, 5, 15, 14, 0, 14], "#000000");
                poly([1, 4, 5.5, 4, 6.5, 6, 14, 6, 14, 13, 1, 13], "#ffff80");
                rect(1, 6, 13, 1, "#ffffff");
                poly([9, 2, 13, 2, 13, 10, 11, 8, 9, 10], "#000000");
                poly([10, 3, 12, 3, 12, 8, 11, 7, 10, 8], "#c00000");
                break;
            }
        }
    }
}
