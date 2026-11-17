import QtQuick

// One playing card, 72x96. cardId: 0-51, suit = cardId / 13 (clubs, diamonds,
// hearts, spades), rank = cardId % 13 + 1. The faces are plain: corner
// indices, the usual pip layouts, and a framed letter on the court cards.
Item {
    id: card

    property int cardId: 0
    property bool faceUp: false
    property int backDesign: 0
    property bool selected: false

    readonly property int suit: Math.floor(cardId / 13)
    readonly property int rank: cardId % 13 + 1
    readonly property bool red: suit === 1 || suit === 2
    readonly property string suitText: ["♣", "♦", "♥", "♠"][suit]
    readonly property string rankText: ["A", "2", "3", "4", "5", "6", "7", "8", "9", "10", "J", "Q", "K"][rank - 1]
    readonly property color ink: red ? "#d00000" : "#000000"

    width: 72
    height: 96

    // pip positions: [column (0 left, 1 middle, 2 right), row 0..1]
    readonly property var pips: {
        var L = 0, M = 1, R = 2, t = 1 / 3, s = 1 / 6;
        switch (rank) {
        case 2: return [[M, 0], [M, 1]];
        case 3: return [[M, 0], [M, 0.5], [M, 1]];
        case 4: return [[L, 0], [R, 0], [L, 1], [R, 1]];
        case 5: return [[L, 0], [R, 0], [M, 0.5], [L, 1], [R, 1]];
        case 6: return [[L, 0], [R, 0], [L, 0.5], [R, 0.5], [L, 1], [R, 1]];
        case 7: return [[L, 0], [R, 0], [M, 0.25], [L, 0.5], [R, 0.5], [L, 1], [R, 1]];
        case 8: return [[L, 0], [R, 0], [M, 0.25], [L, 0.5], [R, 0.5], [M, 0.75], [L, 1], [R, 1]];
        case 9: return [[L, 0], [R, 0], [L, t], [R, t], [M, 0.5], [L, 2 * t], [R, 2 * t], [L, 1], [R, 1]];
        case 10: return [[L, 0], [R, 0], [M, s], [L, t], [R, t], [L, 2 * t], [R, 2 * t], [M, 5 * s], [L, 1], [R, 1]];
        default: return [];
        }
    }

    Rectangle {
        anchors.fill: parent
        radius: 4
        color: card.faceUp ? (card.selected ? "#c0c0ff" : "#ffffff") : "#ffffff"
        border.color: "#000000"
        border.width: 1
    }

    CardBack {
        anchors.fill: parent
        anchors.margins: 1
        visible: !card.faceUp
        design: card.backDesign
    }

    Item {
        anchors.fill: parent
        visible: card.faceUp

        // the corner indices, top left and (upside down) bottom right
        Repeater {
            model: 2
            delegate: Column {
                required property int index
                x: index === 0 ? 3 : card.width - 3 - width
                y: index === 0 ? 3 : card.height - 3 - height
                width: 14
                rotation: index === 0 ? 0 : 180
                spacing: -3
                Text {
                    width: parent.width
                    horizontalAlignment: Text.AlignHCenter
                    text: card.rankText
                    color: card.ink
                    font.family: "Liberation Sans"
                    font.pixelSize: 12
                    font.bold: true
                    font.letterSpacing: card.rank === 10 ? -1.5 : 0
                }
                Text {
                    width: parent.width
                    horizontalAlignment: Text.AlignHCenter
                    text: card.suitText
                    color: card.ink
                    font.family: "DejaVu Sans"
                    font.pixelSize: 11
                }
            }
        }

        // the pips
        Repeater {
            model: card.pips
            delegate: Text {
                required property var modelData
                width: 16
                height: 16
                x: 12 + modelData[0] * 16
                y: 14 + modelData[1] * 52
                rotation: modelData[1] > 0.5 ? 180 : 0
                horizontalAlignment: Text.AlignHCenter
                verticalAlignment: Text.AlignVCenter
                text: card.suitText
                color: card.ink
                font.family: "DejaVu Sans"
                font.pixelSize: 15
            }
        }

        // an ace: one large pip
        Text {
            anchors.centerIn: parent
            visible: card.rank === 1
            text: card.suitText
            color: card.ink
            font.family: "DejaVu Sans"
            font.pixelSize: 34
        }

        // the court cards: a framed panel with the letter and its suit
        Rectangle {
            visible: card.rank > 10
            x: 16
            y: 14
            width: card.width - 32
            height: card.height - 28
            color: "#fff4c0"
            border.color: card.ink
            border.width: 1
            Rectangle {
                anchors.fill: parent
                anchors.margins: 3
                color: "transparent"
                border.color: card.red ? "#e08080" : "#808080"
                border.width: 1
            }
            Column {
                anchors.centerIn: parent
                spacing: 0
                Text {
                    anchors.horizontalCenter: parent.horizontalCenter
                    text: card.rankText
                    color: card.ink
                    font.family: "Liberation Serif"
                    font.pixelSize: 28
                    font.bold: true
                }
                Text {
                    anchors.horizontalCenter: parent.horizontalCenter
                    text: card.suitText
                    color: card.ink
                    font.family: "DejaVu Sans"
                    font.pixelSize: 18
                }
            }
        }
    }
}
