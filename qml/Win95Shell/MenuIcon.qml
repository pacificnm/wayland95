import QtQuick

// Start-menu icons, drawn on a 32x32 grid and scaled to `size` (32 for the
// main menu, 16 for submenus). Generic drawings, not Microsoft artwork.
Canvas {
    id: icon

    property string kind: "folder"
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
        function folder(x, y, w, h) {
            frame(x, y, Math.round(w * 0.45), 5, "#e8c048");
            frame(x, y + 3, w, h - 3, "#f8d878");
            rect(x + 1, y + 4, w - 2, 1, "#fff0b0");
        }
        function page(x, y, w, h, lines) {
            frame(x, y, w, h, "#ffffff");
            for (var i = 0; i < lines; ++i)
                rect(x + 3, y + 4 + i * 3, (i % 3 === 2) ? w / 2 - 3 : w - 6, 1, "#000080");
        }
        function appWindow(x, y, w, h, fill) {
            frame(x, y, w, h, fill);
            rect(x + 1, y + 1, w - 2, 4, "#000080");
        }

        switch (kind) {
        case "programs":
            folder(1, 10, 28, 20);
            appWindow(13, 2, 18, 15, "#ffffff");
            rect(16, 10, 10, 1, "#808080");
            rect(16, 13, 7, 1, "#808080");
            break;
        case "documents":
            page(8, 2, 17, 20, 4);
            folder(1, 12, 30, 18);
            break;
        case "settings":
            frame(9, 2, 17, 15, "#c0c0c0");   // a little control panel
            rect(12, 6, 11, 1, "#000000");
            rect(14, 5, 2, 3, "#000080");
            rect(12, 11, 11, 1, "#000000");
            rect(19, 10, 2, 3, "#000080");
            folder(1, 12, 30, 18);
            break;
        case "find":
            page(3, 2, 18, 24, 6);
            ctx.lineWidth = 3;
            ctx.strokeStyle = "#000000";
            ctx.beginPath(); ctx.moveTo(24, 23); ctx.lineTo(30, 30); ctx.stroke();
            ctx.lineWidth = 2;
            ctx.fillStyle = "#c0e0ff";
            ctx.beginPath(); ctx.arc(19, 18, 6, 0, Math.PI * 2); ctx.fill(); ctx.stroke();
            rect(16, 15, 3, 1, "#ffffff");
            break;
        case "help":
        case "about":
            frame(6, 2, 21, 28, "#000080");    // book cover
            rect(24, 3, 2, 26, "#ffffff");     // page edges
            rect(8, 3, 1, 26, "#4040c0");
            ctx.fillStyle = "#ffff00";
            ctx.font = "bold 18px sans-serif";
            ctx.textAlign = "center";
            ctx.textBaseline = "middle";
            ctx.fillText("?", 16, 17);
            break;
        case "run":
            appWindow(2, 5, 28, 22, "#ffffff");
            ctx.strokeStyle = "#000000";
            ctx.lineWidth = 2;
            ctx.beginPath(); ctx.moveTo(8, 18); ctx.lineTo(21, 18); ctx.stroke();
            ctx.fillStyle = "#000000";
            ctx.beginPath(); ctx.moveTo(19, 13); ctx.lineTo(25, 18); ctx.lineTo(19, 23); ctx.closePath(); ctx.fill();
            break;
        case "shutdown":
            frame(4, 2, 24, 19, "#c0c0c0");    // monitor
            rect(5, 3, 22, 1, "#ffffff");
            frame(7, 5, 18, 13, "#000040");    // dark screen
            rect(13, 21, 6, 2, "#808080");
            frame(3, 23, 26, 7, "#c0c0c0");    // case
            rect(23, 26, 3, 2, "#00c000");
            break;
        case "terminal":
            appWindow(1, 4, 30, 24, "#c0c0c0");
            rect(4, 10, 24, 15, "#000000");
            rect(6, 13, 2, 2, "#c0c0c0");      // ">" as two blocks
            rect(8, 15, 2, 2, "#c0c0c0");
            rect(6, 17, 2, 2, "#c0c0c0");
            rect(11, 18, 6, 2, "#c0c0c0");     // "_"
            break;
        case "files":
            folder(6, 2, 24, 18);
            folder(1, 10, 26, 20);
            break;
        case "control":
            appWindow(2, 4, 28, 24, "#c0c0c0");
            rect(6, 13, 20, 1, "#000000");
            rect(10, 11, 3, 5, "#000080");
            rect(6, 20, 20, 1, "#000000");
            rect(19, 18, 3, 5, "#000080");
            break;
        case "taskbar":
            appWindow(2, 4, 28, 24, "#008080");
            rect(3, 23, 26, 4, "#c0c0c0");
            rect(4, 24, 6, 2, "#000000");
            break;
        case "home":
            folder(1, 6, 30, 22);
            ctx.fillStyle = "#004080";
            ctx.beginPath(); ctx.moveTo(16, 11); ctx.lineTo(24, 18); ctx.lineTo(8, 18); ctx.closePath(); ctx.fill();
            rect(10, 18, 12, 7, "#004080");
            rect(14, 21, 4, 4, "#f8d878");
            break;
        case "repos":
            // stacked books (repositories) with a branch drawn over them
            frame(3, 16, 24, 6, "#2050b0");
            frame(5, 10, 24, 6, "#806000");
            frame(2, 22, 26, 6, "#008040");
            ctx.strokeStyle = "#000000";
            ctx.lineWidth = 2;
            ctx.beginPath(); ctx.moveTo(22, 2); ctx.lineTo(22, 9); ctx.moveTo(29, 2); ctx.lineTo(29, 5);
            ctx.quadraticCurveTo(29, 8, 22, 9); ctx.stroke();
            ctx.fillStyle = "#ff8000";
            ctx.beginPath(); ctx.arc(22, 3, 2.2, 0, 2 * Math.PI); ctx.fill();
            ctx.beginPath(); ctx.arc(29, 3, 2.2, 0, 2 * Math.PI); ctx.fill();
            break;
        case "solitaire":
            // two cards fanned: a blue back behind a red heart
            frame(4, 3, 16, 22, "#ffffff");
            rect(6, 5, 12, 18, "#000080");
            for (var cb = 0; cb < 4; ++cb)
                rect(7 + (cb % 2) * 6, 7 + Math.floor(cb / 2) * 8, 4, 4, "#4080ff");
            frame(12, 8, 16, 22, "#ffffff");
            ctx.fillStyle = "#d00000";
            ctx.beginPath();
            ctx.arc(17.5, 16.5, 2.5, Math.PI, 0); ctx.arc(22.5, 16.5, 2.5, Math.PI, 0);
            ctx.lineTo(20, 24); ctx.closePath(); ctx.fill();
            rect(14, 10, 2, 3, "#d00000");
            break;
        case "mines":
            // a mine on a grey cell, with a red flag beside it
            frame(2, 2, 28, 28, "#c0c0c0");
            rect(3, 3, 26, 1, "#ffffff"); rect(3, 3, 1, 26, "#ffffff");
            ctx.strokeStyle = "#000000";
            ctx.lineWidth = 1.5;
            ctx.beginPath(); ctx.moveTo(14, 9); ctx.lineTo(14, 27); ctx.moveTo(5, 18); ctx.lineTo(23, 18);
            ctx.moveTo(8, 12); ctx.lineTo(20, 24); ctx.moveTo(20, 12); ctx.lineTo(8, 24); ctx.stroke();
            ctx.fillStyle = "#000000";
            ctx.beginPath(); ctx.arc(14, 18, 6, 0, 2 * Math.PI); ctx.fill();
            rect(11, 15, 3, 3, "#ffffff");
            rect(24, 4, 1, 10, "#000000");
            ctx.fillStyle = "#ff0000";
            ctx.beginPath(); ctx.moveTo(25, 4); ctx.lineTo(25, 9); ctx.lineTo(29, 6.5); ctx.closePath(); ctx.fill();
            break;
        case "sysmon":
            // a little monitor showing a green chart
            frame(3, 4, 26, 20, "#c0c0c0");
            rect(6, 7, 20, 14, "#000000");
            ctx.strokeStyle = "#00ff00";
            ctx.lineWidth = 1.5;
            ctx.beginPath();
            ctx.moveTo(7, 18); ctx.lineTo(11, 14); ctx.lineTo(14, 16); ctx.lineTo(18, 9); ctx.lineTo(21, 12); ctx.lineTo(25, 10);
            ctx.stroke();
            rect(13, 24, 6, 2, "#808080");
            frame(9, 26, 14, 3, "#c0c0c0");
            break;
        case "update":
            // a globe with a green arrow coming down onto it
            ctx.fillStyle = "#0060c0"; ctx.strokeStyle = "#000000"; ctx.lineWidth = 1;
            ctx.beginPath(); ctx.arc(14, 17, 11, 0, Math.PI * 2); ctx.fill(); ctx.stroke();
            ctx.fillStyle = "#00a000";
            ctx.beginPath(); ctx.moveTo(6, 11); ctx.lineTo(13, 9); ctx.lineTo(15, 15); ctx.lineTo(10, 20); ctx.lineTo(5, 17); ctx.closePath(); ctx.fill();
            ctx.beginPath(); ctx.moveTo(16, 19); ctx.lineTo(22, 17); ctx.lineTo(23, 24); ctx.lineTo(17, 26); ctx.closePath(); ctx.fill();
            ctx.strokeStyle = "#80c0ff";
            ctx.beginPath(); ctx.ellipse(8, 6, 12, 22); ctx.stroke();
            // the arrow
            rect(24, 2, 4, 9, "#000000"); rect(25, 2, 2, 9, "#40e040");
            ctx.fillStyle = "#000000"; ctx.beginPath(); ctx.moveTo(20, 10); ctx.lineTo(32, 10); ctx.lineTo(26, 17); ctx.closePath(); ctx.fill();
            ctx.fillStyle = "#40e040"; ctx.beginPath(); ctx.moveTo(22, 11); ctx.lineTo(30, 11); ctx.lineTo(26, 15.5); ctx.closePath(); ctx.fill();
            break;
        case "imagewriter":
            // a disc going onto a USB stick
            ctx.fillStyle = "#c0c0c0"; ctx.strokeStyle = "#000000"; ctx.lineWidth = 1;
            ctx.beginPath(); ctx.arc(11, 11, 10, 0, Math.PI * 2); ctx.fill(); ctx.stroke();
            ctx.fillStyle = "#80c0ff"; ctx.beginPath(); ctx.arc(11, 11, 6, -2.2, -0.6); ctx.lineTo(11, 11); ctx.fill();
            ctx.fillStyle = "#ffffff"; ctx.beginPath(); ctx.arc(11, 11, 2, 0, Math.PI * 2); ctx.fill(); ctx.stroke();
            rect(19, 18, 2, 5, "#008000");
            ctx.fillStyle = "#008000"; ctx.beginPath(); ctx.moveTo(16, 22); ctx.lineTo(24, 22); ctx.lineTo(20, 26); ctx.fill();
            rect(22, 14, 9, 17, "#000000"); rect(23, 15, 7, 15, "#000080");
            rect(24, 9, 5, 5, "#000000"); rect(25, 10, 3, 4, "#c0c0c0");
            break;
        case "volume":
            // a speaker with sound waves
            frame(5, 12, 6, 9, "#c0c0c0");
            ctx.fillStyle = "#808080";
            ctx.strokeStyle = "#000000";
            ctx.lineWidth = 1;
            ctx.beginPath(); ctx.moveTo(11, 12); ctx.lineTo(18, 6); ctx.lineTo(18, 27); ctx.lineTo(11, 21);
            ctx.closePath(); ctx.fill(); ctx.stroke();
            ctx.strokeStyle = "#000080";
            ctx.lineWidth = 1.5;
            for (var vw = 0; vw < 3; ++vw) {
                ctx.beginPath(); ctx.arc(18, 16.5, 4 + vw * 3.5, -0.7, 0.7); ctx.stroke();
            }
            break;
        case "notepad":
            frame(6, 4, 21, 26, "#ffffff");         // pad
            for (var ln = 0; ln < 5; ++ln)
                rect(9, 13 + ln * 3, ln === 4 ? 8 : 15, 1, "#000080");
            rect(6, 4, 21, 6, "#4060c0");           // binding
            for (var r = 0; r < 5; ++r)
                frame(8 + r * 4, 1, 2, 6, "#c0c0c0"); // spiral rings
            break;
        case "email":
            // an envelope with a blue arrow flying in
            frame(3, 11, 26, 18, "#ffffe8");
            ctx.strokeStyle = "#000000";
            ctx.lineWidth = 1.5;
            ctx.beginPath(); ctx.moveTo(4, 12); ctx.lineTo(16, 22); ctx.lineTo(28, 12); ctx.stroke();
            ctx.beginPath(); ctx.moveTo(4, 28); ctx.lineTo(13, 19); ctx.moveTo(28, 28); ctx.lineTo(19, 19); ctx.stroke();
            ctx.strokeStyle = "#0060c0";
            ctx.lineWidth = 3;
            ctx.beginPath(); ctx.moveTo(4, 5); ctx.lineTo(20, 5); ctx.stroke();
            ctx.fillStyle = "#0060c0";
            ctx.beginPath(); ctx.moveTo(26, 5); ctx.lineTo(19, 0); ctx.lineTo(19, 10); ctx.closePath(); ctx.fill();
            break;
        case "calc":
            // a gray pocket calculator: a display and a grid of keys
            frame(6, 2, 20, 28, "#c0c0c0");
            rect(7, 3, 18, 1, "#ffffff");
            frame(8, 5, 16, 6, "#e8f0e0");
            rect(16, 7, 6, 2, "#000000");
            for (var cy = 0; cy < 4; ++cy)
                for (var cx = 0; cx < 3; ++cx)
                    frame(8 + cx * 6, 13 + cy * 4, 4, 3, cx === 2 ? "#c00000" : "#ffffff");
            break;
        case "markdown":
            // a page with a big M and a down arrow (the Markdown mark), a
            // pen across its corner
            frame(5, 2, 21, 28, "#ffffff");
            rect(8, 6, 15, 1, "#808080");
            ctx.strokeStyle = "#000080";
            ctx.lineWidth = 2.5;
            ctx.beginPath(); ctx.moveTo(9, 22); ctx.lineTo(9, 11); ctx.lineTo(13, 16); ctx.lineTo(17, 11); ctx.lineTo(17, 22); ctx.stroke();
            ctx.fillStyle = "#000080";
            ctx.beginPath(); ctx.moveTo(19, 18); ctx.lineTo(25, 18); ctx.lineTo(22, 22); ctx.closePath(); ctx.fill();
            rect(21, 11, 2, 8, "#000080");
            rect(8, 25, 15, 1, "#808080");
            ctx.strokeStyle = "#000000";
            ctx.lineWidth = 1;
            ctx.fillStyle = "#e0a000";
            ctx.beginPath(); ctx.moveTo(30, 18); ctx.lineTo(32, 20); ctx.lineTo(22, 30); ctx.lineTo(19, 31); ctx.lineTo(20, 28); ctx.closePath(); ctx.fill(); ctx.stroke();
            break;
        case "doc":
            page(7, 2, 19, 28, 7);
            break;
        case "key":
            // a gold key: round bow with a hole, and a toothed blade
            ctx.fillStyle = "#000000";
            ctx.beginPath(); ctx.arc(10, 12, 8, 0, 2 * Math.PI); ctx.fill();
            ctx.fillStyle = "#e0b000";
            ctx.beginPath(); ctx.arc(10, 12, 7, 0, 2 * Math.PI); ctx.fill();
            ctx.fillStyle = "#fff080";
            ctx.beginPath(); ctx.arc(8, 10, 2.5, 0, 2 * Math.PI); ctx.fill();
            ctx.fillStyle = "#000000";
            ctx.beginPath(); ctx.arc(10, 12, 2.5, 0, 2 * Math.PI); ctx.fill();
            frame(16, 10, 15, 5, "#e0b000");
            frame(24, 14, 3, 5, "#e0b000");
            frame(28, 14, 3, 4, "#e0b000");
            break;
        case "app":
            // a program with no icon of its own: a little window
            appWindow(3, 5, 26, 22, "#ffffff");
            rect(7, 13, 14, 2, "#808080");
            rect(7, 18, 10, 2, "#808080");
            break;
        default: // folder
            folder(1, 6, 30, 22);
            break;
        }
    }
}
