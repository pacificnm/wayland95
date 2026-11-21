import QtQuick
import Win95Ui

// Help: the Help library (content/help/toc.json and help/pages/*.md): contents by section with search, the page
// with "Open this screen", and Previous / Next. The pictures in upstream's pages showed the Electron app and are not
// shipped: each one is replaced by a short note. The current topic is app.helpTopic. Port of views/HelpView.tsx.
Item {
    id: view
    property var app

    property var toc: []
    property var pages: ({})           // id -> markdown ("" while loading or missing)
    property var closed: ({})          // section id -> true while folded
    property string q: ""

    readonly property var allTopics: {
        const out = [];
        toc.forEach(function (g) { g.topics.forEach(function (t) { out.push({ topic: t, group: g }); }); });
        return out;
    }
    readonly property int index: {
        const id = app ? app.helpTopic : "";
        return Math.max(0, allTopics.findIndex(function (x) { return x.topic.id === id; }));
    }
    readonly property var cur: allTopics.length ? allTopics[index] : null
    readonly property var prev: index > 0 ? allTopics[index - 1] : null
    readonly property var next: index + 1 < allTopics.length ? allTopics[index + 1] : null
    readonly property string body: cur ? (pages[cur.topic.id] || "") : ""

    readonly property string needle: q.trim().toLowerCase()
    readonly property var groups: toc.map(function (g) {
        return { id: g.id, title: g.title, all: g.topics.length,
                 topics: needle ? g.topics.filter(function (t) { return (t.title + " " + t.summary + " " + g.title).toLowerCase().indexOf(needle) >= 0; }) : g.topics };
    }).filter(function (g) { return g.topics.length; })
    readonly property var rows: {
        const out = [];
        groups.forEach(function (g) {
            const open = !!needle || !closed[g.id];
            out.push({ group: g, open: open });
            if (open) g.topics.forEach(function (t) { out.push({ topic: t }); });
        });
        return out;
    }

    function setTopic(id) { if (app) app.helpTopic = id; }
    function toggle(id) {
        const c = Object.assign({}, closed);
        if (c[id]) delete c[id]; else c[id] = true;
        closed = c;
    }
    // the pictures are not shipped: a note where each one was
    function prepare(md) {
        return String(md).replace(/^[ \t]*!\[[^\]\n]*\]\([^)\n]*\)[ \t]*$/gm, "*(picture not included)*")
                         .replace(/!\[[^\]\n]*\]\([^)\n]*\)/g, "*(picture not included)*");
    }

    Component.onCompleted: {
        api.invoke("content:get", ["help/toc.json"], function (t) {
            // every section starts folded except the current topic's (onCurChanged opens it once toc is set)
            const c = {};
            (t || []).forEach(function (g) { c[g.id] = true; });
            closed = c;
            toc = t || [];
            toc.forEach(function (g) {
                g.topics.forEach(function (tp) {
                    api.invoke("content:text", ["help/pages/" + tp.id + ".md"], function (text) {
                        if (!text) return;
                        const p = Object.assign({}, view.pages);
                        p[tp.id] = view.prepare(text);
                        view.pages = p;
                    });
                });
            });
            if (testScript === "help-about") {
                // the About box screenshot: Help > About opens Main's aboutLoader
                const kids = view.app.contentItem.children;
                for (let i = 0; i < kids.length; ++i)
                    if (String(kids[i].source || "").indexOf("AboutDialog.qml") >= 0) kids[i].active = true;
            } else if (testScript.indexOf("help-") === 0) view.setTopic(testScript.slice(5));
        });
    }
    // keep the section of the current topic open (e.g. after Next / Previous), and start the page at the top
    onCurChanged: {
        if (cur && closed[cur.group.id]) toggle(cur.group.id);
        page.scrollTo(0);
    }

    DocTitleBar {
        id: titleBar
        width: parent.width
        title: "Help"
        crumb: view.cur ? view.cur.group.title : ""
    }

    // the contents
    Item {
        id: nav
        y: titleBar.height + 4
        width: 230
        height: parent.height - y
        DocField {
            id: search
            width: parent.width - (clearButton.visible ? clearButton.width + 2 : 0)
            placeholder: "Search help"
            onTextChanged: view.q = text
        }
        ToolButton95 {
            id: clearButton
            visible: search.text !== ""
            anchors.right: parent.right
            width: 21
            height: 21
            onClicked: search.text = ""
            Text { anchors.centerIn: parent; text: "✕"; font.family: Theme.fontFamily; font.pixelSize: Theme.fontSize; color: Theme.text }
            ToolTipArea { text: "Clear" }
        }
        DocRows {
            y: search.height + 4
            width: parent.width
            height: parent.height - y
            model: view.rows
            emptyText: view.groups.length === 0 && view.toc.length ? "No topics match “" + view.q + "”." : ""
            delegate: Item {
                id: row
                required property var modelData
                readonly property bool isGroup: modelData.group !== undefined
                readonly property bool active: !isGroup && view.cur && view.cur.topic.id === modelData.topic.id
                width: ListView.view.width
                height: isGroup ? 22 : 19
                Rectangle { anchors.fill: parent; color: row.isGroup ? Theme.face : row.active ? Theme.selection : "transparent" }
                Text {
                    x: row.isGroup ? 4 : 18
                    width: parent.width - x - (count.visible ? count.width + 8 : 4)
                    anchors.verticalCenter: parent.verticalCenter
                    elide: Text.ElideRight
                    textFormat: Text.PlainText
                    text: row.isGroup ? (row.modelData.open ? "▾ " : "▸ ") + row.modelData.group.title
                                      : row.modelData.topic.title + (view.pages[row.modelData.topic.id] ? "" : "  …")
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontSize
                    font.bold: row.isGroup
                    color: row.active ? Theme.selectionText : Theme.text
                }
                Text {
                    id: count
                    visible: row.isGroup
                    anchors.right: parent.right
                    anchors.rightMargin: 6
                    anchors.verticalCenter: parent.verticalCenter
                    text: row.isGroup ? String(row.modelData.group.topics.length) : ""
                    textFormat: Text.PlainText
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontSize - 1
                    color: Theme.shadow
                }
                MouseArea {
                    anchors.fill: parent
                    onClicked: row.isGroup ? view.toggle(row.modelData.group.id) : view.setTopic(row.modelData.topic.id)
                }
            }
        }
    }

    // the page
    DocScroll {
        id: page
        x: nav.width + 6
        y: nav.y
        width: parent.width - x
        height: nav.height
        padding: 16
        Label95 { dim: true; text: view.cur ? view.cur.group.title : "" }
        Heading95 { level: 1; width: page.availableWidth; wrapMode: Text.Wrap; text: view.cur ? view.cur.topic.title : "" }
        Label95 { width: page.availableWidth; wrapMode: Text.Wrap; font.pixelSize: Theme.fontSize + 2; color: "#404040"; text: view.cur ? view.cur.topic.summary : "" }
        Button95 {
            visible: !!view.cur && !!view.cur.topic.view
            text: "Open this screen"
            onClicked: view.app.setView(view.cur.topic.view)
        }
        DocMarkdown {
            visible: view.body !== ""
            width: page.availableWidth
            text: view.body
            onLinkActivated: (link) => api.invoke("app:openExternal", [link])
        }
        Label95 {
            visible: view.body === ""
            width: page.availableWidth
            wrapMode: Text.Wrap
            font.italic: true
            dim: true
            text: "This page is being written. Screenshots and step-by-step instructions will appear here."
        }
        Rectangle { width: page.availableWidth; height: 1; color: Theme.shadow }
        Item {
            width: page.availableWidth
            height: 24
            Button95 {
                visible: !!view.prev
                text: view.prev ? "← " + view.prev.topic.title : ""
                onClicked: view.setTopic(view.prev.topic.id)
            }
            Button95 {
                visible: !!view.next
                anchors.right: parent.right
                text: view.next ? view.next.topic.title + " →" : ""
                onClicked: view.setTopic(view.next.topic.id)
            }
        }
    }
}
