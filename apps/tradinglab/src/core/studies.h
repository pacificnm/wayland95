#pragma once

// The chart's study registry (Moving averages, Bollinger, RSI, MACD...) and how each one is computed.
// Port of renderer/src/chart/studies.ts. Values follow indicators.h: NAN = no value (upstream null).
//
// Upstream params carry only {key, label, default, step?}: there are no min/max bounds. The only rule, enforced by
// the Claude tool and chartSettingsFromSetup, is that a parameter must be a number > 0.

#include "core/bar.h"
#include "core/indicators.h"

#include <QHash>
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>

#include <functional>
#include <optional>
#include <vector>

namespace tl {

struct StudyParam
{
    QString key;
    QString label;
    double def = 0;  // upstream `default`
    double step = 0; // 0 = not given (the input then steps by 1)
};

// the two colours a per-point colour array is made of (rising/falling), so each can be changed on its own
struct StudyTones { QString up, down; };

struct StudyOutput
{
    QString name;
    QString kind;  // "line" | "histogram" | "dots"
    QString color;
    ind::Series values;
    // per-point colours for histograms (e.g. volume up/down); empty = none
    QStringList colors;
    std::optional<StudyTones> tones;
    // plot offset in bars: +n draws n bars into the future, -n shifts back
    int shift = 0;
    // hold each value flat until the next one (pivot levels)
    bool steps = false;
};

// fill between two named outputs
struct StudyCloud { QString upper, lower, upColor, downColor; };

using StudyParams = QHash<QString, double>;

struct StudyDef
{
    QString id;
    QString name;
    QString category; // "Overlay" | "Volume" | "Momentum" | "Trend" | "Volatility"
    QString pane;     // "overlay" (on the price pane) | "separate" (its own pane below)
    std::vector<StudyParam> params;
    // horizontal reference lines in a separate pane
    std::vector<double> levels;
    std::optional<StudyCloud> cloud;
    // expects every param present (see fullParams)
    std::function<std::vector<StudyOutput>(const Bars &, const StudyParams &)> compute;
};

// STUDIES, in upstream order (the Add-study menu order)
const std::vector<StudyDef> &studies();
// nullptr when there is no such study (upstream's `find(...)!` assumed it existed)
const StudyDef *studyById(const QString &id);

// every parameter of the study: the given value when it is a number, else the default
// (Chart.tsx / tools.ts `inst.params[key] ?? default`)
StudyParams fullParams(const StudyDef &def, const QVariantMap &given);
// runs a study on bars with the given (possibly partial) params. Throws tl::Error for an unknown study id.
std::vector<StudyOutput> compute(const QString &studyId, const QVariantMap &params, const Bars &bars);

// the legend title Chart.tsx gives a study's first series, e.g. "SMA 20", "BB 20 2", "VWAP"
QString studyLabel(const StudyDef &def, const StudyParams &params);
// output names a colour override can use: "Name", or "Name:up" and "Name:down" for two-tone outputs
QStringList colorKeys(const StudyDef &def);

// For QML: [{id, name, category, pane, params: [{key, label, default, step?}], levels?: [..],
//   cloud?: {upper, lower, upColor, downColor}, outputs: [{name, kind, color, tones?: {up, down}, shift?, steps?}],
//   colorKeys: [..]}]  (outputs are the default-parameter outputs, as colors.ts studyOutputs finds them)
QVariantMap studyAsVariant(const StudyDef &def);
QVariantList studiesAsVariant();
// computed outputs as records; values become numbers or null, `colors` only when present
QVariantList outputsAsVariant(const std::vector<StudyOutput> &outputs, bool withValues = true);

} // namespace tl
