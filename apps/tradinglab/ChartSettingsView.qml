import QtQuick
import Win95Ui

// Chart Settings: chart type, price scale, crosshair, candle colours, grid; the active studies with their parameters
// and colours; and the list of studies to add. Port of renderer/src/views/ChartSettingsView.tsx.
Item {
    id: view
    property var app

    readonly property var settings: app ? app.chartSettings : core.defaultChartSettings()
    function set(patch) { app.updateChartSettings(Object.assign({}, settings, patch)); }
    function addStudy(studyId) { set({ studies: settings.studies.concat([core.newStudyInstance(studyId, {})]) }); }

    readonly property var types: [["candles", "Candles"], ["hollow", "Hollow candles"], ["bars", "Bars"], ["line", "Line"], ["area", "Area"], ["heikin", "Heikin Ashi"]]
    readonly property var scales: [["normal", "Normal"], ["log", "Logarithmic"], ["percent", "Percentage"]]
    readonly property var crosshairs: [["normal", "Free"], ["magnet", "Magnet (snap to OHLC)"]]
    readonly property var categories: ["Overlay", "Trend", "Momentum", "Volatility", "Volume"]
    readonly property var allStudies: core.studies()
    function indexOf(list, v) { for (let i = 0; i < list.length; ++i) if (list[i][0] === v) return i; return 0; }

    Bevel {
        id: titleBar
        width: parent.width
        height: 30
        style: "raised"
        Heading95 { x: 8; anchors.verticalCenter: parent.verticalCenter; text: "Chart Settings" }
        Button95 {
            anchors.right: parent.right
            anchors.rightMargin: 4
            anchors.verticalCenter: parent.verticalCenter
            width: 110
            text: "Back to Chart"
            onClicked: view.app.setView("chart")
        }
    }

    Bevel {
        id: well
        y: titleBar.height + 4
        width: parent.width
        height: parent.height - y
        style: "sunken"
        face: Theme.face

        Flickable {
            id: flick
            x: 2
            y: 2
            width: parent.width - 4 - (bar.needed ? bar.width : 0)
            height: parent.height - 4
            clip: true
            contentHeight: page.implicitHeight + 16
            boundsBehavior: Flickable.StopAtBounds

            Column {
                id: page
                x: 8
                y: 8
                width: flick.width - 16
                spacing: 10

                // ---- Appearance
                GroupBox95 {
                    width: parent.width
                    height: appearance.implicitHeight + 28
                    title: "Appearance"
                    Grid {
                        id: appearance
                        columns: 2
                        columnSpacing: 10
                        rowSpacing: 6
                        verticalItemAlignment: Grid.AlignVCenter
                        Label95 { text: "Chart type" }
                        ComboBox95 {
                            width: 180
                            popupMenu: popup
                            model: view.types.map(function (t) { return t[1]; })
                            currentIndex: view.indexOf(view.types, view.settings.type)
                            onActivated: (i) => view.set({ type: view.types[i][0] })
                        }
                        Label95 { text: "Price scale" }
                        ComboBox95 {
                            width: 180
                            popupMenu: popup
                            model: view.scales.map(function (t) { return t[1]; })
                            currentIndex: view.indexOf(view.scales, view.settings.scale)
                            onActivated: (i) => view.set({ scale: view.scales[i][0] })
                        }
                        Label95 { text: "Crosshair" }
                        ComboBox95 {
                            width: 180
                            popupMenu: popup
                            model: view.crosshairs.map(function (t) { return t[1]; })
                            currentIndex: view.indexOf(view.crosshairs, view.settings.crosshair)
                            onActivated: (i) => view.set({ crosshair: view.crosshairs[i][0] })
                        }
                        Label95 { text: "Up color" }
                        ChartColorButton { color: view.settings.upColor; onPicked: (c) => view.set({ upColor: c }) }
                        Label95 { text: "Down color" }
                        ChartColorButton { color: view.settings.downColor; onPicked: (c) => view.set({ downColor: c }) }
                        Item { width: 1; height: 18 }
                        CheckBox95 { text: "Grid lines"; checked: view.settings.grid; onToggled: (on) => view.set({ grid: on }) }
                    }
                }

                // ---- Active studies
                GroupBox95 {
                    width: parent.width
                    height: active.implicitHeight + 28
                    title: "Active studies"
                    Column {
                        id: active
                        width: parent.width
                        spacing: 6
                        Label95 { visible: view.settings.studies.length === 0; text: "None. Add one from the list."; dim: true }
                        Repeater {
                            // by count, so a card (and the box being typed in) survives its own edits
                            model: view.settings.studies.length
                            delegate: ChartStudyCard {
                                required property int index
                                width: active.width
                                inst: view.settings.studies[index]
                                studies: view.settings.studies
                                onEdited: (next) => view.set({ studies: view.settings.studies.map(function (s) { return s.uid === next.uid ? next : s; }) })
                                onRemoved: { const uid = inst.uid; view.set({ studies: view.settings.studies.filter(function (s) { return s.uid !== uid; }) }); }
                            }
                        }
                    }
                }

                // ---- Add study
                GroupBox95 {
                    width: parent.width
                    height: adder.implicitHeight + 28
                    title: "Add study"
                    Column {
                        id: adder
                        width: parent.width
                        spacing: 4
                        TextField95 {
                            id: filter
                            width: 220
                            Label95 { x: 5; anchors.verticalCenter: parent.verticalCenter; visible: filter.text === ""; text: "Search studies…"; dim: true }
                        }
                        Repeater {
                            model: view.categories
                            delegate: Column {
                                id: cat
                                required property string modelData
                                readonly property var items: view.allStudies.filter(function (s) {
                                    const q = filter.text.trim().toLowerCase();
                                    return s.category === cat.modelData && (!q || s.name.toLowerCase().indexOf(q) >= 0);
                                })
                                visible: items.length > 0
                                width: adder.width
                                spacing: 0
                                Label95 { topPadding: 6; bottomPadding: 2; text: cat.modelData; bold: true }
                                Repeater {
                                    model: cat.items
                                    delegate: Rectangle {
                                        required property var modelData
                                        width: Math.min(adder.width, 360)
                                        height: 20
                                        color: addMouse.containsMouse ? Theme.selection : "transparent"
                                        Label95 { x: 6; anchors.verticalCenter: parent.verticalCenter; text: parent.modelData.name; color: addMouse.containsMouse ? Theme.selectionText : Theme.text }
                                        Label95 { anchors.right: parent.right; anchors.rightMargin: 6; anchors.verticalCenter: parent.verticalCenter; text: "+ Add"; color: addMouse.containsMouse ? Theme.selectionText : Theme.shadow }
                                        MouseArea { id: addMouse; anchors.fill: parent; hoverEnabled: true; onClicked: view.addStudy(parent.modelData.id) }
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
        ScrollBar95 {
            id: bar
            visible: needed
            flickable: flick
            x: parent.width - width - 2
            y: 2
            width: 16
            height: parent.height - 4
        }
    }
}
