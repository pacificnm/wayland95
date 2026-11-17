import QtQuick
import Win95Ui
import Win95Control

// System Properties: what this machine is, and how it's doing.
PropertySheet {
    id: sheet

    title: "System Properties"
    titles: ["General", "Performance"]
    canApply: false

    property var perf: SystemInfo.performance()

    Timer {
        interval: 2000
        running: sheet.visible && sheet.currentTab === 1
        repeat: true
        onTriggered: sheet.perf = SystemInfo.performance()
    }

    component Line: Text {
        width: 220
        wrapMode: Text.Wrap
        color: Theme.text
        font.family: Theme.fontFamily
        font.pixelSize: Theme.fontSize
    }

    // --------------------------------------------------------------- General
    Item {
        AppletIcon {
            kind: "system"
            size: 96
            x: 10
            y: 20
        }

        Column {
            x: 140
            y: 16
            spacing: 2

            Line { text: "System:" }
            Line { leftPadding: 16; text: "Wayland 95 " + Wayland95Settings.version }
            Line { leftPadding: 16; text: SystemInfo.osName }
            Line { leftPadding: 16; text: SystemInfo.kernel }
            Line { leftPadding: 16; text: "Qt " + SystemInfo.qtVersion }
            Item { width: 1; height: 14 }
            Line { text: "Registered to:" }
            Line { leftPadding: 16; text: SystemInfo.userName }
            Line { leftPadding: 16; text: SystemInfo.hostName }
            Item { width: 1; height: 14 }
            Line { text: "Computer:" }
            Line { leftPadding: 16; text: SystemInfo.cpu }
            Line { leftPadding: 16; text: SystemInfo.memory }
        }
    }

    // ----------------------------------------------------------- Performance
    Item {
        GroupBox95 {
            width: parent.width
            height: 190
            title: "Performance status"

            Grid {
                columns: 2
                columnSpacing: 12
                rowSpacing: 10

                Line { width: 100; text: "Memory:" }
                Line { text: sheet.perf.memoryText }
                Line { width: 100; text: "Swap:" }
                Line { text: sheet.perf.swapText }
                Line { width: 100; text: "Up time:" }
                Line { text: sheet.perf.uptimeText }
                Line { width: 100; text: "Load average:" }
                Line { text: sheet.perf.loadText }
            }

            // Win95-style block progress bar for free memory
            Column {
                anchors.bottom: parent.bottom
                width: parent.width
                spacing: 4
                Line { text: "Free memory:" }
                Bevel {
                    width: parent.width
                    height: 18
                    style: "thinSunken"
                    Row {
                        x: 2; y: 2
                        spacing: 2
                        Repeater {
                            model: Math.max(0, Math.round((parent.parent.width - 4) / 10 * sheet.perf.memoryFreePercent / 100))
                            Rectangle { width: 8; height: 14; color: Theme.selection }
                        }
                    }
                }
            }
        }
    }
}
