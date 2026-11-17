import QtQuick
import Win95Ui

// Read-only text in a sunken well, a line per row (so a long diff stays
// quick), with a scroll bar. diff: true colours added lines green, removed
// ones red, and hunk headers blue.
Bevel {
    id: view

    property string text: ""
    property bool diff: false
    property bool mono: true
    property string placeholder: ""
    readonly property var lines: {
        var l = text.length ? text.replace(/\n$/, "").split("\n") : [];
        return l.length > 5000 ? l.slice(0, 5000).concat(["", "(" + (l.length - 5000) + " more lines not shown)"]) : l;
    }

    style: "sunken"
    face: Theme.window

    function lineColor(line) {
        if (!diff)
            return Theme.text;
        if (line.startsWith("+++") || line.startsWith("---") || line.startsWith("diff ") || line.startsWith("index "))
            return "#606060";
        if (line.startsWith("@@"))
            return "#0000a0";
        if (line.startsWith("+"))
            return "#007000";
        if (line.startsWith("-"))
            return "#b00000";
        return Theme.text;
    }

    Text {
        visible: view.lines.length === 0 && view.placeholder !== ""
        anchors.centerIn: parent
        width: parent.width - 40
        horizontalAlignment: Text.AlignHCenter
        wrapMode: Text.WordWrap
        text: view.placeholder
        color: Theme.shadow
        font.family: Theme.fontFamily
        font.pixelSize: Theme.fontSize
    }

    ListView {
        id: list
        x: 3
        y: 2
        width: parent.width - 5 - (scroll.visible ? scroll.width : 0)
        height: parent.height - 4
        clip: true
        model: view.lines
        boundsBehavior: Flickable.StopAtBounds
        contentWidth: Math.max(width, widest + 8)
        flickableDirection: Flickable.AutoFlickIfNeeded
        property real widest: 0
        delegate: Text {
            required property string modelData
            height: 15
            text: modelData.length ? modelData.replace(/\t/g, "    ") : " "
            color: view.lineColor(modelData)
            font.family: view.mono ? "Liberation Mono" : Theme.fontFamily
            font.pixelSize: Theme.fontSize
            Component.onCompleted: if (implicitWidth > list.widest) list.widest = implicitWidth
        }
        MouseArea {
            anchors.fill: parent
            acceptedButtons: Qt.NoButton
            onWheel: (wheel) => {
                if (wheel.angleDelta.y)
                    scroll.scrollBy(-wheel.angleDelta.y / 120 * 45);
                else if (wheel.angleDelta.x)
                    list.contentX = Math.max(0, Math.min(list.contentWidth - list.width, list.contentX - wheel.angleDelta.x / 2));
            }
        }
    }
    ScrollBar95 {
        id: scroll
        anchors.right: parent.right
        anchors.rightMargin: 2
        y: 2
        height: parent.height - 4
        flickable: list
        stepSize: 15
        visible: needed
    }
    onTextChanged: { list.widest = 0; list.contentY = 0; list.contentX = 0; }
}
