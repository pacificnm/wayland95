#pragma once

// What an FMP plan includes, learned from the connection test. Everything here is pure so the data layer (tool list,
// order-fill bars) and the screens (menus, ribbon) agree. A capability that has not been checked counts as available.
// Port of shared/fmpCaps.ts, plus readCaps/saveCaps from main/capsStore.ts.

#include "util.h"

#include <QMap>
#include <QString>
#include <QStringList>

#include <optional>
#include <vector>

namespace tl {

// CapId is a plain string here: quotes, indices, daily, bars1min, bars5min, bars15min, bars30min, bars1hour, bars4hour,
// search, news, marketNews, profile, analyst, estimates, aftermarket, sectors, movers, screener, congress, fundamentals
struct CapDef
{
    QString id;
    QString label;
    QString lost; // what stops working without it
};

extern const std::vector<CapDef> CAPS;
QString capLabel(const QString &id);

// True when every capability in `needs` is missing, so a feature that works with any one of them stays visible if one survives.
bool missingAll(const QStringList &unavailable, const QStringList &needs);

// Ribbon screens and symbol tabs that are hidden when the plan lacks everything they need.
extern const QMap<QString, QStringList> SCREEN_CAPS;
// Claude tools that need market data the plan may not include. Tools not listed here always stay.
extern const QMap<QString, QStringList> TOOL_CAPS;
// chart interval -> capability (1day has none: value() returns an empty string)
extern const QMap<QString, QString> INTERVAL_CAP;

// ---- order fills ----

struct FillGrain
{
    QString interval;  // the FMP interval whose bars are replayed: 1min | 5min | 15min | 30min | 1hour
    int seconds = 60;
    int offset = 0;    // seconds past the epoch-aligned boundary at which bars start (hourly bars open at :30)
    QString label;
};
// {interval, seconds, offset, label}
Rec toRec(const FillGrain &g);

// The finest bars the plan can supply for filling working orders, or nullopt when it has no intraday bars at all.
std::optional<FillGrain> fillGrain(const QStringList &unavailable);

// First bar boundary at or after time `t` for bars of `g` (a working order can only use bars that start after it was placed).
qint64 nextBar(qint64 t, const FillGrain &g);

// Added to Claude's instructions so it does not try, or promise, data the user's plan does not include. Empty when nothing is missing.
QString planNote(const QStringList &unavailable);

// ---- the stored result (main/capsStore.ts) ----

// The last plan check, FmpCaps {checkedAt (UTC seconds), unavailable: [CapId]}, kept in the settings table under 'fmpCaps'
// so every part of the app (data, orders, chat) reads the same answer. nullopt when never checked.
std::optional<Rec> readCaps();
// nullopt deletes the row
void saveCaps(const std::optional<Rec> &caps);
// caps->unavailable as a string list (empty when nothing was checked)
QStringList unavailableOf(const std::optional<Rec> &caps);

} // namespace tl
