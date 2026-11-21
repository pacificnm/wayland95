import QtQuick

// Disk Manager's drawn icons, on a 32-pixel grid scaled to `size`:
// "hdd", "ssd", "nvme", "usb", "card", "virtual", and "disks" (the program).
Canvas {
    id: icon

    property string name: "hdd"
    property int size: 16

    width: size
    height: size
    onNameChanged: requestPaint()
    onSizeChanged: requestPaint()

    onPaint: {
        var ctx = getContext("2d");
        ctx.reset();
        ctx.scale(size / 32, size / 32);
        function rect(x, y, w, h, c) { ctx.fillStyle = c; ctx.fillRect(x, y, w, h); }
        function frame(x, y, w, h, fill) { rect(x, y, w, h, "#000000"); rect(x + 1, y + 1, w - 2, h - 2, fill); }
        // a 3.5" hard disk, seen from above, slightly tilted: the box and its
        // light
        function hardDisk(y) {
            frame(2, y, 28, 14, "#c0c0c0");
            rect(3, y + 1, 26, 1, "#ffffff");
            rect(3, y + 1, 1, 12, "#ffffff");
            rect(3, y + 12, 26, 1, "#808080");
            rect(4, y + 10, 24, 1, "#808080");
            rect(23, y + 5, 4, 2, "#00c000");
        }
        switch (name) {
        case "ssd":
            // a 2.5" box with a label
            frame(3, 9, 26, 16, "#404040");
            rect(4, 10, 24, 1, "#808080");
            frame(6, 12, 20, 9, "#e0e0ff");
            rect(8, 14, 10, 2, "#000080");
            rect(8, 17, 14, 1, "#808080");
            break;
        case "nvme":
            // an M.2 stick: green board, chips, gold contacts
            rect(1, 11, 30, 11, "#000000");
            rect(2, 12, 28, 9, "#008040");
            rect(4, 13, 8, 7, "#202020");
            rect(14, 13, 7, 7, "#202020");
            rect(23, 14, 3, 5, "#202020");
            for (var c = 0; c < 6; ++c)
                rect(28, 13 + c * 1.3, 2, 0.8, "#e0c040");
            rect(1, 15, 2, 3, "#ffffff");
            break;
        case "usb":
            // a USB stick
            frame(3, 11, 20, 11, "#000080");
            rect(4, 12, 18, 1, "#4040c0");
            frame(22, 13, 8, 7, "#c0c0c0");
            rect(25, 15, 2, 1, "#000000");
            rect(25, 17, 2, 1, "#000000");
            rect(6, 15, 3, 3, "#ff4040");
            break;
        case "card":
            // a memory card with a cut corner
            ctx.fillStyle = "#000000";
            ctx.beginPath(); ctx.moveTo(8, 4); ctx.lineTo(25, 4); ctx.lineTo(25, 28); ctx.lineTo(6, 28); ctx.lineTo(6, 6); ctx.closePath(); ctx.fill();
            ctx.fillStyle = "#2060c0";
            ctx.beginPath(); ctx.moveTo(9, 5); ctx.lineTo(24, 5); ctx.lineTo(24, 27); ctx.lineTo(7, 27); ctx.lineTo(7, 7); ctx.closePath(); ctx.fill();
            for (var g = 0; g < 4; ++g)
                rect(10 + g * 3.5, 6, 2, 5, "#e0c040");
            rect(9, 16, 13, 8, "#ffffff");
            break;
        case "virtual":
            hardDisk(10);
            // a little screen in front: a disk inside a computer
            frame(0, 18, 12, 10, "#ffffff");
            rect(1, 19, 10, 6, "#008080");
            break;
        case "disks":
            // the program: two disks stacked and a pie of partitions
            hardDisk(14);
            hardDisk(4);
            ctx.fillStyle = "#000000";
            ctx.beginPath(); ctx.arc(10, 24, 7.5, 0, Math.PI * 2); ctx.fill();
            ctx.fillStyle = "#0000ff";
            ctx.beginPath(); ctx.moveTo(10, 24); ctx.arc(10, 24, 6.5, -Math.PI / 2, Math.PI * 0.6); ctx.closePath(); ctx.fill();
            ctx.fillStyle = "#ff00ff";
            ctx.beginPath(); ctx.moveTo(10, 24); ctx.arc(10, 24, 6.5, Math.PI * 0.6, Math.PI * 1.5); ctx.closePath(); ctx.fill();
            break;
        default:
            hardDisk(9);
            break;
        }
    }
}
