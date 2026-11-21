import QtQuick
import Win95Ui

// A row of toggle buttons where one is down (upstream's .seg): options [{id, text, tip?}], `current` is the id that is
// down, `picked(id)` fires when another is clicked.
Row {
    id: seg
    property var options: []
    property string current: ""
    signal picked(string id)
    spacing: 0
    height: 22

    Repeater {
        model: seg.options
        delegate: PxButton {
            required property var modelData
            height: 22
            width: Math.max(implicitWidth, 44)
            text: modelData.text
            checked: seg.current === modelData.id
            onClicked: if (seg.current !== modelData.id) seg.picked(modelData.id)
            ToolTipArea { text: parent.modelData.tip || "" }
        }
    }
}
