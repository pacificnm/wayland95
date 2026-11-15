import QtQuick

// A Win95 option button: sunken white circle with a black dot when
// checked. Doesn't manage its group - the owner sets `checked`. `text`
// may use <u>..</u> to underline the access key.
FocusScope {
    id: radio

    property string text: ""
    property bool checked: false
    signal clicked()

    implicitWidth: 18 + label.implicitWidth + 4
    implicitHeight: 18
    activeFocusOnTab: true

    Canvas {
        id: circle
        width: 12
        height: 12
        anchors.verticalCenter: parent.verticalCenter
        onPaint: {
            var ctx = getContext("2d");
            ctx.reset();
            ctx.lineWidth = 1;
            // sunken edge: dark top-left, light bottom-right
            ctx.strokeStyle = Theme.shadow;
            ctx.beginPath(); ctx.arc(6, 6, 5.5, Math.PI * 0.75, Math.PI * 1.75); ctx.stroke();
            ctx.strokeStyle = Theme.highlight;
            ctx.beginPath(); ctx.arc(6, 6, 5.5, Math.PI * 1.75, Math.PI * 0.75); ctx.stroke();
            ctx.strokeStyle = Theme.dark;
            ctx.beginPath(); ctx.arc(6, 6, 4.5, Math.PI * 0.75, Math.PI * 1.75); ctx.stroke();
            ctx.strokeStyle = Theme.light;
            ctx.beginPath(); ctx.arc(6, 6, 4.5, Math.PI * 1.75, Math.PI * 0.75); ctx.stroke();
            ctx.fillStyle = Theme.window;
            ctx.beginPath(); ctx.arc(6, 6, 4, 0, Math.PI * 2); ctx.fill();
            if (radio.checked) {
                ctx.fillStyle = Theme.text;
                ctx.beginPath(); ctx.arc(6, 6, 2, 0, Math.PI * 2); ctx.fill();
            }
        }
    }
    onCheckedChanged: circle.requestPaint()
    // repaint when the color scheme changes
    property var schemeColors: Theme.c
    onSchemeColorsChanged: circle.requestPaint()

    Text {
        id: label
        x: 18
        anchors.verticalCenter: parent.verticalCenter
        text: radio.text
        textFormat: Text.StyledText // allows <u>x</u> access keys
        color: Theme.text
        font.family: Theme.fontFamily
        font.pixelSize: Theme.fontSize
    }

    Loader {
        active: radio.activeFocus
        x: label.x - 2
        y: label.y - 1
        width: label.implicitWidth + 4
        height: label.implicitHeight + 2
        sourceComponent: FocusRect95 {}
    }

    MouseArea {
        anchors.fill: parent
        onClicked: {
            radio.forceActiveFocus();
            radio.clicked();
        }
    }
}
