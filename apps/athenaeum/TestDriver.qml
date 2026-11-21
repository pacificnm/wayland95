import QtQuick

// W95_ATHENAEUM_TEST=NAME: a scripted walk through one feature, for the
// tests, without the mouse: each step calls the program as a click would,
// and grab(name) saves the window (or another one) next to
// W95_ATHENAEUM_GRAB as FILE-name.png. The program quits after the last step.
Item {
    id: driver
    property string name: ""
    property string grabFile: ""
    property var steps: []
    property int step: 0
    readonly property var lib: { var l = LibraryManager.libraryList(); return l.length ? l[0].libraryId : 0; }

    function grab(suffix, win) {
        var file = grabFile.replace(/\.png$/, "") + "-" + suffix + ".png";
        if (windowGrabber.grab(win || window, file))
            console.log("grabbed " + file);
    }
    function page() { return window.page; }

    Timer {
        id: ticker
        interval: 1500
        repeat: true
        running: driver.steps.length > 0
        onTriggered: {
            if (driver.step >= driver.steps.length) {
                running = false;
                Qt.callLater(Qt.quit);
                return;
            }
            try {
                driver.steps[driver.step++]();
            } catch (e) {
                console.log("test step " + driver.step + " failed: " + e);
            }
        }
    }

    Component.onCompleted: {
        var d = driver;
        var scripts = {
            // highlights: in the article, the Notes panel, the Notes page, the selection menu
            notes: [
                function () { window.openArticle(d.lib, "Crater_Lake", ""); },
                function () { d.page().highlight("deepest lake in the United States", "yellow", "Check the depth: 594 m"); },
                function () { d.page().highlight("famous for its clarity", "green", ""); },
                function () { d.page().panel = "notes"; },
                function () { d.grab("panel"); },
                function () {
                    popup.popup(400, 300, d.page().selectionMenu("caldera"));
                },
                function () { d.grab("menu"); popup.close(); },
                function () { window.show("notes"); },
                function () { d.grab("page"); },
            ],
            // projects: one made, an article and an excerpt added, a document written in its window
            project: [
                function () {
                    var id = projectList.create("Crater Lake report", "How Crater Lake formed, and why its water is so clear");
                    projectList.addArticle(id, d.lib, "Crater_Lake", "Crater Lake", "The main source");
                    projectList.addArticle(id, d.lib, "Mount_Mazama", "Mount Mazama", "");
                    projectList.addExcerpt(id, "At 594 metres it is the deepest lake in the United States", d.lib, "Crater_Lake", "Crater Lake", "", "A figure to use");
                    window.show("projects");
                },
                function () { d.grab("list"); },
                function () { window.openProject(projectList.list()[0].projectId); },
                function () { d.grab("sources"); },
                function () { d.page().newDocument(); },
                function () {
                    var w = documentWindows.open_[Object.keys(documentWindows.open_)[0]];
                    w.insert("Crater Lake formed when ");
                    var cites = w.citeMenu();
                    for (var i = 0; i < cites.length; ++i)
                        if (cites[i].text && cites[i].text.indexOf("Mount Mazama") === 0) cites[i].action();
                    w.insert(" collapsed.\n\n");
                    for (var k = 0; k < cites.length; ++k)
                        if (cites[k].text && cites[k].text.indexOf("“") === 0) cites[k].action();
                    w.insert("\n\n");
                    cites = w.citeMenu();
                    cites[0].action();
                },
                function () {
                    var w = documentWindows.open_[Object.keys(documentWindows.open_)[0]];
                    d.grab("document", w);
                },
                function () {
                    var w = documentWindows.open_[Object.keys(documentWindows.open_)[0]];
                    w.close();
                    d.page().project.refresh();
                },
                function () { window.page.bibliography(); },
                function () { d.grab("bibliography"); },
            ],
            // the citation and definition dialogs, the contents menu, the timeline
            reading: [
                function () { window.openArticle(d.lib, "Crater_Lake", ""); },
                function () { window.cite(d.lib, "Crater_Lake", "Crater Lake"); },
                function () { d.grab("cite"); citeDialog.close(); },
                function () { popup.popup(200, 90, d.page().tocItems()); },
                function () { d.grab("contents"); popup.close(); },
                function () { window.define("molecules"); },
                function () { d.grab("define"); defineDialog.close(); },
                function () { window.openTimeline({ libraryId: d.lib, path: "Crater_Lake", articleTitle: "Crater Lake" }); },
                function () { d.grab("timeline"); },
                function () { window.openTitles(d.lib, "Wikipedia Sample: A to Z", "C", false); },
                function () { d.grab("atoz"); },
            ],
            // the Library Manager's catalog (fetched from Kiwix when the internet may be used) and a library's details
            catalog: [
                function () { window.showLibraryManager(1); },
                function () {},
                function () {},
                function () { LibraryManager.catalog.query = "wikipedia simple"; },
                function () { d.grab("catalog"); },
                function () { window.showLibraryManager(0); },
                function () { d.grab("installed"); },
            ],
            // a question with no Ollama: the chat says what is missing
            ask: [
                function () { window.show("ask"); },
                function () { d.page().conversation.ask("How did Crater Lake form?"); },
                function () {},
                function () { d.grab("answer"); },
            ],
            // the classroom: a profile, and the new-course dialog
            classroom: [
                function () { Classroom.saveProfile({ name: "Sam Tester", grade: Classroom.grades[6].key, interests: "hiking, volcanoes", goals: "just curious" }); window.show("classroom"); },
                function () { d.grab("profile"); },
                function () { d.page().start(Classroom.subjects[1].key); },
                function () { d.grab("newcourse"); },
            ],
        };
        steps = scripts[name] || [];
        if (name !== "" && !steps.length)
            console.log("no test script " + name);
    }
}
