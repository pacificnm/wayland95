import QtQuick
import Win95Ui

// One disk drawn as a bar, like Windows' Disk Management: a box naming the
// disk, then its partitions and free space side by side, each about as wide
// as its share of the disk (but never too narrow to click). An extended
// partition isn't drawn itself: the logical partitions in it have a green
// stripe instead of a blue one.
Item {
    id: map

    property var drive: null
    property int number: 0
    property var parts: []
    property int selectedIndex: -1
    signal picked(int index)
    signal menuRequested(int index, real x, real y)

    implicitHeight: 74

    function bytes(n) {
        if (n >= 1e12) return (n / 1e12).toFixed(2) + " TB";
        if (n >= 1e9) return (n / 1e9).toFixed(1) + " GB";
        if (n >= 1e6) return (n / 1e6).toFixed(0) + " MB";
        return Math.round(n / 1e3) + " KB";
    }

    // what's drawn: everything but extended partitions, with their index in parts
    readonly property var segments: {
        var out = [];
        for (var i = 0; i < parts.length; ++i)
            if (parts[i].kind !== "extended")
                out.push({ index: i, part: parts[i] });
        return out;
    }
    // widths: by size, at least `minWidth`, filling the bar exactly
    readonly property int minWidth: 56
    readonly property var widths: {
        var total = 0, avail = Math.max(0, bar.width), n = segments.length;
        for (var i = 0; i < n; ++i)
            total += Math.max(1, segments[i].part.size);
        var w = [], fixed = 0, flexTotal = 0;
        for (i = 0; i < n; ++i) {
            var share = avail * Math.max(1, segments[i].part.size) / Math.max(1, total);
            w.push(share);
        }
        // the narrow ones grow to minWidth; the others give up the room
        for (var pass = 0; pass < 3; ++pass) {
            fixed = 0; flexTotal = 0;
            for (i = 0; i < n; ++i) {
                if (w[i] <= minWidth) fixed += minWidth;
                else flexTotal += w[i];
            }
            var room = avail - fixed;
            for (i = 0; i < n; ++i)
                w[i] = w[i] <= minWidth ? minWidth : Math.max(minWidth, w[i] * room / Math.max(1, flexTotal));
        }
        // whole pixels, the last one taking up what rounding left
        var used = 0;
        for (i = 0; i < n; ++i) {
            w[i] = i === n - 1 ? Math.max(minWidth, avail - used) : Math.floor(w[i]);
            used += w[i];
        }
        return w;
    }

    // the disk's own box
    Item {
        id: label
        width: 104
        height: parent.height
        Bevel { anchors.fill: parent; style: "raised" }
        DriveIcon {
            x: 6
            y: 6
            size: 16
            name: map.drive ? map.drive.kind : "hdd"
        }
        Column {
            x: 26
            y: 6
            width: parent.width - 30
            spacing: 1
            Text {
                width: parent.width
                elide: Text.ElideRight
                text: "Disk " + map.number
                font.bold: true
                color: Theme.text
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontSize
            }
            Text {
                width: parent.width
                elide: Text.ElideRight
                text: !map.drive ? "" : map.drive.table === "gpt" ? "GPT" : map.drive.table === "dos" ? "MBR" : "No table"
                color: Theme.text
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontSize
            }
            Text {
                width: parent.width
                elide: Text.ElideRight
                text: map.drive ? map.bytes(map.drive.size) : ""
                color: Theme.text
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontSize
            }
            Text {
                width: parent.width
                elide: Text.ElideRight
                text: map.drive && map.drive.system ? "System" : map.drive && map.drive.removable ? "Removable" : "Online"
                color: Theme.text
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontSize
            }
        }
    }

    Item {
        id: bar
        anchors.left: label.right
        anchors.leftMargin: 2
        anchors.right: parent.right
        height: parent.height

        Row {
            Repeater {
                model: map.segments
                Item {
                    id: seg
                    required property var modelData
                    required property int index
                    readonly property var part: modelData.part
                    readonly property bool selected: map.selectedIndex === modelData.index
                    width: map.widths[index] || map.minWidth
                    height: bar.height

                    Rectangle {
                        anchors.fill: parent
                        color: Theme.window
                        border.color: "#000000"
                    }
                    // the stripe: blue partition, green logical, black free
                    Rectangle {
                        x: 1
                        y: 1
                        width: parent.width - 2
                        height: 9
                        color: seg.part.kind === "free" ? "#000000"
                             : seg.part.kind === "logical" ? "#008000"
                             : seg.part.kind === "whole" ? "#008080"
                             : seg.part.kind === "empty" ? "#808080" : "#000080"
                    }
                    // free space in an extended partition: a green edge round it
                    Rectangle {
                        visible: seg.part.kind === "free" && seg.part.logical
                        anchors.fill: parent
                        color: "transparent"
                        border.color: "#00a000"
                        border.width: 2
                    }
                    // chosen: hatched, as Windows does
                    Canvas {
                        visible: seg.selected
                        x: 1
                        y: 10
                        width: parent.width - 2
                        height: parent.height - 11
                        onWidthChanged: requestPaint()
                        onHeightChanged: requestPaint()
                        onPaint: {
                            var ctx = getContext("2d");
                            ctx.reset();
                            ctx.strokeStyle = "#a0a0a0";
                            ctx.lineWidth = 1;
                            for (var d = -height; d < width; d += 4) {
                                ctx.beginPath();
                                ctx.moveTo(d, height);
                                ctx.lineTo(d + height, 0);
                                ctx.stroke();
                            }
                        }
                    }
                    Rectangle {
                        visible: seg.selected
                        x: 2
                        y: 11
                        width: parent.width - 4
                        height: parent.height - 13
                        color: "transparent"
                        border.color: Theme.selection
                        border.width: 1
                    }
                    Column {
                        x: 5
                        y: 13
                        width: parent.width - 10
                        spacing: 1
                        Rectangle {
                            // the text sits on the window color over the hatching
                            width: Math.min(parent.width, Math.max(t1.implicitWidth, t2.implicitWidth, t3.implicitWidth))
                            height: t1.height + t2.height + t3.height + 2
                            color: Theme.window
                            Column {
                                spacing: 1
                                width: parent.width
                                Text {
                                    id: t1
                                    width: parent.width
                                    elide: Text.ElideRight
                                    font.bold: true
                                    text: seg.part.kind === "free" ? "Free space"
                                        : seg.part.kind === "empty" ? "Unpartitioned"
                                        : (seg.part.label ? seg.part.label + " " : "")
                                          + "(" + seg.part.device.replace("/dev/", "") + ")"
                                    color: Theme.text
                                    font.family: Theme.fontFamily
                                    font.pixelSize: Theme.fontSize
                                }
                                Text {
                                    id: t2
                                    width: parent.width
                                    elide: Text.ElideRight
                                    text: map.bytes(seg.part.size) + (seg.part.fsName ? " " + seg.part.fsName : "")
                                    color: Theme.text
                                    font.family: Theme.fontFamily
                                    font.pixelSize: Theme.fontSize
                                }
                                Text {
                                    id: t3
                                    width: parent.width
                                    elide: Text.ElideRight
                                    text: seg.part.kind === "free" ? "Unallocated"
                                        : seg.part.kind === "empty" ? "Nothing on it"
                                        : (seg.part.system ? "System" : seg.part.mounted ? "Mounted"
                                           : seg.part.inUse ? "In use" : "Healthy")
                                    color: Theme.text
                                    font.family: Theme.fontFamily
                                    font.pixelSize: Theme.fontSize
                                }
                            }
                        }
                    }
                    MouseArea {
                        anchors.fill: parent
                        acceptedButtons: Qt.LeftButton | Qt.RightButton
                        onPressed: map.picked(seg.modelData.index)
                        onClicked: (mouse) => {
                            if (mouse.button === Qt.RightButton) {
                                var p = mapToItem(map, mouse.x, mouse.y);
                                map.menuRequested(seg.modelData.index, p.x, p.y);
                            }
                        }
                    }
                }
            }
        }
    }
}
