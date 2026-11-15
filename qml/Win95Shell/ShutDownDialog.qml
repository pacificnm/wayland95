import QtQuick
import Win95Ui as Ui
import Win95Shell

// Start > Shut Down... : the screen darkens and you pick what to do. Stand
// by and Hibernate are offered when the computer supports them. Exiting is
// the default when Wayland 95 runs nested inside another desktop, shutting
// down when it's the desktop.
Ui.Dialog95 {
    id: dialog

    title: "Shut Down Wayland 95"
    dialogWidth: 360
    dialogHeight: 108 + options.length * 22
    dimBackground: true

    // "standby", "shutdown", "restart", "hibernate" or "exit"
    signal chosen(string option)

    readonly property var options: {
        var list = [];
        if (Ui.Power.canSuspend)
            list.push({ id: "standby", key: Qt.Key_T, text: "S<u>t</u>and by?" });
        list.push({ id: "shutdown", key: Qt.Key_S, text: "<u>S</u>hut down the computer?" });
        list.push({ id: "restart", key: Qt.Key_R, text: "<u>R</u>estart the computer?" });
        if (Ui.Power.canHibernate)
            list.push({ id: "hibernate", key: Qt.Key_H, text: "<u>H</u>ibernate?" });
        list.push({ id: "exit", key: Qt.Key_C, text: "<u>C</u>lose all programs and exit Wayland 95?" });
        return list;
    }
    property int option: 0

    function indexOf(id) {
        for (var i = 0; i < options.length; ++i)
            if (options[i].id === id)
                return i;
        return 0;
    }
    function open() {
        option = indexOf(Ui.Power.primarySession ? "shutdown" : "exit");
        show();
        radios.itemAt(option).forceActiveFocus();
    }

    function accept() {
        var chosenOption = options[option].id;
        close();
        chosen(chosenOption);
    }

    Keys.onPressed: (event) => {
        var n = options.length;
        if (event.key === Qt.Key_Up || event.key === Qt.Key_Down) {
            option = (option + (event.key === Qt.Key_Down ? 1 : n - 1)) % n;
            radios.itemAt(option).forceActiveFocus();
        } else if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter || event.key === Qt.Key_Y) {
            accept();
        } else if (event.key === Qt.Key_N) {
            close();
        } else {
            for (var i = 0; i < n; ++i) {
                if (options[i].key === event.key) {
                    option = i;
                    radios.itemAt(option).forceActiveFocus();
                    event.accepted = true;
                    return;
                }
            }
            return;
        }
        event.accepted = true;
    }

    MenuIcon {
        x: 14
        y: 14
        kind: "shutdown"
    }

    Text {
        x: 60
        y: 14
        text: "Are you sure you want to:"
        font.family: Theme.fontFamily
        font.pixelSize: Theme.fontSize
    }

    Column {
        x: 60
        y: 36
        spacing: 4

        Repeater {
            id: radios
            model: dialog.options
            delegate: Ui.RadioButton95 {
                required property var modelData
                required property int index
                text: modelData.text
                checked: dialog.option === index
                onClicked: dialog.option = index
            }
        }
    }

    Row {
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottom: parent.bottom
        anchors.bottomMargin: 10
        spacing: 6

        Ui.Button95 {
            text: "Yes"
            isDefault: true
            onClicked: dialog.accept()
        }
        Ui.Button95 {
            text: "No"
            onClicked: dialog.close()
        }
    }
}
