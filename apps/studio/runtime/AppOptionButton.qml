import QtQuick
import Win95Ui

// OptionButton: one of several choices. Choosing one clears the others on
// the form with the same Group. With a DataField, the field holds the
// chosen button's OptionValue (its Caption if that's blank).
AppElement {
    id: option

    property string text: "Option"
    property bool checked: false
    property string group: ""
    property string optionValue: ""
    signal clicked()

    readonly property bool isOptionButton: true
    readonly property string storedValue: optionValue !== "" ? optionValue : text

    activeFocusOnTab: !designMode
    resetProperties: ["checked"]

    onFieldValueChanged: {
        if (bound)
            checked = fieldValue !== undefined && fieldValue !== null && String(fieldValue) === storedValue;
    }

    function choose() {
        var others = parent ? parent.children : [];
        for (var i = 0; i < others.length; ++i) {
            var other = others[i];
            if (other !== option && other.isOptionButton === true && other.group === group && other.field === field)
                other.checked = false;
        }
        checked = true;
        store(storedValue);
        clicked();
    }

    Rectangle {
        anchors.fill: parent
        visible: option.backColor !== ""
        color: option.fillColor
    }

    Rectangle {
        id: circle
        width: 12
        height: 12
        radius: 6
        anchors.verticalCenter: parent.verticalCenter
        color: option.enabled ? Theme.window : Theme.face
        border.color: Theme.shadow
        border.width: 1
        Rectangle {
            visible: option.checked
            anchors.centerIn: parent
            width: 4
            height: 4
            radius: 2
            color: option.enabled ? Theme.text : Theme.disabledText
        }
    }

    Text {
        id: label
        x: 18
        width: parent.width - x
        anchors.verticalCenter: parent.verticalCenter
        elide: Text.ElideRight
        text: option.text
        textFormat: Text.PlainText
        color: option.textColor
        font.family: option.family
        font.pixelSize: option.pixelSize
        font.bold: option.fontBold
        font.italic: option.fontItalic
        font.underline: option.fontUnderline
    }

    Loader {
        active: option.activeFocus
        x: label.x - 2
        y: label.y - 1
        width: Math.min(label.implicitWidth, label.width) + 4
        height: label.height + 2
        sourceComponent: FocusRect95 {}
    }

    MouseArea {
        anchors.fill: parent
        enabled: !option.designMode
        onClicked: {
            option.forceActiveFocus();
            option.choose();
        }
    }
    Keys.onSpacePressed: choose()
}
