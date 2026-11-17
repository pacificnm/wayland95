import QtQuick
import Win95Ui

// A small toolbar button (WordPad's): a 16x16 drawing (`icon`) or a styled
// letter (`letter`, with bold/italic/underline/strike), flat until the
// mouse is over it, pushed in while `checked`. `tip` shows in the status bar.
Item {
    id: button

    property string icon: ""
    property string letter: ""
    property bool letterBold: false
    property bool letterItalic: false
    property bool letterUnderline: false
    property bool letterStrike: false
    property string tip: ""
    property bool enabled: true
    property bool checked: false
    readonly property bool hovered: mouse.containsMouse
    signal clicked()

    width: 23
    height: 22

    readonly property bool down: (mouse.pressed && mouse.containsMouse) || checked

    Bevel {
        anchors.fill: parent
        visible: button.down || (mouse.containsMouse && button.enabled)
        style: button.down ? "pressed" : "raised"
        face: button.checked && !mouse.pressed ? Theme.light : Theme.face
    }

    Item {
        width: 16
        height: 16
        x: (parent.width - width) / 2 + (button.down ? 1 : 0)
        y: (parent.height - height) / 2 + (button.down ? 1 : 0)

        Text {
            visible: button.letter.length > 0
            anchors.centerIn: parent
            text: button.letter
            color: button.enabled ? "#000000" : Theme.disabledText
            font.family: "Liberation Serif"
            font.pixelSize: 15
            font.bold: button.letterBold
            font.italic: button.letterItalic
            font.underline: button.letterUnderline
            font.strikeout: button.letterStrike
        }

        Canvas {
            id: canvas
            visible: button.icon.length > 0
            anchors.fill: parent
            property bool live: button.enabled
            onLiveChanged: requestPaint()
            onPaint: {
                var ctx = getContext("2d");
                ctx.reset();
                var on = button.enabled;
                var ink = on ? "#000000" : "#808080";
                var blue = on ? "#000080" : "#808080";
                function rect(x, y, w, h, c) { ctx.fillStyle = c; ctx.fillRect(x, y, w, h); }
                function frame(x, y, w, h, fill) { rect(x, y, w, h, ink); rect(x + 1, y + 1, w - 2, h - 2, fill); }
                function lines(x, y, w, n, gap) { for (var i = 0; i < n; ++i) rect(x, y + i * gap, w, 1, ink); }
                function page(x, y) {
                    ctx.fillStyle = ink;
                    ctx.beginPath(); ctx.moveTo(x, y); ctx.lineTo(x + 7, y); ctx.lineTo(x + 10, y + 3);
                    ctx.lineTo(x + 10, y + 13); ctx.lineTo(x, y + 13); ctx.closePath(); ctx.fill();
                    ctx.fillStyle = "#ffffff";
                    ctx.beginPath(); ctx.moveTo(x + 1, y + 1); ctx.lineTo(x + 6, y + 1); ctx.lineTo(x + 9, y + 4);
                    ctx.lineTo(x + 9, y + 12); ctx.lineTo(x + 1, y + 12); ctx.closePath(); ctx.fill();
                }
                switch (button.icon) {
                case "new":
                    page(3, 1);
                    break;
                case "open":
                    frame(1, 5, 12, 9, on ? "#f8d878" : "#c0c0c0");
                    frame(1, 4, 5, 2, on ? "#e8c048" : "#c0c0c0");
                    ctx.strokeStyle = blue; ctx.lineWidth = 1.5;
                    ctx.beginPath(); ctx.moveTo(9, 3); ctx.quadraticCurveTo(12, 0, 14, 4); ctx.stroke();
                    rect(13, 4, 3, 1, blue);
                    break;
                case "save":
                    frame(1, 1, 14, 14, on ? "#000080" : "#808080");
                    rect(4, 2, 8, 5, "#ffffff");
                    rect(4, 10, 8, 5, "#c0c0c0");
                    rect(9, 11, 2, 3, ink);
                    break;
                case "find":
                    ctx.strokeStyle = ink; ctx.lineWidth = 1.5;
                    ctx.beginPath(); ctx.arc(6, 6, 4, 0, 2 * Math.PI); ctx.stroke();
                    ctx.lineWidth = 2.5;
                    ctx.beginPath(); ctx.moveTo(9, 9); ctx.lineTo(14, 14); ctx.stroke();
                    break;
                case "cut":
                    ctx.strokeStyle = ink; ctx.lineWidth = 1;
                    ctx.beginPath(); ctx.moveTo(5, 1); ctx.lineTo(10, 10); ctx.moveTo(11, 1); ctx.lineTo(6, 10); ctx.stroke();
                    ctx.strokeStyle = blue; ctx.lineWidth = 1.5;
                    ctx.beginPath(); ctx.arc(5, 12, 2.5, 0, 2 * Math.PI); ctx.stroke();
                    ctx.beginPath(); ctx.arc(11, 12, 2.5, 0, 2 * Math.PI); ctx.stroke();
                    break;
                case "copy":
                    page(1, 0);
                    page(5, 3);
                    lines(7, 8, 5, 3, 2);
                    break;
                case "paste":
                    frame(1, 2, 11, 13, on ? "#a06020" : "#c0c0c0");
                    frame(4, 1, 5, 3, "#c0c0c0");
                    page(6, 3);
                    lines(8, 8, 5, 3, 2);
                    break;
                case "undo":
                    ctx.strokeStyle = blue; ctx.lineWidth = 2;
                    ctx.beginPath(); ctx.arc(8, 9, 5, Math.PI * 1.1, Math.PI * 2.4, false); ctx.stroke();
                    ctx.fillStyle = blue;
                    ctx.beginPath(); ctx.moveTo(0, 6); ctx.lineTo(6, 6); ctx.lineTo(3, 11); ctx.closePath(); ctx.fill();
                    break;
                case "bullets":
                    for (var b = 0; b < 3; ++b) {
                        rect(1, 2 + b * 5, 3, 3, blue);
                        rect(6, 3 + b * 5, 9, 1, ink);
                    }
                    break;
                case "numbers":
                    ctx.fillStyle = blue;
                    ctx.font = "bold 6px sans-serif";
                    for (var n = 0; n < 3; ++n) {
                        ctx.fillText(String(n + 1), 1, 6 + n * 5);
                        rect(6, 3 + n * 5, 9, 1, ink);
                    }
                    break;
                case "checks":
                    for (var c = 0; c < 2; ++c) {
                        frame(1, 2 + c * 7, 6, 6, "#ffffff");
                        rect(9, 4 + c * 7, 6, 1, ink);
                    }
                    ctx.strokeStyle = blue; ctx.lineWidth = 1.5;
                    ctx.beginPath(); ctx.moveTo(2, 4); ctx.lineTo(4, 6); ctx.lineTo(8, 0); ctx.stroke();
                    break;
                case "indent":
                case "outdent":
                    lines(1, 1, 14, 2, 13);
                    lines(7, 5, 8, 3, 3);
                    ctx.fillStyle = blue;
                    ctx.beginPath();
                    if (button.icon === "indent") { ctx.moveTo(1, 5); ctx.lineTo(5, 8); ctx.lineTo(1, 11); }
                    else { ctx.moveTo(5, 5); ctx.lineTo(1, 8); ctx.lineTo(5, 11); }
                    ctx.closePath(); ctx.fill();
                    break;
                case "quote":
                    rect(1, 1, 2, 14, blue);
                    lines(5, 3, 10, 4, 3);
                    break;
                case "code":
                    ctx.strokeStyle = blue; ctx.lineWidth = 1.5;
                    ctx.beginPath(); ctx.moveTo(5, 3); ctx.lineTo(1, 8); ctx.lineTo(5, 13);
                    ctx.moveTo(11, 3); ctx.lineTo(15, 8); ctx.lineTo(11, 13); ctx.stroke();
                    ctx.strokeStyle = ink; ctx.lineWidth = 1;
                    ctx.beginPath(); ctx.moveTo(9.5, 2); ctx.lineTo(6.5, 14); ctx.stroke();
                    break;
                case "link":
                    ctx.strokeStyle = blue; ctx.lineWidth = 1.5;
                    ctx.beginPath(); ctx.moveTo(7, 5); ctx.lineTo(4, 5); ctx.arc(4, 8, 3, Math.PI * 1.5, Math.PI * 0.5, true); ctx.lineTo(7, 11); ctx.stroke();
                    ctx.beginPath(); ctx.moveTo(9, 5); ctx.lineTo(12, 5); ctx.arc(12, 8, 3, Math.PI * 1.5, Math.PI * 0.5, false); ctx.lineTo(9, 11); ctx.stroke();
                    rect(5, 7.5, 6, 1.5, ink);
                    break;
                case "picture":
                    frame(0, 2, 16, 12, on ? "#80c0ff" : "#c0c0c0");
                    ctx.fillStyle = on ? "#008000" : "#808080";
                    ctx.beginPath(); ctx.moveTo(1, 13); ctx.lineTo(6, 6); ctx.lineTo(10, 11); ctx.lineTo(12, 9); ctx.lineTo(15, 13); ctx.closePath(); ctx.fill();
                    rect(11, 4, 3, 3, on ? "#ffff00" : "#ffffff");
                    break;
                case "table":
                    frame(0, 1, 16, 14, "#ffffff");
                    rect(1, 2, 14, 3, on ? "#000080" : "#808080");
                    rect(5, 2, 1, 13, ink); rect(10, 2, 1, 13, ink);
                    rect(1, 5, 14, 1, ink); rect(1, 10, 14, 1, ink);
                    break;
                case "rule":
                    lines(1, 2, 14, 2, 2);
                    rect(0, 7, 16, 2, blue);
                    lines(1, 11, 14, 2, 2);
                    break;
                case "datetime":
                    frame(1, 2, 14, 13, "#ffffff");
                    rect(2, 3, 12, 3, on ? "#c00000" : "#808080");
                    for (var d = 0; d < 6; ++d)
                        rect(3 + (d % 3) * 4, 8 + Math.floor(d / 3) * 3, 2, 2, ink);
                    break;
                }
            }
        }
    }

    MouseArea {
        id: mouse
        anchors.fill: parent
        enabled: button.enabled
        hoverEnabled: true
        onClicked: button.clicked()
    }
}
