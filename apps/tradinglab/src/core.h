#pragma once

// The pure helpers the React screens imported directly from shared/ and chart/ (order analysis, studies, timeframes,
// chart settings, the order-ticket draft, the calculator, candle patterns), callable from QML on the GUI thread.
// Bars cross over as JS arrays of {time, open, high, low, close, volume}.

#include <QObject>
#include <QVariant>

class Core : public QObject
{
    Q_OBJECT
public:
    using QObject::QObject;

    // ---- shared/trade.ts
    Q_INVOKABLE QVariantMap analyze(const QVariantMap &spec, const QVariantMap &ctx) const;
    Q_INVOKABLE QStringList describeOrder(const QVariantMap &spec) const;
    Q_INVOKABLE QString sideLabel(const QString &side) const;
    Q_INVOKABLE QString typeLabel(const QString &type) const;
    Q_INVOKABLE QString money(double n, int decimals = 2) const;
    Q_INVOKABLE QString grouped(double n, int decimals = 0) const;
    Q_INVOKABLE QString uuid() const;

    // ---- shared/nytime.ts
    Q_INVOKABLE QString nyDate(double epochSec) const;
    Q_INVOKABLE bool isMarketOpen(double epochSec) const;
    Q_INVOKABLE bool isExtendedHours(double epochSec) const;
    Q_INVOKABLE double nyToEpoch(const QString &s) const;
    // epoch seconds in an IANA zone ("" = this computer's), with a QDateTime format string (display.ts)
    Q_INVOKABLE QString formatInZone(double epochSec, const QString &zone, const QString &format) const;

    // ---- chart/studies.ts, chart/colors.ts
    Q_INVOKABLE QVariantList studies() const;
    Q_INVOKABLE QVariantMap study(const QString &id) const;
    // [{name, kind, color, values, colors?, tones?, shift?, steps?}] for one instance {uid, studyId, params, colors?, visible}
    Q_INVOKABLE QVariantList computeStudy(const QVariantMap &instance, const QVariantList &studiesInSettings, const QVariantList &bars) const;
    Q_INVOKABLE QString studyLabel(const QString &studyId, const QVariantMap &params) const;
    Q_INVOKABLE QVariantList heikinAshi(const QVariantList &bars) const;

    // ---- chart/timeframe.ts
    Q_INVOKABLE QVariantList intervals() const;
    Q_INVOKABLE QVariantList ranges() const;
    Q_INVOKABLE QVariantList availableRanges() const;
    Q_INVOKABLE QVariantList allowedIntervals(const QString &range) const;
    Q_INVOKABLE bool isAllowed(const QString &range, const QString &interval) const;
    Q_INVOKABLE int barCount(const QString &range, const QString &interval) const;
    Q_INVOKABLE QVariantMap usableTimeframe(const QString &range, const QString &interval) const;
    Q_INVOKABLE void setBlockedIntervals(const QStringList &ids) const;

    // ---- chart/settings.ts, chart/setup.ts
    Q_INVOKABLE QVariantMap defaultChartSettings() const;
    Q_INVOKABLE QVariantMap normalizeChartSettings(const QVariant &stored) const;
    Q_INVOKABLE QVariantMap newStudyInstance(const QString &studyId, const QVariantMap &params = {}) const;
    // {settings, dropped}
    Q_INVOKABLE QVariantMap chartSettingsFromSetup(const QVariantMap &current, const QVariantMap &setup) const;
    Q_INVOKABLE QVariantMap setupFromSettings(const QVariantMap &settings) const;
    Q_INVOKABLE QVariantMap describeSetup(const QVariantMap &setup) const;
    Q_INVOKABLE QStringList chartTypes() const;

    // ---- chart/orderDraft.ts
    Q_INVOKABLE QVariantMap emptyDraft(const QString &side = QStringLiteral("buy")) const;
    Q_INVOKABLE QVariantMap draftToSpec(const QVariantMap &draft, const QString &symbol) const;
    Q_INVOKABLE QVariantMap draftFromSpec(const QVariantMap &spec) const;
    Q_INVOKABLE QVariantList draftLines(const QVariantMap &draft, const QString &symbol, const QVariant &last) const;
    Q_INVOKABLE QVariantList tradeLines(const QString &symbol, const QVariantList &orders, const QVariantList &positions) const;

    // ---- chart/calcState.ts
    Q_INVOKABLE QVariantMap defaultCalc() const;
    Q_INVOKABLE QVariantMap normalizeCalc(const QVariant &stored) const;
    // ctx {last?, atr?, equity, buyingPower?, investedNow}
    Q_INVOKABLE QVariantMap calcToInput(const QVariantMap &calc, const QVariantMap &ctx) const;
    Q_INVOKABLE QVariant latestAtr(const QVariantList &bars, int n = 14) const;

    // ---- shared/candlePatterns.ts (teaching text: content patterns.json)
    Q_INVOKABLE QStringList patternIds() const;
    Q_INVOKABLE QVariantList findPattern(const QString &id, const QVariantList &bars) const;
    Q_INVOKABLE QVariantMap countPatterns(const QVariantList &bars) const;

    // ---- data/fmp.ts
    Q_INVOKABLE int refreshEveryMs(const QString &interval) const;
    Q_INVOKABLE bool sessionActive(double epochSec) const;
    Q_INVOKABLE QVariant pick(const QVariantMap &o, const QStringList &names) const;
    Q_INVOKABLE QString pickStr(const QVariantMap &o, const QStringList &names) const;

    // ---- shared/position.ts, shared/portfolio.ts, shared/options.ts (added in core_more.cpp)
    Q_INVOKABLE QVariant callPure(const QString &name, const QVariantList &args) const;
};
