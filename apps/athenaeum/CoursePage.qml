import QtQuick
import Win95Ui

// A course: its units and lessons (done, quiz scores, readings), each
// unit's test, the questions to review, and its research project.
Page95 {
    id: page
    required property var courseId
    property var course: ({})
    property var missed: []
    readonly property bool planning: Classroom.job === "plan" && Classroom.jobSubject === courseId
    readonly property bool tied: (course.projectId || 0) > 0
    title: Util.pageTitle(course.title || "Course")
    actions: [
        { text: course.completed > 0 ? "&Continue" : "&Start", enabled: course.status === "ready" && nextLesson !== null,
          action: function () { window.openLesson(nextLesson.id, false); } },
        { separator: true },
        { text: tied ? "Open the &Project" : "Keep in a &Project...", action: projectAction },
        { text: "Stop Keeping in the Project", enabled: tied, action: function () { Classroom.untieProject(page.courseId); } },
        { separator: true },
        { text: "Plan &Again...", enabled: !Classroom.busy, action: planAgain },
        { text: "&Delete Course...", action: removeCourse },
    ]
    function refresh() {
        page.course = Classroom.course(page.courseId);
        page.missed = Classroom.missed(page.courseId);
    }
    Component.onCompleted: refresh()
    onShown: refresh()
    Connections {
        target: Classroom
        function onCoursesChanged() { page.refresh(); }
        function onStateChanged() { if (!Classroom.busy) page.refresh(); }
    }
    readonly property var nextLesson: {
        var units = course.units || [];
        for (var u = 0; u < units.length; ++u)
            for (var l = 0; l < units[u].lessons.length; ++l)
                if (!units[u].lessons[l].completed)
                    return units[u].lessons[l];
        return null;
    }
    // units and their lessons, as rows of one list
    readonly property var rows: {
        if (planning)
            return [];
        var out = [];
        var units = course.units || [];
        for (var u = 0; u < units.length; ++u) {
            out.push({ unit: true, n: u + 1, item: units[u] });
            for (var l = 0; l < units[u].lessons.length; ++l)
                out.push({ unit: false, item: units[u].lessons[l], unitItem: units[u] });
        }
        return out;
    }
    function activate(r) {
        if (r.unit)
            window.openUnitTest(r.item.id, r.item.title, course.title);
        else
            window.openLesson(r.item.id, false);
    }
    function projectAction() {
        if (tied) {
            window.openProject(course.projectId);
            return;
        }
        var id = courseId, title = course.title;
        msgBox.show("Keep This Course in a Project",
                    "Each lesson's readings become the project's sources, its introduction a document to write your answers in, and what you highlight while reading is saved there too.\n\nKeep it in a new project, “" + title + "”, or one you choose?",
                    "question", ["New Project", "Choose...", "Cancel"], function (i) {
                        if (i === 0)
                            tiedTo(Classroom.tieProject(id, 0));
                        else if (i === 1)
                            window.pickProject("Keep This Course in a Project", "", false, [], function (projectId) { tiedTo(Classroom.tieProject(id, projectId)); });
                    });
    }
    function tiedTo(projectId) {
        if (projectId > 0)
            window.notify("This course is now kept in a project", false, "Open", function () { window.openProject(projectId); });
    }
    function planAgain() {
        if (course.status !== "ready") {
            Classroom.planAgain(courseId);
            return;
        }
        window.confirm("Plan Again", "Plan this course again?\n\nThe teacher writes a new syllabus with new readings. Lessons you marked done and their introductions are not kept.",
                       function () { Classroom.planAgain(page.courseId); });
    }
    function removeCourse() {
        window.confirm("Delete Course", "Delete “" + Util.plain(course.title || "") + "”?\n\nIts syllabus, introductions and progress are deleted. Your libraries and notes are not touched.",
                       function () { Classroom.removeCourse(page.courseId); window.show("classroom"); });
    }

    // ---- the course
    AthenaeumIcon { x: 4; y: 4; size: 32; name: "school" }
    Heading95 { id: head; x: 46; y: 2; width: parent.width - 300; level: 1; text: page.course.title || "" }
    Label95 {
        id: sub
        x: 46
        y: head.y + head.height + 2
        width: parent.width - 300
        dim: true
        elide: Text.ElideRight
        text: (page.course.subjectName || "") + " · " + (page.course.levelLabel || "")
              + (page.course.status === "ready" ? " · " + page.course.completed + " of " + page.course.lessons + (page.course.lessons === 1 ? " lesson done" : " lessons done") : "")
              + ((page.course.focus || "") !== "" ? " · " + page.course.focus : "")
    }
    Row {
        anchors.right: parent.right
        y: 6
        spacing: 6
        Button95 {
            width: 90
            visible: page.course.status === "ready" && page.nextLesson !== null
            isDefault: true
            text: page.course.completed > 0 ? "Continue" : "Start"
            onClicked: window.openLesson(page.nextLesson.id, false)
        }
        Button95 { width: 130; text: page.tied ? "Open Project" : "Keep in a Project..."; onClicked: page.projectAction() }
    }

    Column {
        id: banners
        y: 46
        width: parent.width
        spacing: 4
        // being planned
        Bevel {
            visible: page.planning
            width: parent.width
            height: visible ? 74 : 0
            style: "sunken"
            face: Theme.window
            Label95 { x: 8; y: 6; bold: true; text: "Your teacher is planning this course" }
            ProgressBar95 { x: 8; y: 24; width: parent.width - 16; height: 18; value: Classroom.total > 1 ? Math.round(100 * Classroom.step / Classroom.total) : -1 }
            Label95 {
                x: 8
                y: 46
                width: parent.width - 16
                small: true
                wrapMode: Text.Wrap
                text: (Classroom.label ? Classroom.label + ". " : "") + "It writes the syllabus, then finds each lesson's readings in your libraries. A lesson nothing in them is about is left out. This takes a minute or so; you can leave this page meanwhile."
            }
        }
        Notice {
            width: parent.width
            kind: page.course.status === "failed" ? "warning" : "info"
            text: page.planning ? "" : page.course.status === "failed" ? Util.plain(page.course.error) : page.course.status === "new" ? "This course has not been planned yet." : ""
            buttons: [{ text: "Plan Again", action: function () { if (!Classroom.busy) Classroom.planAgain(page.courseId); } }]
        }
    }

    // ---- units and lessons
    ListWell {
        id: well
        y: banners.y + banners.height + 4
        width: parent.width
        height: parent.height - y - (missedBox.visible ? missedBox.height + 6 : 0) - 30
        model: page.rows
        emptyText: page.planning ? "" : page.course.status === "ready" ? "This course has no lessons." : ""
        columns: [
            { title: "Lesson", flex: true, min: 200,
              icon: function (r) { return r.unit ? "folder" : r.item.completed ? "ok" : "document"; },
              text: function (r) { return r.unit ? "Unit " + r.n + ": " + r.item.title : "     " + r.item.title; } },
            { title: "Score", w: 90, right: true, text: function (r) {
                return r.unit ? (r.item.testScore >= 0 ? "Test " + r.item.testScore + "%" : "")
                              : (r.item.quizScore >= 0 ? "Quiz " + r.item.quizScore + "%" : ""); } },
            { title: "Readings", w: 70, right: true, text: function (r) { return r.unit ? "" : r.item.readings; } },
        ]
        rowBold: function (r) { return r.unit; }
        rowColor: function (r) {
            var s = r.unit ? r.item.testScore : r.item.quizScore;
            return s >= 0 && s < 70 ? "#806000" : r.unit ? "#000080" : Theme.text;
        }
        onActivated: (i, r) => page.activate(r)
        onMenuRequested: (i, r, x, y) => {
            var p = mapToItem(popup, x, y);
            popup.popup(p.x, p.y, r.unit ? [
                { text: r.item.testScore >= 0 ? "&Unit Test" : "Take the &Unit Test", action: function () { page.activate(r); } },
            ] : [
                { text: "&Open Lesson", action: function () { page.activate(r); } },
                { text: r.item.completed ? "Mark as &Not Done" : "Mark as &Done", action: function () { Classroom.setCompleted(r.item.id, !r.item.completed); } },
            ]);
        }
    }

    // ---- to review: questions answered wrong
    Bevel {
        id: missedBox
        visible: page.missed.length > 0
        y: well.y + well.height + 6
        width: parent.width
        height: 110
        style: "raised"
        Label95 { x: 8; y: 4; bold: true; text: "To review: " + page.missed.length + (page.missed.length === 1 ? " question" : " questions") }
        ListWell {
            id: missedWell
            x: 6
            y: 22
            width: parent.width - 12
            height: parent.height - 28 - 26
            showHeader: false
            model: page.missed
            columns: [
                { title: "", flex: true, icon: function () { return "warning"; }, text: function (m) { return m.text; } },
                { title: "", w: 220, text: function (m) { return "The answer: " + m.options[m.answer]; } },
            ]
            onActivated: (i, m) => window.openReading(m.libraryId, m.path, m.lessonId)
        }
        Row {
            x: 6
            anchors.bottom: parent.bottom
            anchors.bottomMargin: 4
            spacing: 6
            readonly property var m: missedWell.currentItem
            Label95 {
                width: missedBox.width - 280
                height: 21
                verticalAlignment: Text.AlignVCenter
                elide: Text.ElideRight
                text: parent.m ? (parent.m.chosen >= 0 ? "You answered: " + parent.m.options[parent.m.chosen] + " · " : "") + parent.m.lessonTitle : "Choose a question to read about it or ask your teacher."
            }
            Button95 { width: 80; height: 21; text: "Read It"; enabled: !!parent.m; onClicked: window.openReading(parent.m.libraryId, parent.m.path, parent.m.lessonId) }
            Button95 {
                width: 130
                height: 21
                text: "Ask Your Teacher"
                enabled: !!parent.m
                onClicked: {
                    var m = parent.m;
                    window.askTeacher(m.lessonId, "I got this question wrong: “" + m.text + "” I answered “"
                                      + (m.chosen >= 0 ? m.options[m.chosen] : "nothing") + "”. Can you help me understand?");
                }
            }
        }
    }

    Label95 {
        anchors.bottom: parent.bottom
        height: 23
        verticalAlignment: Text.AlignVCenter
        dim: true
        text: "Double-click a lesson to open it, or a unit to take its test."
    }
    Row {
        anchors.bottom: parent.bottom
        anchors.right: parent.right
        spacing: 6
        Button95 { width: 90; text: "Open"; enabled: !!well.currentItem; onClicked: page.activate(well.currentItem) }
        Button95 { width: 100; text: "Plan Again..."; enabled: !Classroom.busy; onClicked: page.planAgain() }
    }
}
