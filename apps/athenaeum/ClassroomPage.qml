import QtQuick
import Win95Ui

// The Classroom: an AI teacher plans a course in a subject at a level, with
// units, lessons and readings from the installed libraries. The learner's
// profile, their courses, and the subjects to start one in.
Page95 {
    id: page
    title: "Classroom"
    actions: [
        { text: Classroom.hasProfile ? "Your &Profile..." : "Set Up Your &Profile...", action: function () { profileDialog.open(); } },
        { text: "&New Course...", enabled: Classroom.hasProfile, action: function () { newCourse.open("other"); } },
        { separator: true },
        { text: "&Open Course", enabled: !!courseWell.currentItem, action: function () { window.openCourse(courseWell.currentItem.id); } },
        { text: "&Delete Course...", enabled: !!courseWell.currentItem, action: function () { removeCourse(courseWell.currentItem); } },
    ]
    function removeCourse(c) {
        var id = c.id;
        window.confirm("Delete Course", "Delete “" + Util.plain(c.title) + "” ? Its syllabus, introductions and progress are deleted. Your libraries and notes are not touched.",
                       function () { Classroom.removeCourse(id); });
    }
    function courseState(c) {
        var planning = Classroom.job === "plan" && Classroom.jobSubject === c.id;
        if (planning) return "The teacher is planning it...";
        if (c.status === "failed") return "Not planned: open it to try again";
        if (c.status === "ready")
            return c.completed >= c.lessons ? (c.lessons === 1 ? "The lesson is done" : "All " + c.lessons + " lessons done")
                                            : c.completed + " of " + c.lessons + (c.lessons === 1 ? " lesson done" : " lessons done");
        return "Waiting to be planned";
    }

    // ---- who is learning
    Bevel {
        id: welcome
        width: parent.width
        height: 64
        style: "raised"
        AthenaeumIcon { x: 12; y: 16; size: 32; name: "school" }
        Heading95 {
            x: 56
            y: 10
            width: parent.width - 220
            level: 2
            text: Classroom.hasProfile ? "Hi, " + (Classroom.profile.name || "").split(" ")[0] : "A teacher for whatever you want to learn"
        }
        Label95 {
            x: 56
            y: 32
            width: parent.width - 220
            wrapMode: Text.Wrap
            maximumLineCount: 2
            elide: Text.ElideRight
            text: Classroom.hasProfile ? (Classroom.profile.grade !== "none" ? "Completed: " + Classroom.profile.gradeLabel : "")
                : "Choose a subject and a level, and the AI teacher plans a course for you: units, lessons, and readings from your own libraries. Start by telling it a little about yourself."
        }
        Button95 {
            anchors.right: parent.right
            anchors.rightMargin: 12
            anchors.verticalCenter: parent.verticalCenter
            width: 140
            text: Classroom.hasProfile ? "Your Profile..." : "Set Up Your Profile..."
            isDefault: !Classroom.hasProfile
            onClicked: profileDialog.open()
        }
    }

    // ---- the learner's courses
    Label95 { id: coursesLabel; y: welcome.height + 6; bold: true; text: "Your courses" }
    ListWell {
        id: courseWell
        y: coursesLabel.y + 18
        width: parent.width
        height: Math.round((parent.height - y - 60) * 0.45)
        model: Classroom.courses
        emptyText: Classroom.hasProfile ? "No courses yet. Choose a subject below to start one." : ""
        columns: [
            { title: "Course", flex: true, min: 160, icon: function () { return "school"; }, text: function (c) { return c.title; } },
            { title: "Subject", w: 140, text: function (c) { return c.subjectName; } },
            { title: "Level", w: 120, text: function (c) { return c.levelLabel; } },
            { title: "Progress", w: 190, text: function (c) { return page.courseState(c); } },
        ]
        rowColor: function (c) { return c.status === "failed" ? "#c00000" : Theme.text; }
        onActivated: (i, c) => window.openCourse(c.id)
        onMenuRequested: (i, c, x, y) => {
            var p = mapToItem(popup, x, y);
            popup.popup(p.x, p.y, [
                { text: "&Open", action: function () { window.openCourse(c.id); } },
                { separator: true },
                { text: "&Delete...", action: function () { page.removeCourse(c); } },
            ]);
        }
    }

    // ---- the subjects to start a course in
    Label95 { id: startLabel; y: courseWell.y + courseWell.height + 8; bold: true; text: "Start a course" }
    ListWell {
        id: subjectWell
        y: startLabel.y + 18
        width: parent.width
        height: parent.height - y - footLabel.height - 6
        model: Classroom.subjects.concat([{ key: "other", name: "Your Own Course...", blurb: "Anything you want to learn about" }])
        columns: [
            { title: "Subject", w: 200, icon: function (s) { return s.key === "other" ? "add" : "book"; }, text: function (s) { return s.name; } },
            { title: "What it covers", flex: true, text: function (s) { return s.blurb; } },
        ]
        onActivated: (i, s) => page.start(s.key)
    }
    function start(key) {
        if (!Classroom.hasProfile) {
            profileDialog.thenSubject = key;
            profileDialog.open();
            return;
        }
        newCourse.open(key);
    }
    Label95 {
        id: footLabel
        anchors.bottom: parent.bottom
        width: parent.width - 130
        height: 30
        wrapMode: Text.Wrap
        small: true
        dim: true
        text: "The teacher is a small AI model running on this computer. Its lessons come from your libraries, and it can be wrong: read the articles it gives you, and check what matters."
    }
    Button95 {
        anchors.bottom: parent.bottom
        anchors.right: parent.right
        width: 120
        text: "Start Course..."
        enabled: !!subjectWell.currentItem
        onClicked: page.start(subjectWell.currentItem.key)
    }

    // ---- the profile
    Dialog95 {
        id: profileDialog
        property string thenSubject: ""
        title: Classroom.hasProfile ? "Your Profile" : "Meet Your Teacher"
        dialogWidth: 440
        dialogHeight: 300
        property int grade: 0
        function open() {
            var p = Classroom.profile;
            nameField.text = p.name || "";
            grade = Math.max(0, Classroom.grades.findIndex(function (g) { return g.key === (p.grade || ""); }));
            interests.text = p.interests || "";
            goals.text = p.goals || "";
            show();
            nameField.input.forceActiveFocus();
        }
        function accept() {
            if (nameField.text.trim() === "")
                return;
            Classroom.saveProfile({ name: nameField.text, grade: Classroom.grades[grade].key, interests: interests.text, goals: goals.text });
            close();
            if (thenSubject !== "") {
                var s = thenSubject;
                thenSubject = "";
                newCourse.open(s);
            }
        }
        Keys.onEscapePressed: { thenSubject = ""; close(); }
        Label95 {
            x: 12
            y: 10
            width: parent.width - 24
            wrapMode: Text.Wrap
            text: "Your teacher uses this to pitch lessons at the right level and to choose examples you will like. It stays on this computer."
        }
        Column {
            x: 12
            y: 46
            width: parent.width - 24
            spacing: 4
            Label95 { text: "Your name:" }
            TextField95 { id: nameField; width: parent.width; onAccepted: profileDialog.accept() }
            Label95 { text: "Highest grade completed:" }
            ComboBox95 {
                width: parent.width
                popupMenu: popup
                model: Classroom.grades.map(function (g) { return g.label; })
                currentIndex: profileDialog.grade
                onActivated: (i) => profileDialog.grade = i
            }
            Label95 { text: "What you enjoy:" }
            TextField95 { id: interests; width: parent.width }
            Label95 { text: "What you want to learn for:" }
            TextField95 { id: goals; width: parent.width }
        }
        Row {
            anchors.right: parent.right
            anchors.rightMargin: 12
            anchors.bottom: parent.bottom
            anchors.bottomMargin: 10
            spacing: 6
            Button95 { text: "Save"; isDefault: true; enabled: nameField.text.trim() !== ""; onClicked: profileDialog.accept() }
            Button95 { text: "Cancel"; onClicked: { profileDialog.thenSubject = ""; profileDialog.close(); } }
        }
    }

    // ---- a new course: a core subject or one of the learner's own, at a level, of a length
    Dialog95 {
        id: newCourse
        property string subject: "other"
        readonly property bool own: subject === "other"
        property int level: 0
        property int ownSubject: 0
        property int length: 1
        readonly property var levelKey: Classroom.levels.length ? Classroom.levels[Math.min(level, Classroom.levels.length - 1)].key : "adult"
        readonly property var ownSubjects: Classroom.subjects.concat([{ key: "other", name: "Other" }])
        readonly property var coverage: visible ? Classroom.coverage(own ? ownSubjects[ownSubject].key : subject, levelKey) : ({})
        title: own ? "Your Own Course" : "New " + Classroom.subject(subject).name + " Course"
        dialogWidth: 460
        dialogHeight: own ? 380 : 350
        function open(key) {
            subject = key;
            titleField.text = own ? "" : Classroom.subject(key).name;
            level = Math.max(0, Classroom.levels.findIndex(function (l) { return l.key === Classroom.defaultLevel(); }));
            ownSubject = Classroom.subjects.length; // "Other"
            length = 1;
            focusArea.text = "";
            show();
            titleField.input.forceActiveFocus();
        }
        function accept() {
            if (titleField.text.trim() === "" || !coverage.ok)
                return;
            var id = Classroom.createCourse(own ? ownSubjects[ownSubject].key : subject, titleField.text, levelKey, focusArea.text,
                                            ["short", "standard", "long"][length]);
            close();
            if (id > 0)
                window.openCourse(id);
        }
        Keys.onEscapePressed: close()
        Column {
            x: 12
            y: 10
            width: parent.width - 24
            spacing: 4
            Label95 { text: "Course:" }
            TextField95 {
                id: titleField
                width: parent.width
                onAccepted: newCourse.accept()
                Text {
                    visible: titleField.text === ""
                    x: 4
                    anchors.verticalCenter: parent.verticalCenter
                    text: "Volcanoes, The Roman Empire, Fractions..."
                    color: Theme.shadow
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontSize
                }
            }
            Label95 { visible: newCourse.own; text: "Subject:" }
            ComboBox95 {
                visible: newCourse.own
                width: parent.width
                popupMenu: popup
                model: newCourse.ownSubjects.map(function (s) { return s.name; })
                currentIndex: newCourse.ownSubject
                onActivated: (i) => newCourse.ownSubject = i
            }
            Label95 { text: "Level:" }
            ComboBox95 {
                width: parent.width
                popupMenu: popup
                model: Classroom.levels.map(function (l) { return l.label; })
                currentIndex: newCourse.level
                onActivated: (i) => newCourse.level = i
            }
            Label95 { text: "Length:" }
            Row {
                spacing: 12
                Repeater {
                    model: ["Short (about 9 lessons)", "Standard (16)", "Long (24)"]
                    RadioButton95 {
                        required property string modelData
                        required property int index
                        text: modelData
                        checked: newCourse.length === index
                        onClicked: newCourse.length = index
                    }
                }
            }
            Label95 { text: "What should it cover? (optional)" }
            TextArea95 {
                id: focusArea
                width: parent.width
                height: 50
                placeholder: newCourse.own ? "What you want to learn about it, in your own words" : "Topics you want, or leave empty for the usual ones at this level"
            }
            Notice {
                width: parent.width
                kind: newCourse.coverage.ok ? "info" : "warning"
                text: newCourse.coverage.ok === undefined ? ""
                    : newCourse.coverage.ok ? "Readings will come from: " + Util.plain((newCourse.coverage.libraries || []).join(", "))
                    : "No installed library covers this subject. Get “" + Util.plain(newCourse.coverage.suggestion || "") + "” from the Library Manager first."
                buttons: newCourse.coverage.ok ? [] : [{ text: "Get It...", action: function () { var s = newCourse.coverage.suggestion; newCourse.close(); window.findLibrary(s); } }]
            }
            Notice {
                width: parent.width
                kind: "info"
                text: (newCourse.coverage.simpleSuggestion || "") !== ""
                      ? "Tip: “Wikipedia in simple English” (530 MB) is written for younger readers. With it installed, lessons at this level read from it first." : ""
                buttons: [{ text: "Get It...", action: function () { var s = newCourse.coverage.simpleSuggestion; newCourse.close(); window.findLibrary(s); } }]
            }
        }
        Row {
            anchors.right: parent.right
            anchors.rightMargin: 12
            anchors.bottom: parent.bottom
            anchors.bottomMargin: 10
            spacing: 6
            Button95 { width: 110; text: "Plan the Course"; isDefault: true; enabled: titleField.text.trim() !== "" && newCourse.coverage.ok === true; onClicked: newCourse.accept() }
            Button95 { text: "Cancel"; onClicked: newCourse.close() }
        }
    }
}
