// Market overview logic. Port of renderer/src/data/market.ts.

#include "marketlogic.h"

#include "normalize.h"

#include <QHash>
#include <QRegularExpression>

#include <algorithm>
#include <vector>

namespace tl {

QVariant pickNum(const Rec &o, std::initializer_list<const char *> names)
{
    for (const char *n : names) {
        const QVariant v = o.value(QLatin1String(n));
        if (isNull(v) || (v.metaType().id() == QMetaType::QString && v.toString().isEmpty()))
            continue;
        const double d = toNumber(v);
        if (std::isfinite(d))
            return d;
    }
    return null();
}

QString pickStr(const Rec &o, std::initializer_list<const char *> names)
{
    for (const char *n : names) {
        const QVariant v = o.value(QLatin1String(n));
        if (isNull(v) || (v.metaType().id() == QMetaType::QString && v.toString().isEmpty()))
            continue;
        return jsValueString(v);
    }
    return QString();
}

namespace {

// Joins each day's performance and P/E lists by name, best performer first.
List join(const List &perf, const List &pe, const char *key)
{
    QHash<QString, QVariant> peBy;
    for (const QVariant &r : pe)
        peBy.insert(pickStr(r.toMap(), {key}), pickNum(r.toMap(), {"pe"}));
    std::vector<Rec> rows;
    for (const QVariant &rv : perf) {
        const Rec r = rv.toMap();
        const QString name = pickStr(r, {key});
        if (name.isEmpty())
            continue;
        const QVariant pev = peBy.value(name);
        rows.push_back(Rec{{QStringLiteral("name"), name}, {QStringLiteral("change"), pickNum(r, {"averageChange"})},
                           {QStringLiteral("pe"), pev.isValid() && !isNull(pev) ? pev : null()}});
    }
    auto change = [](const Rec &s) {
        const QVariant c = s.value(QStringLiteral("change"));
        return isNull(c) ? -INFINITY : c.toDouble();
    };
    // (b.change ?? -Infinity) - (a.change ?? -Infinity); NaN (both missing) counts as equal
    std::stable_sort(rows.begin(), rows.end(), [&](const Rec &a, const Rec &b) { return change(b) - change(a) < 0; });
    List out;
    for (const Rec &r : rows)
        out.append(r);
    return out;
}

List movers(const List &rows)
{
    List out;
    for (const QVariant &rv : rows) {
        const Rec r = rv.toMap();
        const QString symbol = pickStr(r, {"symbol"});
        if (symbol.isEmpty())
            continue;
        out.append(Rec{{QStringLiteral("symbol"), symbol}, {QStringLiteral("name"), pickStr(r, {"name"})}, {QStringLiteral("price"), pickNum(r, {"price"})},
                       {QStringLiteral("change"), pickNum(r, {"change"})}, {QStringLiteral("pct"), pickNum(r, {"changesPercentage", "changePercentage"})},
                       {QStringLiteral("exchange"), pickStr(r, {"exchange"})}});
    }
    return out;
}

List listOf(const Rec &m, const char *k) { return m.value(QLatin1String(k)).toList(); }

} // namespace

Rec buildMarket(const Rec &m)
{
    return Rec{{QStringLiteral("date"), m.value(QStringLiteral("date"))},
               {QStringLiteral("exchange"), m.value(QStringLiteral("exchange"))},
               {QStringLiteral("sectors"), join(listOf(m, "sectors"), listOf(m, "sectorPe"), "sector")},
               {QStringLiteral("industries"), join(listOf(m, "industries"), listOf(m, "industryPe"), "industry")},
               {QStringLiteral("gainers"), movers(listOf(m, "gainers"))},
               {QStringLiteral("losers"), movers(listOf(m, "losers"))},
               {QStringLiteral("actives"), movers(listOf(m, "actives"))},
               {QStringLiteral("asOf"), m.value(QStringLiteral("asOf"))}};
}

Rec defaultMoverFilter()
{
    return Rec{{QStringLiteral("minPrice"), 5}, {QStringLiteral("hideFunds"), true}, {QStringLiteral("exchange"), QStringLiteral("all")}};
}

bool isFundMover(const Rec &mover)
{
    static const QRegularExpression ticker(QStringLiteral("^[A-Z]{4}X$"));
    static const QRegularExpression name(QStringLiteral("\\b(ETF|ETN|Fund|Funds|Trust|Portfolio)\\b"), QRegularExpression::CaseInsensitiveOption);
    return ticker.match(mover.value(QStringLiteral("symbol")).toString()).hasMatch() || name.match(mover.value(QStringLiteral("name")).toString()).hasMatch();
}

List filterMovers(const List &list, const Rec &f, int limit)
{
    const double minPrice = toNumber(f.value(QStringLiteral("minPrice")));
    const bool hideFunds = f.value(QStringLiteral("hideFunds")).toBool();
    const QString exchange = f.value(QStringLiteral("exchange")).toString();
    List out;
    for (const QVariant &v : list) {
        const Rec m = v.toMap();
        const QVariant p = m.value(QStringLiteral("price"));
        const double price = isNull(p) ? 0 : toNumber(p);
        if (!(price >= minPrice))
            continue;
        if (hideFunds && isFundMover(m))
            continue;
        if (exchange != QStringLiteral("all") && m.value(QStringLiteral("exchange")).toString() != exchange)
            continue;
        out.append(v);
    }
    return out.mid(0, std::max(0, limit));
}

} // namespace tl
