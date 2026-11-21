import QtQuick
import Win95Ui

// The scrolling body of a screen (upstream ScreenFrame's pane body): the amber sample-data note, the "Data as of"
// line with a Refresh button, "Loading…", the error banner with Retry, then the screen's own content in a column.
// Used by the per-symbol and market-wide screens (Quote, Analyst, Fundamentals, News, Options, Market...).
Item {
    id: page
    property string sample: ""          // the sample-data note ("" = none)
    property bool loading: false        // shows "Loading…"
    property string loadingText: "Loading…"
    property string error: ""
    property bool canRetry: true
    property real asOf: 0               // UTC seconds the data was fetched (0 = no line)
    property string asOfLabel: "cached to limit FMP requests"
    property bool canRefresh: true
    property bool refreshing: false
    property real gutter: 8
    readonly property real contentWidth: body.width
    readonly property alias flickable: flick
    default property alias content: body.data
    signal retry()
    signal refresh()

    function scrollToTop() { flick.contentY = 0; }

    Flickable {
        id: flick
        anchors.fill: parent
        anchors.rightMargin: bar.visible ? bar.width : 0
        clip: true
        interactive: false
        boundsBehavior: Flickable.StopAtBounds
        contentWidth: width
        contentHeight: outer.height + 2 * page.gutter
        Column {
            id: outer
            x: page.gutter
            y: page.gutter
            width: flick.width - 2 * page.gutter
            spacing: 8
            Notice {
                width: parent.width
                kind: "warning"
                text: page.sample
            }
            Row {
                visible: page.asOf > 0
                spacing: 8
                height: visible ? 23 : 0
                Label95 {
                    anchors.verticalCenter: parent.verticalCenter
                    dim: true
                    text: page.asOf > 0 ? "Data as of " + Util.dateTime(page.asOf) + " · " + Util.ago(page.asOf) + " · " + page.asOfLabel : ""
                }
                Button95 {
                    visible: page.canRefresh
                    height: 21
                    text: page.refreshing ? "Refreshing…" : "Refresh"
                    enabled: !page.refreshing
                    onClicked: page.refresh()
                }
            }
            Label95 { visible: page.loading; text: page.loadingText; dim: true }
            Notice {
                width: parent.width
                kind: "error"
                text: page.error ? Util.plain(page.error) : ""
                buttons: page.canRetry ? [{ text: "Retry", action: function () { page.retry(); } }] : []
            }
            Column {
                id: body
                width: parent.width
                spacing: 8
            }
        }
        WheelHandler {
            onWheel: (event) => {
                flick.contentY = Math.max(0, Math.min(flick.contentHeight - flick.height, flick.contentY - event.angleDelta.y / 120 * 48));
            }
        }
    }
    // W95_TRADINGLAB_TEST=<anything>-bottom: scroll to the end before the window is grabbed (for screenshots)
    Timer {
        running: typeof testScript === "string" && testScript.endsWith("-bottom")
        interval: 4500
        onTriggered: flick.contentY = Math.max(0, flick.contentHeight - flick.height)
    }
    ScrollBar95 {
        id: bar
        anchors.right: parent.right
        y: 0
        height: parent.height
        flickable: flick
        stepSize: 24
    }
}
