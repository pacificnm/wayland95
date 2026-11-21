#pragma once

// Order model and pre-trade analysis shared by the engine and the order ticket. Port of shared/trade.ts.
// An OrderSpec arrives from QML (or a Claude-prepared ticket) as a Rec with upstream's field names:
// {symbol, side, qty, type, limit?, stop?, trailAmount?, trailUnit?, tif, strategy, target?, protect?{type,stop?,limit?,trailAmount?,trailUnit?}, source?, note?}

#include "util.h"

#include <optional>

namespace tl {

struct StopLeg
{
    QString type; // stop | stop_limit | trailing_stop
    QVariant stop, limit, trailAmount; // numbers or null (as given: validity is checked by analyze)
    QString trailUnit; // "$" | "%" | ""
};

struct OrderSpec
{
    QString symbol;
    QString side; // buy | sell | sell_short | buy_to_cover
    QVariant qty;
    QString type; // market | limit | stop | stop_limit | trailing_stop
    QVariant limit, stop, trailAmount;
    QString trailUnit;
    QString tif; // day | gtc
    QString strategy; // single | bracket | oco
    QVariant target;
    std::optional<StopLeg> protect;
    QString source; // user | claude
    QString note;

    static OrderSpec fromRec(const Rec &r);
    Rec toRec() const;
    int qtyInt() const { return qty.toInt(); }
};

struct AnalysisCtx
{
    std::optional<double> last;
    // signed shares currently held in this symbol (negative = short)
    double positionQty = 0;
    double buyingPower = 0;
    // notional already committed to other working orders that open positions (leave 0 when buyingPower is already net of them)
    double reserved = 0;
    // false for cash accounts
    bool canShort = true;
};

struct Analysis
{
    QStringList errors, warnings;
    std::optional<double> entry, notional, stopPrice, targetPrice, risk, reward, rr;
    Rec toRec() const;
};

QString sideLabel(const QString &side);
QString typeLabel(const QString &type);
inline bool isBuy(const QString &s) { return s == QLatin1String("buy") || s == QLatin1String("buy_to_cover"); }
inline bool opensPosition(const QString &s) { return s == QLatin1String("buy") || s == QLatin1String("sell_short"); }
// the side that closes a position opened with `s`
QString exitSide(const QString &s);

// Validates an order and estimates cost and risk. `errors` block submission, `warnings` don't.
Analysis analyze(const OrderSpec &spec, const AnalysisCtx &ctx);
// Confirmation-style text, one line per leg, like a broker's order confirmation.
QStringList describeOrder(const OrderSpec &spec);

} // namespace tl
