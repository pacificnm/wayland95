import QtQuick
import Win95Ui

// The article rows shared by the per-symbol News screen and the Market News screen (port of upstream
// components/NewsList.tsx). Clicking a headline opens the article in the browser. Upstream also showed each
// article's thumbnail; Wayland 95 does not load remote images (they would be fetched from news sites and ad hosts
// as soon as the list is drawn), so the list is text only.
Column {
    id: news
    property var items: []
    property bool showSymbol: false
    signal symbolClicked(string symbol)
    spacing: 0

    function host(url) {
        const m = /^https?:\/\/([^\/?#]+)/i.exec(String(url || ""));
        return m ? m[1].replace(/^www\./, "") : "";
    }
    // upstream NewsList ago(): blank after two weeks (the date is shown anyway)
    function ago(t) {
        const s = Math.max(0, Date.now() / 1000 - t);
        if (s < 90) return "just now";
        if (s < 3600) return Math.round(s / 60) + " min ago";
        if (s < 86400) return Math.round(s / 3600) + " h ago";
        if (s < 86400 * 14) return Math.round(s / 86400) + " d ago";
        return "";
    }

    Repeater {
        model: news.items
        delegate: Item {
            id: art
            required property var modelData
            required property int index
            width: news.width
            height: col.height + 12
            Rectangle {
                anchors.fill: parent
                color: hover.hovered ? "#f2f2ff" : Theme.window
            }
            Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1; color: Theme.shadow; opacity: 0.4 }
            MouseArea {
                id: hover
                readonly property bool hovered: containsMouse
                anchors.fill: parent
                hoverEnabled: true
                cursorShape: art.modelData.url ? Qt.PointingHandCursor : Qt.ArrowCursor
                onClicked: if (art.modelData.url) api.invoke("app:openExternal", [art.modelData.url])
            }
            Column {
                id: col
                x: 8
                y: 6
                width: parent.width - 16
                spacing: 3
                Label95 {
                    width: parent.width
                    wrapMode: Text.Wrap
                    bold: true
                    font.pixelSize: Theme.fontSize + 1
                    color: art.modelData.url ? "#000080" : Theme.text
                    font.underline: hover.hovered && !!art.modelData.url
                    text: (art.modelData.title || "") + (art.modelData.url ? "  ↗" : "")
                }
                Row {
                    spacing: 10
                    Rectangle {
                        visible: news.showSymbol && !!art.modelData.symbol
                        width: symText.implicitWidth + 10
                        height: 15
                        color: symMouse.containsMouse ? Theme.selection : Theme.face
                        border.color: Theme.shadow
                        Label95 {
                            id: symText
                            anchors.centerIn: parent
                            bold: true
                            small: true
                            text: art.modelData.symbol || ""
                            color: symMouse.containsMouse ? Theme.selectionText : Theme.text
                        }
                        MouseArea {
                            id: symMouse
                            anchors.fill: parent
                            hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            onClicked: news.symbolClicked(art.modelData.symbol)
                        }
                        ToolTipArea { text: "Open " + (art.modelData.symbol || "") + " chart" }
                    }
                    Label95 { bold: true; dim: true; text: art.modelData.publisher || news.host(art.modelData.url) }
                    Label95 { dim: true; text: news.ago(art.modelData.time) || Util.dateTime(art.modelData.time) }
                    Label95 { dim: true; text: Util.dateTime(art.modelData.time) }
                }
                Label95 {
                    visible: !!art.modelData.text
                    width: parent.width
                    wrapMode: Text.Wrap
                    maximumLineCount: 4
                    elide: Text.ElideRight
                    text: art.modelData.text || ""
                }
            }
        }
    }
}
