import QtQuick
import QtQuick.Window
import Win95Ui
import Win95Solitaire

// Solitaire: Klondike patience. Build the four foundations (top right) up
// from Ace to King in each suit. On the seven tableau columns, cards go
// down in alternating colours, and only a King fills an empty column.
// Click the stock (top left) to turn cards over; drag cards to move them;
// double-click a card to send it to a foundation, and right-click the
// table to send every card that can go.
Window {
    id: window

    width: 640
    height: 480
    minimumWidth: 600
    minimumHeight: 400
    visible: true
    color: Theme.face
    title: "Solitaire"

    // ---------------------------------------------------------------- game
    // g: { stock, waste, found[4], tab[7] } arrays of cards (0-51, the last
    // is the top), and up[card]: face up
    property var g: ({ stock: [], waste: [], found: [[], [], [], []], tab: [[], [], [], [], [], [], []], up: [] })
    property int version: 0          // bumped on every change (g is changed in place)
    property int score: 0
    property int seconds: 0
    property bool started: false     // the clock runs from the first move
    property bool won: false
    property var history: []         // undo: earlier states

    // options (saved)
    property bool drawThree: false
    property bool keepScore: true
    property bool timed: true
    property int backDesign: 0

    readonly property bool debug: gameDebug

    // a seedable random number generator (mulberry32): --seed deals the same
    // games every run
    property double rngState: gameSeed >= 0 ? gameSeed : Date.now() % 4294967296
    function random() {
        var t = (rngState += 0x6D2B79F5) >>> 0;
        rngState = t;
        t = Math.imul(t ^ (t >>> 15), t | 1);
        t ^= t + Math.imul(t ^ (t >>> 7), t | 61);
        return ((t ^ (t >>> 14)) >>> 0) / 4294967296;
    }

    function rankOf(c) { return c % 13 + 1; }
    function suitOf(c) { return Math.floor(c / 13); }
    function isRed(c) { var s = suitOf(c); return s === 1 || s === 2; }
    function nameOf(c) { return ["A", "2", "3", "4", "5", "6", "7", "8", "9", "10", "J", "Q", "K"][c % 13] + "CDHS"[suitOf(c)]; }
    function top(list) { return list.length ? list[list.length - 1] : -1; }

    function deal() {
        var deck = [];
        for (var i = 0; i < 52; ++i)
            deck.push(i);
        for (var k = 51; k > 0; --k) {
            var j = Math.floor(random() * (k + 1));
            var t = deck[k]; deck[k] = deck[j]; deck[j] = t;
        }
        var s = { stock: [], waste: [], found: [[], [], [], []], tab: [[], [], [], [], [], [], []], up: [] };
        for (var u = 0; u < 52; ++u)
            s.up.push(false);
        var n = 0;
        for (var col = 0; col < 7; ++col)
            for (var row = 0; row <= col; ++row) {
                var c = deck[n++];
                s.tab[col].push(c);
                s.up[c] = row === col;
            }
        while (n < 52)
            s.stock.push(deck[n++]);
        g = s;
        history = [];
        score = 0;
        seconds = 0;
        started = false;
        won = false;
        clock.stop();
        changed();
    }

    function changed() {
        ++version;
        if (debug)
            logState();
    }

    function snapshot() {
        history.push(JSON.stringify({ g: g, score: score }));
        if (history.length > 200)
            history.shift();
        historyChanged();
    }
    function undo() {
        if (!history.length || won)
            return;
        var s = JSON.parse(history.pop());
        historyChanged();
        g = s.g;
        score = s.score;
        changed();
    }

    function addScore(n) {
        if (keepScore)
            score = Math.max(0, score + n);
    }
    function begin() {
        if (!started) {
            started = true;
            if (timed)
                clock.start();
        }
    }

    // the stock: turn one (or three) over; empty, turn the waste back
    function drawCards() {
        if (won)
            return;
        snapshot();
        begin();
        if (g.stock.length) {
            var n = Math.min(drawThree ? 3 : 1, g.stock.length);
            for (var i = 0; i < n; ++i) {
                var c = g.stock.pop();
                g.up[c] = true;
                g.waste.push(c);
            }
        } else if (g.waste.length) {
            while (g.waste.length) {
                var w = g.waste.pop();
                g.up[w] = false;
                g.stock.push(w);
            }
            addScore(drawThree ? -20 : -100);
        } else {
            history.pop();
            historyChanged();
            return;
        }
        changed();
    }

    // pile: { kind: "tab"|"found"|"waste"|"stock", i }
    function pileList(p) {
        return p.kind === "tab" ? g.tab[p.i] : p.kind === "found" ? g.found[p.i]
             : p.kind === "waste" ? g.waste : g.stock;
    }
    function canPlace(cards, to) {
        var first = cards[0], under = top(pileList(to));
        if (to.kind === "found") {
            if (cards.length !== 1)
                return false;
            return under < 0 ? rankOf(first) === 1
                             : suitOf(under) === suitOf(first) && rankOf(first) === rankOf(under) + 1;
        }
        if (to.kind === "tab") {
            if (under < 0)
                return rankOf(first) === 13;
            return g.up[under] && isRed(under) !== isRed(first) && rankOf(under) === rankOf(first) + 1;
        }
        return false;
    }

    // move the cards from `count` from the top of `from` onto `to` (checked)
    function move(from, count, to) {
        var src = pileList(from);
        var cards = src.slice(src.length - count);
        if (!canPlace(cards, to))
            return false;
        snapshot();
        begin();
        src.splice(src.length - count, count);
        var dst = pileList(to);
        for (var i = 0; i < cards.length; ++i)
            dst.push(cards[i]);
        if (to.kind === "found")
            addScore(from.kind === "found" ? 0 : 10);
        else if (from.kind === "waste")
            addScore(5);
        else if (from.kind === "found")
            addScore(-15);
        // turn over the card left on top of a tableau column
        if (from.kind === "tab" && src.length && !g.up[top(src)]) {
            g.up[top(src)] = true;
            addScore(5);
        }
        changed();
        log("move " + nameOf(cards[0]) + " " + from.kind + from.i + " " + to.kind + to.i);
        checkWin();
        return true;
    }

    // a card to a foundation, if one takes it
    function toFoundation(from) {
        var c = top(pileList(from));
        if (c < 0 || !g.up[c])
            return false;
        for (var f = 0; f < 4; ++f)
            if (canPlace([c], { kind: "found", i: f }))
                return move(from, 1, { kind: "found", i: f });
        return false;
    }
    // every card that can go to a foundation, until none can
    function sendHome() {
        var any = true;
        while (any && !won) {
            any = false;
            if (toFoundation({ kind: "waste", i: 0 }))
                any = true;
            for (var t = 0; t < 7; ++t)
                if (toFoundation({ kind: "tab", i: t }))
                    any = true;
        }
    }

    function checkWin() {
        for (var f = 0; f < 4; ++f)
            if (g.found[f].length !== 13)
                return;
        won = true;
        clock.stop();
        if (keepScore && timed && seconds >= 30)
            score += Math.floor(700000 / seconds);
        log("won " + score);
        winBox.start();
    }

    function log(s) { if (debug) console.log("solitaire: " + s); }
    // the piles, in window coordinates, for the tests: NAME COUNT TOPCARD X Y
    // (X, Y: the middle of its top card, or of the empty place)
    function logState() {
        var L = layout;
        function line(name, list, base) {
            var t = top(list), at = t >= 0 ? L.cards[t] : base;
            var x = Math.round(table.x + at.x + cardW / 2), y = Math.round(table.y + at.y + (t >= 0 && name.indexOf("tab") === 0 ? 12 : cardH / 2));
            var upFrom = -1;
            for (var i = 0; i < list.length; ++i)
                if (g.up[list[i]]) { upFrom = i; break; }
            console.log("solitaire: pile " + name + " " + list.length + " " + (t >= 0 ? nameOf(t) : "-") + " " + x + " " + y
                        + " " + (upFrom >= 0 ? list.slice(upFrom).map(nameOf).join(",") : "-"));
        }
        line("stock", g.stock, L.bases.stock);
        line("waste", g.waste, L.bases.waste);
        for (var f = 0; f < 4; ++f)
            line("found" + f, g.found[f], L.bases.found[f]);
        for (var t2 = 0; t2 < 7; ++t2)
            line("tab" + t2, g.tab[t2], L.bases.tab[t2]);
        console.log("solitaire: score " + score);
    }

    Timer {
        id: clock
        interval: 1000
        repeat: true
        onTriggered: {
            ++window.seconds;
            // standard scoring: time costs 2 points every 10 seconds
            if (window.seconds % 10 === 0)
                window.addScore(-2);
        }
    }

    function saveOptions() {
        GameStore.setValue("drawThree", drawThree ? "true" : "false");
        GameStore.setValue("keepScore", keepScore ? "true" : "false");
        GameStore.setValue("timed", timed ? "true" : "false");
        GameStore.setValue("back", backDesign);
    }

    Component.onCompleted: {
        drawThree = String(GameStore.value("drawThree", "false")) === "true";
        keepScore = String(GameStore.value("keepScore", "true")) === "true";
        timed = String(GameStore.value("timed", "true")) === "true";
        backDesign = Number(GameStore.value("back", 0)) % 4;
        deal();
        table.forceActiveFocus();
    }

    // -------------------------------------------------------------- layout
    readonly property int cardW: 72
    readonly property int cardH: 96
    readonly property int margin: 12
    readonly property int gap: Math.max(8, Math.floor((table.width - 2 * margin - 7 * cardW) / 6))
    function colX(c) { return margin + c * (cardW + gap); }

    // where every card goes: cards[c] = { x, y, z }, and the empty places
    readonly property var layout: {
        void version;
        var L = { cards: [], bases: { stock: null, waste: null, found: [], tab: [] } };
        var topY = 10, tabY = topY + cardH + 14;
        L.bases.stock = { x: colX(0), y: topY };
        L.bases.waste = { x: colX(1), y: topY };
        var i, z = 0;
        for (i = 0; i < g.stock.length; ++i)
            L.cards[g.stock[i]] = { x: colX(0) + Math.floor(i / 8) * 2, y: topY + Math.floor(i / 8) * 1, z: z++ };
        // the waste: in Draw Three, its top three fan out to the right
        var fanFrom = drawThree ? Math.max(0, g.waste.length - 3) : g.waste.length;
        for (i = 0; i < g.waste.length; ++i)
            L.cards[g.waste[i]] = { x: colX(1) + (i >= fanFrom ? (i - fanFrom) * 14 : 0), y: topY, z: z++ };
        for (var f = 0; f < 4; ++f) {
            L.bases.found[f] = { x: colX(3 + f), y: topY };
            for (i = 0; i < g.found[f].length; ++i)
                L.cards[g.found[f][i]] = { x: colX(3 + f), y: topY, z: z++ };
        }
        // a long column squeezes its face-up cards to stay on the table
        var room = table.height - tabY - cardH - 6;
        for (var t = 0; t < 7; ++t) {
            var col = g.tab[t];
            L.bases.tab[t] = { x: colX(t), y: tabY };
            var downs = 0, ups = 0;
            for (i = 0; i < col.length; ++i)
                g.up[col[i]] ? ++ups : ++downs;
            var downStep = 4, upStep = 16;
            if (ups > 1 && downs * downStep + (ups - 1) * upStep > room)
                upStep = Math.max(6, Math.floor((room - downs * downStep) / (ups - 1)));
            var y = tabY;
            for (i = 0; i < col.length; ++i) {
                L.cards[col[i]] = { x: colX(t), y: y, z: 100 + i };
                y += g.up[col[i]] ? upStep : downStep;
            }
        }
        return L;
    }

    // ------------------------------------------------------------- menus
    function gameMenu() {
        return [
            { text: "Deal", shortcut: "F2", action: window.deal },
            { separator: true },
            { text: "Undo", shortcut: "Ctrl+Z", enabled: history.length > 0 && !won, action: window.undo },
            { text: "Deck...", action: function () { deckDialog.open(); } },
            { separator: true },
            { text: "Draw One", checked: !drawThree, action: function () { window.setDraw(false); } },
            { text: "Draw Three", checked: drawThree, action: function () { window.setDraw(true); } },
            { separator: true },
            { text: "Keep Score", checked: keepScore, action: function () {
                window.keepScore = !window.keepScore; window.saveOptions(); window.deal();
            } },
            { text: "Timed Game", checked: timed, action: function () {
                window.timed = !window.timed; window.saveOptions(); window.deal();
            } },
            { separator: true },
            { text: "Exit", action: window.close },
        ];
    }
    function helpMenu() {
        return [
            { text: "How to Play", action: function () {
                msgBox.show("How to Play",
                            "Move all the cards onto the four places at the top right,\n"
                            + "one per suit, from Ace up to King.\n\n"
                            + "On the seven columns, put a card on one a rank higher of the\n"
                            + "other colour (a red 6 on a black 7). Only a King goes in an\n"
                            + "empty column. You can move a run of cards together.\n\n"
                            + "Click the deck at the top left to turn cards over. Double-click\n"
                            + "a card to send it up; right-click to send up all that can go.",
                            "info", ["OK"], function () { table.forceActiveFocus(); });
            } },
            { separator: true },
            { text: "About Solitaire", action: function () {
                msgBox.show("About Solitaire", "Solitaire\n\nKlondike patience for the Wayland 95 desktop.\nVersion "
                            + Wayland95Settings.version, "info", ["OK"], function () { table.forceActiveFocus(); });
            } },
        ];
    }
    function setDraw(three) {
        if (drawThree === three)
            return;
        drawThree = three;
        saveOptions();
        deal();
    }

    readonly property bool modalOpen: popup.visible || msgBox.visible || deckDialog.visible

    Shortcut { sequence: "F2"; enabled: !window.modalOpen; onActivated: window.deal() }
    Shortcut { sequence: "Ctrl+Z"; enabled: !window.modalOpen; onActivated: window.undo() }

    MenuBar95 {
        id: menuBar
        x: 0
        y: 0
        width: parent.width
        height: 20
        popupMenu: popup
        menus: [
            { title: "Game", items: window.gameMenu },
            { title: "Help", items: window.helpMenu },
        ]
    }

    // ------------------------------------------------------------- table
    Rectangle {
        id: table
        x: 0
        y: menuBar.height
        width: parent.width
        height: parent.height - menuBar.height - statusBar.height
        color: "#008000"
        clip: true
        focus: true

        // the empty places: an outline; the stock shows whether it can turn over again
        Repeater {
            model: 12
            delegate: Item {
                required property int index
                readonly property var base: index === 0 ? window.layout.bases.stock
                                          : index < 5 ? window.layout.bases.found[index - 1]
                                          : window.layout.bases.tab[index - 5]
                x: base.x
                y: base.y
                width: window.cardW
                height: window.cardH
                Rectangle {
                    anchors.fill: parent
                    radius: 4
                    color: "transparent"
                    border.color: "#004000"
                    border.width: 2
                }
                // the stock, when it's empty: a ring to turn the waste back, or an X
                Rectangle {
                    visible: index === 0
                    anchors.centerIn: parent
                    width: 34
                    height: 34
                    radius: 17
                    color: "transparent"
                    border.color: window.g.waste.length || window.version < 0 ? "#40ff40" : "transparent"
                    border.width: 5
                }
            }
        }

        // the cards
        Repeater {
            model: 52
            delegate: Card {
                id: cardItem
                required property int index
                cardId: index
                readonly property var at: window.layout.cards[index] || { x: 0, y: 0, z: 0 }
                readonly property bool dragged: dragger.cards.indexOf(index) >= 0
                faceUp: window.version >= 0 && window.g.up[index] === true
                backDesign: window.backDesign
                x: at.x + (dragged ? dragger.dx : 0)
                y: at.y + (dragged ? dragger.dy : 0)
                z: dragged ? 1000 + dragger.cards.indexOf(index) : at.z
                Behavior on x { enabled: !cardItem.dragged; NumberAnimation { duration: 140; easing.type: Easing.OutQuad } }
                Behavior on y { enabled: !cardItem.dragged; NumberAnimation { duration: 140; easing.type: Easing.OutQuad } }
            }
        }

        MouseArea {
            id: dragger
            anchors.fill: parent
            z: 2000
            acceptedButtons: Qt.LeftButton | Qt.RightButton
            property var cards: []    // the cards being dragged
            property var from: null   // their pile
            property real dx: 0
            property real dy: 0
            property real startX: 0
            property real startY: 0
            property bool moved: false

            function inside(x, y, at) { return x >= at.x && y >= at.y && x < at.x + window.cardW && y < at.y + window.cardH; }
            // the pile and position of the top card under the point
            function hit(x, y) {
                var L = window.layout, best = null, bestZ = -1;
                var piles = [{ kind: "stock", i: 0 }, { kind: "waste", i: 0 }];
                for (var f = 0; f < 4; ++f) piles.push({ kind: "found", i: f });
                for (var t = 0; t < 7; ++t) piles.push({ kind: "tab", i: t });
                for (var p = 0; p < piles.length; ++p) {
                    var list = window.pileList(piles[p]);
                    for (var k = 0; k < list.length; ++k) {
                        var at = L.cards[list[k]];
                        if (inside(x, y, at) && at.z > bestZ) {
                            bestZ = at.z;
                            best = { pile: piles[p], index: k };
                        }
                    }
                    if (!list.length) {
                        var b = piles[p].kind === "stock" ? L.bases.stock : piles[p].kind === "waste" ? L.bases.waste
                              : piles[p].kind === "found" ? L.bases.found[piles[p].i] : L.bases.tab[piles[p].i];
                        if (inside(x, y, b) && bestZ < 0)
                            best = { pile: piles[p], index: -1 };
                    }
                }
                return best;
            }
            // where dropped cards go: the legal pile they overlap most
            function dropTarget() {
                var L = window.layout, first = L.cards[cards[0]];
                var x = first.x + dx, y = first.y + dy, best = null, bestArea = 0;
                function consider(p, rx, ry, rh) {
                    var w = Math.min(x + window.cardW, rx + window.cardW) - Math.max(x, rx);
                    var h = Math.min(y + window.cardH, ry + rh) - Math.max(y, ry);
                    if (w > 0 && h > 0 && w * h > bestArea && window.canPlace(cards, p)) {
                        bestArea = w * h;
                        best = p;
                    }
                }
                for (var f = 0; f < 4; ++f)
                    consider({ kind: "found", i: f }, L.bases.found[f].x, L.bases.found[f].y, window.cardH);
                for (var t = 0; t < 7; ++t) {
                    var col = window.g.tab[t], b = L.bases.tab[t];
                    var bottom = col.length ? L.cards[col[col.length - 1]].y + window.cardH : b.y + window.cardH;
                    consider({ kind: "tab", i: t }, b.x, b.y, bottom - b.y);
                }
                return best;
            }

            onPressed: (mouse) => {
                table.forceActiveFocus();
                window.log("press " + Math.round(mouse.x + table.x) + " " + Math.round(mouse.y + table.y));
                if (mouse.button !== Qt.LeftButton || window.won)
                    return;
                var h = hit(mouse.x, mouse.y);
                cards = [];
                from = null;
                moved = false;
                dx = dy = 0;
                startX = mouse.x;
                startY = mouse.y;
                if (!h || h.index < 0 || h.pile.kind === "stock")
                    return;
                var list = window.pileList(h.pile);
                if (!window.g.up[list[h.index]])
                    return;
                // the waste and foundations give only their top card
                if (h.pile.kind !== "tab" && h.index !== list.length - 1)
                    return;
                from = h.pile;
                cards = list.slice(h.index);
            }
            onPositionChanged: (mouse) => {
                if (!cards.length)
                    return;
                if (!moved && Math.abs(mouse.x - startX) + Math.abs(mouse.y - startY) < 4)
                    return;
                moved = true;
                dx = mouse.x - startX;
                dy = mouse.y - startY;
            }
            onReleased: (mouse) => {
                if (mouse.button === Qt.RightButton) {
                    window.sendHome();
                    return;
                }
                if (moved && cards.length) {
                    var to = dropTarget();
                    var f = from, n = cards.length;
                    cards = [];
                    dx = dy = 0;
                    if (to)
                        window.move(f, n, to);
                    else
                        window.log("refused");
                    return;
                }
                cards = [];
                // a click on the stock turns cards over
                var h = hit(mouse.x, mouse.y);
                if (h && h.pile.kind === "stock")
                    window.drawCards();
            }
            onDoubleClicked: (mouse) => {
                var h = hit(mouse.x, mouse.y);
                if (!h || h.index < 0 || h.pile.kind === "stock" || h.pile.kind === "found")
                    return;
                if (h.index === window.pileList(h.pile).length - 1)
                    window.toFoundation(h.pile);
            }
            onCanceled: { cards = []; dx = dy = 0; }
        }
    }

    // the score and time
    Item {
        id: statusBar
        anchors.bottom: parent.bottom
        width: parent.width
        height: 20
        Bevel {
            anchors.fill: parent
            anchors.margins: 1
            style: "thinSunken"
            Text {
                anchors.right: parent.right
                anchors.rightMargin: 8
                anchors.verticalCenter: parent.verticalCenter
                text: (window.keepScore ? "Score: " + window.score + "    " : "")
                      + (window.timed ? "Time: " + window.seconds : "")
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontSize
            }
        }
    }

    // ------------------------------------------------------------ dialogs
    Dialog95 {
        id: deckDialog
        title: "Select Card Back"
        dialogWidth: 4 * 86 + 30
        dialogHeight: 170
        property int choice: 0
        function open() { choice = window.backDesign; show(); }
        onClosed: table.forceActiveFocus()

        Row {
            x: 14
            y: 12
            spacing: 14
            Repeater {
                model: 4
                delegate: Item {
                    required property int index
                    width: 72
                    height: 96
                    Rectangle {
                        anchors.fill: parent
                        anchors.margins: -4
                        color: "transparent"
                        border.color: "#000080"
                        border.width: 2
                        visible: deckDialog.choice === index
                    }
                    Card {
                        cardId: 0
                        faceUp: false
                        backDesign: index
                    }
                    MouseArea {
                        anchors.fill: parent
                        onClicked: deckDialog.choice = index
                        onDoubleClicked: { window.backDesign = index; window.saveOptions(); deckDialog.close(); }
                    }
                }
            }
        }
        Row {
            anchors.right: parent.right
            anchors.rightMargin: 14
            y: 118
            spacing: 6
            Button95 {
                text: "OK"
                isDefault: true
                onClicked: { window.backDesign = deckDialog.choice; window.saveOptions(); deckDialog.close(); }
            }
            Button95 { text: "Cancel"; onClicked: deckDialog.close() }
        }
    }

    // a win: a moment to see the finished foundations, then a new deal is offered
    Item {
        id: winBox
        function start() {
            fall.restart();
        }
        Timer {
            id: fall
            interval: 900
            onTriggered: msgBox.show("Solitaire", "Congratulations, you won!"
                                     + (window.keepScore ? "\n\nScore: " + window.score : "")
                                     + "\n\nDeal again?", "question", ["Yes", "No"],
                                     function (i) { if (i === 0) window.deal(); table.forceActiveFocus(); })
        }
    }

    Menu95 {
        id: popup
        onClosed: if (!window.modalOpen) table.forceActiveFocus()
    }
    MessageBox95 { id: msgBox }
}
