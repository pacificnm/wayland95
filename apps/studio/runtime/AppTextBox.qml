import QtQuick
import Win95Ui

// TextBox: a sunken box to type in, one line or (MultiLine) several with a
// scroll bar. With a DataField it edits that field of the form's record.
AppElement {
    id: box

    property string text: ""
    property bool multiLine: false
    property bool readOnly: false
    property bool password: false
    property int maxLength: 0
    property string alignment: "Left"
    signal changed()
    signal accepted()
    signal gotFocus()
    signal lostFocus()

    defaultBack: Theme.window
    activeFocusOnTab: !designMode
    resetProperties: ["text"]

    readonly property Item editor: multiLine ? area : line
    readonly property int hAlign: alignment === "Center" ? TextInput.AlignHCenter
                                : alignment === "Right" ? TextInput.AlignRight : TextInput.AlignLeft

    function selectAll() { editor.selectAll(); }

    // the field -> the box (a record was loaded, or code set the field)
    property bool loading: false
    onFieldValueChanged: {
        if (!bound)
            return;
        var s = fieldValue === undefined || fieldValue === null ? ""
              : fieldInfo.type === "datetime" ? String(fieldValue).replace("T", " ") : String(fieldValue);
        if (s !== text) {
            loading = true;
            text = s;
            loading = false;
        }
    }
    onTextChanged: {
        if (line.text !== text)
            line.text = text;
        if (area.text !== text)
            area.text = text;
        if (bound && !loading)
            store(text);
    }
    onActiveFocusChanged: {
        if (designMode)
            return;
        if (activeFocus)
            gotFocus();
        else
            lostFocus();
    }

    Bevel {
        anchors.fill: parent
        style: "sunken"
        face: box.fillColor
    }

    TextInput {
        id: line
        visible: !box.multiLine
        focus: !box.multiLine
        anchors.fill: parent
        anchors.leftMargin: 4
        anchors.rightMargin: 4
        verticalAlignment: TextInput.AlignVCenter
        horizontalAlignment: box.hAlign
        clip: true
        enabled: !box.designMode
        readOnly: box.readOnly
        selectByMouse: true
        echoMode: box.password ? TextInput.Password : TextInput.Normal
        maximumLength: box.maxLength > 0 ? box.maxLength : 32767
        color: box.textColor
        selectionColor: Theme.selection
        selectedTextColor: Theme.selectionText
        font.family: box.family
        font.pixelSize: box.pixelSize
        font.bold: box.fontBold
        font.italic: box.fontItalic
        font.underline: box.fontUnderline
        onTextChanged: {
            if (!box.multiLine && text !== box.text) {
                box.text = text;
                box.changed();
            }
        }
        onAccepted: box.accepted()
    }

    Flickable {
        id: flick
        visible: box.multiLine
        x: 3
        y: 3
        width: parent.width - 6 - (bar.needed ? bar.width : 0)
        height: parent.height - 6
        clip: true
        contentWidth: width
        contentHeight: Math.max(height, area.contentHeight + 4)
        boundsBehavior: Flickable.StopAtBounds
        interactive: !box.designMode

        TextEdit {
            id: area
            focus: box.multiLine
            width: flick.width
            height: flick.contentHeight
            leftPadding: 2
            rightPadding: 2
            enabled: !box.designMode
            readOnly: box.readOnly
            selectByMouse: true
            wrapMode: TextEdit.Wrap
            textFormat: TextEdit.PlainText
            horizontalAlignment: box.hAlign
            color: box.textColor
            selectionColor: Theme.selection
            selectedTextColor: Theme.selectionText
            font.family: box.family
            font.pixelSize: box.pixelSize
            font.bold: box.fontBold
            font.italic: box.fontItalic
            font.underline: box.fontUnderline
            onTextChanged: {
                if (box.multiLine && text !== box.text) {
                    box.text = text;
                    box.changed();
                }
            }
            // keep the cursor in view
            onCursorRectangleChanged: {
                if (cursorRectangle.y < flick.contentY)
                    flick.contentY = cursorRectangle.y;
                else if (cursorRectangle.y + cursorRectangle.height > flick.contentY + flick.height)
                    flick.contentY = cursorRectangle.y + cursorRectangle.height - flick.height;
            }
        }
    }
    ScrollBar95 {
        id: bar
        visible: box.multiLine && needed
        flickable: flick
        anchors.right: parent.right
        anchors.rightMargin: 2
        y: 2
        height: parent.height - 4
    }
}
