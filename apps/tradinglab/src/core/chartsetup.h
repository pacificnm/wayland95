#pragma once

// A strategy's chart setup <-> the chart settings. Port of renderer/src/chart/setup.ts.
//
// ChartSetup Rec (shared/strategies.ts): {range, interval, type?, studies: [{study, params?: {key: number}}]}
// ChartSettings / StudyInstance Recs: see chartsettings.h.

#include "core/util.h"

#include <QStringList>

namespace tl {

struct SetupResult
{
    Rec settings;
    QStringList dropped; // e.g. `study "foo"`, `sma parameter "len"`
};

// Applies a strategy's chart setup on top of the current settings. Unknown studies/parameters are dropped and reported.
// Each study becomes a new visible instance with a fresh uid (tl::uuid()).
SetupResult chartSettingsFromSetup(const Rec &current, const Rec &setup);

// The reverse: describe the chart the user has now, to save as a strategy's setup (visible studies only).
Rec setupFromSettings(const Rec &settings);

// {range: "1 Year", interval: "Day", studies: ["Moving Average (20)", ...]} for showing a setup to people (and Claude).
// Note: upstream listed param values in the stored object's key order; a QVariantMap is key-sorted, so here they
// follow the study's own parameter order (unknown keys last).
Rec describeSetup(const Rec &setup);

} // namespace tl
