import QtQuick
import Win95Ui

// A lesson's quiz or a unit's test: written by the teacher from the
// readings (every answer checked against its passage), taken, and graded
// with the right answer and where it is in the reading.
Item {
    id: view
    required property string kind  // lesson | unit
    required property var targetId
    property var quiz: ({})
    property var chosen: []         // the option picked for each question, -1 none
    property bool answering: false  // taking it (again), rather than looking at the result
    readonly property bool writing: Classroom.job === "quiz" && Classroom.quizKind === kind && Classroom.jobSubject === targetId
    readonly property var result: quiz.result || null
    signal openReading(var libraryId, string path)
    signal askTeacher(var lessonId, string question)

    function refresh() {
        view.quiz = Classroom.quiz(view.kind, view.targetId);
        view.answering = view.quiz.id !== undefined && !view.quiz.result;
        view.chosen = (view.quiz.questions || []).map(function () { return -1; });
    }
    Component.onCompleted: refresh()
    Connections {
        target: Classroom
        function onQuizReady(kind, targetId) {
            if (kind === view.kind && targetId === view.targetId)
                view.refresh();
        }
    }
    function letter(i) { return String.fromCharCode(65 + i); }
    function submit() {
        var r = Classroom.submitQuiz(view.quiz.id, view.chosen);
        var q = Object.assign({}, view.quiz);
        q.result = r;
        view.quiz = q;
        view.answering = false;
        flick.contentY = 0;
    }

    // ---- no quiz yet, or being written
    Column {
        visible: view.quiz.id === undefined || view.writing
        anchors.centerIn: parent
        width: Math.min(parent.width - 40, 460)
        spacing: 8
        Heading95 {
            width: parent.width
            horizontalAlignment: Text.AlignHCenter
            level: 3
            text: view.writing ? "Your teacher is writing the questions..." : view.kind === "unit" ? "Test yourself on this unit" : "Check what you learned"
        }
        Label95 {
            width: parent.width
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.Wrap
            text: view.writing ? Classroom.label
                : view.kind === "unit" ? "Questions on each of the unit's lessons, written from their readings. Every answer is checked against the reading it comes from. It takes a minute or two to write."
                : "Five questions on this lesson's readings. Every answer is checked against the reading it comes from. Pass with 70% or more and the lesson is marked finished. It takes about a minute to write."
        }
        ProgressBar95 {
            visible: view.writing
            width: parent.width
            height: 18
            value: Classroom.total > 1 ? Math.round(100 * Classroom.step / Classroom.total) : -1
        }
        Button95 {
            anchors.horizontalCenter: parent.horizontalCenter
            width: 130
            text: view.writing ? "Stop" : view.kind === "unit" ? "Write the Test" : "Write the Quiz"
            enabled: view.writing || !Classroom.busy
            isDefault: true
            onClicked: view.writing ? Classroom.stop() : Classroom.makeQuiz(view.kind, view.targetId)
        }
        Label95 {
            visible: Classroom.busy && !view.writing
            width: parent.width
            horizontalAlignment: Text.AlignHCenter
            dim: true
            text: "The teacher is busy with something else. Try again in a moment."
        }
    }

    // ---- the questions
    Bevel {
        visible: view.quiz.id !== undefined && !view.writing
        width: parent.width
        height: parent.height - 30
        style: "sunken"
        face: Theme.window
        Flickable {
            id: flick
            x: 2
            y: 2
            width: parent.width - 4 - (bar.visible ? bar.width : 0)
            height: parent.height - 4
            clip: true
            interactive: false
            contentHeight: col.implicitHeight + 16
            Column {
                id: col
                x: 12
                y: 8
                width: parent.width - 24
                spacing: 12
                // the score
                Rectangle {
                    visible: !!view.result && !view.answering
                    width: parent.width
                    height: visible ? 50 : 0
                    color: view.result && view.result.passed ? "#e0ffe0" : "#fff4d0"
                    border.color: Theme.shadow
                    Text {
                        id: pct
                        x: 10
                        anchors.verticalCenter: parent.verticalCenter
                        text: view.result ? view.result.percent + "%" : ""
                        font.family: Theme.fontFamily
                        font.pixelSize: 24
                        font.bold: true
                        color: "#000000"
                    }
                    Column {
                        x: pct.x + pct.implicitWidth + 14
                        anchors.verticalCenter: parent.verticalCenter
                        Label95 {
                            bold: true
                            color: "#000000"
                            text: !view.result ? "" : view.result.passed ? "Well done: " + view.result.score + " of " + view.result.total + " right"
                                                                         : view.result.score + " of " + view.result.total + " right: look again at what you missed"
                        }
                        Label95 {
                            color: "#000000"
                            text: view.kind === "lesson" && view.result && view.result.passed ? "The lesson is marked finished." : "Each answer below shows the passage it comes from."
                        }
                    }
                }
                Repeater {
                    model: view.writing ? [] : view.answering ? (view.quiz.questions || []) : (view.result ? view.result.questions : [])
                    Column {
                        id: card
                        required property var modelData
                        required property int index
                        readonly property bool graded: !view.answering
                        width: col.width
                        spacing: 4
                        Row {
                            spacing: 6
                            width: parent.width
                            Text {
                                text: card.graded ? (card.modelData.correct ? "✔" : "✘") : (card.index + 1) + "."
                                color: card.graded ? (card.modelData.correct ? "#008000" : "#c00000") : Theme.text
                                font.family: Theme.fontFamily
                                font.pixelSize: Theme.fontSize + 1
                                font.bold: true
                            }
                            Label95 {
                                width: parent.width - 24
                                wrapMode: Text.Wrap
                                bold: true
                                font.pixelSize: Theme.fontSize + 1
                                text: card.modelData.text
                            }
                        }
                        Label95 {
                            x: 22
                            visible: card.graded && view.kind === "unit" && (card.modelData.lessonTitle || "") !== ""
                            dim: true
                            small: true
                            text: card.modelData.lessonTitle || ""
                        }
                        Repeater {
                            model: card.modelData.options
                            Item {
                                id: option
                                required property string modelData
                                required property int index
                                readonly property bool isAnswer: card.graded && index === card.modelData.answer
                                readonly property bool wasChosen: card.graded && index === card.modelData.chosen
                                x: 22
                                width: card.width - 22
                                height: Math.max(20, optText.implicitHeight + 4)
                                Rectangle {
                                    anchors.fill: parent
                                    visible: option.isAnswer || (option.wasChosen && !option.isAnswer)
                                    color: option.isAnswer ? "#d8f8d8" : "#ffd8d8"
                                }
                                RadioButton95 {
                                    id: radio
                                    y: 2
                                    enabled: !card.graded
                                    text: ""
                                    checked: card.graded ? option.wasChosen : view.chosen[card.index] === option.index
                                    onClicked: optMouse.clicked(null)
                                }
                                Text {
                                    id: optText
                                    x: 22
                                    y: 2
                                    width: parent.width - 26
                                    wrapMode: Text.Wrap
                                    textFormat: Text.PlainText
                                    text: view.letter(option.index) + ")  " + option.modelData
                                    color: Theme.text
                                    font.family: Theme.fontFamily
                                    font.pixelSize: Theme.fontSize
                                    font.bold: option.isAnswer
                                }
                                MouseArea {
                                    id: optMouse
                                    anchors.fill: parent
                                    enabled: !card.graded
                                    onClicked: {
                                        var c = view.chosen.slice();
                                        c[card.index] = option.index;
                                        view.chosen = c;
                                    }
                                }
                            }
                        }
                        Label95 {
                            x: 22
                            width: card.width - 22
                            visible: card.graded && (card.modelData.explanation || "") !== ""
                            wrapMode: Text.Wrap
                            text: card.modelData.explanation || ""
                        }
                        Label95 {
                            x: 22
                            width: card.width - 22
                            visible: card.graded && (card.modelData.evidence || "") !== ""
                            wrapMode: Text.Wrap
                            dim: true
                            font.italic: true
                            text: "From “" + (card.modelData.title || "") + "”: " + (card.modelData.evidence || "")
                        }
                        Row {
                            x: 22
                            visible: card.graded
                            spacing: 6
                            Button95 { width: 80; height: 21; text: "Read It"; onClicked: view.openReading(card.modelData.libraryId, card.modelData.path) }
                            Button95 {
                                visible: !card.modelData.correct
                                width: 130
                                height: 21
                                text: "Ask Your Teacher"
                                onClicked: view.askTeacher(card.modelData.lessonId, "I got this question wrong: “" + card.modelData.text + "” I answered “"
                                                           + (card.modelData.chosen >= 0 ? card.modelData.options[card.modelData.chosen] : "nothing") + "”. Can you help me understand?")
                            }
                        }
                    }
                }
            }
            WheelHandler {
                onWheel: (event) => {
                    flick.contentY = Math.max(0, Math.min(flick.contentHeight - flick.height, flick.contentY - event.angleDelta.y / 120 * 60));
                }
            }
        }
        ScrollBar95 {
            id: bar
            anchors.right: parent.right
            anchors.rightMargin: 2
            y: 2
            height: parent.height - 4
            flickable: flick
            stepSize: 20
        }
    }
    Row {
        visible: view.quiz.id !== undefined && !view.writing
        anchors.bottom: parent.bottom
        anchors.right: parent.right
        spacing: 6
        Label95 {
            visible: view.answering && view.chosen.some(function (c) { return c < 0; })
            height: 23
            verticalAlignment: Text.AlignVCenter
            dim: true
            text: { var n = view.chosen.filter(function (c) { return c < 0; }).length; return n + (n === 1 ? " question left" : " questions left"); }
        }
        Button95 {
            visible: view.answering
            width: 130
            text: "Check My Answers"
            isDefault: true
            enabled: view.chosen.length > 0 && view.chosen.every(function (c) { return c >= 0; })
            onClicked: view.submit()
        }
        Button95 {
            visible: !view.answering
            width: 90
            text: "Try Again"
            onClicked: {
                view.chosen = (view.quiz.questions || []).map(function () { return -1; });
                view.answering = true;
                flick.contentY = 0;
            }
        }
        Button95 { visible: !view.answering; width: 110; text: "New Questions"; enabled: !Classroom.busy; onClicked: Classroom.makeQuiz(view.kind, view.targetId) }
    }
}
