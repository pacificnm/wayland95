import QtQuick
import Win95Ui

// A lesson: the teacher's introduction (written from the readings, as it
// streams in), the readings to open, its quiz, and "Ask Your Teacher", a
// tutor that answers from the readings and guides rather than tells.
Page95 {
    id: page
    property int startTab: 0      // 1: the quiz, 2: the teacher
    property string askText: ""   // a question to put to the teacher (from a missed quiz question)
    required property var lessonId
    property var lesson: ({})
    readonly property bool writing: Classroom.job === "intro" && Classroom.jobSubject === lessonId
    readonly property bool tied: (lesson.projectId || 0) > 0
    title: Util.pageTitle(lesson.title || "Lesson")
    actions: [
        { text: "&Previous Lesson", enabled: (lesson.previousId || 0) > 0, action: function () { window.openLesson(lesson.previousId, true); } },
        { text: "&Next Lesson", enabled: (lesson.nextId || 0) > 0, action: function () { window.openLesson(lesson.nextId, true); } },
        { separator: true },
        { text: lesson.completed ? "Mark as Not &Done" : "I've &Finished This Lesson", action: function () { Classroom.setCompleted(page.lessonId, !lesson.completed); } },
        { text: "Write the &Introduction Again", enabled: !Classroom.busy && (lesson.readings || []).length > 0, action: function () { Classroom.writeIntro(page.lessonId); } },
        { text: "New &Chat with the Teacher", enabled: conversation.chatId > 0 && !conversation.busy, action: newChat },
        { separator: true },
        { text: tied ? "Open the &Project" : "Keep the Course in a &Project...", action: projectAction },
    ]
    function askTeacher(question) {
        tabs.currentIndex = 2;
        teacher.setQuestion(question);
    }
    function newChat() {
        conversation.chatId = 0;
        conversation.lessonId = page.lessonId;
        tabs.currentIndex = 2;
        teacher.focusInput();
    }
    function projectAction() {
        if (tied) {
            window.openProject(lesson.projectId);
            return;
        }
        var id = lesson.courseId;
        window.pickProject("Keep This Course in a Project", "", false, [], function (projectId) {
            var p = Classroom.tieProject(id, projectId);
            if (p > 0)
                window.notify("This course is now kept in a project", false, "Open", function () { window.openProject(p); });
        });
    }
    function refresh() { page.lesson = Classroom.lesson(page.lessonId); }
    Component.onCompleted: {
        refresh();
        tabs.currentIndex = startTab;
        if (askText !== "")
            Qt.callLater(function () { page.askTeacher(askText); });
        if ((lesson.intro || "") === "" && (lesson.readings || []).length > 0 && !Classroom.busy)
            Classroom.writeIntro(page.lessonId);
    }
    Connections {
        target: Classroom
        function onIntroReady(lessonId) { if (lessonId === page.lessonId) page.refresh(); }
        function onCoursesChanged() { page.refresh(); }
    }

    // the teacher: the latest chat about this lesson, or a new one
    Conversation {
        id: conversation
        lessonId: page.lessonId
        Component.onCompleted: {
            var chat = Classroom.latestChat(page.lessonId);
            if (chat > 0)
                chatId = chat;
        }
    }

    Label95 { id: where; width: parent.width; dim: true; elide: Text.ElideRight
              text: (page.lesson.courseTitle || "") + " · " + (page.lesson.unitTitle || "") + " · Lesson " + (page.lesson.number || 0) + " of " + (page.lesson.count || 0) }
    Heading95 { id: head; y: where.height; width: parent.width - 200; level: 1; text: page.lesson.title || "" }
    Row {
        anchors.right: parent.right
        y: where.height
        spacing: 4
        Button95 { width: 90; text: "< Previous"; enabled: (page.lesson.previousId || 0) > 0; onClicked: window.openLesson(page.lesson.previousId, true) }
        Button95 { width: 90; text: "Next >"; enabled: (page.lesson.nextId || 0) > 0; onClicked: window.openLesson(page.lesson.nextId, true) }
    }
    Label95 { id: objective; y: head.y + head.height + 2; width: parent.width; wrapMode: Text.Wrap; font.italic: true; text: page.lesson.objective || "" }

    TabWidget95 {
        id: tabs
        y: objective.y + objective.height + 6
        width: parent.width
        height: parent.height - y
        titles: ["Lesson", "Quiz", "Ask Your Teacher"]

        // ---- the introduction and the readings
        Item {
            TextView95 {
                id: intro
                width: parent.width - readings.width - 8
                height: parent.height - 30
                textFormat: Text.MarkdownText
                text: page.writing ? (Classroom.streamed !== "" ? Util.markdown(Classroom.streamed.replace(/^\s*# [^\n]*\n?/, ""))
                                                                : "*" + (Classroom.label !== "" ? Classroom.label + "..." : "Getting ready...") + "*")
                    : (page.lesson.intro || "") !== "" ? Util.markdown(page.lesson.intro)
                    : Classroom.busy ? "*Your teacher is busy with another course. The introduction can be written once it has finished.*"
                    : "*No introduction yet.*"
            }
            Row {
                anchors.bottom: parent.bottom
                spacing: 6
                Button95 { width: 70; visible: page.writing; text: "Stop"; onClicked: Classroom.stop() }
                Button95 {
                    width: 80
                    visible: !page.writing && (page.lesson.intro || "") === ""
                    text: "Write It"
                    enabled: !Classroom.busy && (page.lesson.readings || []).length > 0
                    onClicked: Classroom.writeIntro(page.lessonId)
                }
                Label95 {
                    width: intro.width - 100
                    height: 23
                    verticalAlignment: Text.AlignVCenter
                    visible: (page.lesson.intro || "") !== "" && !page.writing
                    small: true
                    dim: true
                    elide: Text.ElideRight
                    text: (page.lesson.readings || []).length > 1 ? "[1], [2]: the reading a sentence comes from. [?]: a figure the readings do not have; check it."
                                                                 : "[?]: a figure the reading does not have; check it."
                }
            }
            // what to read
            Item {
                id: readings
                anchors.right: parent.right
                width: Math.min(260, parent.width * 0.38)
                height: parent.height
                Label95 { id: readLabel; bold: true; text: "Read" }
                ListWell {
                    id: readWell
                    y: 18
                    width: parent.width
                    height: parent.height - y - readTip.height - projectBox.height - 40
                    model: page.lesson.readings || []
                    columns: [{ title: "", flex: true, icon: function (r) { return r.available ? "article" : "warning"; }, text: function (r) { return r.n + ". " + r.title; } }]
                    showHeader: false
                    rowColor: function (r) { return r.available ? Theme.text : Theme.shadow; }
                    onActivated: (i, r) => { if (r.available) window.openReading(r.libraryId, r.path, page.lessonId); }
                }
                Label95 {
                    id: readTip
                    y: readWell.y + readWell.height + 2
                    width: parent.width
                    wrapMode: Text.Wrap
                    small: true
                    dim: true
                    text: readWell.currentItem && !readWell.currentItem.available ? "Its library is no longer installed."
                        : "Double-click to read. While you read: highlight what matters, right-click a word to look it up, and use Ask beside the article if something is unclear."
                }
                Column {
                    id: projectBox
                    y: readTip.y + readTip.height + 6
                    width: parent.width
                    spacing: 4
                    Label95 { width: parent.width; elide: Text.ElideRight; text: page.tied ? "Kept in the project “" + page.lesson.projectTitle + "”" : "Not kept in a project" }
                    Button95 { width: parent.width; text: page.tied ? "Open Project" : "Keep the Course in a Project..."; onClicked: page.projectAction() }
                }
                Button95 {
                    anchors.bottom: parent.bottom
                    width: parent.width
                    isDefault: !page.lesson.completed
                    text: page.lesson.completed ? "Mark as Not Done" : "I've Finished This Lesson"
                    onClicked: Classroom.setCompleted(page.lessonId, !page.lesson.completed)
                }
            }
        }

        // ---- the quiz
        Item {
            QuizView {
                anchors.fill: parent
                kind: "lesson"
                targetId: page.lessonId
                onOpenReading: (libraryId, path) => window.openReading(libraryId, path, page.lessonId)
                onAskTeacher: (lessonId, question) => page.askTeacher(question)
            }
        }

        // ---- the teacher
        Item {
            Label95 {
                id: tutorTip
                visible: conversation.chatId === 0
                width: parent.width
                height: visible ? implicitHeight + 6 : 0
                wrapMode: Text.Wrap
                text: "Ask about anything in this lesson, or try your answer to one of its questions. Your teacher answers from the lesson's readings and will help you work things out rather than just giving answers."
            }
            ChatView {
                id: teacher
                y: tutorTip.height
                width: parent.width
                height: parent.height - y
                conversation: conversation
                onOpenArticle: (libraryId, path, fragment) => window.push(articlePage, { url: LibraryBrowser.articleUrl(libraryId, path), fragment: fragment, lessonId: page.lessonId })
            }
        }
    }
}
