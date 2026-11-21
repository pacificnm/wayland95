#pragma once

// Chart settings as the chart screen keeps them (saved under the "chart" setting). Port of renderer/src/chart/settings.ts.
//
// ChartSettings Rec: {range, interval, type, scale, grid, crosshair, upColor, downColor, studies}
//   type:      "candles" | "hollow" | "bars" | "line" | "area" | "heikin"
//   scale:     "normal" | "log" | "percent"
//   crosshair: "normal" | "magnet"
//   studies:   [StudyInstance]
// StudyInstance Rec: {uid, studyId, params: {key: number}, visible, colors?: {"SMA": "#rrggbb", "Volume:up": ...}}
//   colors are overrides by output name ("SMA", "Upper", "MACD"...); two-tone outputs use "Name:up" and "Name:down"

#include "core/util.h"

#include <QStringList>

namespace tl {

const QStringList &chartTypes();     // candles, hollow, bars, line, area, heikin
const QStringList &scaleModes();     // normal, log, percent
const QStringList &crosshairModes(); // normal, magnet

// DEFAULT_CHART_SETTINGS: 1Y of daily candles, grid on, volume pane
Rec defaultChartSettings();
// what App.tsx did with the saved value: `s && {...DEFAULT_CHART_SETTINGS, ...s}` (a missing/non-object value gives the defaults)
Rec normalizeChartSettings(const QVariant &stored);
// a new visible instance of a study, as the Add-study menu creates it: {uid: uuid(), studyId, params, visible: true}
Rec newStudyInstance(const QString &studyId, const Rec &params = {});

} // namespace tl
