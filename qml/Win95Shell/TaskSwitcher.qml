import QtQuick
import Win95Ui as Ui
import Win95Shell

// The Alt+Tab box, like Win95's: a row of window icons with the selected
// one boxed, and its title underneath. TaskSwitchKeys drives it.
Item {
    id: switcher

    anchors.fill: parent
    visible: false

    property var windows: []   // WindowManager.switchOrder()
    property int index: 0
    readonly property int perRow: 7
    readonly property int cell: 43

    // our own programs' icons (their app IDs aren't icon names)
    readonly property var ownIcons: ({
        win95notepad: "notepad", win95files: "files", win95terminal: "terminal", win95control: "control",
        win95sysmon: "sysmon", win95volume: "volume", win95imagewriter: "imagewriter",
        win95solitaire: "solitaire", win95mines: "mines", win95find: "find", win95repos: "repos", win95markdown: "markdown", win95photos: "photos", win95archive: "archive", "win95archive-progress": "archive", win95calc: "calc", win95email: "email", win95web: "web", win95studio: "studio",
    })
    function appIdOf(frame) {
        if (frame.toplevel)
            return frame.toplevel.appId || "";
        if (frame.xwindow)
            return frame.xwindow.appId || "";
        return "";
    }

    function start(backwards) {
        windows = WindowManager.switchOrder();
        if (windows.length === 0)
            return;
        // from the active window, one step on (or back)
        index = windows.length === 1 ? 0 : (backwards ? windows.length - 1 : 1);
        visible = windows.length > 1;
    }
    function step(backwards) {
        if (windows.length === 0)
            return;
        index = (index + (backwards ? -1 : 1) + windows.length) % windows.length;
    }
    function finish() {
        visible = false;
        var w = windows[index];
        windows = [];
        if (!w || !w.frame)
            return;
        if (w.frame.minimized)
            WindowManager.setMinimized(w.frame, false);
        w.frame.requestActivate();
    }
    function cancel() {
        visible = false;
        windows = [];
    }
    // win95shell: the compositor runs Alt+Tab and says what to show
    // (windows as [{title, appId, minimized}])
    function showList(list, selected) {
        windows = list;
        index = selected;
        visible = list.length > 1;
    }
    function select(selected) { index = selected; }
    function hide() {
        visible = false;
        windows = [];
    }

    Item {
        id: box
        anchors.centerIn: parent
        width: Math.min(switcher.windows.length, switcher.perRow) * switcher.cell + 24
        height: Math.ceil(switcher.windows.length / switcher.perRow) * switcher.cell + 54

        Bevel { anchors.fill: parent; raised: true }

        Grid {
            id: icons
            x: 12
            y: 12
            columns: switcher.perRow
            Repeater {
                model: switcher.windows
                Item {
                    required property var modelData
                    required property int index
                    readonly property string appId: modelData.appId !== undefined ? modelData.appId
                                                                                  : switcher.appIdOf(modelData.frame)
                    readonly property string iconUrl: switcher.ownIcons[appId] ? "" : ProgramMenu.iconFor(appId)
                    width: switcher.cell
                    height: switcher.cell
                    // the selected one: a box around it
                    Rectangle {
                        anchors.fill: parent
                        anchors.margins: 1
                        visible: parent.index === switcher.index
                        color: "transparent"
                        border.color: "#000000"
                        border.width: 2
                    }
                    Image {
                        id: appIcon
                        anchors.centerIn: parent
                        width: 32
                        height: 32
                        sourceSize: Qt.size(32, 32)
                        source: parent.iconUrl
                        visible: status === Image.Ready
                        fillMode: Image.PreserveAspectFit
                    }
                    MenuIcon {
                        anchors.centerIn: parent
                        visible: !appIcon.visible
                        kind: switcher.ownIcons[parent.appId] || "app"
                        opacity: parent.modelData.minimized ? 0.6 : 1
                    }
                }
            }
        }

        Ui.Bevel {
            x: 12
            y: icons.y + icons.height + 8
            width: parent.width - 24
            height: 22
            style: "sunken"
            Text {
                x: 6
                width: parent.width - 12
                anchors.verticalCenter: parent.verticalCenter
                horizontalAlignment: Text.AlignHCenter
                elide: Text.ElideRight
                text: switcher.windows.length ? switcher.windows[switcher.index].title : ""
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontSize
                font.bold: true
            }
        }
    }
}
