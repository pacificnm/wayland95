// Stock screener logic. Port of renderer/src/data/screener.ts.

#include "screenerlogic.h"

#include "jsnum.h"
#include "normalize.h"

#include <QStringList>

#include <algorithm>
#include <vector>

namespace tl {

namespace {

QString strOf(const Rec &r, const char *k) { return r.value(QLatin1String(k)).toString(); }
// a non-negative number typed into a filter box, or none
std::optional<double> filterNum(const QString &s)
{
    const double v = jsParseFloat(s);
    return std::isfinite(v) && v >= 0 ? std::optional<double>(v) : std::nullopt;
}
std::optional<double> fnum(const Rec &f, const char *k) { return filterNum(strOf(f, k)); }
std::optional<double> field(const Rec &r, const char *k) { return num(r.value(QLatin1String(k))); }
// `x ? a : b` for a number | null
bool truthy(const std::optional<double> &x) { return x && *x != 0 && !std::isnan(*x); }

void putIf(Rec &r, const char *k, const std::optional<double> &v)
{
    if (v)
        r.insert(QLatin1String(k), *v);
}
void putIfStr(Rec &r, const char *k, const QString &v)
{
    if (!v.isEmpty())
        r.insert(QLatin1String(k), v);
}

List tally(const List &rows, const char *key)
{
    QStringList order;
    QHash<QString, int> counts;
    for (const QVariant &v : rows) {
        QString k = strOf(v.toMap(), key);
        if (k.isEmpty())
            k = QStringLiteral("Other");
        if (!counts.contains(k))
            order << k;
        counts[k] += 1;
    }
    std::stable_sort(order.begin(), order.end(), [&](const QString &a, const QString &b) { return counts.value(b) < counts.value(a); });
    List out;
    for (const QString &k : order)
        out.append(Rec{{QStringLiteral("name"), k}, {QStringLiteral("count"), counts.value(k)}});
    return out;
}

} // namespace

Rec defaultScreenerFilters()
{
    return Rec{{QStringLiteral("sector"), QString()}, {QStringLiteral("industry"), QString()}, {QStringLiteral("exchange"), QString()},
               {QStringLiteral("country"), QStringLiteral("US")}, {QStringLiteral("capMin"), QStringLiteral("300")}, {QStringLiteral("capMax"), QString()},
               {QStringLiteral("priceMin"), QString()}, {QStringLiteral("priceMax"), QString()}, {QStringLiteral("volumeMin"), QStringLiteral("200000")},
               {QStringLiteral("betaMin"), QString()}, {QStringLiteral("betaMax"), QString()}, {QStringLiteral("yieldMin"), QString()},
               {QStringLiteral("relVolMin"), QString()}, {QStringLiteral("kind"), QStringLiteral("stocks")}, {QStringLiteral("active"), true},
               {QStringLiteral("limit"), 250}};
}

bool hasClientFilters(const Rec &f) { return fnum(f, "yieldMin").has_value() || fnum(f, "relVolMin").has_value(); }

Rec toScreenerQuery(const Rec &f)
{
    auto cap = [](const std::optional<double> &m) { return m ? std::optional<double>(*m * 1000000) : std::nullopt; };
    Rec q;
    putIfStr(q, "sector", strOf(f, "sector"));
    putIfStr(q, "industry", strOf(f, "industry"));
    putIfStr(q, "exchange", strOf(f, "exchange"));
    putIfStr(q, "country", strOf(f, "country"));
    putIf(q, "marketCapMoreThan", cap(fnum(f, "capMin")));
    putIf(q, "marketCapLowerThan", cap(fnum(f, "capMax")));
    putIf(q, "priceMoreThan", fnum(f, "priceMin"));
    putIf(q, "priceLowerThan", fnum(f, "priceMax"));
    putIf(q, "volumeMoreThan", fnum(f, "volumeMin"));
    putIf(q, "betaMoreThan", fnum(f, "betaMin"));
    putIf(q, "betaLowerThan", fnum(f, "betaMax"));
    const QString kind = strOf(f, "kind");
    if (kind == QStringLiteral("etfs"))
        q.insert(QStringLiteral("isEtf"), true);
    else if (kind == QStringLiteral("stocks"))
        q.insert(QStringLiteral("isEtf"), false);
    if (kind != QStringLiteral("both"))
        q.insert(QStringLiteral("isFund"), false);
    if (f.value(QStringLiteral("active")).toBool())
        q.insert(QStringLiteral("isActivelyTrading"), true);
    q.insert(QStringLiteral("limit"), hasClientFilters(f) ? QVariant(3000) : f.value(QStringLiteral("limit")));
    return q;
}

std::optional<double> relVolume(const Rec &r)
{
    const auto vol = field(r, "volume"), avg = field(r, "avgVolume");
    return vol && truthy(avg) ? std::optional<double>(*vol / *avg) : std::nullopt;
}

std::optional<double> dividendYield(const Rec &r)
{
    const auto div = field(r, "dividend"), price = field(r, "price");
    return div && truthy(price) ? std::optional<double>((*div / *price) * 100) : std::nullopt;
}

List applyClientFilters(const List &rows, const Rec &f)
{
    const auto y = fnum(f, "yieldMin"), rv = fnum(f, "relVolMin");
    const QString kind = strOf(f, "kind");
    List out;
    for (const QVariant &v : rows) {
        const Rec r = v.toMap();
        const bool etf = r.value(QStringLiteral("isEtf")).toBool(), fund = r.value(QStringLiteral("isFund")).toBool();
        if (kind == QStringLiteral("stocks") && (etf || fund))
            continue;
        if (kind == QStringLiteral("etfs") && !etf)
            continue;
        if (y && dividendYield(r).value_or(-1) < *y)
            continue;
        if (rv && relVolume(r).value_or(-1) < *rv)
            continue;
        out.append(v);
    }
    return out;
}

List sortScreenerRows(const List &rows, const QString &key, int dir)
{
    // the sort value: a number, a string, or nothing
    struct Val
    {
        bool missing = true;
        bool isStr = false;
        double n = 0;
        QString s;
    };
    auto val = [&](const Rec &r) -> Val {
        Val v;
        if (key == QStringLiteral("symbol") || key == QStringLiteral("name")) {
            v.missing = false;
            v.isStr = true;
            v.s = key == QStringLiteral("symbol") ? strOf(r, "symbol") : strOf(r, "name").toLower();
            return v;
        }
        std::optional<double> x;
        if (key == QStringLiteral("relVol"))
            x = relVolume(r);
        else if (key == QStringLiteral("yield"))
            x = dividendYield(r);
        else
            x = num(r.value(key));
        if (x) {
            v.missing = false;
            v.n = *x;
        }
        return v;
    };
    std::vector<std::pair<Val, QVariant>> items;
    items.reserve(rows.size());
    for (const QVariant &r : rows)
        items.emplace_back(val(r.toMap()), r);
    auto cmp = [&](const Val &x, const Val &y) -> int {
        if (x.missing && y.missing)
            return 0;
        if (x.missing)
            return 1;
        if (y.missing)
            return -1;
        const int c = x.isStr ? (x.s < y.s ? -1 : y.s < x.s ? 1 : 0) : (x.n < y.n ? -1 : x.n > y.n ? 1 : 0);
        return c * dir;
    };
    std::stable_sort(items.begin(), items.end(), [&](const auto &a, const auto &b) { return cmp(a.first, b.first) < 0; });
    List out;
    for (const auto &it : items)
        out.append(it.second);
    return out;
}

Rec presetFilters(const Rec &preset)
{
    Rec f = defaultScreenerFilters();
    f.insert(QStringLiteral("capMin"), QString());
    f.insert(QStringLiteral("volumeMin"), QString());
    const Rec apply = preset.value(QStringLiteral("apply")).toMap();
    for (auto it = apply.cbegin(); it != apply.cend(); ++it)
        f.insert(it.key(), it.value());
    return f;
}

Rec summarizeScreener(const List &rows)
{
    std::vector<double> caps;
    for (const QVariant &v : rows)
        if (const auto c = num(v.toMap().value(QStringLiteral("marketCap"))))
            caps.push_back(*c);
    std::sort(caps.begin(), caps.end());
    return Rec{{QStringLiteral("count"), rows.size()},
               {QStringLiteral("medianCap"), caps.empty() ? null() : QVariant(caps[caps.size() / 2])},
               {QStringLiteral("sectors"), tally(rows, "sector").mid(0, 8)},
               {QStringLiteral("exchanges"), tally(rows, "exchange")}};
}

QString screenerCsv(const List &rows)
{
    QStringList lines{QStringLiteral("Symbol,Name,Sector,Industry,Exchange,Market cap,Price,Volume,Avg volume,Rel volume,Beta,Dividend yield %")};
    auto orBlank = [](const QVariant &v) { return isNull(v) ? QString() : jsValueString(v); };
    for (const QVariant &v : rows) {
        const Rec r = v.toMap();
        const auto rel = relVolume(r), dy = dividendYield(r);
        QString name = strOf(r, "name");
        name.replace(QLatin1Char('"'), QStringLiteral("\"\""));
        lines << QStringList{strOf(r, "symbol"),
                             QLatin1Char('"') + name + QLatin1Char('"'),
                             strOf(r, "sector"),
                             QLatin1Char('"') + strOf(r, "industry") + QLatin1Char('"'),
                             strOf(r, "exchange"),
                             orBlank(r.value(QStringLiteral("marketCap"))),
                             orBlank(r.value(QStringLiteral("price"))),
                             orBlank(r.value(QStringLiteral("volume"))),
                             orBlank(r.value(QStringLiteral("avgVolume"))),
                             rel ? fixed(*rel, 2) : QString(),
                             orBlank(r.value(QStringLiteral("beta"))),
                             dy ? fixed(*dy, 2) : QString()}
                     .join(QLatin1Char(','));
    }
    return lines.join(QLatin1Char('\n'));
}

Rec screenerToolArgs(const Rec &f)
{
    auto n = [&](const char *k) -> std::optional<double> {
        const double x = jsParseFloat(strOf(f, k));
        return std::isfinite(x) ? std::optional<double>(x) : std::nullopt;
    };
    Rec out;
    putIfStr(out, "sector", strOf(f, "sector"));
    putIfStr(out, "industry", strOf(f, "industry"));
    putIfStr(out, "exchange", strOf(f, "exchange"));
    putIfStr(out, "country", strOf(f, "country"));
    if (f.contains(QStringLiteral("kind")))
        out.insert(QStringLiteral("type"), f.value(QStringLiteral("kind")));
    putIf(out, "market_cap_min_millions", n("capMin"));
    putIf(out, "market_cap_max_millions", n("capMax"));
    putIf(out, "price_min", n("priceMin"));
    putIf(out, "price_max", n("priceMax"));
    putIf(out, "volume_min", n("volumeMin"));
    putIf(out, "beta_min", n("betaMin"));
    putIf(out, "beta_max", n("betaMax"));
    putIf(out, "dividend_yield_min", n("yieldMin"));
    putIf(out, "relative_volume_min", n("relVolMin"));
    out.insert(QStringLiteral("limit"), 25);
    return out;
}

} // namespace tl
