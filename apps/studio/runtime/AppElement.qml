import QtQuick
import Win95Ui

// What every Wayland Studio element is built on: its colors and font (""
// and 0 mean "the desktop's"), its tool tip, and its tie to a field of the
// form's record (DataField). An element that shows a field reads
// `fieldValue`, writes with store(), and gets a red frame and the
// message as its tool tip when the field fails validation.
//
// `designMode` is the Studio's canvas: draw, but don't act. resetValue()
// (form.reset()) puts `resetProperties` back as they were at the start.
FocusScope {
    id: element

    property var form: null
    property bool designMode: false
    property string toolTip: ""

    property string foreColor: ""
    property string backColor: ""
    property string fontFamily: ""
    property int fontSize: 0        // pixels
    property bool fontBold: false
    property bool fontItalic: false
    property bool fontUnderline: false

    property string field: ""

    // what an element with no BackColor is filled with
    property color defaultBack: Theme.face
    readonly property color textColor: !enabled ? Theme.disabledText : foreColor !== "" ? foreColor : Theme.text
    readonly property color fillColor: backColor !== "" ? backColor : defaultBack
    readonly property string family: fontFamily !== "" ? fontFamily : Theme.fontFamily
    readonly property int pixelSize: fontSize > 0 ? fontSize : Theme.fontSize

    readonly property bool bound: field !== "" && form !== null && !designMode && form.table !== ""
    readonly property var fieldValue: bound ? form.record[field] : undefined
    readonly property var fieldInfo: bound ? form.fieldInfo(field) : ({})
    readonly property string error: bound && form.errors[field] ? form.errors[field] : ""
    readonly property string tipText: error !== "" ? error : toolTip

    // form.reset(): these properties go back to what they were when the form
    // opened (an element with a DataField shows the new record instead)
    property var resetProperties: []
    property var designed: ({})
    Component.onCompleted: {
        var d = {};
        for (var i = 0; i < resetProperties.length; ++i)
            d[resetProperties[i]] = element[resetProperties[i]];
        designed = d;
    }
    function resetValue() {
        if (bound)
            return;
        for (var i = 0; i < resetProperties.length; ++i) {
            var name = resetProperties[i];
            if (designed[name] !== undefined)
                element[name] = designed[name];
        }
    }

    function store(value) {
        if (bound)
            form.setField(field, value);
    }

    // a field's value as text
    function display(value, info) {
        if (value === undefined || value === null)
            return "";
        var type = info ? info.type : "";
        if (type === "boolean")
            return value ? "Yes" : "No";
        if (type === "lookup" && form && form.db)
            return form.db.lookupLabel(info.lookupTable, value);
        if (type === "datetime")
            return String(value).replace("T", " ");
        return String(value);
    }

    Rectangle {
        visible: element.error !== ""
        anchors.fill: parent
        anchors.margins: -2
        z: 100
        color: "transparent"
        border.color: "#ff0000"
        border.width: 1
    }

    HoverHandler {
        id: hover
        enabled: !element.designMode && element.form !== null
        onHoveredChanged: {
            if (!hovered && element.form)
                element.form.hideTip(element);
        }
    }
    Timer {
        interval: 500
        running: hover.hovered && element.tipText !== ""
        onTriggered: {
            if (element.form)
                element.form.showTip(element, element.tipText);
        }
    }
}
