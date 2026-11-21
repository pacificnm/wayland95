// Core::callPure: the pure functions of shared/position.ts, shared/portfolio.ts, shared/options.ts, shared/accounts.ts
// and the screener, congress and market helpers, by their upstream names, for the screens and Claude's tools.
#include "core.h"

#include "core/accountsinfo.h"
#include "core/barsvariant.h"
#include "core/congresslogic.h"
#include "core/fmphelpers.h"
#include "core/marketlogic.h"
#include "core/optionsmath.h"
#include "core/portfolio.h"
#include "core/position.h"
#include "core/sampledata.h"
#include "core/screenerlogic.h"
#include "core/studycolors.h"
#include "core/util.h"

#include <QJSValue>

#include <functional>

using namespace tl;

namespace {
using Fn = std::function<QVariant(const QVariantList &)>;
Rec r(const QVariantList &a, int i) { return a.value(i).toMap(); }
List l(const QVariantList &a, int i) { return a.value(i).toList(); }
double d(const QVariantList &a, int i) { return a.value(i).toDouble(); }

const QHash<QString, Fn> &table()
{
    static const QHash<QString, Fn> t{
        // shared/position.ts
        {QStringLiteral("defaultRules"), [](const QVariantList &) { return QVariant(defaultRulesRec()); }},
        {QStringLiteral("calculate"), [](const QVariantList &a) { return QVariant(calculate(r(a, 0), Rules::fromRec(r(a, 1)))); }},
        // shared/portfolio.ts
        {QStringLiteral("kinds"), [](const QVariantList &) { return QVariant(kinds()); }},
        {QStringLiteral("guessKind"), [](const QVariantList &a) { return QVariant(guessKind(a.value(0).toString())); }},
        {QStringLiteral("priceOf"), [](const QVariantList &a) { return priceOf(r(a, 0), r(a, 1)); }},
        {QStringLiteral("planPortfolio"), [](const QVariantList &a) { return QVariant(planPortfolio(Pf::fromRec(r(a, 0)), pricesFromRec(r(a, 1)))); }},
        {QStringLiteral("summarizeHoldings"), [](const QVariantList &a) { return QVariant(summarizeHoldings(l(a, 0), r(a, 1))); }},
        {QStringLiteral("equalWeights"), [](const QVariantList &a) { return QVariant(equalWeightsList(a.value(0).toInt())); }},
        {QStringLiteral("normalizeWeights"), [](const QVariantList &a) { return QVariant(normalizeWeights(l(a, 0))); }},
        {QStringLiteral("effectivePortfolio"), [](const QVariantList &a) { return QVariant(effectivePortfolio(r(a, 0), a.value(1), r(a, 2))); }},
        {QStringLiteral("planSell"), [](const QVariantList &a) { return QVariant(planSell(Pf::fromRec(r(a, 0)), pricesFromRec(r(a, 1)), SellOptions::fromRec(r(a, 2)))); }},
        {QStringLiteral("isSymbol"), [](const QVariantList &a) { return QVariant(symbolRe().match(a.value(0).toString()).hasMatch()); }},
        // shared/options.ts
        {QStringLiteral("optionMid"), [](const QVariantList &a) { return QVariant(optionMid(r(a, 0))); }},
        {QStringLiteral("contractsFor"), [](const QVariantList &a) { return QVariant(contractsFor(r(a, 0), a.value(1).toString())); }},
        {QStringLiteral("atmStrike"), [](const QVariantList &a) { return orNull(atmStrike(l(a, 0), d(a, 1))); }},
        {QStringLiteral("atmIv"), [](const QVariantList &a) { return orNull(atmIv(l(a, 0), d(a, 1))); }},
        {QStringLiteral("straddle"), [](const QVariantList &a) { return straddle(l(a, 0), d(a, 1)); }},
        {QStringLiteral("expectedMove"), [](const QVariantList &a) { return QVariant(expectedMove(d(a, 0), d(a, 1), d(a, 2))); }},
        {QStringLiteral("activity"), [](const QVariantList &a) { return QVariant(activity(l(a, 0))); }},
        {QStringLiteral("maxPain"), [](const QVariantList &a) { return orNull(maxPain(l(a, 0))); }},
        {QStringLiteral("termStructure"), [](const QVariantList &a) { return QVariant(termStructure(r(a, 0))); }},
        {QStringLiteral("strikeRows"), [](const QVariantList &a) {
             return QVariant(strikeRows(l(a, 0), d(a, 1), isNumber(a.value(2)) ? std::optional<int>(a.value(2).toInt()) : std::nullopt)); }},
        {QStringLiteral("historicalVol"), [](const QVariantList &a) {
             QVector<double> c;
             for (const QVariant &v : l(a, 0)) c.append(v.toDouble());
             return orNull(historicalVol(c, a.value(1, 30).toInt())); }},
        {QStringLiteral("contractLabel"), [](const QVariantList &a) { return QVariant(contractLabel(r(a, 0))); }},
        // shared/accounts.ts
        {QStringLiteral("accountTypeLabels"), [](const QVariantList &) { return QVariant(accountTypeLabels()); }},
        {QStringLiteral("brokers"), [](const QVariantList &) { return QVariant(brokers()); }},
        {QStringLiteral("checkAccountInput"), [](const QVariantList &a) { return QVariant(checkAccountInput(r(a, 0))); }},
        // data/screener.ts
        {QStringLiteral("defaultScreenerFilters"), [](const QVariantList &) { return QVariant(defaultScreenerFilters()); }},
        {QStringLiteral("hasClientFilters"), [](const QVariantList &a) { return QVariant(hasClientFilters(r(a, 0))); }},
        {QStringLiteral("toScreenerQuery"), [](const QVariantList &a) { return QVariant(toScreenerQuery(r(a, 0))); }},
        {QStringLiteral("relVolume"), [](const QVariantList &a) { return orNull(relVolume(r(a, 0))); }},
        {QStringLiteral("dividendYield"), [](const QVariantList &a) { return orNull(dividendYield(r(a, 0))); }},
        {QStringLiteral("applyClientFilters"), [](const QVariantList &a) { return QVariant(applyClientFilters(l(a, 0), r(a, 1))); }},
        {QStringLiteral("sortScreenerRows"), [](const QVariantList &a) { return QVariant(sortScreenerRows(l(a, 0), a.value(1).toString(), a.value(2).toInt())); }},
        {QStringLiteral("presetFilters"), [](const QVariantList &a) { return QVariant(presetFilters(r(a, 0))); }},
        {QStringLiteral("summarizeScreener"), [](const QVariantList &a) { return QVariant(summarizeScreener(l(a, 0))); }},
        {QStringLiteral("screenerCsv"), [](const QVariantList &a) { return QVariant(screenerCsv(l(a, 0))); }},
        {QStringLiteral("screenerToolArgs"), [](const QVariantList &a) { return QVariant(screenerToolArgs(r(a, 0))); }},
        // data/congress.ts
        {QStringLiteral("delayDays"), [](const QVariantList &a) { return orNull(delayDays(r(a, 0))); }},
        {QStringLiteral("isCongressBuy"), [](const QVariantList &a) { return QVariant(isCongressBuy(r(a, 0))); }},
        {QStringLiteral("isCongressSell"), [](const QVariantList &a) { return QVariant(isCongressSell(r(a, 0))); }},
        {QStringLiteral("summarizeCongress"), [](const QVariantList &a) { return QVariant(summarizeCongress(l(a, 0))); }},
        {QStringLiteral("defaultTradeFilter"), [](const QVariantList &) { return QVariant(defaultTradeFilter()); }},
        {QStringLiteral("filterTrades"), [](const QVariantList &a) { return QVariant(filterTrades(l(a, 0), r(a, 1))); }},
        {QStringLiteral("groupMembers"), [](const QVariantList &a) { return QVariant(groupMembers(l(a, 0))); }},
        // data/market.ts
        {QStringLiteral("buildMarket"), [](const QVariantList &a) { return QVariant(buildMarket(r(a, 0))); }},
        {QStringLiteral("defaultMoverFilter"), [](const QVariantList &) { return QVariant(defaultMoverFilter()); }},
        {QStringLiteral("isFundMover"), [](const QVariantList &a) { return QVariant(isFundMover(r(a, 0))); }},
        {QStringLiteral("filterMovers"), [](const QVariantList &a) { return QVariant(filterMovers(l(a, 0), r(a, 1), a.value(2, 12).toInt())); }},
        // data/fmp.ts, data/candles.ts, chart/useChartData.ts (ChartData.qml)
        {QStringLiteral("fetchInterval"), [](const QVariantList &a) { return QVariant(fetchInterval(a.value(0).toString())); }},
        {QStringLiteral("loadFromDay"), [](const QVariantList &a) { return QVariant(loadFromDay(a.value(0).toString(), a.value(1).toString())); }},
        {QStringLiteral("lastBars"), [](const QVariantList &a) { return QVariant(barsToVariant(lastBars(barsFromVariant(l(a, 0)), a.value(1).toString(), a.value(2).toString()))); }},
        {QStringLiteral("refreshFromDay"), [](const QVariantList &a) { return QVariant(refreshFromDay(barsFromVariant(l(a, 0)))); }},
        {QStringLiteral("mergeRefreshed"), [](const QVariantList &a) {
             return QVariant(barsToVariant(mergeRefreshed(barsFromVariant(l(a, 0)), barsFromVariant(l(a, 1)), a.value(2).toString(), a.value(3).toString()))); }},
        {QStringLiteral("sameCandles"), [](const QVariantList &a) { return QVariant(sameCandles(barsFromVariant(l(a, 0)), barsFromVariant(l(a, 1)))); }},
        {QStringLiteral("chartKey"), [](const QVariantList &a) { return QVariant(chartKey(a.value(0).toString(), a.value(1).toString(), a.value(2).toString())); }},
        {QStringLiteral("aggregate"), [](const QVariantList &a) { return QVariant(barsToVariant(aggregate(barsFromVariant(l(a, 0)), a.value(1).toString()))); }},
        // ChartSettingsView.tsx: the colour swatches of one study instance [{key, label, value, custom}]
        {QStringLiteral("studySwatches"), [](const QVariantList &a) {
             const Rec inst = r(a, 0);
             List out;
             const StudyDef *def = studyById(inst.value(QStringLiteral("studyId")).toString());
             if (!def)
                 return QVariant(out);
             const Rec custom = inst.value(QStringLiteral("colors")).toMap();
             const int copy = copyIndex(l(a, 1), inst.value(QStringLiteral("uid")).toString());
             const auto one = [&](const QString &key, const QString &label, const QString &value) {
                 out << Rec{{QStringLiteral("key"), key}, {QStringLiteral("label"), label}, {QStringLiteral("value"), value.left(7)},
                            {QStringLiteral("custom"), custom.contains(key)}};
             };
             int oi = 0;
             for (const StudyOutput &o : studyOutputs(*def)) {
                 if (const auto t = toneColors(o, inst)) {
                     one(o.name + QStringLiteral(":up"), o.name + QStringLiteral(" (rising)"), t->up);
                     one(o.name + QStringLiteral(":down"), o.name + QStringLiteral(" (falling)"), t->down);
                 } else {
                     one(o.name, o.name, lineColor(o, inst, copy, oi));
                 }
                 ++oi;
             }
             return QVariant(out); }},
        {QStringLiteral("sampleCandles"), [](const QVariantList &a) {
             return QVariant(barsToVariant(sampleCandles(a.value(0).toString(), a.value(1).toString(), a.value(2).toString()))); }},
    };
    return t;
}
}

QVariant Core::callPure(const QString &name, const QVariantList &args) const
{
    const auto it = table().constFind(name);
    if (it == table().constEnd()) {
        qWarning("Trading Lab: unknown pure function %s", qPrintable(name));
        return tl::null();
    }
    QVariantList plainArgs;
    for (const QVariant &a : args)
        plainArgs.append(a.metaType() == QMetaType::fromType<QJSValue>() ? a.value<QJSValue>().toVariant() : a);
    try {
        return (*it)(plainArgs);
    } catch (const tl::Error &e) {
        qWarning("Trading Lab: %s: %s", qPrintable(name), qPrintable(e.message()));
        return tl::null();
    }
}
