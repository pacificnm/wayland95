import QtQuick

// Catches the input over the screen saver: a key, a click or the wheel
// wakes it; so does a real move of the mouse (a nudge of the desk doesn't).
// During `grace` (just after Preview was clicked) moves and the wheel don't.
MouseArea {
    id: area

    property bool grace: false
    signal woken()

    property point origin
    property bool haveOrigin: false
    function reset() { haveOrigin = false; }
    // the keyboard back here (from the password box, when it closes)
    function takeKeys() { keys.forceActiveFocus(); }

    anchors.fill: parent
    hoverEnabled: true
    acceptedButtons: Qt.AllButtons
    cursorShape: Qt.BlankCursor
    onPositionChanged: (mouse) => {
        if (grace)
            return;
        if (!haveOrigin) {
            origin = Qt.point(mouse.x, mouse.y);
            haveOrigin = true;
        } else if (Math.abs(mouse.x - origin.x) + Math.abs(mouse.y - origin.y) > 10) {
            area.woken();
        }
    }
    onPressed: area.woken()
    onWheel: { if (!grace) area.woken(); }

    Item {
        id: keys
        anchors.fill: parent
        focus: true
        Keys.onPressed: (event) => { event.accepted = true; area.woken(); }
    }
}
