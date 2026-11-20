import QtQuick
import Win95Ui

// Slider: Win95's trackbar, across or up and down: a value from Min to Max,
// dragged, clicked or moved with the arrow keys (Page Up and Page Down by
// LargeChange). With a DataField it shows and changes a number field.
//
//   slider1.value  min  max     changed: the person moved it (slider1.value)
AppElement {
    id: slider

    property int min: 0
    property int max: 100
    property int value: 0
    property int smallChange: 1
    property int largeChange: 10
    property int tickCount: 11
    property string orientation: "Horizontal"
    signal changed()

    resetProperties: ["value"]
    activeFocusOnTab: !designMode

    readonly property bool vertical: orientation === "Vertical"
    // what it shows: its DataField's number, or its Value
    readonly property real shownValue: {
        var v = bound ? Number(fieldValue) : value;
        return isNaN(v) ? min : Math.max(Math.min(min, max), Math.min(Math.max(min, max), v));
    }

    // with a DataField, value is the field's
    onShownValueChanged: if (bound && value !== Math.round(shownValue)) value = Math.round(shownValue)

    function moved(v) {
        v = Math.round(v);
        if (bound)
            store(v);
        value = v;
        changed();
    }

    Loader {
        id: track
        anchors.fill: parent
        focus: true
        sourceComponent: slider.vertical ? upright : across
    }
    Component {
        id: across
        Slider95 {
            id: s
            y: Math.max(0, Math.round((slider.height - 30) / 2))
            width: slider.width
            height: Math.min(30, slider.height)
            from: slider.min
            to: slider.max
            stepSize: Math.max(1, slider.smallChange)
            tickCount: slider.tickCount
            enabled: slider.enabled && !slider.designMode
            focus: true
            value: slider.shownValue
            onMoved: {
                slider.moved(value);
                value = Qt.binding(function () { return slider.shownValue; });
            }
            Keys.priority: Keys.BeforeItem
            Keys.onPressed: (event) => {
                if (event.key === Qt.Key_PageUp || event.key === Qt.Key_PageDown) {
                    setValue(value + (event.key === Qt.Key_PageUp ? 1 : -1) * Math.max(1, slider.largeChange));
                    event.accepted = true;
                }
            }
        }
    }
    Component {
        id: upright
        VSlider95 {
            id: v
            x: Math.max(0, Math.round((slider.width - 30) / 2))
            width: Math.min(30, slider.width)
            height: slider.height
            from: slider.min
            to: slider.max
            stepSize: Math.max(1, slider.smallChange)
            tickCount: slider.tickCount
            enabled: slider.enabled && !slider.designMode
            focus: true
            value: slider.shownValue
            onMoved: {
                slider.moved(value);
                value = Qt.binding(function () { return slider.shownValue; });
            }
            Keys.priority: Keys.BeforeItem
            Keys.onPressed: (event) => {
                if (event.key === Qt.Key_PageUp || event.key === Qt.Key_PageDown) {
                    setValue(value + (event.key === Qt.Key_PageUp ? 1 : -1) * Math.max(1, slider.largeChange));
                    event.accepted = true;
                }
            }
        }
    }
}
