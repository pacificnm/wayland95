#include "core.h"

#include <QDateTime>
#include <QJSValue>
#include <QLocale>
#include <QTimeZone>

#include "core/barsvariant.h"
#include "core/calcstate.h"
#include "core/candlepatterns.h"
#include "core/chartsettings.h"
#include "core/chartsetup.h"
#include "core/fmphelpers.h"
#include "core/indicators.h"
#include "core/nytime.h"
#include "core/orderdraft.h"
#include "core/studies.h"
#include "core/studycolors.h"
#include "core/timeframe.h"
#include "core/trade.h"

using namespace tl;

namespace {
// a JS object or array handed to a QVariant parameter arrives as a QJSValue: make it plain maps and lists
QVariant plain(const QVariant &v) { return v.canConvert<QJSValue>() && v.metaType() == QMetaType::fromType<QJSValue>() ? v.value<QJSValue>().toVariant() : v; }
}

QVariantMap Core::analyze(const QVariantMap &spec, const QVariantMap &c) const
{
    AnalysisCtx ctx;
    ctx.last = num(c.value(QStringLiteral("last")));
    ctx.positionQty = c.value(QStringLiteral("positionQty")).toDouble();
    ctx.buyingPower = c.value(QStringLiteral("buyingPower")).toDouble();
    ctx.reserved = c.value(QStringLiteral("reserved")).toDouble();
    ctx.canShort = c.value(QStringLiteral("canShort"), true).toBool();
    return tl::analyze(OrderSpec::fromRec(spec), ctx).toRec();
}
QStringList Core::describeOrder(const QVariantMap &spec) const { return tl::describeOrder(OrderSpec::fromRec(spec)); }
QString Core::sideLabel(const QString &side) const { return tl::sideLabel(side); }
QString Core::typeLabel(const QString &type) const { return tl::typeLabel(type); }
QString Core::money(double n, int decimals) const { return tl::money(n, decimals); }
QString Core::grouped(double n, int decimals) const { return tl::grouped(n, decimals); }
QString Core::uuid() const { return tl::uuid(); }

QString Core::nyDate(double t) const { return tl::nyDate(qint64(t)); }
bool Core::isMarketOpen(double t) const { return tl::isMarketOpen(qint64(t)); }
bool Core::isExtendedHours(double t) const { return tl::isExtendedHours(qint64(t)); }
double Core::nyToEpoch(const QString &s) const { return double(tl::nyToEpoch(s)); }
QString Core::formatInZone(double t, const QString &zone, const QString &format) const
{
    const QTimeZone tz = zone.isEmpty() ? QTimeZone::systemTimeZone() : QTimeZone(zone.toUtf8());
    return QLocale(QLocale::English, QLocale::UnitedStates).toString(QDateTime::fromSecsSinceEpoch(qint64(t), tz.isValid() ? tz : QTimeZone::systemTimeZone()), format);
}

QVariantList Core::studies() const { return studiesAsVariant(); }
QVariantMap Core::study(const QString &id) const
{
    const StudyDef *d = studyById(id);
    return d ? studyAsVariant(*d) : QVariantMap();
}

QVariantList Core::computeStudy(const QVariantMap &inst, const QVariantList &all, const QVariantList &bars) const
{
    const QString id = inst.value(QStringLiteral("studyId")).toString();
    const StudyDef *def = studyById(id);
    if (!def)
        return {};
    std::vector<StudyOutput> outs;
    try {
        outs = tl::compute(id, inst.value(QStringLiteral("params")).toMap(), barsFromVariant(bars));
    } catch (const Error &) {
        return {};
    }
    const int copy = copyIndex(all, inst.value(QStringLiteral("uid")).toString());
    QVariantList out = outputsAsVariant(outs, true);
    for (int i = 0; i < int(outs.size()) && i < out.size(); ++i) {
        QVariantMap m = out[i].toMap();
        m.insert(QStringLiteral("color"), lineColor(outs[i], inst, copy, i));
        const QStringList pc = recolour(outs[i], inst);
        if (!pc.isEmpty())
            m.insert(QStringLiteral("colors"), pc);
        out[i] = m;
    }
    return out;
}

QString Core::studyLabel(const QString &studyId, const QVariantMap &params) const
{
    const StudyDef *d = studyById(studyId);
    return d ? tl::studyLabel(*d, fullParams(*d, params)) : studyId;
}

QVariantList Core::heikinAshi(const QVariantList &bars) const { return barsToVariant(ind::heikinAshi(barsFromVariant(bars))); }

QVariantList Core::intervals() const { return intervalsAsVariant(); }
QVariantList Core::ranges() const { return rangesAsVariant(); }
QVariantList Core::availableRanges() const { return rangesAsVariant(tl::availableRanges()); }
QVariantList Core::allowedIntervals(const QString &range) const { return intervalsAsVariant(tl::allowedIntervals(range)); }
bool Core::isAllowed(const QString &range, const QString &interval) const { return tl::isAllowed(range, interval); }
int Core::barCount(const QString &range, const QString &interval) const { return tl::barCount(range, interval); }
QVariantMap Core::usableTimeframe(const QString &range, const QString &interval) const
{
    const Timeframe t = tl::usableTimeframe(range, interval);
    return {{QStringLiteral("range"), t.range}, {QStringLiteral("interval"), t.interval}};
}
void Core::setBlockedIntervals(const QStringList &ids) const { tl::setBlockedIntervals(ids); }

QVariantMap Core::defaultChartSettings() const { return tl::defaultChartSettings(); }
QVariantMap Core::normalizeChartSettings(const QVariant &stored) const { return tl::normalizeChartSettings(plain(stored)); }
QVariantMap Core::newStudyInstance(const QString &studyId, const QVariantMap &params) const { return tl::newStudyInstance(studyId, params); }
QVariantMap Core::chartSettingsFromSetup(const QVariantMap &current, const QVariantMap &setup) const
{
    const SetupResult r = tl::chartSettingsFromSetup(current, setup);
    return {{QStringLiteral("settings"), r.settings}, {QStringLiteral("dropped"), r.dropped}};
}
QVariantMap Core::setupFromSettings(const QVariantMap &settings) const { return tl::setupFromSettings(settings); }
QVariantMap Core::describeSetup(const QVariantMap &setup) const { return tl::describeSetup(setup); }
QStringList Core::chartTypes() const { return tl::chartTypes(); }

QVariantMap Core::emptyDraft(const QString &side) const { return tl::emptyDraft(side); }
QVariantMap Core::draftToSpec(const QVariantMap &draft, const QString &symbol) const { return tl::draftToSpec(draft, symbol); }
QVariantMap Core::draftFromSpec(const QVariantMap &spec) const { return tl::draftFromSpec(spec); }
QVariantList Core::draftLines(const QVariantMap &draft, const QString &symbol, const QVariant &last) const { return tl::draftLines(draft, symbol, num(plain(last))); }
QVariantList Core::tradeLines(const QString &symbol, const QVariantList &orders, const QVariantList &positions) const { return tl::tradeLines(symbol, orders, positions); }

QVariantMap Core::defaultCalc() const { return tl::defaultCalc(); }
QVariantMap Core::normalizeCalc(const QVariant &stored) const { return tl::normalizeCalc(plain(stored)); }
QVariantMap Core::calcToInput(const QVariantMap &calc, const QVariantMap &c) const
{
    CalcContext ctx;
    ctx.last = num(c.value(QStringLiteral("last")));
    ctx.atr = num(c.value(QStringLiteral("atr")));
    ctx.equity = c.value(QStringLiteral("equity")).toDouble();
    ctx.buyingPower = num(c.value(QStringLiteral("buyingPower")));
    ctx.investedNow = c.value(QStringLiteral("investedNow")).toDouble();
    return tl::calcToInput(calc, ctx);
}
QVariant Core::latestAtr(const QVariantList &bars, int n) const { return orNull(tl::latestAtr(barsFromVariant(bars), n)); }

QStringList Core::patternIds() const { return tl::patternIds(); }
QVariantList Core::findPattern(const QString &id, const QVariantList &bars) const
{
    QVariantList out;
    for (int i : tl::findPattern(id, barsFromVariant(bars)))
        out.append(i);
    return out;
}
QVariantMap Core::countPatterns(const QVariantList &bars) const
{
    QVariantMap out;
    const auto counts = tl::countPatterns(barsFromVariant(bars));
    for (auto it = counts.cbegin(); it != counts.cend(); ++it)
        out.insert(it.key(), it.value());
    return out;
}

int Core::refreshEveryMs(const QString &interval) const { return tl::refreshEveryMs(interval); }
bool Core::sessionActive(double t) const { return tl::sessionActive(qint64(t)); }
QVariant Core::pick(const QVariantMap &o, const QStringList &names) const { return orNull(tl::pick(o, names)); }
QString Core::pickStr(const QVariantMap &o, const QStringList &names) const { return tl::pickStr(o, names); }
