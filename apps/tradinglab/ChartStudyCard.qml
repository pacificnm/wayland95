import QtQuick
import Win95Ui

// One active study in Chart Settings (and the chart's Edit Study box): its name and parameters, hide/show, remove,
// its parameter boxes and its colour swatches. Port of the `.study` block of renderer/src/views/ChartSettingsView.tsx.
// `inst` is the study instance {uid, studyId, params, visible, colors?}; `studies` the whole list (for copy colours).
// It does not change settings itself: `edited(next)` hands back the changed instance, `removed()` asks to drop it.
Rectangle {
    id: card
    property var inst
    property var studies: []
    signal edited(var next)
    signal removed()

    // only re-read when the study changes, so the parameter boxes are not rebuilt (and lose focus) on every keystroke
    readonly property string studyId: inst ? inst.studyId : ""
    readonly property var def: studyId ? core.study(studyId) : ({})
    readonly property var params: def.params || []
    readonly property var swatches: inst ? core.callPure("studySwatches", [inst, studies]) : []
    function valueOf(p) { const v = inst && inst.params ? inst.params[p.key] : undefined; return v !== undefined && v !== null ? v : p["default"]; }

    function patch(fn) { card.edited(fn(Object.assign({}, card.inst))); }
    // value null removes the override and goes back to the default colour
    function setColor(key, value) {
        patch(function (x) {
            const colors = Object.assign({}, x.colors || {});
            if (value === null) delete colors[key]; else colors[key] = value;
            if (Object.keys(colors).length) x.colors = colors; else delete x.colors;
            return x;
        });
    }

    implicitHeight: body.implicitHeight + 12
    color: Theme.window
    border.color: Theme.shadow

    Column {
        id: body
        x: 6
        y: 6
        width: parent.width - 12
        spacing: 5

        Item {
            width: parent.width
            height: 22
            Label95 {
                anchors.verticalCenter: parent.verticalCenter
                width: parent.width - buttons.width - 6
                elide: Text.ElideRight
                text: (card.def.name || "") + (card.params.length ? " · " + card.params.map(function (p) { return card.valueOf(p); }).join(", ") : "")
                color: card.inst && card.inst.visible ? Theme.text : Theme.shadow
                bold: true
            }
            Row {
                id: buttons
                anchors.right: parent.right
                spacing: 2
                ToolButton95 {
                    width: 46
                    height: 22
                    text: card.inst && card.inst.visible ? "Hide" : "Show"
                    onClicked: card.patch(function (s) { s.visible = !s.visible; return s; })
                    ToolTipArea { text: card.inst && card.inst.visible ? "Hide" : "Show" }
                }
                ToolButton95 {
                    width: 24
                    height: 22
                    onClicked: card.removed()
                    Icon95 { anchors.centerIn: parent; name: "delete" }
                    ToolTipArea { text: "Remove" }
                }
            }
        }

        Flow {
            visible: card.params.length > 0
            width: parent.width
            spacing: 10
            Repeater {
                model: card.params
                delegate: Row {
                    id: prow
                    required property var modelData
                    spacing: 4
                    Label95 { anchors.verticalCenter: parent.verticalCenter; text: prow.modelData.label }
                    TextField95 {
                        id: field
                        width: 56
                        Component.onCompleted: text = String(card.valueOf(prow.modelData))
                        Connections {
                            target: field.input
                            function onTextEdited() {
                                const v = parseFloat(field.text);
                                if (v > 0) card.patch(function (s) { const p = Object.assign({}, s.params || {}); p[prow.modelData.key] = v; s.params = p; return s; });
                            }
                            function onEditingFinished() { field.text = String(card.valueOf(prow.modelData)); }
                        }
                    }
                }
            }
        }

        Flow {
            width: parent.width
            spacing: 10
            Label95 { height: 22; verticalAlignment: Text.AlignVCenter; text: "Colors"; dim: true }
            Repeater {
                model: card.swatches
                delegate: ChartColorButton {
                    required property var modelData
                    color: modelData.value
                    label: modelData.label
                    custom: modelData.custom
                    onPicked: (c) => card.setColor(modelData.key, c)
                    onReset: card.setColor(modelData.key, null)
                }
            }
            Button95 {
                visible: card.inst && card.inst.colors !== undefined && card.inst.colors !== null
                width: 90
                height: 22
                text: "Reset colors"
                onClicked: card.patch(function (x) { delete x.colors; return x; })
            }
        }
    }
}
