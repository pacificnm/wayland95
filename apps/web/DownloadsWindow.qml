import QtQuick
import QtQuick.Window
import QtWebEngine
import Win95Ui
import Win95Web

// Tools > Downloads: what's been saved this time (in the Downloads folder),
// with progress, Cancel, Open and Show in Folder. A window of its own, so
// it can sit beside the browser.
Window {
    id: win

    width: 460
    height: 320
    minimumWidth: 360
    minimumHeight: 200
    title: "Downloads"
    color: Theme.face
    visible: false

    property int chosen: -1
    // (this time's, private windows' too)
    readonly property var list: Profile.downloads.concat(Browser.privateProfile ? Browser.privateProfile.downloads : []).reverse()
    readonly property var item: chosen >= 0 && chosen < list.length ? list[chosen] : null

    function show() {
        if (chosen < 0 && list.length)
            chosen = 0;
        visible = true;
        raise();
        requestActivate();
    }
    function size(n) {
        if (n < 0) return "?";
        if (n < 1024) return n + " bytes";
        if (n < 1024 * 1024) return Math.round(n / 1024) + " KB";
        if (n < 1024 * 1024 * 1024) return (n / 1048576).toFixed(1) + " MB";
        return (n / 1073741824).toFixed(2) + " GB";
    }
    function path(d) { return d.downloadDirectory + "/" + d.downloadFileName; }
    function done(d) { return d.state === WebEngineDownloadRequest.DownloadCompleted; }
    function describe(d) {
        switch (d.state) {
        case WebEngineDownloadRequest.DownloadCompleted: return "Done - " + size(d.totalBytes >= 0 ? d.totalBytes : d.receivedBytes);
        case WebEngineDownloadRequest.DownloadCancelled: return "Cancelled";
        case WebEngineDownloadRequest.DownloadInterrupted: return "Failed - " + d.interruptReasonString;
        }
        if (d.isPaused) return "Paused - " + size(d.receivedBytes) + (d.totalBytes > 0 ? " of " + size(d.totalBytes) : "");
        return size(d.receivedBytes) + (d.totalBytes > 0 ? " of " + size(d.totalBytes) : "");
    }

    Shortcut { sequence: "Escape"; onActivated: win.close() }
    Connections {
        target: Profile
        function onDownloadStarted() { win.chosen = 0; }
    }

    Bevel {
        x: 8; y: 8
        width: parent.width - 16
        height: parent.height - 16 - 36
        style: "sunken"
        face: Theme.window
        ListView {
            id: listView
            x: 2; y: 2
            width: parent.width - 4
            height: parent.height - 4
            clip: true
            model: win.list
            delegate: Rectangle {
                id: row
                required property var modelData
                required property int index
                readonly property bool sel: win.chosen === index
                width: ListView.view.width
                height: 44
                color: sel ? Theme.selection : "transparent"
                WebIcon { x: 6; y: 6; size: 32; kind: "file" }
                Text {
                    x: 46; y: 4
                    width: parent.width - 52
                    elide: Text.ElideMiddle
                    text: row.modelData.downloadFileName
                    color: row.sel ? Theme.selectionText : Theme.text
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontSize
                    font.bold: true
                }
                ProgressBar95 {
                    x: 46; y: 22
                    width: 140
                    height: 16
                    visible: !row.modelData.isFinished
                    value: row.modelData.totalBytes > 0 ? Math.round(100 * row.modelData.receivedBytes / row.modelData.totalBytes) : -1
                }
                Text {
                    x: row.modelData.isFinished ? 46 : 194
                    y: 23
                    width: parent.width - x - 6
                    elide: Text.ElideRight
                    text: win.describe(row.modelData)
                    color: row.sel ? Theme.selectionText : Theme.text
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontSize
                }
                MouseArea {
                    anchors.fill: parent
                    onClicked: win.chosen = row.index
                    onDoubleClicked: if (win.done(row.modelData)) Browser.openFile(win.path(row.modelData))
                }
            }
            Text {
                anchors.centerIn: parent
                visible: win.list.length === 0
                text: "Nothing has been downloaded yet."
                color: Theme.text
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontSize
            }
        }
    }
    Row {
        anchors.right: parent.right
        anchors.rightMargin: 8
        anchors.bottom: parent.bottom
        anchors.bottomMargin: 8
        spacing: 6
        Button95 {
            width: 75; text: "<u>O</u>pen"
            enabled: !!win.item && win.done(win.item)
            onClicked: Browser.openFile(win.path(win.item))
        }
        Button95 {
            width: 100; text: "Show in <u>F</u>older"
            enabled: !!win.item && win.done(win.item)
            onClicked: Browser.showInFolder(win.path(win.item))
        }
        Button95 {
            width: 75
            text: win.item && win.item.isPaused ? "<u>R</u>esume" : "<u>P</u>ause"
            enabled: !!win.item && !win.item.isFinished
            onClicked: if (win.item.isPaused) win.item.resume(); else win.item.pause()
        }
        Button95 {
            width: 75; text: "<u>C</u>ancel"
            enabled: !!win.item && !win.item.isFinished
            onClicked: win.item.cancel()
        }
        Button95 { width: 75; text: "Close"; onClicked: win.close() }
    }
}
