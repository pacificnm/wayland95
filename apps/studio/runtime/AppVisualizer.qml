import QtQuick
import Win95Ui

// Visualizer: what a MediaPlayer on the form is playing, as it plays:
// Spectrum (bars, low notes to high, with peaks that fall slowly),
// Oscilloscope (the wave) or Meter (how loud). Player is the MediaPlayer's
// Name. ForeColor: the bars' and the wave's color (blank: green, yellow
// and red, as a meter's); BackColor behind them (blank: black).
//
//   vis1.player = "player2"   vis1.style = "Oscilloscope"
//   (the numbers themselves: player1.levels, player1.level, player1.waveform)
AppElement {
    id: vis

    property string player: ""
    property string style: "Spectrum"
    property int bars: 20
    property bool showPeaks: true
    property string peakColor: ""
    property string borderStyle: "Sunken"
    signal clicked()

    defaultBack: "#000000"
    activeFocusOnTab: false

    // the MediaPlayer (found once the form has made all its elements)
    property var source: null
    function attach() {
        var p = !designMode && form && player !== "" ? form.element(player) : null;
        source = p && p.analyzer !== undefined ? p : null;
        if (player !== "" && !designMode && form && !source)
            console.warn(form.formName + ", " + objectName + ": there's no MediaPlayer named " + player + " on the form");
    }
    onPlayerChanged: Qt.callLater(attach)
    onFormChanged: Qt.callLater(attach)
    Component.onCompleted: Qt.callLater(attach)
    onBarsChanged: if (source) source.analyzer.bands = Math.max(1, Math.min(64, bars))
    onSourceChanged: if (source) source.analyzer.bands = Math.max(1, Math.min(64, bars))

    readonly property var analyzer: source ? source.analyzer : null
    // (the Studio's: a still picture of what it'll look like)
    readonly property var sample: [0.55, 0.8, 0.9, 0.75, 0.6, 0.7, 0.5, 0.62, 0.45, 0.5, 0.38, 0.42, 0.3, 0.33, 0.25, 0.28, 0.2, 0.15, 0.12, 0.08]
    readonly property int bandCount: Math.max(1, Math.min(64, bars))
    function bandAt(i) {
        if (designMode)
            return sample[Math.floor(i * sample.length / bandCount)];
        return analyzer && i < analyzer.levels.length ? analyzer.levels[i] : 0;
    }
    function peakAt(i) {
        if (designMode)
            return Math.min(1, bandAt(i) + 0.08);
        return analyzer && i < analyzer.peaks.length ? analyzer.peaks[i] : 0;
    }

    Rectangle {
        anchors.fill: parent
        color: vis.fillColor
        border.width: vis.borderStyle === "Single" ? 1 : 0
        border.color: Theme.text
    }
    Bevel {
        anchors.fill: parent
        visible: vis.borderStyle === "Sunken"
        style: "sunken"
        face: "transparent"
    }

    Item {
        id: area
        anchors.fill: parent
        anchors.margins: vis.borderStyle === "None" ? 0 : 2
        clip: true

        // Spectrum: a bar for each band, coloured as a meter is from the bottom up
        Row {
            visible: vis.style === "Spectrum"
            anchors.fill: parent
            anchors.margins: 1
            spacing: 1
            Repeater {
                model: vis.bandCount
                delegate: Item {
                    id: bar
                    required property int index
                    readonly property real level: { vis.analyzer ? vis.analyzer.levels : null; return vis.bandAt(index); }
                    readonly property real peak: { vis.analyzer ? vis.analyzer.peaks : null; return vis.peakAt(index); }
                    width: Math.max(1, (area.width - 2 - (vis.bandCount - 1)) / vis.bandCount)
                    height: area.height - 2
                    Item {
                        anchors.bottom: parent.bottom
                        width: parent.width
                        height: Math.round(parent.height * bar.level)
                        clip: true
                        Rectangle {
                            anchors.bottom: parent.bottom
                            width: parent.width
                            height: bar.height
                            color: vis.foreColor !== "" ? vis.foreColor : "#00c000"
                            gradient: vis.foreColor !== "" ? null : meterColors
                        }
                    }
                    Rectangle {
                        visible: vis.showPeaks && bar.peak > 0.01
                        y: Math.round((bar.height - 2) * (1 - bar.peak))
                        width: parent.width
                        height: 2
                        color: vis.peakColor !== "" ? vis.peakColor : "#c0c0c0"
                    }
                }
            }
        }

        // Oscilloscope: the wave
        Canvas {
            id: scope
            visible: vis.style === "Oscilloscope"
            anchors.fill: parent
            property var wave: vis.designMode ? [] : (vis.analyzer ? vis.analyzer.waveform : [])
            onWaveChanged: requestPaint()
            onVisibleChanged: requestPaint()
            onPaint: {
                var ctx = getContext("2d");
                ctx.reset();
                var w = width, h = height, pts = wave && wave.length ? wave : [];
                ctx.strokeStyle = vis.foreColor !== "" ? vis.foreColor : "#00e000";
                ctx.lineWidth = 1;
                ctx.beginPath();
                if (pts.length === 0) {
                    for (var x = 0; x <= w; x += 2) {
                        var y = h / 2 + (vis.designMode ? Math.sin(x / w * Math.PI * 6) * h * 0.3 : 0);
                        if (x === 0) ctx.moveTo(x, y); else ctx.lineTo(x, y);
                    }
                } else {
                    for (var i = 0; i < pts.length; ++i) {
                        var px = i * w / (pts.length - 1), py = h / 2 - pts[i] * (h / 2 - 1);
                        if (i === 0) ctx.moveTo(px, py); else ctx.lineTo(px, py);
                    }
                }
                ctx.stroke();
            }
        }

        // Meter: how loud, across
        Item {
            visible: vis.style === "Meter"
            anchors.fill: parent
            anchors.margins: 1
            readonly property real level: vis.designMode ? 0.7 : (vis.analyzer ? vis.analyzer.level : 0)
            Item {
                width: Math.round(parent.width * parent.level)
                height: parent.height
                clip: true
                Rectangle {
                    width: parent.parent.width
                    height: parent.height
                    color: vis.foreColor !== "" ? vis.foreColor : "#00c000"
                    gradient: vis.foreColor !== "" ? null : meterAcross
                }
            }
        }
    }

    Gradient {
        id: meterColors
        GradientStop { position: 0.0; color: "#ff2000" }
        GradientStop { position: 0.3; color: "#ffd000" }
        GradientStop { position: 0.55; color: "#00e000" }
        GradientStop { position: 1.0; color: "#008000" }
    }
    Gradient {
        id: meterAcross
        orientation: Gradient.Horizontal
        GradientStop { position: 0.0; color: "#008000" }
        GradientStop { position: 0.45; color: "#00e000" }
        GradientStop { position: 0.7; color: "#ffd000" }
        GradientStop { position: 1.0; color: "#ff2000" }
    }

    MouseArea {
        anchors.fill: parent
        enabled: !vis.designMode
        onClicked: vis.clicked()
    }
}
