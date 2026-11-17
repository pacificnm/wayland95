import QtQuick

// Three red seven-segment digits on black, in a thin sunken border: the
// mine count and the clock. Negative numbers show a minus in front.
Rectangle {
    id: counter

    property int value: 0

    width: 41
    height: 25
    color: "#000000"
    border.width: 0

    readonly property string digits: {
        var v = Math.max(-99, Math.min(999, value));
        if (v < 0)
            return "-" + ("0" + (-v)).slice(-2);
        return ("00" + v).slice(-3);
    }

    // a hairline sunken edge
    Rectangle { x: 0; y: 0; width: parent.width; height: 1; color: "#808080" }
    Rectangle { x: 0; y: 0; width: 1; height: parent.height; color: "#808080" }
    Rectangle { x: 0; y: parent.height - 1; width: parent.width; height: 1; color: "#ffffff" }
    Rectangle { x: parent.width - 1; y: 0; width: 1; height: parent.height; color: "#ffffff" }

    Row {
        x: 2
        y: 2
        spacing: 1
        Repeater {
            model: 3
            delegate: Canvas {
                id: digit
                required property int index
                readonly property string ch: counter.digits.charAt(index)
                width: 12
                height: 21
                onChChanged: requestPaint()
                onPaint: {
                    var ctx = getContext("2d");
                    ctx.reset();
                    // segments: a top, b top-right, c bottom-right, d bottom,
                    // e bottom-left, f top-left, g middle
                    var on = {
                        "0": "abcdef", "1": "bc", "2": "abged", "3": "abgcd", "4": "fgbc",
                        "5": "afgcd", "6": "afgedc", "7": "abc", "8": "abcdefg", "9": "abcdfg", "-": "g"
                    }[ch] || "";
                    var segs = {
                        a: [[2, 1], [10, 1], [8, 3], [4, 3]],
                        d: [[2, 20], [10, 20], [8, 18], [4, 18]],
                        g: [[3, 10.5], [4, 9.5], [8, 9.5], [9, 10.5], [8, 11.5], [4, 11.5]],
                        f: [[1, 2], [3, 4], [3, 9], [1, 10]],
                        b: [[11, 2], [9, 4], [9, 9], [11, 10]],
                        e: [[1, 11], [3, 12], [3, 17], [1, 19]],
                        c: [[11, 11], [9, 12], [9, 17], [11, 19]],
                    };
                    for (var s in segs) {
                        ctx.fillStyle = on.indexOf(s) >= 0 ? "#ff0000" : "#400000";
                        var p = segs[s];
                        ctx.beginPath();
                        ctx.moveTo(p[0][0], p[0][1]);
                        for (var i = 1; i < p.length; ++i)
                            ctx.lineTo(p[i][0], p[i][1]);
                        ctx.closePath();
                        ctx.fill();
                    }
                }
            }
        }
    }
}
