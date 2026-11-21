import QtQuick
import Win95Ui

// Help > About Trading Lab: version, build, Qt version, data folder, license, the upstream repository, and credits
// to the original Electron app this is a port of. Main.qml's aboutLoader sets `app` and calls show(); closing it
// unloads it so the menu can open it again. Upstream's update check is left out (Wayland 95 updates its programs
// itself), and so is the TradingView credit (no Lightweight Charts here). Port of components/AboutDialog.tsx.
Dialog95 {
    id: dialog
    property var app
    property var about: null

    title: "About Trading Lab"
    dialogWidth: 460
    dialogHeight: 384
    // the loader that holds this has no size of its own: cover the window, so the dialog centres in it
    anchors.fill: undefined
    width: app ? app.width : 0
    height: app ? app.height : 0

    Component.onCompleted: api.invoke("app:about", [], function (a) { dialog.about = a; })
    onClosed: {
        const loader = parent;
        Qt.callLater(function () { if (loader && loader.active !== undefined) loader.active = false; });
    }

    readonly property string details: about ? about.name + " " + about.version + " · Qt " + about.qt + " · Wayland 95" : ""

    Item {
        x: 14
        y: 12
        width: parent.width - 28
        height: parent.height - 24

        Icon95 { id: logo; x: 0; y: 2; name: "chart"; size: 32; width: 32; height: 32 }
        Column {
            x: logo.width + 12
            width: parent.width - x
            spacing: 2
            Label95 { text: "Trading Lab"; bold: true; font.pixelSize: Theme.fontSize + 5 }
            Label95 { dim: true; text: dialog.about ? "Version " + dialog.about.version : "Loading…" }
        }
        Column {
            y: 46
            width: parent.width
            spacing: 8
            Label95 { visible: !!dialog.about; width: parent.width; wrapMode: Text.Wrap; text: dialog.about ? dialog.about.description : "" }
            Grid {
                visible: !!dialog.about
                columns: 2
                columnSpacing: 12
                rowSpacing: 3
                Label95 { dim: true; text: "Version" }
                Label95 { bold: true; text: dialog.about ? dialog.about.version : "" }
                Label95 { dim: true; text: "Qt" }
                Label95 { bold: true; text: dialog.about ? dialog.about.qt : "" }
                Label95 { dim: true; text: "Data folder" }
                Label95 { bold: true; width: 330; elide: Text.ElideMiddle; text: dialog.about ? dialog.about.dataFolder : "" }
                Label95 { dim: true; text: "License" }
                Label95 { bold: true; text: dialog.about ? dialog.about.license : "" }
            }
            Row {
                visible: !!dialog.about
                spacing: 6
                Button95 {
                    text: "GitHub repository"
                    onClicked: api.invoke("app:openExternal", [dialog.about.repository])
                }
                Button95 {
                    text: "Copy version info"
                    onClicked: {
                        copyHelper.text = dialog.details;
                        copyHelper.selectAll();
                        copyHelper.copy();
                        if (dialog.app) dialog.app.toast("success", "Version details copied");
                    }
                }
            }
            Rectangle { width: parent.width; height: 1; color: Theme.shadow }
            Text {
                width: parent.width
                wrapMode: Text.Wrap
                textFormat: Text.StyledText
                color: Theme.shadow
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontSize
                text: "<b>Based on</b> the original Trading Lab, an Electron and React app (github.com/pacificnm/trading-labs.app), "
                      + "redrawn here for Wayland 95 with Qt. Trading Lab is open source under the MIT License, Copyright (c) 2026 jaimie garner.<br><br>"
                      + "<b>Data</b> from Financial Modeling Prep (market data, fundamentals, news, disclosures) and Cboe delayed quotes (options). "
                      + "<b>Assistant</b> powered by Anthropic Claude.<br><br>"
                      + "Educational software. Nothing here is investment advice, and paper trades use simulated fills."
            }
        }
        Button95 {
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            isDefault: true
            text: "OK"
            onClicked: dialog.close()
        }
        // the clipboard, through a text field nobody sees
        TextEdit { id: copyHelper; visible: false }
    }
}
