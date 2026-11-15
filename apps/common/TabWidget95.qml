import QtQuick

// Win95 property-sheet tabs: a row of tabs over a raised page. The selected
// tab stands 2px taller and merges into the page. Pages are the children,
// in order; only the current one is shown.
//
//   TabWidget95 {
//       titles: ["Background", "Settings"]
//       Item { ... }   // page 0
//       Item { ... }   // page 1
//   }
FocusScope {
    id: tabs

    property var titles: []
    property int currentIndex: 0
    default property alias pages: pageArea.data

    readonly property int tabHeight: 20

    // every page fills the tab area; only the current one is visible
    function updatePages() {
        for (var i = 0; i < pageArea.children.length; ++i) {
            var page = pageArea.children[i];
            if (page.anchors.fill !== pageArea)
                page.anchors.fill = pageArea;
            page.visible = (i === currentIndex);
        }
    }
    onCurrentIndexChanged: updatePages()
    Component.onCompleted: updatePages()

    Keys.onPressed: (event) => {
        if ((event.modifiers & Qt.ControlModifier) && event.key === Qt.Key_Tab) {
            currentIndex = (currentIndex + 1) % titles.length;
            event.accepted = true;
        } else if ((event.modifiers & Qt.ControlModifier) && event.key === Qt.Key_Backtab) {
            currentIndex = (currentIndex + titles.length - 1) % titles.length;
            event.accepted = true;
        }
    }

    // the page frame
    Bevel {
        id: frame
        y: tabs.tabHeight
        width: parent.width
        height: parent.height - y
        style: "raised"
    }

    Item {
        id: pageArea
        x: 8
        y: frame.y + 10
        width: frame.width - 16
        height: frame.height - 18
    }

    Row {
        x: 2
        Repeater {
            model: tabs.titles
            delegate: Item {
                id: tab
                required property string modelData
                required property int index
                readonly property bool selected: tabs.currentIndex === index

                width: label.implicitWidth + 16
                height: tabs.tabHeight + (selected ? 2 : 0)
                y: selected ? -2 : 0
                z: selected ? 1 : 0

                Rectangle { // face, covering the page's top edge when selected
                    x: 1; y: 1
                    width: parent.width - 2
                    height: parent.height - (tab.selected ? 0 : 1)
                    color: Theme.face
                }
                Rectangle { x: 2; y: 0; width: parent.width - 4; height: 1; color: Theme.highlight }   // top
                Rectangle { x: 0; y: 2; width: 1; height: parent.height - 2; color: Theme.highlight }  // left
                Rectangle { x: 1; y: 1; width: 1; height: 1; color: Theme.highlight }                  // corner
                Rectangle { x: parent.width - 2; y: 2; width: 1; height: parent.height - 2; color: Theme.shadow } // right
                Rectangle { x: parent.width - 1; y: 2; width: 1; height: parent.height - 2; color: Theme.dark }
                Rectangle { x: parent.width - 2; y: 1; width: 1; height: 1; color: Theme.dark }

                Text {
                    id: label
                    anchors.centerIn: parent
                    anchors.verticalCenterOffset: tab.selected ? -1 : 1
                    text: tab.modelData
                    color: Theme.text
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontSize
                }
                Loader {
                    active: tab.selected && tabs.activeFocus
                    x: label.x - 2
                    y: label.y - 1
                    width: label.implicitWidth + 4
                    height: label.implicitHeight + 2
                    sourceComponent: FocusRect95 {}
                }
                MouseArea {
                    anchors.fill: parent
                    onPressed: {
                        tabs.currentIndex = tab.index;
                        tabs.forceActiveFocus();
                    }
                }
            }
        }
    }
}
