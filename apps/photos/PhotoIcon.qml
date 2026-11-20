import QtQuick
import Win95Ui

// Photo Gallery's own toolbar icons, drawn on a 16x16 grid like Icon95's
// (whose names work here too: "open", "save", "delete"...).
Item {
    id: icon

    property string name: ""
    property bool enabled: true
    implicitWidth: 16
    implicitHeight: 16

    readonly property var own: ["rotateLeft", "rotateRight", "flipH", "flipV", "crop", "zoomIn", "zoomOut",
                                "fit", "actual", "slideshow", "gallery", "adjust", "wallpaper", "resize"]
    readonly property bool isOwn: own.indexOf(name) >= 0

    Icon95 {
        anchors.fill: parent
        visible: !icon.isOwn
        name: icon.isOwn ? "" : icon.name
        enabled: icon.enabled
    }

    // disabled: grey with a white copy a pixel down and right, as Icon95
    Repeater {
        model: icon.isOwn ? (icon.enabled ? ["#000000"] : ["#ffffff", "#808080"]) : []
        Canvas {
            required property string modelData
            required property int index
            x: !icon.enabled && index === 0 ? 1 : 0
            y: x
            width: icon.width
            height: icon.height
            property string drawn: icon.name + modelData
            onDrawnChanged: requestPaint()
            Component.onCompleted: requestPaint()
            onPaint: {
                var ctx = getContext("2d");
                ctx.reset();
                ctx.scale(width / 16, height / 16);
                icon.draw(ctx, icon.name, modelData, icon.enabled ? "#000080" : modelData,
                          icon.enabled ? "#ffffff" : "transparent");
            }
        }
    }

    function arrowArc(ctx, ink) {
        // a three-quarter circle, clockwise, ending at the top with an arrow
        ctx.strokeStyle = ink;
        ctx.lineWidth = 1.6;
        ctx.beginPath();
        ctx.arc(8, 9.5, 4.8, 0, Math.PI * 1.5, false);
        ctx.stroke();
        ctx.fillStyle = ink;
        ctx.beginPath();
        ctx.moveTo(7, 1.5); ctx.lineTo(11, 4.7); ctx.lineTo(7, 7.9);
        ctx.closePath();
        ctx.fill();
    }

    function lens(ctx, ink, light) {
        ctx.fillStyle = light;
        ctx.strokeStyle = ink;
        ctx.lineWidth = 1.5;
        ctx.beginPath(); ctx.arc(6.5, 6.5, 4.5, 0, Math.PI * 2); ctx.fill(); ctx.stroke();
        ctx.lineWidth = 2.5;
        ctx.beginPath(); ctx.moveTo(10, 10); ctx.lineTo(14.5, 14.5); ctx.stroke();
        ctx.lineWidth = 1.5;
    }

    function draw(ctx, name, ink, accent, light) {
        ctx.lineCap = "butt";
        switch (name) {
        case "rotateRight":
            arrowArc(ctx, ink);
            break;
        case "rotateLeft":
            ctx.translate(16, 0);
            ctx.scale(-1, 1);
            arrowArc(ctx, ink);
            break;
        case "flipH":
            // two triangles facing each other across a dotted mirror line
            ctx.fillStyle = accent;
            ctx.beginPath(); ctx.moveTo(1, 13); ctx.lineTo(6.5, 3); ctx.lineTo(6.5, 13); ctx.closePath(); ctx.fill();
            ctx.strokeStyle = ink;
            ctx.lineWidth = 1;
            ctx.beginPath(); ctx.moveTo(15, 12.5); ctx.lineTo(9.5, 3.5); ctx.lineTo(9.5, 12.5); ctx.closePath(); ctx.stroke();
            ctx.fillStyle = ink;
            for (var y = 1; y < 16; y += 3)
                ctx.fillRect(7.5, y, 1, 2);
            break;
        case "flipV":
            ctx.translate(16, 0);
            ctx.rotate(Math.PI / 2);
            draw(ctx, "flipH", ink, accent, light);
            break;
        case "crop":
            // the two L-shaped bars of a crop tool
            ctx.fillStyle = ink;
            ctx.fillRect(4, 1, 2, 11); ctx.fillRect(4, 10, 11, 2);
            ctx.fillRect(1, 4, 11, 2); ctx.fillRect(10, 4, 2, 11);
            break;
        case "zoomIn":
            lens(ctx, ink, light);
            ctx.fillStyle = ink;
            ctx.fillRect(4, 6, 5, 1); ctx.fillRect(6, 4, 1, 5);
            break;
        case "zoomOut":
            lens(ctx, ink, light);
            ctx.fillStyle = ink;
            ctx.fillRect(4, 6, 5, 1);
            break;
        case "fit":
            // a frame with arrows out to its corners
            ctx.strokeStyle = ink;
            ctx.lineWidth = 1;
            ctx.strokeRect(0.5, 2.5, 15, 11);
            ctx.fillStyle = accent;
            ctx.beginPath(); ctx.moveTo(2, 4); ctx.lineTo(6, 4); ctx.lineTo(2, 8); ctx.closePath(); ctx.fill();
            ctx.beginPath(); ctx.moveTo(14, 12); ctx.lineTo(10, 12); ctx.lineTo(14, 8); ctx.closePath(); ctx.fill();
            ctx.strokeStyle = accent;
            ctx.beginPath(); ctx.moveTo(3, 5); ctx.lineTo(13, 11); ctx.stroke();
            break;
        case "actual":
            ctx.fillStyle = ink;
            ctx.font = "bold 8px sans-serif";
            ctx.fillText("1:1", 1.5, 11);
            ctx.strokeStyle = ink;
            ctx.lineWidth = 1;
            ctx.strokeRect(0.5, 2.5, 15, 11);
            break;
        case "slideshow":
            // a screen on a stand, playing
            ctx.fillStyle = light;
            ctx.fillRect(1, 1, 14, 10);
            ctx.strokeStyle = ink;
            ctx.lineWidth = 1;
            ctx.strokeRect(0.5, 0.5, 15, 11);
            ctx.fillStyle = accent;
            ctx.beginPath(); ctx.moveTo(6, 3); ctx.lineTo(11, 6); ctx.lineTo(6, 9); ctx.closePath(); ctx.fill();
            ctx.fillStyle = ink;
            ctx.fillRect(7, 12, 2, 2); ctx.fillRect(4, 14, 8, 1);
            break;
        case "gallery":
            // a grid of little pictures
            for (var gy = 0; gy < 2; ++gy)
                for (var gx = 0; gx < 2; ++gx) {
                    ctx.fillStyle = light;
                    ctx.fillRect(1 + gx * 8, 1 + gy * 8, 6, 6);
                    ctx.strokeStyle = ink;
                    ctx.lineWidth = 1;
                    ctx.strokeRect(0.5 + gx * 8, 0.5 + gy * 8, 7, 7);
                    ctx.fillStyle = accent;
                    ctx.fillRect(2 + gx * 8, 4 + gy * 8, 4, 2);
                }
            break;
        case "adjust":
            // half-dark circle: light and contrast
            ctx.fillStyle = light;
            ctx.beginPath(); ctx.arc(8, 8, 6.5, 0, Math.PI * 2); ctx.fill();
            ctx.fillStyle = ink;
            ctx.beginPath(); ctx.arc(8, 8, 6.5, -Math.PI / 2, Math.PI / 2); ctx.closePath(); ctx.fill();
            ctx.strokeStyle = ink;
            ctx.lineWidth = 1;
            ctx.beginPath(); ctx.arc(8, 8, 6.5, 0, Math.PI * 2); ctx.stroke();
            break;
        case "wallpaper":
            // a monitor with a picture on it
            ctx.fillStyle = accent;
            ctx.fillRect(2, 2, 12, 8);
            ctx.strokeStyle = ink;
            ctx.lineWidth = 1;
            ctx.strokeRect(1.5, 1.5, 13, 9);
            ctx.fillStyle = light;
            ctx.beginPath(); ctx.moveTo(3, 9); ctx.lineTo(6, 5); ctx.lineTo(9, 9); ctx.closePath(); ctx.fill();
            ctx.fillStyle = ink;
            ctx.fillRect(6, 11, 4, 2); ctx.fillRect(3, 13, 10, 2);
            break;
        case "resize":
            ctx.strokeStyle = ink;
            ctx.lineWidth = 1;
            ctx.strokeRect(0.5, 0.5, 15, 15);
            ctx.fillStyle = light;
            ctx.fillRect(1, 7, 8, 8);
            ctx.strokeRect(0.5, 6.5, 9, 9);
            ctx.strokeStyle = accent;
            ctx.lineWidth = 1.5;
            ctx.beginPath(); ctx.moveTo(8, 8); ctx.lineTo(14, 2); ctx.stroke();
            ctx.fillStyle = accent;
            ctx.beginPath(); ctx.moveTo(15, 1); ctx.lineTo(15, 6); ctx.lineTo(10, 1); ctx.closePath(); ctx.fill();
            break;
        }
    }
}
