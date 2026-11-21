import QtQuick
import Win95Ui

// Markdown that sits in a page (it does not scroll by itself): a lesson body, a help page, a comment. The text is
// split at ```chartdiagram fenced blocks, which are drawn by ChartDiagram.qml; everything else goes through
// Util.markdown and Qt's Markdown reader. Links open in the browser (web addresses only, checked by the backend).
Column {
    id: md
    property string text: ""
    property int fontSize: Theme.fontSize + 1
    signal linkActivated(string link)
    spacing: 0

    readonly property var parts: {
        const out = [];
        const re = /^[ \t]*```chartdiagram[^\n]*\n([\s\S]*?)\n[ \t]*```[ \t]*$/gm;
        const s = String(text || "");
        let last = 0, m;
        while ((m = re.exec(s)) !== null) {
            if (m.index > last) out.push({ kind: "md", text: s.slice(last, m.index) });
            out.push({ kind: "diagram", text: m[1] });
            last = m.index + m[0].length;
        }
        if (last < s.length) out.push({ kind: "md", text: s.slice(last) });
        return out.filter(function (p) { return p.kind === "diagram" || p.text.trim() !== ""; });
    }

    Repeater {
        model: md.parts
        delegate: Loader {
            id: part
            required property var modelData
            width: md.width
            sourceComponent: modelData.kind === "diagram" ? diagramPart : textPart
            Component {
                id: diagramPart
                ChartDiagram { width: part.width; source: part.modelData.text }
            }
            Component {
                id: textPart
                TextEdit {
                    width: part.width
                    readOnly: true
                    selectByMouse: true
                    wrapMode: TextEdit.Wrap
                    textFormat: TextEdit.MarkdownText
                    text: Util.markdown(part.modelData.text)
                    color: Theme.text
                    selectionColor: Theme.selection
                    selectedTextColor: Theme.selectionText
                    font.family: Theme.fontFamily
                    font.pixelSize: md.fontSize
                    onLinkActivated: (link) => md.linkActivated(link)
                    HoverHandler { cursorShape: parent.hoveredLink !== "" ? Qt.PointingHandCursor : Qt.IBeamCursor }
                }
            }
        }
    }
}
