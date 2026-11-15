import QtQuick

// A Win95 menu bar. `menus` is [{ title, items: function() -> Menu95 items }]
// (a function so enabled/checked states are fresh each time it opens).
// `popupMenu` is the window's Menu95 instance.
Item {
    id: bar

    property var menus: []
    property Menu95 popupMenu
    property int openIndex: -1

    implicitHeight: 20

    function open(i) {
        if (!popupMenu || i < 0 || i >= menus.length)
            return;
        openIndex = i;
        var title = titles.itemAt(i);
        var p = title.mapToItem(popupMenu, 0, title.height);
        popupMenu.popup(p.x, p.y, menus[i].items());
    }

    Connections {
        target: bar.popupMenu
        function onClosed() { bar.openIndex = -1; }
        function onOutsidePressed(x, y) {
            // a press on another title switches menus instead of just closing
            for (var i = 0; i < titles.count; ++i) {
                var t = titles.itemAt(i);
                var p = t.mapFromItem(bar.popupMenu, x, y);
                if (p.x >= 0 && p.y >= 0 && p.x < t.width && p.y < t.height) {
                    if (i !== bar.lastClosedIndex)
                        bar.open(i);
                    return;
                }
            }
        }
    }
    // the index that was open when the outside press arrived (so clicking
    // the open menu's own title closes it rather than reopening)
    property int lastClosedIndex: -1
    onOpenIndexChanged: if (openIndex >= 0) lastClosedIndex = openIndex

    Row {
        anchors.fill: parent
        spacing: 0

        Repeater {
            id: titles
            model: bar.menus
            delegate: Item {
                id: title
                required property var modelData
                required property int index
                width: label.implicitWidth + 14
                height: bar.height

                Bevel {
                    anchors.fill: parent
                    style: bar.openIndex === title.index ? "thinSunken"
                                                         : (titleMouse.containsMouse ? "thinRaised" : "raised")
                    filled: false
                    visible: bar.openIndex === title.index || titleMouse.containsMouse
                }
                Text {
                    id: label
                    anchors.centerIn: parent
                    anchors.horizontalCenterOffset: bar.openIndex === title.index ? 1 : 0
                    anchors.verticalCenterOffset: bar.openIndex === title.index ? 1 : 0
                    text: title.modelData.title
                    color: Theme.menuText
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontSize
                }
                MouseArea {
                    id: titleMouse
                    anchors.fill: parent
                    hoverEnabled: true
                    onPressed: {
                        bar.lastClosedIndex = -1;
                        bar.open(title.index);
                    }
                }
            }
        }
    }
}
