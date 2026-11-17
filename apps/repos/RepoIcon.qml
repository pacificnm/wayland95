import QtQuick

// A repository's small icon, our own drawings on a 16x16 grid: "repo" (a
// bound book), "private" (the book with a padlock), "fork" (two branches
// joining), "archive" (a filing box), "local" (a folder: a repository only
// on this computer).
Canvas {
    id: icon

    property string kind: "repo"
    property int size: 16

    width: size
    height: size
    onKindChanged: requestPaint()

    onPaint: {
        var ctx = getContext("2d");
        ctx.reset();
        ctx.scale(size / 16, size / 16);
        function rect(x, y, w, h, c) { ctx.fillStyle = c; ctx.fillRect(x, y, w, h); }
        function book(cover) {
            rect(2, 1, 11, 14, "#000000");
            rect(3, 2, 9, 12, cover);
            rect(3, 2, 2, 12, "#000040");        // spine
            rect(6, 4, 5, 1, "#ffffff");         // label
            rect(6, 6, 4, 1, "#ffffff");
            rect(5, 14, 8, 1, "#c0c0c0");        // pages
        }
        switch (kind) {
        case "private":
            book("#806000");
            rect(8, 8, 8, 8, "#000000");         // padlock
            rect(9, 11, 6, 4, "#e0b000");
            ctx.strokeStyle = "#000000";
            ctx.lineWidth = 1.2;
            ctx.beginPath(); ctx.arc(12, 10, 2.2, Math.PI, 0); ctx.stroke();
            rect(11.5, 12, 1, 2, "#000000");
            break;
        case "fork":
            ctx.strokeStyle = "#000080";
            ctx.lineWidth = 1.6;
            ctx.beginPath();
            ctx.moveTo(4, 3); ctx.lineTo(4, 7); ctx.quadraticCurveTo(4, 9, 8, 10); ctx.lineTo(8, 13);
            ctx.moveTo(12, 3); ctx.lineTo(12, 7); ctx.quadraticCurveTo(12, 9, 8, 10);
            ctx.stroke();
            ctx.fillStyle = "#008000";
            [[4, 3], [12, 3], [8, 13]].forEach(function (p) {
                ctx.beginPath(); ctx.arc(p[0], p[1], 2.2, 0, 2 * Math.PI); ctx.fill();
            });
            break;
        case "archive":
            rect(1, 3, 14, 4, "#000000");
            rect(2, 4, 12, 2, "#c8a060");
            rect(2, 7, 12, 8, "#000000");
            rect(3, 7, 10, 7, "#e0c080");
            rect(6, 9, 4, 1, "#000000");
            break;
        case "local":
            rect(1, 3, 6, 3, "#000000");
            rect(1, 5, 14, 10, "#000000");
            rect(2, 4, 4, 2, "#e8c048");
            rect(2, 6, 12, 8, "#f8d878");
            ctx.fillStyle = "#c04000";
            ctx.beginPath(); ctx.arc(8, 10, 2.2, 0, 2 * Math.PI); ctx.fill();
            break;
        default:
            book("#2050b0");
            break;
        }
    }
}
