import QtQuick
import Win95Ui

// A Win95 list in a sunken well: raised column headers over rows, a
// selection bar and a ScrollBar95, like Explorer's Details view.
//
//   ListWell {
//       model: someListModel or a JS array
//       columns: [{ title: "Name", w: 200 (or flex: true), right?: bool,
//                   text: function (item) { return item.title; },
//                   icon?: function (item) { return "book"; } }]
//       onActivated: (index, item) => ...          // double-click, Enter
//       onMenuRequested: (index, item, x, y) => ... // right-click (x, y in the well)
//   }
//
// `item` is the row: an array element, or a model row's roles.
Bevel {
    id: well

    property var model: []
    property var columns: []
    property bool showHeader: true
    property int rowHeight: 18
    property int currentIndex: -1
    property string emptyText: ""
    property var rowColor: null      // function (item) -> text color, or null
    property var rowBold: null       // function (item) -> bool, or null
    readonly property alias list: list
    readonly property int count: list.count
    readonly property var currentItem: currentIndex >= 0 && list.currentItem ? list.currentItem.item : null

    signal activated(int index, var item)
    signal menuRequested(int index, var item, real x, real y)

    style: "sunken"
    face: Theme.window
    activeFocusOnTab: true
    onActiveFocusChanged: if (activeFocus) list.forceActiveFocus()

    // the flexible column takes what is left
    readonly property real fixedWidth: {
        var w = 0;
        for (var i = 0; i < columns.length; ++i)
            if (!columns[i].flex)
                w += columns[i].w;
        return w;
    }
    function columnWidth(c) {
        return c.flex ? Math.max(c.min || 80, list.width - fixedWidth) : c.w;
    }

    Row {
        id: header
        x: 2
        y: 2
        height: well.showHeader ? 18 : 0
        visible: well.showHeader
        width: parent.width - 4
        clip: true
        Repeater {
            model: well.columns
            Item {
                required property var modelData
                width: well.columnWidth(modelData)
                height: 18
                Bevel { anchors.fill: parent; style: "raised" }
                Text {
                    x: 4
                    width: parent.width - 8
                    height: parent.height
                    verticalAlignment: Text.AlignVCenter
                    horizontalAlignment: parent.modelData.right ? Text.AlignRight : Text.AlignLeft
                    elide: Text.ElideRight
                    textFormat: Text.PlainText
                    text: parent.modelData.title
                    color: Theme.text
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontSize
                }
            }
        }
    }

    ListView {
        id: list
        x: 2
        y: header.y + header.height
        width: parent.width - 4 - (bar.visible ? bar.width : 0)
        height: parent.height - y - 2
        clip: true
        interactive: false
        boundsBehavior: Flickable.StopAtBounds
        model: well.model
        currentIndex: well.currentIndex
        onCurrentIndexChanged: if (currentIndex >= 0) positionViewAtIndex(currentIndex, ListView.Contain)
        Keys.onPressed: (event) => {
            var n = list.count;
            switch (event.key) {
            case Qt.Key_Down: if (well.currentIndex < n - 1) well.currentIndex++; break;
            case Qt.Key_Up: if (well.currentIndex > 0) well.currentIndex--; break;
            case Qt.Key_Home: if (n) well.currentIndex = 0; break;
            case Qt.Key_End: if (n) well.currentIndex = n - 1; break;
            case Qt.Key_PageDown: if (n) well.currentIndex = Math.min(n - 1, Math.max(0, well.currentIndex) + Math.floor(list.height / well.rowHeight)); break;
            case Qt.Key_PageUp: if (n) well.currentIndex = Math.max(0, well.currentIndex - Math.floor(list.height / well.rowHeight)); break;
            case Qt.Key_Return:
            case Qt.Key_Enter: if (well.currentItem) well.activated(well.currentIndex, well.currentItem); break;
            case Qt.Key_Menu:
                if (well.currentItem)
                    well.menuRequested(well.currentIndex, well.currentItem, 20, list.y + 20);
                break;
            default: return;
            }
            event.accepted = true;
        }
        delegate: Item {
            id: row
            required property var model
            required property int index
            readonly property var item: model.modelData !== undefined ? model.modelData : model
            readonly property bool selected: index === well.currentIndex
            width: list.width
            height: well.rowHeight
            Rectangle {
                anchors.fill: parent
                color: row.selected ? Theme.selection : "transparent"
            }
            Loader {
                anchors.fill: parent
                active: row.selected && list.activeFocus
                sourceComponent: FocusRect95 { color: "#ffff80" }
            }
            Row {
                Repeater {
                    model: well.columns
                    Item {
                        id: cell
                        required property var modelData
                        required property int index
                        width: well.columnWidth(modelData)
                        height: well.rowHeight
                        readonly property string iconName: modelData.icon ? modelData.icon(row.item) : ""
                        AthenaeumIcon {
                            visible: cell.iconName !== ""
                            x: 3
                            y: Math.round((parent.height - 16) / 2)
                            name: cell.iconName
                        }
                        Text {
                            x: cell.iconName !== "" ? 23 : 4
                            width: parent.width - x - 4
                            height: parent.height
                            verticalAlignment: Text.AlignVCenter
                            horizontalAlignment: cell.modelData.right ? Text.AlignRight : Text.AlignLeft
                            elide: Text.ElideRight
                            textFormat: Text.PlainText
                            text: { var t = cell.modelData.text(row.item); return t === undefined || t === null ? "" : String(t).replace(/\n/g, " "); }
                            color: row.selected ? Theme.selectionText : well.rowColor ? well.rowColor(row.item) : Theme.text
                            font.bold: well.rowBold ? well.rowBold(row.item) : false
                            font.family: Theme.fontFamily
                            font.pixelSize: Theme.fontSize
                        }
                    }
                }
            }
            MouseArea {
                anchors.fill: parent
                acceptedButtons: Qt.LeftButton | Qt.RightButton
                onPressed: {
                    list.forceActiveFocus();
                    well.currentIndex = row.index;
                }
                onClicked: (mouse) => {
                    if (mouse.button === Qt.RightButton) {
                        var p = mapToItem(well, mouse.x, mouse.y);
                        well.menuRequested(row.index, row.item, p.x, p.y);
                    }
                }
                onDoubleClicked: (mouse) => {
                    if (mouse.button === Qt.LeftButton)
                        well.activated(row.index, row.item);
                }
            }
        }
        WheelHandler {
            onWheel: (event) => {
                list.contentY = Math.max(0, Math.min(list.contentHeight - list.height,
                                                     list.contentY - event.angleDelta.y / 120 * 3 * well.rowHeight));
            }
        }
    }
    ScrollBar95 {
        id: bar
        anchors.right: parent.right
        anchors.rightMargin: 2
        y: list.y
        height: list.height
        flickable: list
        stepSize: well.rowHeight
    }
    Text {
        visible: list.count === 0 && well.emptyText !== ""
        anchors.centerIn: parent
        width: parent.width - 40
        horizontalAlignment: Text.AlignHCenter
        wrapMode: Text.Wrap
        textFormat: Text.PlainText
        text: well.emptyText
        color: Theme.shadow
        font.family: Theme.fontFamily
        font.pixelSize: Theme.fontSize
    }
}
