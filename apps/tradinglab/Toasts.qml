import QtQuick
import Win95Ui

// Upstream's toasts (renderer/src/toast.tsx): small notes stacked in the corner that go away by themselves,
// some with a button ("Undo", "View"). Here they are Win95 tooltips-yellow boxes.
Column {
    id: toasts
    spacing: 4
    width: 320
    property var items: []
    function add(kind, text, opts) {
        const it = { id: Date.now() + Math.random(), kind: kind, text: String(text), title: opts.title || "", action: opts.action || null,
                     duration: opts.duration === undefined ? (kind === "error" ? 9000 : 5000) : opts.duration };
        items = items.concat([it]).slice(-5);
    }
    function dismiss(id) { items = items.filter(function (x) { return x.id !== id; }); }

    Repeater {
        model: toasts.items
        delegate: Rectangle {
            id: box
            required property var modelData
            width: toasts.width
            height: body.implicitHeight + 12 + (actionBtn.visible ? 26 : 0)
            color: "#ffffe1"
            border.color: Theme.dark
            Icon95 {
                x: 6; y: 6
                name: box.modelData.kind === "success" ? "ok" : box.modelData.kind === "error" ? "error" : box.modelData.kind === "warning" ? "warning" : "info"
            }
            Text {
                id: body
                x: 28; y: 6
                width: parent.width - 52
                wrapMode: Text.Wrap
                textFormat: Text.StyledText
                text: (box.modelData.title ? "<b>" + Util.plain(box.modelData.title) + "</b><br>" : "") + Util.plain(box.modelData.text)
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontSize
                color: "black"
            }
            Text {
                x: parent.width - 16; y: 3
                text: "×"
                font.pixelSize: 13
                MouseArea { anchors.fill: parent; anchors.margins: -4; onClicked: toasts.dismiss(box.modelData.id) }
            }
            Button95 {
                id: actionBtn
                visible: !!box.modelData.action
                x: 28
                y: body.y + body.implicitHeight + 4
                height: 22
                text: box.modelData.action ? box.modelData.action.label : ""
                onClicked: { const a = box.modelData.action; toasts.dismiss(box.modelData.id); a.onClick(); }
            }
            Timer {
                running: box.modelData.duration > 0
                interval: box.modelData.duration
                onTriggered: toasts.dismiss(box.modelData.id)
            }
        }
    }
}
