// Position calculator state. Port of renderer/src/chart/calcState.ts.

#include "calcstate.h"

#include "core/indicators.h"
#include "core/jsnum.h"

#include <algorithm>
#include <cmath>

namespace tl {

Rec defaultCalc()
{
    return Rec{
        {QStringLiteral("symbol"), QString()}, {QStringLiteral("side"), QStringLiteral("long")}, {QStringLiteral("followLive"), true},
        {QStringLiteral("entry"), QString()}, {QStringLiteral("accountBasis"), QStringLiteral("paper")}, {QStringLiteral("customAccount"), QStringLiteral("100000")},
        {QStringLiteral("riskMode"), QStringLiteral("percent")}, {QStringLiteral("riskPct"), QStringLiteral("1")}, {QStringLiteral("riskDollars"), QStringLiteral("500")},
        {QStringLiteral("stopMode"), QStringLiteral("atr")}, {QStringLiteral("stopPrice"), QString()}, {QStringLiteral("stopPct"), QStringLiteral("2")},
        {QStringLiteral("stopAtr"), QStringLiteral("1.5")}, {QStringLiteral("targetMode"), QStringLiteral("r")}, {QStringLiteral("targetPrice"), QString()},
        {QStringLiteral("targetPct"), QStringLiteral("6")}, {QStringLiteral("targetR"), QStringLiteral("2")}, {QStringLiteral("winRate"), QStringLiteral("50")},
        {QStringLiteral("commission"), QStringLiteral("0")},
    };
}

Rec normalizeCalc(const QVariant &stored)
{
    Rec r = defaultCalc();
    if (stored.metaType().id() != QMetaType::QVariantMap)
        return r;
    const Rec s = stored.toMap();
    for (auto it = s.cbegin(); it != s.cend(); ++it)
        r.insert(it.key(), it.value());
    return r;
}

// parseFloat, NaN unless finite
static double n(const QVariant &s)
{
    const double v = jsParseFloat(s.toString());
    return std::isfinite(v) ? v : NAN;
}

static Rec spec(const char *mode, const char *key, double v)
{
    return Rec{{QStringLiteral("mode"), QLatin1String(mode)}, {QLatin1String(key), v}};
}

Rec calcToInput(const Rec &c, const CalcContext &ctx)
{
    auto s = [&](const char *k) { return c.value(QLatin1String(k)).toString(); };
    auto v = [&](const char *k) { return c.value(QLatin1String(k)); };
    const double entry = c.value(QStringLiteral("followLive")).toBool() ? ctx.last.value_or(NAN) : n(v("entry"));
    const QString stopMode = s("stopMode"), targetMode = s("targetMode");
    const Rec stop = stopMode == QLatin1String("price") ? spec("price", "price", n(v("stopPrice")))
                   : stopMode == QLatin1String("percent") ? spec("percent", "pct", n(v("stopPct")))
                                                          : spec("atr", "mult", n(v("stopAtr")));
    const QVariant target = targetMode == QLatin1String("none") ? null()
                          : targetMode == QLatin1String("price") ? QVariant(spec("price", "price", n(v("targetPrice"))))
                          : targetMode == QLatin1String("percent") ? QVariant(spec("percent", "pct", n(v("targetPct"))))
                                                                   : QVariant(spec("r", "r", n(v("targetR"))));
    const Rec risk = s("riskMode") == QLatin1String("percent") ? spec("percent", "pct", n(v("riskPct"))) : spec("dollars", "dollars", n(v("riskDollars")));
    const bool custom = s("accountBasis") == QLatin1String("custom");
    const double wr = n(v("winRate"));
    const double comm = n(v("commission"));
    return Rec{
        {QStringLiteral("side"), s("side")},
        {QStringLiteral("entry"), entry},
        {QStringLiteral("stop"), stop},
        {QStringLiteral("target"), target},
        {QStringLiteral("risk"), risk},
        {QStringLiteral("account"), custom ? n(v("customAccount")) : ctx.equity},
        {QStringLiteral("buyingPower"), custom ? null() : orNull(ctx.buyingPower)},
        {QStringLiteral("investedNow"), ctx.investedNow},
        {QStringLiteral("atr"), orNull(ctx.atr)},
        {QStringLiteral("winRate"), std::isfinite(wr) ? QVariant(std::max(0.0, std::min(100.0, wr)) / 100) : null()},
        {QStringLiteral("commission"), std::max(0.0, (std::isnan(comm) || comm == 0) ? 0.0 : comm)}, // Math.max(0, n(..) || 0)
    };
}

std::optional<double> latestAtr(const Bars &bars, int n)
{
    const ind::Series a = ind::atr(bars, n);
    for (auto it = a.rbegin(); it != a.rend(); ++it)
        if (ind::has(*it))
            return *it;
    return std::nullopt;
}

} // namespace tl
