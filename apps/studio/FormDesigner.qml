import QtQuick
import Win95Ui
import Win95App

// The form designer: the form as it will look, on a dotted grid its
// elements snap to. Pick an element in the Toolbox and drag out where it
// goes (or double-click it there); click an element to select it
// (Ctrl+click for more, or drag a box around several), drag to move, drag a
// handle to size. Double-click opens its code.
//
// The elements drawn here are the real ones (Win95App's App*.qml) in
// designMode, so the canvas is what the program shows.
FocusScope {
    id: designer

    // Main.qml: the project and what's selected
    property var studio

    readonly property var form: { studio.revision; return studio.currentForm(); }
    readonly property var controls: { studio.revision; return form ? form.controls : []; }
    readonly property int grid: Math.max(1, studio.gridSize)
    readonly property int formW: mode === "form" ? formRect.w : (form ? form.width : 0)
    readonly property int formH: mode === "form" ? formRect.h : (form ? form.height : 0)

    // a drag under way: "move", "resize", "band" (a selection box),
    // "create" (a new element's box) or "form" (the form's own size)
    property string mode: ""
    property real pressX: 0
    property real pressY: 0
    property bool moved: false
    property int dragDx: 0
    property int dragDy: 0
    property string resizeName: ""
    property string resizeHandle: ""
    property var resizeFrom: ({ x: 0, y: 0, w: 0, h: 0 })
    property var resizeRect: ({ x: 0, y: 0, w: 0, h: 0 })
    property var bandRect: ({ x: 0, y: 0, w: 0, h: 0 })
    property var formRect: ({ w: 0, h: 0 })

    function snap(v, free) { return free ? Math.round(v) : Math.round(v / grid) * grid; }

    function controlNamed(name) {
        for (var i = 0; i < controls.length; ++i) {
            if (controls[i].name === name)
                return controls[i];
        }
        return null;
    }
    function controlAt(x, y) {
        for (var i = controls.length - 1; i >= 0; --i) {
            var c = controls[i];
            if (x >= c.x && x < c.x + c.width && y >= c.y && y < c.y + c.height)
                return c;
        }
        return null;
    }
    // where an element is drawn right now (a drag hasn't changed the project yet)
    function liveRect(c, selected) {
        studio.revision;
        if (!c)
            return { x: 0, y: 0, w: 0, h: 0 };
        // (a Menu or ToolBar stays across the top)
        if (selected && mode === "move" && !studio.isDocked(c))
            return { x: c.x + dragDx, y: c.y + dragDy, w: c.width, h: c.height };
        if (mode === "resize" && resizeName === c.name)
            return resizeRect;
        return { x: c.x, y: c.y, w: c.width, h: c.height };
    }
    readonly property var handleNames: ["nw", "n", "ne", "e", "se", "s", "sw", "w"]
    function handlePoint(r, h) {
        var cx = h.indexOf("w") >= 0 ? r.x : h.indexOf("e") >= 0 ? r.x + r.w : r.x + r.w / 2;
        var cy = h.indexOf("n") >= 0 ? r.y : h.indexOf("s") >= 0 ? r.y + r.h : r.y + r.h / 2;
        return { x: Math.round(cx), y: Math.round(cy) };
    }
    // the handle of the (one) selected element under a point
    function handleAt(x, y) {
        if (studio.selection.length !== 1)
            return "";
        var c = controlNamed(studio.selection[0]);
        if (!c)
            return "";
        var r = { x: c.x, y: c.y, w: c.width, h: c.height };
        for (var i = 0; i < handleNames.length; ++i) {
            var p = handlePoint(r, handleNames[i]);
            if (Math.abs(x - p.x) <= 4 && Math.abs(y - p.y) <= 4)
                return handleNames[i];
        }
        return "";
    }
    function cursorFor(h) {
        return h === "n" || h === "s" ? Qt.SizeVerCursor : h === "e" || h === "w" ? Qt.SizeHorCursor
             : h === "nw" || h === "se" ? Qt.SizeFDiagCursor : Qt.SizeBDiagCursor;
    }
    function normalized(x1, y1, x2, y2) {
        return { x: Math.min(x1, x2), y: Math.min(y1, y2), w: Math.abs(x2 - x1), h: Math.abs(y2 - y1) };
    }

    function contextMenu(item, x, y) {
        var has = studio.selection.length > 0;
        var p = item.mapToItem(studio.popupMenu, x, y);
        studio.popupMenu.popup(p.x, p.y, [
            { text: "View &Code", action: function () { studio.viewCode(has ? studio.selection[studio.selection.length - 1] : ""); } },
            { separator: true },
            { text: "Cu&t", enabled: has, action: studio.cut },
            { text: "&Copy", enabled: has, action: studio.copy },
            { text: "&Paste", enabled: studio.clipboard.length > 0, action: studio.paste },
            { text: "&Delete", enabled: has, action: studio.deleteSelected },
            { separator: true },
            { text: "Bring to &Front", enabled: has, action: function () { studio.restack(true); } },
            { text: "Send to &Back", enabled: has, action: function () { studio.restack(false); } },
        ]);
    }

    Bevel {
        anchors.fill: parent
        style: "sunken"
        face: Theme.window
    }

    Flickable {
        id: flick
        x: 2
        y: 2
        // (room for the scroll bars whether they show or not: sizes that
        // depend on each other's bars chase their tails)
        width: parent.width - 4 - 16
        height: parent.height - 4 - 16
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        interactive: false
        contentWidth: Math.max(width, frame.x + frame.width + 40)
        contentHeight: Math.max(height, frame.y + frame.height + 40)

        // a click beside the form: the form itself
        MouseArea {
            width: flick.contentWidth
            height: flick.contentHeight
            onPressed: {
                designer.forceActiveFocus();
                studio.selection = [];
            }
        }

        // the form's window: frame, title bar, then the form
        Item {
            id: frame
            x: 16
            y: 14
            width: designer.formW + 8
            height: designer.formH + 30
            visible: designer.form !== null

            Bevel { anchors.fill: parent; style: "raised" }
            Rectangle {
                x: 4
                y: 4
                width: parent.width - 8
                height: 18
                gradient: Gradient {
                    orientation: Gradient.Horizontal
                    GradientStop { position: 0; color: Theme.titleActiveStart }
                    GradientStop { position: 1; color: Theme.titleActiveEnd }
                }
                Text {
                    x: 4
                    width: parent.width - 60
                    anchors.verticalCenter: parent.verticalCenter
                    elide: Text.ElideRight
                    text: designer.form ? (designer.form.props.title || designer.form.name) : ""
                    color: Theme.titleActiveText
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontSize
                    font.bold: true
                }
                Row {
                    anchors.right: parent.right
                    anchors.rightMargin: 2
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: 0
                    Repeater {
                        model: ["_", "□", "✕"]
                        delegate: Item {
                            required property string modelData
                            required property int index
                            width: index === 2 ? 18 : 16
                            height: 14
                            Bevel { x: parent.index === 2 ? 2 : 0; width: 16; height: 14; style: "raised" }
                            Text {
                                x: parent.index === 2 ? 2 : 0
                                width: 16
                                height: 14
                                horizontalAlignment: Text.AlignHCenter
                                verticalAlignment: Text.AlignVCenter
                                text: parent.modelData
                                font.pixelSize: 9
                                font.bold: true
                                color: Theme.text
                            }
                        }
                    }
                }
                MouseArea {
                    anchors.fill: parent
                    onPressed: {
                        designer.forceActiveFocus();
                        studio.selection = [];
                    }
                    onDoubleClicked: studio.viewCode("")
                }
            }

            Item {
                id: surface
                x: 4
                y: 26
                width: designer.formW
                height: designer.formH
                clip: true

                Rectangle {
                    anchors.fill: parent
                    color: designer.form && designer.form.props.backColor ? designer.form.props.backColor : Theme.face
                }
                Image {
                    anchors.fill: parent
                    fillMode: Image.Tile
                    source: designer.form && designer.form.props.picture
                            ? StudioTools.pictureUrl(studio.projectDir, designer.form.props.picture) : ""
                }
                Canvas {
                    id: dots
                    anchors.fill: parent
                    visible: studio.showGrid
                    property int step: designer.grid
                    property color ink: Theme.text
                    onStepChanged: requestPaint()
                    onWidthChanged: requestPaint()
                    onHeightChanged: requestPaint()
                    // (what's asked for while the designer is hidden isn't painted)
                    onVisibleChanged: {
                        if (visible)
                            requestPaint();
                    }
                    onInkChanged: requestPaint()
                    onPaint: {
                        var ctx = getContext("2d");
                        ctx.reset();
                        ctx.fillStyle = ink;
                        var s = Math.max(4, step);
                        for (var y = s; y < height; y += s) {
                            for (var x = s; x < width; x += s)
                                ctx.fillRect(x, y, 1, 1);
                        }
                    }
                }

                Repeater {
                    model: designer.controls.length
                    delegate: Item {
                        id: holder
                        required property int index
                        readonly property var control: { studio.revision; return designer.controls[index] || null; }
                        readonly property string type: { studio.revision; return control ? control.type : ""; }
                        readonly property string name: { studio.revision; return control ? control.name : ""; }
                        readonly property bool selected: studio.selection.indexOf(name) >= 0
                        readonly property var live: designer.liveRect(control, selected)
                        x: live.x
                        y: live.y
                        width: live.w
                        height: live.h

                        function apply() {
                            var item = loader.item, c = control;
                            if (!item || !c)
                                return;
                            item.designMode = true;
                            var props = studio.propsOf(c.type);
                            for (var i = 0; i < props.length; ++i) {
                                var p = props[i];
                                if (p.name === "visible")
                                    continue; // (an invisible element still has to be seen here)
                                var v = c.props && c.props[p.name] !== undefined ? c.props[p.name] : p["default"];
                                if (p.type === "image")
                                    v = StudioTools.pictureUrl(studio.projectDir, v);
                                item[p.name] = v;
                            }
                            if (c.type === "DataGrid")
                                item.designColumns = studio.gridColumns(c);
                        }

                        Loader {
                            id: loader
                            anchors.fill: parent
                            source: holder.type !== "" ? "qrc:/qt/qml/Win95App/" + studio.elementInfo(holder.type).component + ".qml" : ""
                            onLoaded: holder.apply()
                        }
                        Connections {
                            target: studio
                            function onRevisionChanged() { holder.apply(); }
                        }
                        // one that won't be seen in the program: hatched over
                        Rectangle {
                            anchors.fill: parent
                            visible: holder.control !== null && holder.control.props.visible === false
                            color: "transparent"
                            border.color: Theme.shadow
                            border.width: 1
                            opacity: 0.8
                            Label95 { anchors.centerIn: parent; text: "(not visible)"; color: Theme.shadow }
                        }
                    }
                }

                // the selection's handles
                Repeater {
                    model: studio.selection
                    delegate: Item {
                        id: marks
                        required property string modelData
                        required property int index
                        readonly property var rect: designer.liveRect(designer.controlNamed(modelData), true)
                        readonly property bool primary: index === studio.selection.length - 1
                        Repeater {
                            model: designer.handleNames
                            delegate: Rectangle {
                                required property string modelData
                                readonly property var at: designer.handlePoint(marks.rect, modelData)
                                x: at.x - 3
                                y: at.y - 3
                                width: 7
                                height: 7
                                color: marks.primary ? Theme.selection : Theme.window
                                border.color: marks.primary ? Theme.window : Theme.selection
                                border.width: 1
                            }
                        }
                    }
                }

                // a selection box, or a new element's
                Rectangle {
                    visible: (designer.mode === "band" || designer.mode === "create") && designer.moved
                    x: designer.bandRect.x
                    y: designer.bandRect.y
                    width: designer.bandRect.w
                    height: designer.bandRect.h
                    color: "transparent"
                    border.color: Theme.text
                    border.width: 1
                    opacity: designer.mode === "create" ? 1 : 0.6
                }

                MouseArea {
                    id: mouse
                    anchors.fill: parent
                    acceptedButtons: Qt.LeftButton | Qt.RightButton
                    hoverEnabled: true
                    property string hoverHandle: ""
                    cursorShape: studio.tool !== "" ? Qt.CrossCursor
                               : designer.mode === "resize" ? designer.cursorFor(designer.resizeHandle)
                               : hoverHandle !== "" ? designer.cursorFor(hoverHandle) : Qt.ArrowCursor

                    onPressed: (event) => {
                        designer.forceActiveFocus();
                        var x = event.x, y = event.y;
                        var more = (event.modifiers & (Qt.ControlModifier | Qt.ShiftModifier)) !== 0;
                        designer.pressX = x;
                        designer.pressY = y;
                        designer.moved = false;
                        designer.dragDx = 0;
                        designer.dragDy = 0;
                        var c = designer.controlAt(x, y);
                        if (event.button === Qt.RightButton) {
                            if (c && studio.selection.indexOf(c.name) < 0)
                                studio.selection = [c.name];
                            else if (!c)
                                studio.selection = [];
                            designer.contextMenu(mouse, x, y);
                            return;
                        }
                        if (studio.tool !== "") {
                            designer.mode = "create";
                            designer.bandRect = { x: designer.snap(x), y: designer.snap(y), w: 0, h: 0 };
                            return;
                        }
                        var h = designer.handleAt(x, y);
                        if (h !== "") {
                            var sc = designer.controlNamed(studio.selection[0]);
                            designer.resizeName = sc.name;
                            designer.resizeHandle = h;
                            designer.resizeFrom = { x: sc.x, y: sc.y, w: sc.width, h: sc.height };
                            designer.resizeRect = designer.resizeFrom;
                            designer.mode = "resize";
                            return;
                        }
                        if (c) {
                            var at = studio.selection.indexOf(c.name);
                            if (more) {
                                var s = studio.selection.slice();
                                if (at >= 0)
                                    s.splice(at, 1);
                                else
                                    s.push(c.name);
                                studio.selection = s;
                                return;
                            }
                            if (at < 0)
                                studio.selection = [c.name];
                            else if (at !== studio.selection.length - 1) {
                                // the one clicked is the one the Properties window shows
                                var order = studio.selection.slice();
                                order.splice(at, 1);
                                order.push(c.name);
                                studio.selection = order;
                            }
                            designer.mode = "move";
                            return;
                        }
                        if (!more)
                            studio.selection = [];
                        designer.mode = "band";
                        designer.bandRect = { x: x, y: y, w: 0, h: 0 };
                    }

                    onPositionChanged: (event) => {
                        var x = event.x, y = event.y;
                        if (!pressed) {
                            hoverHandle = studio.tool === "" ? designer.handleAt(x, y) : "";
                            return;
                        }
                        var dx = x - designer.pressX, dy = y - designer.pressY;
                        var free = (event.modifiers & Qt.AltModifier) !== 0;
                        if (!designer.moved && Math.abs(dx) < 3 && Math.abs(dy) < 3)
                            return;
                        designer.moved = true;
                        if (designer.mode === "move") {
                            // the element under the pointer lands on the grid; the rest keep their places beside it
                            var lead = designer.controlNamed(studio.selection[studio.selection.length - 1]);
                            if (!lead)
                                return;
                            designer.dragDx = designer.snap(lead.x + dx, free) - lead.x;
                            designer.dragDy = designer.snap(lead.y + dy, free) - lead.y;
                        } else if (designer.mode === "resize") {
                            var f = designer.resizeFrom, h = designer.resizeHandle;
                            var left = f.x, top = f.y, right = f.x + f.w, bottom = f.y + f.h;
                            var least = free ? 1 : designer.grid;
                            if (h.indexOf("w") >= 0)
                                left = Math.min(designer.snap(f.x + dx, free), right - least);
                            if (h.indexOf("e") >= 0)
                                right = Math.max(designer.snap(f.x + f.w + dx, free), left + least);
                            if (h.indexOf("n") >= 0)
                                top = Math.min(designer.snap(f.y + dy, free), bottom - least);
                            if (h.indexOf("s") >= 0)
                                bottom = Math.max(designer.snap(f.y + f.h + dy, free), top + least);
                            designer.resizeRect = { x: left, y: top, w: right - left, h: bottom - top };
                        } else if (designer.mode === "create") {
                            designer.bandRect = designer.normalized(designer.snap(designer.pressX, free), designer.snap(designer.pressY, free),
                                                                    designer.snap(x, free), designer.snap(y, free));
                        } else if (designer.mode === "band") {
                            designer.bandRect = designer.normalized(designer.pressX, designer.pressY, x, y);
                        }
                    }

                    onReleased: (event) => {
                        var was = designer.mode, r = designer.bandRect;
                        if (was === "move" && designer.moved && (designer.dragDx !== 0 || designer.dragDy !== 0)) {
                            var dx = designer.dragDx, dy = designer.dragDy;
                            designer.mode = "";
                            studio.moveSelected(dx, dy);
                        } else if (was === "resize" && designer.moved) {
                            var rr = designer.resizeRect, name = designer.resizeName;
                            designer.mode = "";
                            studio.setGeometry(name, rr.x, rr.y, rr.w, rr.h);
                        } else if (was === "create") {
                            var type = studio.tool;
                            designer.mode = "";
                            if (!designer.moved || r.w < designer.grid || r.h < designer.grid)
                                studio.addControl(type, designer.snap(designer.pressX), designer.snap(designer.pressY), 0, 0);
                            else
                                studio.addControl(type, r.x, r.y, r.w, r.h);
                            studio.tool = "";
                        } else if (was === "band" && designer.moved) {
                            var names = (event.modifiers & (Qt.ControlModifier | Qt.ShiftModifier)) ? studio.selection.slice() : [];
                            for (var i = 0; i < designer.controls.length; ++i) {
                                var c = designer.controls[i];
                                if (c.x < r.x + r.w && c.x + c.width > r.x && c.y < r.y + r.h && c.y + c.height > r.y
                                    && names.indexOf(c.name) < 0)
                                    names.push(c.name);
                            }
                            studio.selection = names;
                        }
                        designer.mode = "";
                        designer.moved = false;
                        designer.dragDx = 0;
                        designer.dragDy = 0;
                    }

                    onDoubleClicked: (event) => {
                        if (event.button !== Qt.LeftButton || studio.tool !== "")
                            return;
                        var c = designer.controlAt(event.x, event.y);
                        studio.viewCode(c ? c.name : "");
                    }
                }
            }

            // the form's own size: its right edge, bottom edge and corner
            Repeater {
                model: ["e", "s", "se"]
                delegate: Rectangle {
                    id: grip
                    required property string modelData
                    x: modelData === "s" ? Math.round(frame.width / 2) - 3 : frame.width - 1
                    y: modelData === "e" ? Math.round(frame.height / 2) - 3 : frame.height - 1
                    width: 7
                    height: 7
                    color: studio.selection.length === 0 ? Theme.selection : Theme.window
                    border.color: studio.selection.length === 0 ? Theme.window : Theme.selection
                    border.width: 1
                    MouseArea {
                        anchors.fill: parent
                        anchors.margins: -3
                        cursorShape: designer.cursorFor(grip.modelData)
                        property point from
                        property int w0
                        property int h0
                        onPressed: (event) => {
                            designer.forceActiveFocus();
                            studio.selection = [];
                            from = mapToItem(flick.contentItem, event.x, event.y);
                            w0 = designer.form.width;
                            h0 = designer.form.height;
                            designer.formRect = { w: w0, h: h0 };
                            designer.mode = "form";
                        }
                        onPositionChanged: (event) => {
                            if (designer.mode !== "form")
                                return;
                            var p = mapToItem(flick.contentItem, event.x, event.y);
                            var free = (event.modifiers & Qt.AltModifier) !== 0;
                            designer.formRect = {
                                w: grip.modelData === "s" ? w0 : Math.max(48, designer.snap(w0 + p.x - from.x, free)),
                                h: grip.modelData === "e" ? h0 : Math.max(24, designer.snap(h0 + p.y - from.y, free)),
                            };
                        }
                        onReleased: {
                            var r = designer.formRect;
                            designer.mode = "";
                            if (r.w !== w0 || r.h !== h0)
                                studio.setFormSize(r.w, r.h);
                        }
                    }
                }
            }
        }
    }
    ScrollBar95 {
        id: vbar
        flickable: flick
        anchors.right: parent.right
        anchors.rightMargin: 2
        y: 2
        height: flick.height
    }
    ScrollBar95 {
        id: hbar
        flickable: flick
        orientation: Qt.Horizontal
        x: 2
        anchors.bottom: parent.bottom
        anchors.bottomMargin: 2
        width: flick.width
    }

    Keys.onPressed: (event) => {
        var step = (event.modifiers & Qt.ControlModifier) ? 1 : grid;
        var dx = event.key === Qt.Key_Left ? -step : event.key === Qt.Key_Right ? step : 0;
        var dy = event.key === Qt.Key_Up ? -step : event.key === Qt.Key_Down ? step : 0;
        if (dx !== 0 || dy !== 0) {
            if (studio.selection.length > 0) {
                if (event.modifiers & Qt.ShiftModifier)
                    studio.resizeSelected(dx, dy);
                else
                    studio.moveSelected(dx, dy);
            }
        } else if (event.key === Qt.Key_Delete || event.key === Qt.Key_Backspace) {
            studio.deleteSelected();
        } else if (event.key === Qt.Key_Escape) {
            if (studio.tool !== "")
                studio.tool = "";
            else
                studio.selection = [];
        } else {
            return;
        }
        event.accepted = true;
    }
}
