import QtQuick
import QtQuick.Window
import Win95Ui
import Win95Photos

// The slide show: the folder's photos full-screen, one after another, each
// fading in. Space pauses, the arrow keys step, Escape (or a click) ends it.
FocusScope {
    id: show

    property PhotoFolder folder
    property int interval: 5000
    property string path: ""
    property bool paused: false
    // the window's visibility before (it goes full-screen)
    property int restoreTo: Window.Windowed

    signal ended(string path)

    anchors.fill: parent
    visible: false
    z: 3000

    function start(from) {
        path = from || folder.photoAt(0);
        if (!path)
            return;
        paused = false;
        restoreTo = Window.window.visibility;
        Window.window.showFullScreen();
        shown.next(path);
        visible = true;
        forceActiveFocus();
        timer.restart();
    }

    function stop() {
        if (!visible)
            return;
        timer.stop();
        visible = false;
        if (restoreTo === Window.Maximized)
            Window.window.showMaximized();
        else
            Window.window.showNormal();
        ended(path);
    }

    function step(dir) {
        var p = folder.stepFrom(path, dir);
        if (!p)
            return;
        path = p;
        shown.next(p);
        if (!paused)
            timer.restart();
    }

    Rectangle {
        anchors.fill: parent
        color: "#000000"
    }

    // two images: the new one fades in over the old
    Item {
        id: shown
        anchors.fill: parent
        property int front: 0

        function next(p) {
            var img = front === 0 ? b : a;
            img.opacity = 0;
            img.source = PhotoFiles.viewUrl(p);
            front = 1 - front;
        }

        // the one that just loaded shows, the other goes
        function loaded(img, other) {
            if (img.status === Image.Ready && img.source.toString() === PhotoFiles.viewUrl(show.path)) {
                img.opacity = 1;
                other.opacity = 0;
            }
        }

        Image {
            id: a
            anchors.fill: parent
            fillMode: Image.PreserveAspectFit
            asynchronous: true
            cache: false
            smooth: true
            mipmap: true
            opacity: 0
            Behavior on opacity { NumberAnimation { duration: 600 } }
            onStatusChanged: shown.loaded(a, b)
        }
        Image {
            id: b
            anchors.fill: parent
            fillMode: Image.PreserveAspectFit
            asynchronous: true
            cache: false
            smooth: true
            mipmap: true
            opacity: 0
            Behavior on opacity { NumberAnimation { duration: 600 } }
            onStatusChanged: shown.loaded(b, a)
        }
    }

    Text {
        anchors.bottom: parent.bottom
        anchors.right: parent.right
        anchors.margins: 12
        visible: show.paused
        text: "Paused"
        color: "#c0c0c0"
        font.family: Theme.fontFamily
        font.pixelSize: 14
    }

    Timer {
        id: timer
        interval: show.interval
        repeat: true
        running: false
        onTriggered: show.step(1)
    }

    MouseArea {
        anchors.fill: parent
        cursorShape: Qt.BlankCursor
        acceptedButtons: Qt.LeftButton | Qt.RightButton
        onClicked: show.stop()
        onWheel: (wheel) => show.step(wheel.angleDelta.y > 0 ? -1 : 1)
    }

    Keys.onPressed: (event) => {
        switch (event.key) {
        case Qt.Key_Escape:
        case Qt.Key_F5:
            show.stop();
            break;
        case Qt.Key_Space:
            show.paused = !show.paused;
            if (show.paused)
                timer.stop();
            else
                timer.restart();
            break;
        case Qt.Key_Right:
        case Qt.Key_Down:
        case Qt.Key_PageDown:
            show.step(1);
            break;
        case Qt.Key_Left:
        case Qt.Key_Up:
        case Qt.Key_PageUp:
        case Qt.Key_Backspace:
            show.step(-1);
            break;
        default:
            return;
        }
        event.accepted = true;
    }
}
