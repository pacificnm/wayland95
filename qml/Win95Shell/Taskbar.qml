import QtQuick
import Win95Ui as Ui
import Win95Shell

Item {
    id: taskbar
    height: Theme.taskbarHeight

    // where the Start menu, the window menu and the tray's pop-ups go: the
    // item the taskbar sits at the bottom of (the Qt compositor's desktop),
    // or the shell's full-screen pop-up layer (win95shell)
    property Item popupParent: taskbar.parent
    // the tray's tooltips (the shell puts them where they don't take clicks)
    property Item tipParent: popupParent
    // any of them showing (the shell shows its pop-up layer while so)
    readonly property bool anyPopupVisible: popupOpen || windowMenu.visible || endTaskBox.visible
    readonly property bool tipShowing: tray.tipShowing

    // exposed so Main.qml can draw a desktop-wide click-catcher that closes
    // the Start menu when you click anywhere outside it
    property alias startMenuOpen: startMenu.visible
    function closeStartMenu() { startMenu.close() }
    // the volume pop-up closes the same way
    readonly property bool popupOpen: startMenu.visible || tray.popupOpen
    function closePopups() {
        startMenu.close();
        tray.closePopups();
    }
    function toggleStartMenu() {
        if (startMenu.visible)
            startMenu.close();
        else
            startMenu.open();
    }

    // forwarded from the Start menu to Main.qml, which owns the dialogs
    signal runRequested()
    signal shutDownRequested()
    signal helpRequested()
    signal startMenuClosed()

    Bevel { anchors.fill: parent; raised: true }

    Row {
        anchors.left: parent.left
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        anchors.margins: 3
        spacing: 4

        StartButton {
            height: taskbar.height - 6
            active: startMenu.visible
            onClicked: taskbar.toggleStartMenu()
        }

        Repeater {
            model: WindowManager
            delegate: TaskButton {
                id: button
                height: taskbar.height - 6
                text: model.title
                appId: model.appId || ""
                active: model.active && !model.minimized
                onClicked: WindowManager.toggleFromTaskbar(index)
                onMenuRequested: (mx, my) => {
                    var p = button.mapToItem(windowMenu, mx, my);
                    taskbar.openWindowMenu(model.frame, model.title, p.x, p.y);
                }
            }
        }
    }

    // the notification area: indicators, other programs' icons and the clock
    Tray {
        id: tray
        anchors.right: parent.right
        anchors.rightMargin: 4
        anchors.verticalCenter: parent.verticalCenter
        popupParent: taskbar.popupParent
        tipParent: taskbar.tipParent
    }

    // ---- right-click on a taskbar button: the window's menu --------------
    property var menuFrame: null
    function openWindowMenu(frame, title, x, y) {
        menuFrame = frame;
        var minimized = !!frame.minimized, maximized = !!frame.maximized;
        windowMenu.popup(x, y, [
            { text: "&Restore", enabled: minimized || maximized, action: function () { taskbar.restore(frame); } },
            { text: "Mi&nimize", enabled: !minimized, action: function () { WindowManager.setMinimized(frame, true); } },
            { text: "Ma&ximize", enabled: !maximized || minimized, action: function () { taskbar.maximize(frame); } },
            { separator: true },
            { text: "&Close", action: function () { frame.closeWindow(); } },
            { text: "&End Task", action: function () { taskbar.endTask(frame, title); } },
        ]);
    }
    function restore(frame) {
        if (frame.minimized)
            WindowManager.setMinimized(frame, false);
        else if (frame.maximized)
            frame.toggleMaximize();
        frame.requestActivate();
    }
    function maximize(frame) {
        if (frame.minimized)
            WindowManager.setMinimized(frame, false);
        if (!frame.maximized)
            frame.toggleMaximize();
        frame.requestActivate();
    }
    function endTask(frame, title) {
        endTaskBox.show("End Task", (title ? "\"" + title + "\"" : "This program")
                        + " will be ended right away. Anything you haven't saved in it will be lost.\n\nEnd it anyway?",
                        "warning", ["End Task", "Cancel"], function (choice) {
            if (choice === 0)
                frame.forceClose();
        });
    }
    Ui.Menu95 {
        id: windowMenu
        parent: taskbar.popupParent
        z: 3000
    }
    Ui.MessageBox95 {
        id: endTaskBox
        parent: taskbar.popupParent
        z: 3001
    }

    StartMenu {
        id: startMenu
        // just above the taskbar, at the bottom of the pop-up parent
        parent: taskbar.popupParent
        z: 2900 // above windows and notifications, under the window menu
        x: 2
        anchors.bottom: parent.bottom
        anchors.bottomMargin: taskbar.height - 1
        onRunRequested: taskbar.runRequested()
        onShutDownRequested: taskbar.shutDownRequested()
        onHelpRequested: taskbar.helpRequested()
        onClosed: taskbar.startMenuClosed()
        // (the End Task box is the taskbar's one message box)
        onProblem: (title, text) => endTaskBox.show(title, text, "error", ["OK"])
    }
}
