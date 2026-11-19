import QtQuick
import Win95Ui

// A sunken list of programs: icon, name, and optionally a second line
// (the summary) and a note on the right ("Installed", "Flathub"...).
// Items are maps with at least `key`, `name` and `icon`.
Bevel {
    id: list

    property var model: []
    property string selectedKey: ""
    readonly property var selected: {
        for (var i = 0; i < model.length; ++i)
            if (model[i].key === selectedKey)
                return model[i];
        return null;
    }
    // large rows: 32px icon, name in bold, summary underneath
    property bool large: false
    // check boxes in front (Updates); checked keys are in `checked`
    property bool checkable: false
    property var checked: ({})
    // the note on the right of an item
    property var noteFor: function (item) { return ""; }
    // shown when the list is empty
    property string emptyText: ""
    // instead of the item's `icon` image: a component, which sees the
    // item as parent.item
    property Component iconComponent: null

    signal activated(var item)
    signal checkToggled(string key, bool on)

    style: "sunken"
    face: Theme.window

    function select(i) {
        if (model.length === 0)
            return;
        i = Math.max(0, Math.min(model.length - 1, i));
        selectedKey = model[i].key;
        view.positionViewAtIndex(i, ListView.Contain);
    }
    function selectedIndex() {
        for (var i = 0; i < model.length; ++i)
            if (model[i].key === selectedKey)
                return i;
        return -1;
    }

    ListView {
        id: view
        x: 2
        y: 2
        width: parent.width - 4 - (bar.needed ? bar.width : 0)
        height: parent.height - 4
        clip: true
        interactive: false
        model: list.model
        activeFocusOnTab: true
        keyNavigationEnabled: false
        Keys.onUpPressed: list.select(list.selectedIndex() - 1)
        Keys.onDownPressed: list.select(list.selectedIndex() + 1)
        Keys.onPressed: (event) => {
            var page = Math.max(1, Math.floor(view.height / (list.large ? 40 : 20)));
            if (event.key === Qt.Key_PageDown) list.select(list.selectedIndex() + page);
            else if (event.key === Qt.Key_PageUp) list.select(list.selectedIndex() - page);
            else if (event.key === Qt.Key_Home) list.select(0);
            else if (event.key === Qt.Key_End) list.select(list.model.length - 1);
            else if (event.key === Qt.Key_Space && list.checkable && list.selected)
                list.checkToggled(list.selectedKey, !list.checked[list.selectedKey]);
            else if ((event.key === Qt.Key_Return || event.key === Qt.Key_Enter) && list.selected)
                list.activated(list.selected);
            else return;
            event.accepted = true;
        }

        delegate: Item {
            id: row
            required property var modelData
            required property int index
            readonly property bool isSelected: modelData.key === list.selectedKey
            readonly property string note: list.noteFor(modelData)
            width: view.width
            height: list.large ? 40 : 20

            Rectangle {
                anchors.fill: parent
                color: row.isSelected ? Theme.selection : "transparent"
            }

            CheckBox95 {
                id: check
                visible: list.checkable
                x: 4
                anchors.verticalCenter: parent.verticalCenter
                checked: !!list.checked[row.modelData.key]
                onToggled: (on) => {
                    list.checkToggled(row.modelData.key, on);
                    // back to showing `checked`, which the handler may have
                    // left as it was (Services asks first)
                    checked = Qt.binding(function () { return !!list.checked[row.modelData.key]; });
                }
            }

            Item {
                id: iconBox
                x: list.checkable ? 26 : 4
                anchors.verticalCenter: parent.verticalCenter
                width: list.large ? 32 : 16
                height: width
                Loader {
                    anchors.fill: parent
                    active: !!list.iconComponent
                    sourceComponent: list.iconComponent
                    property var item: row.modelData
                    property int size: iconBox.width
                }
                Image {
                    id: image
                    visible: !list.iconComponent
                    anchors.fill: parent
                    source: row.modelData.icon || ""
                    sourceSize: Qt.size(parent.width, parent.height)
                    fillMode: Image.PreserveAspectFit
                    asynchronous: true
                    smooth: true
                }
                AppletIcon {
                    visible: !list.iconComponent && image.status !== Image.Ready
                    size: iconBox.width
                    kind: "programs"
                }
            }

            Text {
                id: noteText
                anchors.right: parent.right
                anchors.rightMargin: 6
                anchors.verticalCenter: parent.verticalCenter
                text: row.note
                color: row.isSelected ? Theme.selectionText : Theme.shadow
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontSize
            }

            Column {
                x: iconBox.x + iconBox.width + 6
                width: noteText.x - x - 8
                anchors.verticalCenter: parent.verticalCenter
                Text {
                    width: parent.width
                    elide: Text.ElideRight
                    text: row.modelData.name
                    color: row.isSelected ? Theme.selectionText : Theme.text
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontSize
                    font.bold: list.large
                }
                Text {
                    visible: list.large
                    width: parent.width
                    elide: Text.ElideRight
                    text: row.modelData.summary || ""
                    color: row.isSelected ? Theme.selectionText : Theme.text
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontSize
                }
            }

            MouseArea {
                x: iconBox.x
                width: parent.width - x
                height: parent.height
                onPressed: {
                    view.forceActiveFocus();
                    list.selectedKey = row.modelData.key;
                }
                onDoubleClicked: list.activated(row.modelData)
            }
        }

        WheelHandler { acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad; onWheel: (event) => bar.scrollBy(-event.angleDelta.y / 120 * (list.large ? 120 : 60)) }
    }

    Text {
        anchors.centerIn: parent
        width: parent.width - 30
        horizontalAlignment: Text.AlignHCenter
        wrapMode: Text.Wrap
        visible: list.model.length === 0
        text: list.emptyText
        color: Theme.shadow
        font.family: Theme.fontFamily
        font.pixelSize: Theme.fontSize
    }

    ScrollBar95 {
        id: bar
        visible: needed
        anchors.right: parent.right
        anchors.rightMargin: 2
        y: 2
        height: parent.height - 4
        flickable: view
        stepSize: list.large ? 40 : 20
    }
}
