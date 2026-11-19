import QtQuick
import Win95Ui
import Win95Control

// Hardware: Win95's Device Manager. The computer's devices by type, read
// from the kernel; devices without a working driver get a yellow "!".
// Updates by itself when hardware is plugged in or removed.
PropertySheet {
    id: sheet

    title: "Hardware"
    titles: ["Device Manager"]
    canApply: false

    property var computer: Hardware.computer()
    property var cats: Hardware.categories()
    // which categories are open; ones with problems start open, as in Win95
    property var expanded: ({})
    property string selectedKey: "computer"

    function reload() {
        cats = Hardware.categories();
    }
    function hasProblem(cat) {
        return cat.devices.some(function (d) { return d.problem; });
    }
    function isExpanded(cat) {
        return expanded[cat.id] !== undefined ? expanded[cat.id] : hasProblem(cat);
    }
    function toggle(cat) {
        var copy = Object.assign({}, expanded);
        copy[cat.id] = !isExpanded(cat);
        expanded = copy;
    }
    readonly property int problemCount: {
        var n = 0;
        for (var i = 0; i < cats.length; ++i)
            n += cats[i].devices.filter(function (d) { return d.problem; }).length;
        return n;
    }

    // the tree, flattened into rows
    readonly property var rows: {
        var list = [{ key: "computer", kind: "computer", depth: 0, name: computer.name, icon: "computer" }];
        for (var i = 0; i < cats.length; ++i) {
            var c = cats[i];
            list.push({ key: "cat:" + c.id, kind: "category", depth: 1, name: c.name, icon: c.icon,
                        problem: hasProblem(c), cat: c, open: isExpanded(c) });
            if (!isExpanded(c))
                continue;
            for (var j = 0; j < c.devices.length; ++j)
                list.push({ key: "dev:" + c.id + ":" + j, kind: "device", depth: 2, name: c.devices[j].name,
                            icon: c.devices[j].icon, problem: c.devices[j].problem, device: c.devices[j],
                            category: c.name });
        }
        return list;
    }
    readonly property var selectedRow: {
        for (var i = 0; i < rows.length; ++i)
            if (rows[i].key === selectedKey)
                return rows[i];
        return null;
    }
    function selectIndex(i) {
        if (i >= 0 && i < rows.length) {
            selectedKey = rows[i].key;
            tree.positionViewAtIndex(i, ListView.Contain);
        }
    }
    function currentIndex() {
        for (var i = 0; i < rows.length; ++i)
            if (rows[i].key === selectedKey)
                return i;
        return 0;
    }
    function activate(row) {
        if (row.kind === "category")
            toggle(row.cat);
        else
            propertiesDialog.open(row);
    }

    Connections {
        target: Hardware
        function onChanged() { sheet.reload(); }
    }

    // ------------------------------------------------------- Device Manager
    Item {
        Bevel {
            id: treeBox
            width: parent.width
            height: parent.height - buttons.height - summary.height - 16
            style: "sunken"
            face: Theme.window

            ListView {
                id: tree
                x: 2; y: 2
                width: parent.width - 4 - treeBar.width
                height: parent.height - 4
                clip: true
                interactive: false
                model: sheet.rows
                activeFocusOnTab: true
                focus: true
                keyNavigationEnabled: false
                Keys.onUpPressed: sheet.selectIndex(sheet.currentIndex() - 1)
                Keys.onDownPressed: sheet.selectIndex(sheet.currentIndex() + 1)
                Keys.onReturnPressed: if (sheet.selectedRow) sheet.activate(sheet.selectedRow)
                Keys.onRightPressed: {
                    var r = sheet.selectedRow;
                    if (r && r.kind === "category" && !r.open)
                        sheet.toggle(r.cat);
                }
                Keys.onLeftPressed: {
                    var r = sheet.selectedRow;
                    if (r && r.kind === "category" && r.open)
                        sheet.toggle(r.cat);
                }

                delegate: Item {
                    id: row
                    required property var modelData
                    required property int index
                    readonly property bool isSelected: modelData.key === sheet.selectedKey
                    width: tree.width
                    height: 19

                    // the [+]/[-] box for categories
                    Rectangle {
                        visible: row.modelData.kind === "category"
                        x: 6 + 16
                        anchors.verticalCenter: parent.verticalCenter
                        width: 9; height: 9
                        color: Theme.window
                        border.color: Theme.shadow
                        Rectangle { anchors.centerIn: parent; width: 5; height: 1; color: Theme.text }
                        Rectangle {
                            anchors.centerIn: parent; width: 1; height: 5; color: Theme.text
                            visible: !row.modelData.open
                        }
                        MouseArea {
                            anchors.fill: parent
                            anchors.margins: -3
                            onClicked: sheet.toggle(row.modelData.cat)
                        }
                    }

                    HardwareIcon {
                        id: rowIcon
                        x: 4 + row.modelData.depth * 18 + (row.modelData.kind === "category" ? 16 : 0)
                        anchors.verticalCenter: parent.verticalCenter
                        kind: row.modelData.icon
                        problem: !!row.modelData.problem && row.modelData.kind === "device"
                    }
                    Rectangle {
                        x: rowIcon.x + 20
                        anchors.verticalCenter: parent.verticalCenter
                        width: Math.min(label.implicitWidth + 4, row.width - x - 2)
                        height: 15
                        color: row.isSelected ? Theme.selection : "transparent"
                        Text {
                            id: label
                            x: 2
                            width: parent.width - 4
                            anchors.verticalCenter: parent.verticalCenter
                            elide: Text.ElideRight
                            text: row.modelData.name
                            color: row.isSelected ? Theme.selectionText : Theme.text
                            font.family: Theme.fontFamily
                            font.pixelSize: Theme.fontSize
                        }
                    }
                    MouseArea {
                        x: rowIcon.x
                        width: parent.width - x
                        height: parent.height
                        onPressed: {
                            tree.forceActiveFocus();
                            sheet.selectedKey = row.modelData.key;
                        }
                        onDoubleClicked: sheet.activate(row.modelData)
                    }
                }

                WheelHandler { acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad; onWheel: (event) => treeBar.scrollBy(-event.angleDelta.y / 120 * 57) }
            }
            ScrollBar95 {
                id: treeBar
                anchors.right: parent.right
                anchors.rightMargin: 2
                y: 2
                height: parent.height - 4
                flickable: tree
                stepSize: 19
            }
        }

        Text {
            id: summary
            y: treeBox.height + 6
            width: parent.width
            wrapMode: Text.Wrap
            text: sheet.problemCount === 0 ? "All devices are working properly."
                : sheet.problemCount === 1 ? "1 device has a problem. It's marked with a yellow \"!\"."
                : sheet.problemCount + " devices have problems. They're marked with a yellow \"!\"."
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontSize
        }

        Row {
            id: buttons
            anchors.bottom: parent.bottom
            spacing: 6
            Button95 {
                text: "P<u>r</u>operties"
                enabled: !!sheet.selectedRow && sheet.selectedRow.kind !== "category"
                onClicked: propertiesDialog.open(sheet.selectedRow)
            }
            Button95 {
                text: "Re<u>f</u>resh"
                onClicked: sheet.reload()
            }
        }
    }

    extras: [
        Dialog95 {
            id: propertiesDialog
            property var row: null
            readonly property var details: !row ? []
                : (row.kind === "computer" ? sheet.computer.details : row.device.details)
                      .filter(function (d) { return d[1] !== undefined && String(d[1]).length > 0; })
            title: (row ? row.name : "") + " Properties"
            dialogWidth: 380
            dialogHeight: 180 + details.length * 18

            function open(r) {
                row = r;
                show();
            }

            Row {
                x: 14
                y: 14
                spacing: 12
                HardwareIcon {
                    size: 32
                    kind: propertiesDialog.row ? propertiesDialog.row.icon : "system"
                    problem: !!propertiesDialog.row && !!propertiesDialog.row.problem
                }
                Column {
                    anchors.verticalCenter: parent.verticalCenter
                    Text {
                        width: 300
                        elide: Text.ElideRight
                        text: propertiesDialog.row ? propertiesDialog.row.name : ""
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.fontSize
                        font.bold: true
                    }
                    Text {
                        text: propertiesDialog.row && propertiesDialog.row.category ? propertiesDialog.row.category : ""
                        color: Theme.shadow
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.fontSize
                    }
                }
            }

            GroupBox95 {
                id: statusGroup
                x: 12
                y: 56
                width: parent.width - 24
                height: 58
                title: "Device status"
                Text {
                    width: parent.width
                    wrapMode: Text.Wrap
                    text: !propertiesDialog.row ? ""
                        : propertiesDialog.row.kind === "computer" ? "This is your computer."
                        : propertiesDialog.row.device.status
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontSize
                }
            }

            Column {
                x: 16
                y: statusGroup.y + statusGroup.height + 8
                spacing: 4
                Repeater {
                    model: propertiesDialog.details
                    Row {
                        required property var modelData
                        spacing: 8
                        Text {
                            width: 120
                            text: parent.modelData[0] + ":"
                            font.family: Theme.fontFamily
                            font.pixelSize: Theme.fontSize
                        }
                        Text {
                            width: 220
                            elide: Text.ElideRight
                            text: parent.modelData[1]
                            font.family: Theme.fontFamily
                            font.pixelSize: Theme.fontSize
                        }
                    }
                }
            }

            Button95 {
                anchors.right: parent.right
                anchors.rightMargin: 10
                anchors.bottom: parent.bottom
                anchors.bottomMargin: 10
                text: "OK"
                isDefault: true
                onClicked: propertiesDialog.close()
            }
        }
    ]
}
