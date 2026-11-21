import QtQuick
import Win95Ui

// A unit's test: questions on each of its lessons, from their readings.
Page95 {
    id: page
    required property var unitId
    property string unitTitle: ""
    property string courseTitle: ""
    title: "Test: " + Util.pageTitle(unitTitle)

    Label95 { id: course; dim: true; text: page.courseTitle }
    Heading95 { id: head; y: course.height; width: parent.width; level: 1; text: "Unit test: " + page.unitTitle }
    QuizView {
        y: head.y + head.height + 6
        width: parent.width
        height: parent.height - y
        kind: "unit"
        targetId: page.unitId
        onOpenReading: (libraryId, path) => window.openArticle(libraryId, path)
        onAskTeacher: (lessonId, question) => window.askTeacher(lessonId, question)
    }
}
