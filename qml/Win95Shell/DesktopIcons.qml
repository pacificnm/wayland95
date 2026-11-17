import QtQuick
import Win95Shell

// A simple desktop icon grid. Icons are drawn as plain generic shapes
// (not Microsoft's actual icon artwork - that's trademarked/copyrighted)
// just to get recognizable placeholders on the desktop. Double-clicking
// one is a no-op hook for now; wire `onDoubleClicked` up to a real action
// (launching a file manager, opening a settings window, etc.) later.
GridView {
    id: grid
    cellWidth: 80
    cellHeight: 80
    interactive: false

    // an icon was right-clicked: its entry (label, kind, program, arg), and
    // where, in this item's coordinates (the desktop shows its menu)
    signal menuRequested(var entry, real x, real y)
    function open(entry) {
        if (entry.program)
            Launcher.launch(entry.program, entry.program === "win95control" ? ["--applet", entry.arg]
                                           : entry.arg ? [entry.arg] : []);
    }

    // Draws the generic computer (monitor + case) on a 32x32 grid, at
    // (ox, oy) scaled by s. Shared by the My Computer and Network icons.
    function drawComputer(ctx, ox, oy, s) {
        function rect(x, y, w, h, color) {
            ctx.fillStyle = color;
            ctx.fillRect(ox + x * s, oy + y * s, Math.max(1, w * s), Math.max(1, h * s));
        }
        function frame(x, y, w, h, fill) {
            rect(x, y, w, h, "#000000");
            rect(x + 1, y + 1, w - 2, h - 2, fill);
        }
        // monitor
        frame(4, 1, 24, 20, "#c0c0c0");
        rect(5, 2, 22, 1, "#ffffff");
        rect(5, 2, 1, 18, "#ffffff");
        frame(7, 4, 18, 13, "#008080");   // screen
        rect(8, 5, 6, 1, "#40c0c0");      // glare
        rect(13, 21, 6, 2, "#808080");    // neck
        // case
        frame(2, 23, 28, 8, "#c0c0c0");
        rect(3, 24, 26, 1, "#ffffff");
        rect(5, 26, 12, 2, "#808080");    // drive slot
        rect(24, 26, 3, 2, "#00c000");    // power light
    }

    model: ListModel {
        ListElement { label: "My Computer"; kind: "computer"; program: "win95files";    arg: "computer:" }
        ListElement { label: "Network";     kind: "network";  program: "win95control";  arg: "network" }
        ListElement { label: "Recycle Bin"; kind: "bin";      program: "win95files";    arg: "trash:" }
        ListElement { label: "Terminal";    kind: "terminal"; program: "win95terminal"; arg: "" }
    }

    delegate: Item {
        width: grid.cellWidth
        height: grid.cellHeight

        property bool selected: false

        Column {
            anchors.centerIn: parent
            spacing: 4

            Item {
                width: 32
                height: 32
                anchors.horizontalCenter: parent.horizontalCenter

                Canvas {
                    // a monitor on a desktop case
                    visible: kind === "computer"
                    anchors.fill: parent
                    onPaint: grid.drawComputer(getContext("2d"), 0, 0, 1)
                }
                Canvas {
                    // three small computers wired to each other
                    visible: kind === "network"
                    anchors.fill: parent
                    onPaint: {
                        var ctx = getContext("2d");
                        // cabling first, so the machines sit on top of it
                        ctx.strokeStyle = "#000000";
                        ctx.lineWidth = 1;
                        ctx.beginPath();
                        ctx.moveTo(8.5, 13); ctx.lineTo(8.5, 20.5); ctx.lineTo(24.5, 20.5); ctx.lineTo(24.5, 13);
                        ctx.moveTo(16.5, 20.5); ctx.lineTo(16.5, 24);
                        ctx.stroke();
                        grid.drawComputer(ctx, 0, 0, 0.5);
                        grid.drawComputer(ctx, 16, 0, 0.5);
                        grid.drawComputer(ctx, 8, 17, 0.5);
                    }
                }
                Rectangle {
                    // a little black console window with a prompt
                    visible: kind === "terminal"
                    anchors.fill: parent
                    anchors.margins: 3
                    color: "#c0c0c0"
                    border.color: "black"
                    Rectangle { x: 1; y: 1; width: parent.width - 2; height: 5; color: "#000080" }
                    Rectangle {
                        x: 3; y: 8; width: parent.width - 6; height: parent.height - 11
                        color: "black"
                        Text {
                            x: 2; y: 1
                            text: ">_"
                            color: "#c0c0c0"
                            font.family: "DejaVu Sans Mono"
                            font.pixelSize: 9
                            font.bold: true
                        }
                    }
                }
                Canvas {
                    // a wire-mesh wastebasket; crumpled paper pokes out of
                    // the top while the Recycle Bin has anything in it
                    id: binIcon
                    visible: kind === "bin"
                    anchors.fill: parent
                    readonly property bool full: TrashWatcher.full
                    onFullChanged: requestPaint()

                    onPaint: {
                        var ctx = getContext("2d");
                        ctx.reset();
                        ctx.lineWidth = 1;

                        // tapered body
                        ctx.beginPath();
                        ctx.moveTo(5.5, 9.5);
                        ctx.lineTo(26.5, 9.5);
                        ctx.lineTo(23.5, 30.5);
                        ctx.lineTo(8.5, 30.5);
                        ctx.closePath();
                        ctx.fillStyle = "#e8e8e8";
                        ctx.fill();

                        // mesh: ribs converging toward the base, plus bands
                        ctx.strokeStyle = "#808080";
                        for (var i = 1; i < 6; ++i) {
                            var t = i / 6;
                            ctx.beginPath();
                            ctx.moveTo(5.5 + 21 * t, 9.5);
                            ctx.lineTo(8.5 + 15 * t, 30.5);
                            ctx.stroke();
                        }
                        for (var y = 14.5; y < 30; y += 5) {
                            var inset = (y - 9.5) / 21 * 3;
                            ctx.beginPath();
                            ctx.moveTo(5.5 + inset, y);
                            ctx.lineTo(26.5 - inset, y);
                            ctx.stroke();
                        }

                        ctx.strokeStyle = "#000000";
                        ctx.beginPath();
                        ctx.moveTo(5.5, 9.5);
                        ctx.lineTo(26.5, 9.5);
                        ctx.lineTo(23.5, 30.5);
                        ctx.lineTo(8.5, 30.5);
                        ctx.closePath();
                        ctx.stroke();

                        // crumpled paper
                        if (binIcon.full) {
                            ctx.fillStyle = "#ffffff";
                            var balls = [[11, 7, 4], [17, 5, 4.5], [22, 7.5, 3.5]];
                            for (var b = 0; b < balls.length; ++b) {
                                ctx.beginPath();
                                ctx.arc(balls[b][0], balls[b][1], balls[b][2], 0, Math.PI * 2);
                                ctx.fill();
                                ctx.stroke();
                            }
                            ctx.strokeStyle = "#808080";
                            ctx.beginPath();
                            ctx.moveTo(15, 4); ctx.lineTo(18, 7);
                            ctx.moveTo(10, 6); ctx.lineTo(12, 8);
                            ctx.stroke();
                            ctx.strokeStyle = "#000000";
                        }

                        // rim, drawn over the paper
                        ctx.fillStyle = "#c0c0c0";
                        ctx.fillRect(3.5, 8.5, 25, 3);
                        ctx.strokeRect(3.5, 8.5, 25, 3);
                        ctx.fillStyle = "#ffffff";
                        ctx.fillRect(4.5, 9.5, 23, 1);
                    }
                }
            }

            Rectangle {
                anchors.horizontalCenter: parent.horizontalCenter
                color: parent.parent.selected ? Theme.selectionBlue : "transparent"
                width: label.implicitWidth + 4
                height: label.implicitHeight + 2

                Text {
                    id: label
                    anchors.centerIn: parent
                    text: model.label
                    color: "white"
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontSize
                }
            }
        }

        MouseArea {
            anchors.fill: parent
            acceptedButtons: Qt.LeftButton | Qt.RightButton
            onClicked: (mouse) => {
                if (mouse.button === Qt.RightButton) {
                    parent.selected = true;
                    var p = mapToItem(grid, mouse.x, mouse.y);
                    grid.menuRequested({ label: model.label, kind: model.kind, program: model.program, arg: model.arg },
                                       p.x, p.y);
                } else {
                    parent.selected = !parent.selected;
                }
            }
            onDoubleClicked: (mouse) => {
                if (mouse.button === Qt.LeftButton)
                    grid.open({ program: model.program, arg: model.arg });
            }
        }
    }
}
