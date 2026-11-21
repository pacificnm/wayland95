import QtQuick
import QtQuick.Window
import Win95Ui

// A colour swatch with its name, for a study output's colour (ChartSettingsView.tsx `<input type="color">`). Click it
// for a palette and a #rrggbb box; `custom` shows a small button that puts the default colour back.
Item {
    id: btn
    property string color: "#000000"
    property string label: ""
    property bool custom: false
    signal picked(string color)
    signal reset()

    implicitWidth: row.implicitWidth
    implicitHeight: 22

    readonly property var palette: ["#26a69a", "#4caf50", "#cddc39", "#ffeb3b", "#ffb300", "#ff9800", "#ff5722", "#ef5350",
                                     "#e91e63", "#ab47bc", "#7e57c2", "#3f51b5", "#2196f3", "#00bcd4", "#795548", "#9e9e9e",
                                     "#ffffff", "#000000"]

    Row {
        id: row
        anchors.verticalCenter: parent.verticalCenter
        spacing: 4
        Bevel {
            id: swatch
            width: 22
            height: 18
            style: swatchMouse.pressed ? "pressed" : "raised"
            Rectangle { x: 4; y: 4; width: parent.width - 8; height: parent.height - 8; color: btn.color; border.color: Theme.dark }
            MouseArea { id: swatchMouse; anchors.fill: parent; onClicked: btn.open() }
            ToolTipArea { text: btn.custom ? "Custom color. Click the arrow to restore the default." : "Default color" }
        }
        Label95 { anchors.verticalCenter: parent.verticalCenter; text: btn.label }
        ToolButton95 {
            visible: btn.custom
            width: 18
            height: 18
            text: "↺"
            onClicked: btn.reset()
            ToolTipArea { text: "Restore default" }
        }
    }

    function open() {
        const p = swatch.mapToItem(layer, 0, swatch.height + 2);
        panel.x = Math.max(0, Math.min(p.x, layer.width - panel.width));
        panel.y = p.y + panel.height > layer.height ? Math.max(0, p.y - swatch.height - 4 - panel.height) : p.y;
        hex.text = btn.color;
        layer.visible = true;
    }

    // the palette, over the whole window so it is not clipped by a scrolling page
    Item {
        id: layer
        parent: btn.Window.contentItem
        anchors.fill: parent
        visible: false
        z: 3000
        MouseArea { anchors.fill: parent; onPressed: layer.visible = false }
        Bevel {
            id: panel
            width: 6 * 20 + 12
            height: 3 * 20 + 40
            style: "raised"
            MouseArea { anchors.fill: parent } // keeps presses inside from closing it
            Grid {
                x: 6
                y: 6
                columns: 6
                spacing: 2
                Repeater {
                    model: btn.palette
                    delegate: Rectangle {
                        required property string modelData
                        width: 18
                        height: 18
                        color: modelData
                        border.color: modelData === btn.color ? Theme.text : Theme.shadow
                        border.width: modelData === btn.color ? 2 : 1
                        MouseArea { anchors.fill: parent; onClicked: { layer.visible = false; btn.picked(parent.modelData); } }
                    }
                }
            }
            TextField95 {
                id: hex
                x: 6
                y: 3 * 20 + 12
                width: parent.width - 12
                onAccepted: {
                    const v = text.trim();
                    if (/^#[0-9a-fA-F]{6}$/.test(v)) { layer.visible = false; btn.picked(v.toLowerCase()); }
                }
                onEscaped: layer.visible = false
            }
        }
    }
}
