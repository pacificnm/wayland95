// Order ticket drafts and chart order lines. Port of renderer/src/chart/orderDraft.ts.

#include "orderdraft.h"

#include "core/jsnum.h"

#include <cmath>

namespace tl {

#define S(x) QStringLiteral(x)

Rec emptyDraft(const QString &side)
{
    return Rec{{S("side"), side}, {S("qty"), S("10")}, {S("type"), S("market")}, {S("limit"), QString()}, {S("stop"), QString()},
               {S("trailAmount"), QString()}, {S("trailUnit"), S("$")}, {S("tif"), S("day")}, {S("strategy"), S("single")},
               {S("target"), QString()}, {S("protectType"), S("stop")}, {S("protectStop"), QString()}, {S("protectLimit"), QString()},
               {S("protectTrail"), QString()}, {S("protectTrailUnit"), S("$")}, {S("source"), S("user")}, {S("journal"), true},
               {S("journalNote"), QString()}};
}

// parseFloat, undefined unless finite
static std::optional<double> numOf(const QVariant &s)
{
    const double n = jsParseFloat(s.toString());
    return std::isfinite(n) ? std::optional<double>(n) : std::nullopt;
}
static void put(Rec &r, const QString &k, const std::optional<double> &v)
{
    if (v) r.insert(k, *v);
}
// String(Math.round(n * 100) / 100)
static QString fixed2(double n) { return jsString(jsRound(n * 100) / 100); }

Rec draftToSpec(const Rec &d, const QString &symbol)
{
    const double q = jsParseInt(d.value(S("qty")).toString());
    Rec spec{{S("symbol"), symbol}, {S("side"), d.value(S("side"))}, {S("qty"), std::isnan(q) || q == 0 ? 0.0 : q},
             {S("type"), d.value(S("type"))}, {S("tif"), d.value(S("tif"))}, {S("strategy"), d.value(S("strategy"))},
             {S("trailUnit"), d.value(S("trailUnit"))}, {S("source"), d.value(S("source"))}};
    put(spec, S("limit"), numOf(d.value(S("limit"))));
    put(spec, S("stop"), numOf(d.value(S("stop"))));
    put(spec, S("trailAmount"), numOf(d.value(S("trailAmount"))));
    if (d.contains(S("note")) && !isNull(d.value(S("note"))))
        spec.insert(S("note"), d.value(S("note")));
    const QString strategy = d.value(S("strategy")).toString();
    if (strategy != QLatin1String("single")) {
        Rec protect{{S("type"), d.value(S("protectType"))}, {S("trailUnit"), d.value(S("protectTrailUnit"))}};
        put(protect, S("stop"), numOf(d.value(S("protectStop"))));
        put(protect, S("limit"), numOf(d.value(S("protectLimit"))));
        put(protect, S("trailAmount"), numOf(d.value(S("protectTrail"))));
        spec.insert(S("protect"), protect);
        if (strategy == QLatin1String("bracket"))
            put(spec, S("target"), numOf(d.value(S("target"))));
    }
    return spec;
}

Rec draftFromSpec(const Rec &s)
{
    const Rec p = s.value(S("protect")).toMap();
    const bool hasP = s.contains(S("protect")) && !isNull(s.value(S("protect")));
    auto fx = [](const Rec &r, const char *k) { const auto v = num(r.value(QLatin1String(k))); return v ? fixed2(*v) : QString(); };
    auto str = [](const Rec &r, const char *k) { const auto v = num(r.value(QLatin1String(k))); return v ? jsString(*v) : QString(); };
    auto orDef = [](const Rec &r, const char *k, const QString &def) { const QVariant v = r.value(QLatin1String(k)); return isNull(v) ? def : v.toString(); };
    Rec d{{S("side"), s.value(S("side"))}, {S("qty"), str(s, "qty")}, {S("type"), s.value(S("type"))},
          {S("limit"), fx(s, "limit")}, {S("stop"), fx(s, "stop")}, {S("trailAmount"), str(s, "trailAmount")},
          {S("trailUnit"), orDef(s, "trailUnit", S("$"))}, {S("tif"), s.value(S("tif"))}, {S("strategy"), s.value(S("strategy"))},
          {S("target"), fx(s, "target")}, {S("protectType"), hasP ? orDef(p, "type", S("stop")) : S("stop")},
          {S("protectStop"), hasP ? fx(p, "stop") : QString()}, {S("protectLimit"), hasP ? fx(p, "limit") : QString()},
          {S("protectTrail"), hasP ? str(p, "trailAmount") : QString()}, {S("protectTrailUnit"), hasP ? orDef(p, "trailUnit", S("$")) : S("$")},
          {S("source"), orDef(s, "source", S("user"))}, {S("journal"), true}, {S("journalNote"), QString()}};
    if (!isNull(s.value(S("note"))))
        d.insert(S("note"), s.value(S("note")));
    return d;
}

static QString shortSide(const QString &s)
{
    if (s == QLatin1String("buy")) return S("BUY");
    if (s == QLatin1String("sell")) return S("SELL");
    if (s == QLatin1String("sell_short")) return S("SHORT");
    if (s == QLatin1String("buy_to_cover")) return S("COVER");
    return S("undefined");
}

static QString typeShort(const QString &t)
{
    if (t == QLatin1String("market")) return S("MKT");
    if (t == QLatin1String("limit")) return S("LMT");
    if (t == QLatin1String("stop")) return S("STP");
    if (t == QLatin1String("stop_limit")) return S("STP LMT");
    if (t == QLatin1String("trailing_stop")) return S("TRAIL");
    return S("undefined");
}

// `${n >= 0 ? '+' : '-'}$${Math.abs(n).toFixed(2)}`
static QString money0(double n) { return (n >= 0 ? S("+") : S("-")) + S("$") + fixed(std::abs(n), 2); }

static Rec orderLine(const QString &id, double price, const QString &color, const char *style, bool draggable, const QString &label)
{
    return Rec{{S("id"), id}, {S("price"), price}, {S("label"), label}, {S("color"), color}, {S("style"), QLatin1String(style)}, {S("draggable"), draggable}};
}

List draftLines(const Rec &d, const QString &symbol, std::optional<double> last)
{
    const Rec spec = draftToSpec(d, symbol);
    List lines;
    const double qty = spec.value(S("qty")).toDouble();
    const QString type = spec.value(S("type")).toString(), side = spec.value(S("side")).toString(), strategy = spec.value(S("strategy")).toString();
    std::optional<double> entryPrice;
    if (type == QLatin1String("limit") || type == QLatin1String("stop_limit")) entryPrice = num(spec.value(S("limit")));
    else if (type == QLatin1String("stop")) entryPrice = num(spec.value(S("stop")));
    const std::optional<double> ep = entryPrice ? entryPrice : last;
    if (ep) {
        lines << orderLine(S("draft:entry"), *ep, S("#2196f3"), "draft", entryPrice.has_value(),
                           shortSide(side) + QLatin1Char(' ') + typeShort(type) + (entryPrice ? QLatin1Char(' ') + fixed(*ep, 2) : QString()) + S(" ×") + jsString(qty));
    }
    if (strategy != QLatin1String("single") && spec.contains(S("protect"))) {
        const Rec p = spec.value(S("protect")).toMap();
        const bool isLong = side == QLatin1String("buy") || side == QLatin1String("sell"); // long entry, or an exit of a long position
        const auto trail = num(p.value(S("trailAmount")));
        std::optional<double> dist;
        if (ep && trail && *trail != 0)
            dist = p.value(S("trailUnit")).toString() == QLatin1String("%") ? (*ep * *trail) / 100 : *trail;
        const bool trailing = p.value(S("type")).toString() == QLatin1String("trailing_stop");
        std::optional<double> sp;
        if (trailing) { if (ep && dist) sp = isLong ? *ep - *dist : *ep + *dist; }
        else sp = num(p.value(S("stop")));
        if (sp)
            lines << orderLine(S("draft:stop"), *sp, S("#ef5350"), "draft", !trailing, S("STOP ") + (trailing ? S("(trail) ") : QString()) + fixed(*sp, 2));
    }
    const auto target = num(spec.value(S("target")));
    if (strategy == QLatin1String("bracket") && target)
        lines << orderLine(S("draft:target"), *target, S("#26a69a"), "draft", true, S("TARGET ") + fixed(*target, 2));
    return lines;
}

static QString idString(const QVariant &v) { return isNumber(v) ? jsString(v.toDouble()) : v.toString(); }

List tradeLines(const QString &symbol, const List &orders, const List &positions)
{
    List lines;
    for (const QVariant &pv : positions) {
        const Rec pos = pv.toMap();
        if (pos.value(S("symbol")).toString() != symbol)
            continue;
        const auto pl = num(pos.value(S("unrealized")));
        const double qty = pos.value(S("qty")).toDouble(), avg = pos.value(S("avg")).toDouble();
        lines << orderLine(S("pos:") + symbol, avg, !pl ? S("#9e9e9e") : *pl >= 0 ? S("#26a69a") : S("#ef5350"), "position", false,
                           (qty > 0 ? S("LONG") : S("SHORT")) + QLatin1Char(' ') + jsString(std::abs(qty)) + S(" @ ") + fixed(avg, 2)
                               + (pl ? S("  ") + money0(*pl) : QString()));
        break;
    }
    for (const QVariant &ov : orders) {
        const Rec o = ov.toMap();
        const QString status = o.value(S("status")).toString();
        if (o.value(S("symbol")).toString() != symbol || (status != QLatin1String("working") && status != QLatin1String("pending")))
            continue;
        const QString type = o.value(S("type")).toString(), role = o.value(S("role")).toString();
        const auto price = num(o.value(type == QLatin1String("limit") ? S("limit_price") : S("stop_price")));
        if (!price)
            continue;
        const QString color = role == QLatin1String("target") ? S("#26a69a")
                            : role == QLatin1String("stop") || type == QLatin1String("stop") || type == QLatin1String("stop_limit") ? S("#ef5350")
                                                                                                                                    : S("#2196f3");
        lines << orderLine(S("order:") + idString(o.value(S("id"))), *price, color, status == QLatin1String("pending") ? "draft" : "working", true,
                           (status == QLatin1String("pending") ? S("NEXT ") : QString()) + shortSide(o.value(S("side")).toString()) + QLatin1Char(' ')
                               + typeShort(type) + QLatin1Char(' ') + fixed(*price, 2) + S(" ×") + idString(o.value(S("qty"))));
    }
    return lines;
}

#undef S

} // namespace tl
