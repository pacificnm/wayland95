// Port of shared/trade.ts.
#include "trade.h"

#include <cmath>

namespace tl {

namespace {
const QString S_SYMBOL = QStringLiteral("symbol");

QString px(double n) { return fixed(n, 2); }
bool pos(const QVariant &v) { return isPos(v); }
double d(const QVariant &v) { return v.toDouble(); }

std::optional<double> trailDistance(const QVariant &amount, const QString &unit, double ref)
{
    if (!pos(amount))
        return std::nullopt;
    return unit == QLatin1String("%") ? (ref * d(amount)) / 100 : d(amount);
}

// undefined stays an invalid QVariant (upstream's `?:` fields); explicit null too
QVariant field(const Rec &r, const QString &k) { return isNull(r.value(k)) ? QVariant() : r.value(k); }
}

OrderSpec OrderSpec::fromRec(const Rec &r)
{
    OrderSpec s;
    s.symbol = r.value(S_SYMBOL).toString();
    s.side = r.value(QStringLiteral("side")).toString();
    s.qty = field(r, QStringLiteral("qty"));
    s.type = r.value(QStringLiteral("type")).toString();
    s.limit = field(r, QStringLiteral("limit"));
    s.stop = field(r, QStringLiteral("stop"));
    s.trailAmount = field(r, QStringLiteral("trailAmount"));
    s.trailUnit = r.value(QStringLiteral("trailUnit")).toString();
    s.tif = r.value(QStringLiteral("tif")).toString();
    s.strategy = r.value(QStringLiteral("strategy"), QStringLiteral("single")).toString();
    s.target = field(r, QStringLiteral("target"));
    const QVariant p = r.value(QStringLiteral("protect"));
    if (p.metaType().id() == QMetaType::QVariantMap) {
        const Rec pr = p.toMap();
        s.protect = StopLeg{pr.value(QStringLiteral("type")).toString(), field(pr, QStringLiteral("stop")), field(pr, QStringLiteral("limit")),
                            field(pr, QStringLiteral("trailAmount")), pr.value(QStringLiteral("trailUnit")).toString()};
    }
    s.source = r.value(QStringLiteral("source")).toString();
    s.note = r.value(QStringLiteral("note")).toString();
    return s;
}

Rec OrderSpec::toRec() const
{
    Rec r{{S_SYMBOL, symbol}, {QStringLiteral("side"), side}, {QStringLiteral("qty"), qty}, {QStringLiteral("type"), type},
          {QStringLiteral("tif"), tif}, {QStringLiteral("strategy"), strategy}};
    auto put = [&r](const char *k, const QVariant &v) { if (v.isValid()) r.insert(QLatin1String(k), v); };
    put("limit", limit);
    put("stop", stop);
    put("trailAmount", trailAmount);
    if (!trailUnit.isEmpty()) r.insert(QStringLiteral("trailUnit"), trailUnit);
    put("target", target);
    if (protect) {
        Rec p{{QStringLiteral("type"), protect->type}};
        if (protect->stop.isValid()) p.insert(QStringLiteral("stop"), protect->stop);
        if (protect->limit.isValid()) p.insert(QStringLiteral("limit"), protect->limit);
        if (protect->trailAmount.isValid()) p.insert(QStringLiteral("trailAmount"), protect->trailAmount);
        if (!protect->trailUnit.isEmpty()) p.insert(QStringLiteral("trailUnit"), protect->trailUnit);
        r.insert(QStringLiteral("protect"), p);
    }
    if (!source.isEmpty()) r.insert(QStringLiteral("source"), source);
    if (!note.isEmpty()) r.insert(QStringLiteral("note"), note);
    return r;
}

Rec Analysis::toRec() const
{
    return Rec{{QStringLiteral("errors"), errors}, {QStringLiteral("warnings"), warnings},
               {QStringLiteral("entry"), orNull(entry)}, {QStringLiteral("notional"), orNull(notional)},
               {QStringLiteral("stopPrice"), orNull(stopPrice)}, {QStringLiteral("targetPrice"), orNull(targetPrice)},
               {QStringLiteral("risk"), orNull(risk)}, {QStringLiteral("reward"), orNull(reward)}, {QStringLiteral("rr"), orNull(rr)}};
}

QString sideLabel(const QString &side)
{
    if (side == QLatin1String("buy")) return QStringLiteral("Buy");
    if (side == QLatin1String("sell")) return QStringLiteral("Sell");
    if (side == QLatin1String("sell_short")) return QStringLiteral("Sell short");
    if (side == QLatin1String("buy_to_cover")) return QStringLiteral("Buy to cover");
    return side;
}

QString typeLabel(const QString &type)
{
    if (type == QLatin1String("market")) return QStringLiteral("Market");
    if (type == QLatin1String("limit")) return QStringLiteral("Limit");
    if (type == QLatin1String("stop")) return QStringLiteral("Stop");
    if (type == QLatin1String("stop_limit")) return QStringLiteral("Stop limit");
    if (type == QLatin1String("trailing_stop")) return QStringLiteral("Trailing stop");
    return type;
}

QString exitSide(const QString &s)
{
    if (s == QLatin1String("buy")) return QStringLiteral("sell");
    if (s == QLatin1String("sell_short")) return QStringLiteral("buy_to_cover");
    return s;
}

Analysis analyze(const OrderSpec &spec, const AnalysisCtx &ctx)
{
    Analysis a;
    QStringList &errors = a.errors, &warnings = a.warnings;
    const QString &side = spec.side, &type = spec.type;
    const bool buy = isBuy(side);
    const bool qtyOk = isInteger(spec.qty) && spec.qty.toDouble() >= 1;
    const double qty = spec.qty.toDouble();

    if (spec.symbol.isEmpty()) errors << QStringLiteral("Symbol is required.");
    if (!qtyOk) errors << QStringLiteral("Quantity must be a whole number of shares, 1 or more.");
    if ((type == QLatin1String("limit") || type == QLatin1String("stop_limit")) && !pos(spec.limit)) errors << QStringLiteral("Enter a limit price.");
    if ((type == QLatin1String("stop") || type == QLatin1String("stop_limit")) && !pos(spec.stop)) errors << QStringLiteral("Enter a stop price.");
    if (type == QLatin1String("trailing_stop") && !pos(spec.trailAmount)) errors << QStringLiteral("Enter a trailing amount.");
    static const QStringList types{QStringLiteral("market"), QStringLiteral("limit"), QStringLiteral("stop"), QStringLiteral("stop_limit"), QStringLiteral("trailing_stop")};
    if (spec.strategy == QLatin1String("single") && !types.contains(type)) errors << QStringLiteral("Unknown order type.");

    const std::optional<double> last = ctx.last;
    std::optional<double> entry;
    if (type == QLatin1String("limit") || type == QLatin1String("stop_limit")) entry = pos(spec.limit) ? std::optional<double>(d(spec.limit)) : std::nullopt;
    else if (type == QLatin1String("stop")) entry = pos(spec.stop) ? std::optional<double>(d(spec.stop)) : std::nullopt;
    else entry = last;
    if (!entry && last) entry = last;

    // position rules (stocks: a plain Sell needs shares, shorting is explicit)
    const double held = ctx.positionQty;
    const QString heldS = QString::number(held), negHeldS = QString::number(-held);
    if (side == QLatin1String("sell") && held < qty)
        errors << (held > 0 ? QStringLiteral("You hold %1 shares; you can sell at most %1.").arg(heldS)
                            : QStringLiteral("You hold no shares to sell. To bet on a decline use \"Sell short\"."));
    if (side == QLatin1String("buy_to_cover") && -held < qty)
        errors << (held < 0 ? QStringLiteral("You are short %1 shares; you can cover at most %1.").arg(negHeldS)
                            : QStringLiteral("You have no short position to cover."));
    if (side == QLatin1String("buy") && held < 0) errors << QStringLiteral("You are short this symbol. Use \"Buy to cover\" to close it first.");
    if (side == QLatin1String("sell_short") && !ctx.canShort) errors << QStringLiteral("This is a cash account, which cannot sell short. Use a margin account to short.");
    if (side == QLatin1String("sell_short") && held > 0) errors << QStringLiteral("You hold shares of this symbol. Sell them first before shorting.");

    // sanity checks against the current price
    if (last) {
        if (type == QLatin1String("limit") && pos(spec.limit)) {
            if (buy && d(spec.limit) > *last) warnings << QStringLiteral("Buy limit %1 is above the last price %2, so it will fill immediately at the market.").arg(px(d(spec.limit)), px(*last));
            if (!buy && d(spec.limit) < *last) warnings << QStringLiteral("Sell limit %1 is below the last price %2, so it will fill immediately at the market.").arg(px(d(spec.limit)), px(*last));
        }
        if ((type == QLatin1String("stop") || type == QLatin1String("stop_limit")) && pos(spec.stop)) {
            if (buy && d(spec.stop) < *last) warnings << QStringLiteral("Buy stop %1 is below the last price %2, so it will trigger immediately.").arg(px(d(spec.stop)), px(*last));
            if (!buy && d(spec.stop) > *last) warnings << QStringLiteral("Sell stop %1 is above the last price %2, so it will trigger immediately.").arg(px(d(spec.stop)), px(*last));
        }
    }
    if (type == QLatin1String("stop_limit") && pos(spec.stop) && pos(spec.limit) && (buy ? d(spec.limit) < d(spec.stop) : d(spec.limit) > d(spec.stop)))
        warnings << QStringLiteral("Limit %1 is %2 the stop %3: the order may trigger but not fill.").arg(px(d(spec.limit)), buy ? QStringLiteral("below") : QStringLiteral("above"), px(d(spec.stop)));

    // exit legs
    std::optional<double> stopPrice, targetPrice;
    const QString exit = exitSide(side);
    const bool isLong = exit == QLatin1String("sell");
    const bool bracket = spec.strategy == QLatin1String("bracket"), oco = spec.strategy == QLatin1String("oco");
    if (bracket || oco) {
        if (bracket && !opensPosition(side)) errors << QStringLiteral("A bracket needs an entry that opens a position (Buy or Sell short).");
        if (oco && opensPosition(side)) errors << QStringLiteral("OCO is for exit orders. Choose Sell or Buy to cover, or use a bracket for a new entry.");
        if (!spec.protect) errors << QStringLiteral("Add a protective stop leg.");
        else {
            const StopLeg &p = *spec.protect;
            if (p.type != QLatin1String("trailing_stop") && !pos(p.stop)) errors << QStringLiteral("Enter the stop price for the stop leg.");
            if (p.type == QLatin1String("stop_limit") && !pos(p.limit)) errors << QStringLiteral("Enter the limit price for the stop-limit leg.");
            if (p.type == QLatin1String("trailing_stop") && !pos(p.trailAmount)) errors << QStringLiteral("Enter the trailing amount for the stop leg.");
            if (p.type == QLatin1String("trailing_stop") && entry) {
                const auto dist = trailDistance(p.trailAmount, p.trailUnit, *entry);
                if (dist) stopPrice = isLong ? *entry - *dist : *entry + *dist;
            } else if (pos(p.stop))
                stopPrice = d(p.stop);
        }
        if (bracket) {
            if (!pos(spec.target)) errors << QStringLiteral("Enter a profit target price.");
            else targetPrice = d(spec.target);
        }
        const std::optional<double> ref = bracket ? entry : last;
        if (ref) {
            const QString dir = isLong ? QStringLiteral("long") : QStringLiteral("short");
            if (targetPrice && (isLong ? *targetPrice <= *ref : *targetPrice >= *ref))
                errors << QStringLiteral("For a %1 trade the profit target must be %2 the entry (%3).").arg(dir, isLong ? QStringLiteral("above") : QStringLiteral("below"), px(*ref));
            if (stopPrice && (isLong ? *stopPrice >= *ref : *stopPrice <= *ref) && bracket)
                errors << QStringLiteral("For a %1 trade the stop must be %2 the entry (%3).").arg(dir, isLong ? QStringLiteral("below") : QStringLiteral("above"), px(*ref));
            if (oco && stopPrice && (isLong ? *stopPrice >= *ref : *stopPrice <= *ref))
                warnings << QStringLiteral("The stop %1 is on the wrong side of the last price %2; it will trigger immediately.").arg(px(*stopPrice), px(*ref));
        }
    }

    std::optional<double> notional;
    if (entry && qtyOk) notional = *entry * qty;
    if (opensPosition(side) && notional) {
        const double room = ctx.buyingPower - ctx.reserved;
        if (*notional > room) errors << QStringLiteral("This order needs %1 but only %2 of buying power is free.").arg(money(*notional), money(std::max(0.0, room)));
    }

    std::optional<double> risk, reward, rr;
    if (bracket && entry && stopPrice && qtyOk) {
        risk = std::fabs(*entry - *stopPrice) * qty;
        if (targetPrice) {
            reward = std::fabs(*targetPrice - *entry) * qty;
            if (*risk > 0) rr = *reward / *risk;
        }
        if (rr && *rr < 1)
            warnings << QStringLiteral("Reward is smaller than risk (1 : %1). You would need to win more than %2% of the time to break even.")
                            .arg(fixed(*rr, 2)).arg(std::lround(100 / (1 + *rr)));
    }
    if (!last) warnings << QStringLiteral("No live price is available for this symbol, so estimates are missing.");

    a.entry = entry; a.notional = notional; a.stopPrice = stopPrice; a.targetPrice = targetPrice;
    a.risk = risk; a.reward = reward; a.rr = rr;
    return a;
}

namespace {
QString legText(const QString &side, int qty, const QString &symbol, const QString &type, const QVariant &limit, const QVariant &stop,
                const QVariant &trailAmount, const QString &trailUnit, const QString &tif)
{
    const QString act = isBuy(side) ? QStringLiteral("BUY +%1").arg(qty) : QStringLiteral("SELL -%1").arg(qty);
    const QString label = side == QLatin1String("sell_short") ? QStringLiteral(" (short)") : side == QLatin1String("buy_to_cover") ? QStringLiteral(" (cover)") : QString();
    QString price;
    // a JS number prints as itself (1.5, 2), as `${o.trailAmount}` does
    const QString trail = trailAmount.isValid() ? QString::number(trailAmount.toDouble()) : QStringLiteral("undefined");
    if (type == QLatin1String("market")) price = QStringLiteral("MKT");
    else if (type == QLatin1String("limit")) price = QStringLiteral("@%1 LMT").arg(px(limit.toDouble()));
    else if (type == QLatin1String("stop")) price = QStringLiteral("STP %1").arg(px(stop.toDouble()));
    else if (type == QLatin1String("stop_limit")) price = QStringLiteral("STP %1 LMT %2").arg(px(stop.toDouble()), px(limit.toDouble()));
    else price = QStringLiteral("TRAIL %1").arg(trailUnit == QLatin1String("%") ? trail + QLatin1Char('%') : QLatin1Char('$') + trail);
    return QStringLiteral("%1 %2%3 %4 %5").arg(act, symbol, label, price, tif.toUpper());
}
}

QStringList describeOrder(const OrderSpec &spec)
{
    const int qty = spec.qtyInt();
    QStringList lines{legText(spec.side, qty, spec.symbol, spec.type, spec.limit, spec.stop, spec.trailAmount, spec.trailUnit, spec.tif)};
    const QString exit = exitSide(spec.side);
    if (spec.strategy == QLatin1String("bracket")) lines[0] += QStringLiteral("  — triggers OCO:");
    if (spec.strategy == QLatin1String("oco")) lines[0] += QStringLiteral("  — OCO with:");
    if (spec.strategy == QLatin1String("bracket") && spec.target.isValid())
        lines << QStringLiteral("   ") + legText(exit, qty, spec.symbol, QStringLiteral("limit"), spec.target, {}, {}, {}, QStringLiteral("gtc"));
    if (spec.strategy != QLatin1String("single") && spec.protect) {
        const StopLeg &p = *spec.protect;
        lines << QStringLiteral("   ") + legText(spec.strategy == QLatin1String("oco") ? spec.side : exit, qty, spec.symbol, p.type, p.limit, p.stop, p.trailAmount, p.trailUnit, QStringLiteral("gtc"));
    }
    return lines;
}

} // namespace tl
