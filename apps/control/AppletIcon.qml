import QtQuick

// Control Panel applet icons, drawn on a 32x32 grid and scaled to `size`.
// Generic drawings, not Microsoft artwork.
Canvas {
    id: icon

    property string kind: "system"
    property int size: 32

    width: size
    height: size
    onKindChanged: requestPaint()
    onSizeChanged: requestPaint()

    onPaint: {
        var ctx = getContext("2d");
        ctx.reset();
        ctx.scale(size / 32, size / 32);

        function rect(x, y, w, h, c) { ctx.fillStyle = c; ctx.fillRect(x, y, w, h); }
        function frame(x, y, w, h, fill) { rect(x, y, w, h, "#000000"); rect(x + 1, y + 1, w - 2, h - 2, fill); }
        function monitor(screen) {
            frame(3, 1, 26, 21, "#c0c0c0");
            rect(4, 2, 24, 1, "#ffffff");
            frame(6, 4, 20, 14, screen);
            rect(12, 22, 8, 3, "#808080");
            frame(7, 25, 18, 5, "#c0c0c0");
        }

        switch (kind) {
        case "display":
            monitor("#008080");
            // a little wallpaper: hill and sun
            rect(7, 13, 18, 4, "#00a000");
            rect(19, 6, 4, 4, "#ffff00");
            frame(9, 7, 7, 5, "#ffffff");  // a window on the desktop
            rect(10, 8, 5, 1, "#000080");
            break;
        case "datetime":
            // calendar page with a clock in front of it
            frame(2, 3, 20, 22, "#ffffff");
            rect(3, 4, 18, 5, "#c00000");
            for (var r = 0; r < 3; ++r)
                for (var c = 0; c < 4; ++c)
                    rect(5 + c * 4, 12 + r * 4, 2, 2, "#808080");
            ctx.fillStyle = "#ffffff";
            ctx.strokeStyle = "#000000";
            ctx.lineWidth = 1.5;
            ctx.beginPath(); ctx.arc(21, 21, 9, 0, Math.PI * 2); ctx.fill(); ctx.stroke();
            ctx.lineWidth = 1.5;
            ctx.beginPath(); ctx.moveTo(21, 21); ctx.lineTo(21, 14.5); ctx.moveTo(21, 21); ctx.lineTo(25.5, 22); ctx.stroke();
            break;
        case "keyboard":
            frame(1, 9, 30, 15, "#c0c0c0");
            rect(2, 10, 28, 1, "#ffffff");
            for (var row = 0; row < 3; ++row)
                for (var k = 0; k < 6; ++k)
                    frame(3 + k * 4 + (row % 2), 12 + row * 3, 3, 3, "#ffffff");
            frame(8, 21, 15, 2, "#ffffff"); // space bar
            break;
        case "taskbar":
            frame(1, 4, 30, 24, "#008080");
            frame(6, 7, 16, 11, "#c0c0c0");   // a window
            rect(7, 8, 14, 3, "#000080");
            rect(2, 22, 28, 5, "#c0c0c0");     // the taskbar
            rect(2, 22, 28, 1, "#ffffff");
            frame(3, 23, 7, 3, "#c0c0c0");     // Start
            frame(23, 23, 6, 3, "#c0c0c0");    // clock
            break;
        case "mouse":
            // a two-button mouse with its cable
            ctx.strokeStyle = "#000000";
            ctx.lineWidth = 1.5;
            ctx.beginPath(); ctx.moveTo(16, 7); ctx.bezierCurveTo(16, 2, 24, 4, 28, 1); ctx.stroke();
            ctx.fillStyle = "#e0e0e0";
            ctx.lineWidth = 1;
            ctx.beginPath();
            ctx.moveTo(8, 14); ctx.bezierCurveTo(8, 6, 24, 6, 24, 14);
            ctx.lineTo(24, 23); ctx.bezierCurveTo(24, 32, 8, 32, 8, 23);
            ctx.closePath(); ctx.fill(); ctx.stroke();
            ctx.beginPath(); ctx.moveTo(8, 15); ctx.lineTo(24, 15); ctx.moveTo(16, 8); ctx.lineTo(16, 15); ctx.stroke();
            rect(9, 10, 6, 4, "#808080"); // left button pressed
            break;
        case "sounds":
            // a speaker with sound waves
            frame(4, 11, 6, 10, "#c0c0c0");
            ctx.fillStyle = "#808080";
            ctx.strokeStyle = "#000000";
            ctx.beginPath(); ctx.moveTo(10, 11); ctx.lineTo(18, 4); ctx.lineTo(18, 28); ctx.lineTo(10, 21); ctx.closePath();
            ctx.fill(); ctx.stroke();
            ctx.lineWidth = 1.5;
            ctx.strokeStyle = "#000080";
            for (var w = 0; w < 3; ++w) {
                ctx.beginPath(); ctx.arc(18, 16, 5 + w * 4, -0.7, 0.7); ctx.stroke();
            }
            break;
        case "network":
            // three small computers on a cable
            ctx.strokeStyle = "#000000";
            ctx.lineWidth = 1;
            ctx.beginPath();
            ctx.moveTo(7.5, 12); ctx.lineTo(7.5, 20.5); ctx.lineTo(24.5, 20.5); ctx.lineTo(24.5, 12);
            ctx.moveTo(16.5, 20.5); ctx.lineTo(16.5, 23);
            ctx.stroke();
            function mini(ox, oy) {
                frame(ox + 1, oy, 13, 10, "#c0c0c0");
                rect(ox + 3, oy + 2, 9, 6, "#008080");
                frame(ox, oy + 11, 15, 4, "#c0c0c0");
            }
            mini(0, 0);
            mini(17, 0);
            mini(9, 17);
            break;
        case "bluetooth":
            // the Bluetooth symbol: a white rune on a blue oval
            ctx.fillStyle = "#0060d0";
            ctx.strokeStyle = "#002060";
            ctx.lineWidth = 1;
            ctx.beginPath();
            ctx.roundedRect(7.5, 1.5, 17, 29, 8.5, 8.5);
            ctx.fill();
            ctx.stroke();
            ctx.strokeStyle = "#ffffff";
            ctx.lineWidth = 2.2;
            ctx.lineJoin = "miter";
            ctx.beginPath();
            ctx.moveTo(11, 10);
            ctx.lineTo(21, 20.5);
            ctx.lineTo(16, 25.5);
            ctx.lineTo(16, 6.5);
            ctx.lineTo(21, 11.5);
            ctx.lineTo(11, 22);
            ctx.stroke();
            break;
        case "power":
            // a battery with a plug
            rect(9, 2, 6, 3, "#000000");
            frame(5, 4, 14, 26, "#ffffff");
            rect(7, 14, 10, 14, "#00a000");
            ctx.strokeStyle = "#000000";
            ctx.lineWidth = 2;
            ctx.beginPath(); ctx.moveTo(19, 22); ctx.bezierCurveTo(24, 22, 24, 14, 27, 14); ctx.stroke();
            frame(25, 9, 6, 9, "#c0c0c0");
            rect(27, 6, 1, 3, "#000000"); rect(29, 6, 1, 3, "#000000");
            break;
        case "accessibility":
            // a monitor with the universal access figure on it
            monitor("#0000c0");
            ctx.fillStyle = "#ffffff";
            ctx.beginPath(); ctx.arc(16, 7, 1.8, 0, 2 * Math.PI); ctx.fill();
            rect(10, 9, 12, 1.6, "#ffffff");        // arms
            rect(15, 9, 2, 5, "#ffffff");           // body
            ctx.strokeStyle = "#ffffff";
            ctx.lineWidth = 1.6;
            ctx.beginPath(); ctx.moveTo(15.5, 13.5); ctx.lineTo(13, 17.5); ctx.moveTo(16.5, 13.5); ctx.lineTo(19, 17.5); ctx.stroke();
            break;
        case "regional":
            // a globe
            ctx.fillStyle = "#000000";
            ctx.beginPath(); ctx.arc(16, 16, 14, 0, 2 * Math.PI); ctx.fill();
            ctx.fillStyle = "#2878e0";
            ctx.beginPath(); ctx.arc(16, 16, 13, 0, 2 * Math.PI); ctx.fill();
            ctx.fillStyle = "#20a020";
            ctx.beginPath();   // the Americas, roughly
            ctx.moveTo(7, 7); ctx.lineTo(14, 6); ctx.lineTo(13, 12); ctx.lineTo(10, 14); ctx.lineTo(12, 19);
            ctx.lineTo(11, 26); ctx.lineTo(8, 21); ctx.lineTo(6, 14); ctx.closePath(); ctx.fill();
            ctx.beginPath();   // Europe and Africa
            ctx.moveTo(18, 6); ctx.lineTo(25, 7); ctx.lineTo(24, 12); ctx.lineTo(27, 15); ctx.lineTo(23, 25);
            ctx.lineTo(20, 22); ctx.lineTo(19, 15); ctx.lineTo(17, 12); ctx.closePath(); ctx.fill();
            ctx.strokeStyle = "rgba(255,255,255,0.6)";
            ctx.lineWidth = 0.8;
            ctx.beginPath(); ctx.ellipse(9, 3, 14, 26); ctx.stroke();
            ctx.beginPath(); ctx.moveTo(3, 16); ctx.lineTo(29, 16); ctx.stroke();
            break;
        case "printers":
            // a yellow folder with a printer in front
            frame(1, 5, 13, 5, "#e8c048");
            frame(1, 8, 30, 14, "#f8d878");
            frame(9, 12, 13, 9, "#ffffff");
            frame(4, 19, 25, 10, "#c0c0c0");
            rect(5, 20, 23, 1, "#ffffff");
            rect(7, 22, 19, 2, "#000000");
            rect(24, 25, 3, 2, "#00c000");
            break;
        case "fonts":
            // a yellow folder with a big serif "A" on it
            frame(1, 9, 13, 5, "#e8c048");
            frame(1, 12, 30, 18, "#f8d878");
            rect(2, 13, 28, 1, "#fff0b0");
            ctx.font = "bold 18px 'Liberation Serif'";
            ctx.textAlign = "center";
            ctx.fillStyle = "#000080";
            ctx.fillText("A", 17, 28);
            ctx.fillStyle = "#c00000";
            ctx.font = "bold 10px 'Liberation Serif'";
            ctx.fillText("a", 25, 27);
            break;
        case "passwords":
            // a gold key over a lock
            frame(14, 14, 16, 15, "#c0c0c0");
            rect(15, 15, 14, 2, "#ffffff");
            frame(20, 19, 4, 6, "#000000");
            ctx.strokeStyle = "#000000";
            ctx.lineWidth = 2;
            ctx.beginPath(); ctx.arc(22, 13, 5, Math.PI, 0); ctx.stroke();
            ctx.fillStyle = "#000000";
            ctx.beginPath(); ctx.arc(8, 9, 7, 0, 2 * Math.PI); ctx.fill();
            ctx.fillStyle = "#e0b000";
            ctx.beginPath(); ctx.arc(8, 9, 6, 0, 2 * Math.PI); ctx.fill();
            ctx.fillStyle = "#000000";
            ctx.beginPath(); ctx.arc(8, 9, 2, 0, 2 * Math.PI); ctx.fill();
            ctx.save(); ctx.translate(11, 13); ctx.rotate(Math.PI / 4);
            frame(0, -2, 15, 4, "#e0b000");
            frame(9, 1, 2, 4, "#e0b000");
            frame(12, 1, 2, 3, "#e0b000");
            ctx.restore();
            break;
        case "programs":
        case "install":
            // a software box with a disc coming out of it
            frame(2, 10, 18, 20, "#c08040");
            rect(3, 11, 16, 3, "#e0a060");
            frame(5, 17, 12, 8, "#ffffff");
            rect(7, 19, 8, 2, "#000080");
            rect(7, 22, 5, 1, "#808080");
            ctx.fillStyle = "#000000";
            ctx.beginPath(); ctx.arc(22, 11, 9, 0, 2 * Math.PI); ctx.fill();
            var disc = ctx.createLinearGradient(14, 3, 30, 19);
            disc.addColorStop(0, "#e0e0ff"); disc.addColorStop(0.5, "#ffffc0"); disc.addColorStop(1, "#c0e0ff");
            ctx.fillStyle = disc;
            ctx.beginPath(); ctx.arc(22, 11, 8, 0, 2 * Math.PI); ctx.fill();
            ctx.fillStyle = "#000000";
            ctx.beginPath(); ctx.arc(22, 11, 2.5, 0, 2 * Math.PI); ctx.fill();
            ctx.fillStyle = "#ffffff";
            ctx.beginPath(); ctx.arc(22, 11, 1.5, 0, 2 * Math.PI); ctx.fill();
            if (kind === "install") {
                // a green arrow: into the computer
                ctx.fillStyle = "#008000";
                ctx.beginPath(); ctx.moveTo(26, 20); ctx.lineTo(31, 25); ctx.lineTo(26, 30); ctx.closePath(); ctx.fill();
                rect(19, 23, 8, 4, "#008000");
            }
            break;
        case "updates":
            // a globe with a circular arrow
            ctx.fillStyle = "#000000";
            ctx.beginPath(); ctx.arc(16, 16, 12, 0, 2 * Math.PI); ctx.fill();
            ctx.fillStyle = "#2060ff";
            ctx.beginPath(); ctx.arc(16, 16, 11, 0, 2 * Math.PI); ctx.fill();
            ctx.fillStyle = "#00a000";
            ctx.beginPath(); ctx.moveTo(9, 9); ctx.lineTo(16, 7); ctx.lineTo(15, 14); ctx.lineTo(10, 17); ctx.closePath(); ctx.fill();
            ctx.beginPath(); ctx.moveTo(18, 16); ctx.lineTo(24, 14); ctx.lineTo(23, 23); ctx.lineTo(18, 25); ctx.closePath(); ctx.fill();
            ctx.strokeStyle = "#ffff00";
            ctx.lineWidth = 2.5;
            ctx.beginPath(); ctx.arc(16, 16, 14, -0.3, 1.2 * Math.PI); ctx.stroke();
            ctx.fillStyle = "#ffff00";
            ctx.beginPath(); ctx.moveTo(25, 6); ctx.lineTo(31, 12); ctx.lineTo(24, 13); ctx.closePath(); ctx.fill();
            break;
        case "hardware":
            // an expansion card: green board, chips, gold contacts
            frame(1, 7, 30, 17, "#008000");
            frame(4, 10, 9, 8, "#202020");
            frame(15, 10, 6, 5, "#202020");
            frame(23, 10, 5, 5, "#202020");
            for (var hc = 0; hc < 12; ++hc)
                rect(3 + hc * 2.2, 24, 1.4, 4, "#d0a000");
            rect(2, 3, 3, 21, "#a0a0a0"); // the bracket
            rect(15, 17, 13, 2, "#c0c0c0");
            break;
        case "services":
            // two rack servers, lights on, and a cable between them
            for (var su = 0; su < 2; ++su) {
                var sy = 4 + su * 12;
                frame(3, sy, 26, 10, "#c0c0c0");
                rect(4, sy + 1, 24, 1, "#ffffff");
                frame(6, sy + 3, 12, 4, "#808080");
                rect(21, sy + 4, 2, 2, "#00ff00");
                rect(24, sy + 4, 2, 2, su === 0 ? "#ffff00" : "#00ff00");
            }
            rect(15, 26, 2, 4, "#000000");
            rect(8, 29, 16, 2, "#000000");
            break;
        case "ssh":
            // a terminal with a key in front of it
            frame(1, 3, 26, 20, "#c0c0c0");
            frame(3, 5, 22, 16, "#000000");
            rect(5, 8, 2, 2, "#c0c0c0"); rect(7, 10, 2, 2, "#c0c0c0"); rect(5, 12, 2, 2, "#c0c0c0");
            rect(11, 13, 6, 1, "#c0c0c0");
            ctx.strokeStyle = "#000000";
            ctx.fillStyle = "#ffd800";
            ctx.lineWidth = 1;
            ctx.beginPath(); ctx.arc(23, 22, 5, 0, Math.PI * 2); ctx.fill(); ctx.stroke();
            rect(22, 21, 2, 2, "#000000");
            frame(14, 26, 10, 3, "#ffd800");
            rect(15, 28, 2, 3, "#ffd800"); rect(19, 28, 2, 3, "#ffd800");
            break;
        case "postgresql":
        case "database":
            // a stack of disks: the usual database drum
            ctx.strokeStyle = "#000000";
            ctx.lineWidth = 1;
            for (var dz = 2; dz >= 0; --dz) {
                var dy = 7 + dz * 7;
                ctx.fillStyle = "#4a7fc0";
                ctx.beginPath();
                ctx.moveTo(5, dy); ctx.lineTo(5, dy + 7);
                ctx.bezierCurveTo(5, dy + 11, 27, dy + 11, 27, dy + 7);
                ctx.lineTo(27, dy);
                ctx.closePath(); ctx.fill(); ctx.stroke();
            }
            ctx.fillStyle = "#8fb8ff";
            ctx.beginPath(); ctx.ellipse(5, 3, 22, 8); ctx.fill(); ctx.stroke();
            if (kind === "postgresql") {
                rect(20, 13, 2, 2, "#00ff00");
                rect(20, 20, 2, 2, "#00ff00");
            }
            break;
        case "python":
            // a console with the interactive prompt
            frame(2, 4, 28, 24, "#c0c0c0");
            rect(3, 5, 26, 3, "#000080");
            frame(4, 9, 24, 17, "#000000");
            ctx.fillStyle = "#ffd800";
            ctx.font = "bold 9px monospace";
            ctx.fillText(">>>", 6, 18);
            rect(6, 21, 10, 1, "#30c0ff");
            break;
        case "nodejs":
            // a page of script: braces in a green tile
            frame(4, 2, 22, 28, "#ffffff");
            rect(19, 3, 6, 6, "#c0c0c0");
            frame(7, 11, 18, 14, "#3c8c3c");
            ctx.fillStyle = "#ffffff";
            ctx.font = "bold 10px monospace";
            ctx.fillText("{ }", 8, 21);
            for (var nl = 0; nl < 3; ++nl)
                rect(7, 4 + nl * 2, 10, 1, "#808080");
            break;
        case "folder":
            frame(2, 9, 28, 19, "#ffff80");
            frame(2, 6, 12, 5, "#ffff80");
            rect(3, 10, 26, 1, "#ffffff");
            break;
        case "system":
        default:
            monitor("#000080");
            // a little info "i" on the screen
            rect(15, 6, 2, 2, "#ffffff");
            rect(15, 9, 2, 6, "#ffffff");
            break;
        }
    }
}
