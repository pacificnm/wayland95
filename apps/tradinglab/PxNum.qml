import QtQuick
import Win95Ui

// A number box that keeps what is being typed and saves when you leave it or press Enter (upstream PortfolioView's
// NumCell). Escape puts the saved value back. `committed(n)` fires only for a real number that differs from `value`.
Row {
    id: cell
    property var value: null
    property string prefix: ""
    property string suffix: ""
    property int fieldWidth: 84
    property string placeholder: ""
    property string tip: ""
    signal committed(real n)

    spacing: 2
    height: 19

    function show(v) { return v === null || v === undefined || !isFinite(v) ? "" : String(Math.round(v * 1e6) / 1e6); }
    function commit() {
        const t = field.text;
        const n = Number(t.replace(/[$,%\s]/g, ""));
        if (t.trim() === "" || !isFinite(n)) { field.text = show(value); return; }
        if (n !== value) committed(n);
    }
    onValueChanged: if (!field.input.activeFocus) field.text = show(value)
    Component.onCompleted: field.text = show(value)

    Text {
        visible: cell.prefix !== ""
        anchors.verticalCenter: parent.verticalCenter
        text: cell.prefix
        color: Theme.shadow
        font.family: Theme.fontFamily
        font.pixelSize: Theme.fontSize
    }
    TextField95 {
        id: field
        width: cell.fieldWidth
        height: 19
        input.horizontalAlignment: TextInput.AlignRight
        onAccepted: field.input.focus = false
        onEscaped: { field.text = cell.show(cell.value); field.input.focus = false; }
        Connections {
            target: field.input
            function onActiveFocusChanged() { if (!field.input.activeFocus) cell.commit(); }
        }
        Text {
            visible: field.text === "" && !field.input.activeFocus
            anchors.right: parent.right
            anchors.rightMargin: 4
            anchors.verticalCenter: parent.verticalCenter
            text: cell.placeholder
            color: Theme.shadow
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontSize
        }
        ToolTipArea { text: cell.tip }
    }
    Text {
        visible: cell.suffix !== ""
        anchors.verticalCenter: parent.verticalCenter
        text: cell.suffix
        color: Theme.shadow
        font.family: Theme.fontFamily
        font.pixelSize: Theme.fontSize
    }
}
