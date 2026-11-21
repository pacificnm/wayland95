import QtQuick
import Win95Ui

// A small coloured tag (upstream .jr-badge): level, status, bias, rarity, "written by Claude".
// tone: green | blue | amber | red | grey | purple
Rectangle {
    id: badge
    property string text: ""
    property string tone: "grey"
    readonly property var tones: ({ green: ["#dff2df", "#1e6b1e"], blue: ["#dde8fb", "#1f4f9e"], amber: ["#fbf0d0", "#80590a"],
                                    red: ["#fbdede", "#a01e1e"], grey: ["#e8e8e8", "#404040"], purple: ["#ece2fa", "#5b2c9a"] })
    implicitWidth: t.implicitWidth + 10
    implicitHeight: 16
    color: (tones[tone] || tones.grey)[0]
    border.color: (tones[tone] || tones.grey)[1]
    Text {
        id: t
        anchors.centerIn: parent
        text: badge.text
        textFormat: Text.PlainText
        color: (badge.tones[badge.tone] || badge.tones.grey)[1]
        font.family: Theme.fontFamily
        font.pixelSize: Theme.fontSize - 1
    }
}
