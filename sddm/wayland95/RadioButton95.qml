import QtQuick

// A Win95 option button. The owner sets `checked`. `text` may use <u>x</u>.
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
            ctx.strokeStyle = W95.shadow;
            ctx.beginPath(); ctx.arc(6, 6, 5.5, Math.PI * 0.75, Math.PI * 1.75); ctx.stroke();
            ctx.strokeStyle = W95.highlight;
            ctx.beginPath(); ctx.arc(6, 6, 5.5, Math.PI * 1.75, Math.PI * 0.75); ctx.stroke();
            ctx.strokeStyle = W95.dark;
            ctx.beginPath(); ctx.arc(6, 6, 4.5, Math.PI * 0.75, Math.PI * 1.75); ctx.stroke();
            ctx.strokeStyle = W95.light;
            ctx.beginPath(); ctx.arc(6, 6, 4.5, Math.PI * 1.75, Math.PI * 0.75); ctx.stroke();
            ctx.fillStyle = W95.window;
            ctx.beginPath(); ctx.arc(6, 6, 4, 0, Math.PI * 2); ctx.fill();
            if (radio.checked) {
                ctx.fillStyle = W95.text;
                ctx.beginPath(); ctx.arc(6, 6, 2, 0, Math.PI * 2); ctx.fill();
            }
        }
    }
    onCheckedChanged: circle.requestPaint()

    Text {
        id: label
        x: 18
        anchors.verticalCenter: parent.verticalCenter
        text: radio.text
        textFormat: Text.StyledText
        color: W95.text
        font.family: W95.fontFamily
        font.pixelSize: W95.fontSize
    }
    // dotted focus rectangle around the label
    Rectangle {
        visible: radio.activeFocus
        x: label.x - 2
        y: label.y - 1
        width: label.implicitWidth + 4
        height: label.implicitHeight + 2
        color: "transparent"
        border.color: W95.dark
        opacity: 0.6
    }

    MouseArea {
        anchors.fill: parent
        onClicked: {
            radio.forceActiveFocus();
            radio.clicked();
        }
    }
}
