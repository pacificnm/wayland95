import QtQuick
import Win95Ui

// Installs, removes and updates for one window: asks first (listing what
// else comes along), then shows the progress until it's done. Closing the
// progress box doesn't stop the work; its Cancel button does.
//
//   SoftwareTasks { id: tasks; messageBox: sheet.messageBox }
//   tasks.install(item)  /  tasks.remove(program)  /  tasks.update(list)
Dialog95 {
    id: tasks

    property MessageBox95 messageBox
    // what this window asked for, waiting for Software's plan
    property string waiting: ""
    // this window started the running task
    property bool mine: false

    signal done(bool ok)

    title: "Add/Remove Programs"
    dialogWidth: 340
    dialogHeight: 150

    function install(item) { ask("install", function () { Software.prepareInstall(item); }); }
    function remove(program) { ask("remove", function () { Software.prepareRemove(program); }); }
    function update(list) {
        updatingDesktop = list.some(function (u) { return u.source === "deb" && u.id === "wayland95"; });
        ask("update", function () { Software.prepareUpdates(list); });
    }
    // the update includes Wayland 95 itself: the running desktop is the old
    // version until the next login
    property bool updatingDesktop: false
    property bool finishedUpdate: false

    function ask(action, prepare) {
        if (Software.busy) {
            messageBox.show(title, "Please wait until \"" + Software.task + "\" has finished.", "info", ["OK"]);
            return;
        }
        waiting = action;
        prepare();
    }

    // a new kernel or system library needs a restart; a new Wayland 95 a
    // fresh login
    function afterUpdate() {
        if (Power.restartRequired()) {
            messageBox.show(title, "The updates are installed. Some of them take effect only after "
                            + "the computer restarts.\n\nDo you want to restart your computer now?",
                            "question", ["Yes", "No"], function (i) { if (i === 0) Power.reboot(); });
        } else if (updatingDesktop) {
            messageBox.show(title, "Wayland 95 was updated. The new version starts the next time you "
                            + "log on.\n\nDo you want to log off now? Save your work first.",
                            "question", ["Yes", "No"], function (i) { if (i === 0) Power.logOff(); });
        }
        updatingDesktop = false;
    }

    function names(list) {
        var shown = list.slice(0, 12).join(", ");
        return list.length > 12 ? shown + " and " + (list.length - 12) + " more" : shown;
    }

    Connections {
        target: Software
        function onPlan(action, items, others, error) {
            if (action !== tasks.waiting)
                return;
            tasks.waiting = "";
            var what = items.length === 1 ? items[0].name : items.length + " updates";
            if (error) {
                tasks.messageBox.show(tasks.title, "Can't " + action + " " + what + ".\n\n" + error, "error", ["OK"]);
                return;
            }
            var adds = others.filter(function (n) { return n.charAt(0) !== "-"; });
            var drops = others.filter(function (n) { return n.charAt(0) === "-"; })
                              .map(function (n) { return n.substring(1); });
            var text, icon = "question";
            if (action === "install") {
                text = "Are you sure you want to install " + what + "?";
                if (adds.length)
                    text += "\n\nThese will be installed too, because it needs them:\n" + tasks.names(adds) + ".";
            } else if (action === "remove") {
                text = "Are you sure you want to remove " + what + " and all of its components?";
                if (adds.length) {
                    icon = "warning";
                    text += "\n\nThese will be removed too, because they need it:\n" + tasks.names(adds) + ".";
                }
            } else {
                text = "Install " + what + "?";
                if (adds.length)
                    text += "\n\nThese new packages are needed as well:\n" + tasks.names(adds) + ".";
            }
            if (drops.length) {
                icon = "warning";
                text += "\n\nThese will be removed:\n" + tasks.names(drops) + ".";
            }
            tasks.messageBox.show(tasks.title, text, icon, ["Yes", "No"], function (i) {
                if (i !== 0)
                    return;
                tasks.mine = true;
                tasks.finishedUpdate = action === "update";
                Software.commit();
                tasks.show();
            });
        }
        function onTaskFinished(ok, message) {
            if (!tasks.mine)
                return;
            tasks.mine = false;
            tasks.close();
            if (!ok)
                tasks.messageBox.show(tasks.title, message, message === "Cancelled." ? "info" : "error", ["OK"]);
            else if (tasks.finishedUpdate)
                tasks.afterUpdate();
            tasks.finishedUpdate = false;
            tasks.done(ok);
        }
    }

    Column {
        x: 12
        y: 12
        width: parent.width - 24
        spacing: 8

        Text {
            width: parent.width
            elide: Text.ElideRight
            text: Software.task
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontSize
            font.bold: true
        }
        Text {
            width: parent.width
            elide: Text.ElideRight
            text: Software.status
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontSize
        }
        ProgressBar95 {
            width: parent.width
            value: Software.percent
        }
    }

    Button95 {
        anchors.right: parent.right
        anchors.rightMargin: 10
        anchors.bottom: parent.bottom
        anchors.bottomMargin: 10
        text: "Cancel"
        onClicked: Software.cancel()
    }
}
