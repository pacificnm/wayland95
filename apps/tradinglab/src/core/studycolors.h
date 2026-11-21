#pragma once

// Which colour each study output is drawn in: user overrides, copy-specific defaults, two-tone outputs.
// Port of renderer/src/chart/colors.ts. A study instance is a Rec {uid, studyId, params, visible, colors?}
// (see chartsettings.h); `colors` maps output names ("SMA", "Upper"...) or "Name:up"/"Name:down" to "#rrggbb".

#include "core/studies.h"
#include "core/util.h"

#include <QStringList>

#include <optional>
#include <vector>

namespace tl {

// Colours given to the 2nd, 3rd... copy of the same study (an SMA 50 and an SMA 200 should not look alike).
const QStringList &dupPalette();

// The outputs (names and default colours) a study draws, found by running it once on dummy data with default params.
const std::vector<StudyOutput> &studyOutputs(const StudyDef &def);

// Which copy of its study this instance is (0 for the first). `studies` is the settings' list of instance Recs.
int copyIndex(const List &studies, const QString &uid);

// The colour actually drawn for one output: the user's override, else a copy-specific default, else the study's own.
QString lineColor(const StudyOutput &out, const Rec &inst, int copy, int outIndex);

// Up and down colours of a two-tone output (histogram bars, Supertrend), with overrides applied. Alpha of the default is kept.
std::optional<StudyTones> toneColors(const StudyOutput &out, const Rec &inst);

// Recolour a per-point colour array using the two tones (unchanged when the output has no tones).
QStringList recolour(const StudyOutput &out, const Rec &inst);

// upstream defOf: same as studyById
inline const StudyDef *defOf(const QString &id) { return studyById(id); }

// a "#rrggbb" string
bool isHex(const QVariant &v);

} // namespace tl
