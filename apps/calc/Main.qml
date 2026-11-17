import QtQuick
import QtQuick.Window
import Win95Ui
import Win95Calc

// Calculator: Standard (memory, digits, + - * /, sqrt, %, 1/x) and
// Scientific (functions, brackets, Hex/Dec/Oct/Bin and logic), in the
// style of Windows 95's. The arithmetic is CalcEngine's; every key works
// from the keyboard too (Help > Keyboard).
Window {
    id: window

    readonly property bool sci: calc.scientific
    // a fixed size for each view (the limits widened before the size
    // changes, so it can grow, then narrowed to it)
    function fitView() {
        var w = sci ? 486 : 262, h = sci ? 318 : 256;
        minimumWidth = 0; minimumHeight = 0;
        maximumWidth = 100000; maximumHeight = 100000;
        width = w; height = h;
        minimumWidth = w; maximumWidth = w;
        minimumHeight = h; maximumHeight = h;
    }
    onSciChanged: fitView()
    width: 262
    height: 256
    visible: true
    color: Theme.face
    title: "Calculator"

    readonly property color blue: "#0000c0"
    readonly property color red: "#c00000"
    readonly property color purple: "#800080"

    CalcEngine {
        id: calc
        scientific: startScientific
        onChanged: if (calcDebug) console.log("calc: " + display)
    }

    Component.onCompleted: {
        fitView();
        keys.forceActiveFocus();
    }

    function press(k) {
        calc.press(k);
        keys.forceActiveFocus();
    }
    // a digit this base has
    function digitOk(d) { return parseInt(d, 16) < calc.base; }

    // ------------------------------------------------------------- menus
    function editMenu() {
        return [
            { text: "&Copy", shortcut: "Ctrl+C", enabled: !calc.error, action: function () { clip.put(calc.copyText()); } },
            { text: "&Paste", shortcut: "Ctrl+V", action: function () { calc.paste(clip.take()); } },
        ];
    }
    function viewMenu() {
        return [
            { text: "S&tandard", checked: !calc.scientific, action: function () { calc.scientific = false; } },
            { text: "&Scientific", checked: calc.scientific, action: function () { calc.scientific = true; } },
            { separator: true },
            { text: "&Digit Grouping", checked: calc.grouping, action: function () { calc.grouping = !calc.grouping; } },
        ];
    }
    function helpMenu() {
        return [
            { text: "&Keyboard", action: function () {
                msgBox.show("Calculator Keyboard",
                            "Digits, . + - * / and = (or Enter) as they are.\n"
                            + "Esc  C        Del  CE        Backspace  Back\n"
                            + "@  sqrt       %  percent     r  1/x        F9  +/-\n"
                            + "Ctrl+M  MS   Ctrl+R  MR   Ctrl+L  MC   Ctrl+P  M+\n\n"
                            + "Scientific: ( )   ^ x^y   q x^2   # x^3   s sin   o cos   t tan\n"
                            + "n ln   l log   ! n!   p pi   ; Int   & And   | Or   ~ Not   < Lsh\n"
                            + "A-F in Hex.", "info", ["OK"], function () { keys.forceActiveFocus(); });
            } },
            { separator: true },
            { text: "&About Calculator", action: function () {
                msgBox.show("About Calculator", "Calculator\n\nFor the Wayland 95 desktop.", "info", ["OK"],
                            function () { keys.forceActiveFocus(); });
            } },
        ];
    }

    MenuBar95 {
        id: menuBar
        x: 2
        y: 2
        width: parent.width - 4
        height: 20
        popupMenu: popup
        menus: [
            { title: "Edit", items: window.editMenu },
            { title: "View", items: window.viewMenu },
            { title: "Help", items: window.helpMenu },
        ]
    }

    // the keyboard: every key goes to CalcEngine, and its button flashes
    Item {
        id: keys
        focus: true
        Keys.onPressed: (event) => {
            if ((event.modifiers & Qt.ControlModifier) && event.key === Qt.Key_C) {
                clip.put(calc.copyText());
            } else if ((event.modifiers & Qt.ControlModifier) && event.key === Qt.Key_V) {
                calc.paste(clip.take());
            } else {
                var k = calc.keyFor(event.text, event.key, event.modifiers);
                if (!k)
                    return;
                calc.press(k);
                var b = window.buttonFor(k);
                if (b)
                    b.flash();
            }
            event.accepted = true;
        }
    }
    function buttonFor(k) {
        var all = [stdBlock, sciBlock, logicBlock, hexRow, topRow];
        for (var i = 0; i < all.length; ++i) {
            var found = findIn(all[i], k);
            if (found)
                return found;
        }
        return null;
    }
    function findIn(item, k) {
        if (!item || !item.visible)
            return null;
        if (item.key === k && item.flash)
            return item;
        for (var i = 0; i < item.children.length; ++i) {
            var f = findIn(item.children[i], k);
            if (f)
                return f;
        }
        return null;
    }

    // a hidden TextEdit for the clipboard
    TextEdit {
        id: clip
        visible: false
        function put(t) { text = t; selectAll(); copy(); }
        function take() { text = ""; paste(); return text; }
    }

    // ------------------------------------------------------------- layout
    readonly property int margin: 10
    readonly property int cell: 40 // a key and the gap after it

    // Scientific's settings: Hex/Dec/Oct/Bin, Deg/Rad/Grad, Inv, Hyp
    Row {
        id: modes
        visible: window.sci
        x: window.margin
        y: menuBar.y + menuBar.height + 6
        spacing: 8
        Bevel {
            width: 196
            height: 24
            style: "thinSunken"
            Row {
                anchors.centerIn: parent
                spacing: 6
                Repeater {
                    model: [["Hex", 16], ["Dec", 10], ["Oct", 8], ["Bin", 2]]
                    RadioButton95 {
                        required property var modelData
                        text: modelData[0]
                        checked: calc.base === modelData[1]
                        onClicked: { calc.base = modelData[1]; keys.forceActiveFocus(); }
                    }
                }
            }
        }
        Bevel {
            width: 156
            height: 24
            style: "thinSunken"
            opacity: calc.base === 10 ? 1 : 0.5
            Row {
                anchors.centerIn: parent
                spacing: 6
                Repeater {
                    model: [["Deg", "deg"], ["Rad", "rad"], ["Grad", "grad"]]
                    RadioButton95 {
                        required property var modelData
                        text: modelData[0]
                        checked: calc.angle === modelData[1]
                        onClicked: { calc.angle = modelData[1]; keys.forceActiveFocus(); }
                    }
                }
            }
        }
        CheckBox95 {
            anchors.verticalCenter: parent.verticalCenter
            text: "Inv"
            checked: calc.inv
            onToggled: (on) => { calc.inv = on; keys.forceActiveFocus(); }
        }
        CheckBox95 {
            anchors.verticalCenter: parent.verticalCenter
            text: "Hyp"
            checked: calc.hyp
            onToggled: (on) => { calc.hyp = on; keys.forceActiveFocus(); }
        }
    }

    // the display
    Bevel {
        id: displayBox
        x: window.margin
        y: window.sci ? modes.y + modes.height + 6 : menuBar.y + menuBar.height + 8
        width: window.width - 2 * window.margin
        height: 26
        style: "sunken"
        face: Theme.window
        Text {
            id: displayText
            objectName: "display"
            anchors.right: parent.right
            anchors.rightMargin: 6
            anchors.verticalCenter: parent.verticalCenter
            width: parent.width - 12
            horizontalAlignment: Text.AlignRight
            elide: Text.ElideLeft
            text: calc.display
            color: Theme.text
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontSize + 3
        }
    }

    // memory, brackets, Back / CE / C
    Item {
        id: topRow
        x: window.margin
        y: displayBox.y + displayBox.height + 8
        width: displayBox.width
        height: 29

        Bevel {
            id: memBox
            width: 30
            height: 26
            y: 1
            style: "thinSunken"
            Text {
                anchors.centerIn: parent
                text: calc.memory ? "M" : ""
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontSize + 1
            }
        }
        Bevel {
            visible: window.sci
            x: memBox.width + 6
            width: 44
            height: 26
            y: 1
            style: "thinSunken"
            Text {
                anchors.centerIn: parent
                text: calc.openBrackets > 0 ? "(=" + calc.openBrackets : ""
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontSize + 1
            }
        }
        Row {
            anchors.right: parent.right
            spacing: 4
            CalcButton { width: 66; text: "Back"; key: "Back"; ink: window.red; onClicked: (k) => window.press(k) }
            CalcButton { width: 66; text: "CE"; ink: window.red; onClicked: (k) => window.press(k) }
            CalcButton { width: 66; text: "C"; key: "Clear"; ink: window.red; onClicked: (k) => window.press(k) }
        }
    }

    // Scientific's functions, left of the digits
    Grid {
        id: sciBlock
        visible: window.sci
        x: window.margin
        y: topRow.y + topRow.height + 8
        columns: 3
        spacing: 4
        Repeater {
            model: [
                { t: "sin", k: "sin" }, { t: "x^y", k: "x^y" }, { t: "ln", k: "ln" },
                { t: "cos", k: "cos" }, { t: "x^3", k: "x^3" }, { t: "log", k: "log" },
                { t: "tan", k: "tan" }, { t: "x^2", k: "x^2" }, { t: "n!", k: "n!" },
                { t: "(", k: "(" }, { t: ")", k: ")" }, { t: "pi", k: "pi" },
            ]
            CalcButton {
                required property var modelData
                text: modelData.t
                key: modelData.k
                ink: window.purple
                enabled: calc.base === 10 || ["x^y", "x^2", "x^3", "(", ")", "n!"].indexOf(modelData.k) >= 0
                onClicked: (k) => window.press(k)
            }
        }
    }

    // memory and the digits: Standard's keys
    Grid {
        id: stdBlock
        x: window.sci ? sciBlock.x + 3 * window.cell + 8 : window.margin
        y: topRow.y + topRow.height + 8
        columns: 6
        spacing: 4
        Repeater {
            model: [
                { t: "MC", c: "r" }, { t: "7" }, { t: "8" }, { t: "9" }, { t: "/", c: "r" }, { t: "sqrt", c: "b" },
                { t: "MR", c: "r" }, { t: "4" }, { t: "5" }, { t: "6" }, { t: "*", c: "r" }, { t: "%", c: "b" },
                { t: "MS", c: "r" }, { t: "1" }, { t: "2" }, { t: "3" }, { t: "-", c: "r" }, { t: "1/x", c: "b" },
                { t: "M+", c: "r" }, { t: "0" }, { t: "+/-", c: "b" }, { t: ".", c: "b" }, { t: "+", c: "r" }, { t: "=", c: "r" },
            ]
            CalcButton {
                required property var modelData
                text: modelData.t
                ink: modelData.c === "r" ? window.red : window.blue
                enabled: (modelData.t.length === 1 && modelData.t >= "0" && modelData.t <= "9") ? window.digitOk(modelData.t)
                         : modelData.t === "." || modelData.t === "sqrt" || modelData.t === "%" || modelData.t === "1/x"
                           ? calc.base === 10 : true
                onClicked: (k) => window.press(k)
            }
        }
    }

    // logic, right of the digits (Scientific)
    Grid {
        id: logicBlock
        visible: window.sci
        x: stdBlock.x + 6 * window.cell + 8
        y: stdBlock.y
        columns: 2
        spacing: 4
        Repeater {
            model: ["Mod", "And", "Or", "Xor", "Lsh", "Not", "Int"]
            CalcButton {
                required property var modelData
                text: modelData
                ink: window.red
                enabled: modelData !== "Int" || calc.base === 10
                onClicked: (k) => window.press(k)
            }
        }
    }

    // the hex digits (Scientific)
    Row {
        id: hexRow
        visible: window.sci
        x: stdBlock.x + window.cell
        y: stdBlock.y + 4 * 33 + 4
        spacing: 4
        Repeater {
            model: ["A", "B", "C", "D", "E", "F"]
            CalcButton {
                required property var modelData
                text: modelData
                // (C here is the digit; the C key above is "Clear")
                key: modelData
                ink: window.blue
                enabled: calc.base === 16
                onClicked: (k) => window.press(k)
            }
        }
    }

    Menu95 {
        id: popup
        onClosed: keys.forceActiveFocus()
    }
    MessageBox95 { id: msgBox }
}
