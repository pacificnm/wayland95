// Options chain analytics. Port of shared/options.ts.

#include "optionsmath.h"

#include "jsnum.h"

#include <QDate>
#include <QMap>

#include <algorithm>
#include <set>

namespace tl {

namespace {

double strikeOf(const QVariant &c) { return toNumber(c.toMap().value(QStringLiteral("strike"))); }
QString typeOf(const Rec &c) { return c.value(QStringLiteral("type")).toString(); }
double numField(const Rec &c, const char *key) { return toNumber(c.value(QLatin1String(key))); }

} // namespace

double optionMid(const Rec &c)
{
    const double bid = numField(c, "bid"), ask = numField(c, "ask");
    if (bid > 0 && ask > 0)
        return (bid + ask) / 2;
    // c.last ?? c.theo ?? 0
    const QVariant last = c.value(QStringLiteral("last"));
    if (!isNull(last))
        return toNumber(last);
    const QVariant theo = c.value(QStringLiteral("theo"));
    if (!isNull(theo))
        return toNumber(theo);
    return 0;
}

List contractsFor(const Rec &chain, const QString &exp)
{
    List out;
    for (const QVariant &c : chain.value(QStringLiteral("contracts")).toList())
        if (c.toMap().value(QStringLiteral("exp")).toString() == exp)
            out.append(c);
    return out;
}

std::optional<double> atmStrike(const List &cs, double price)
{
    std::optional<double> best;
    for (const QVariant &c : cs) {
        const double k = strikeOf(c);
        if (!best || std::abs(k - price) < std::abs(*best - price))
            best = k;
    }
    return best;
}

std::optional<double> atmIv(const List &cs, double price)
{
    const auto k = atmStrike(cs, price);
    if (!k)
        return std::nullopt;
    double sum = 0;
    int n = 0;
    for (const QVariant &v : cs) {
        const Rec c = v.toMap();
        if (strikeOf(v) == *k && !isNull(c.value(QStringLiteral("iv")))) {
            sum += toNumber(c.value(QStringLiteral("iv")));
            ++n;
        }
    }
    return n ? std::optional<double>(sum / n) : std::nullopt;
}

QVariant straddle(const List &cs, double price)
{
    const auto k = atmStrike(cs, price);
    if (!k)
        return null();
    std::optional<Rec> call, put;
    for (const QVariant &v : cs) {
        if (strikeOf(v) != *k)
            continue;
        const Rec c = v.toMap();
        if (!call && typeOf(c) == QStringLiteral("C"))
            call = c;
        if (!put && typeOf(c) == QStringLiteral("P"))
            put = c;
    }
    if (!call || !put)
        return null();
    return Rec{{QStringLiteral("strike"), *k}, {QStringLiteral("cost"), optionMid(*call) + optionMid(*put)}};
}

Rec activity(const List &cs)
{
    double callVolume = 0, putVolume = 0, callOi = 0, putOi = 0;
    for (const QVariant &v : cs) {
        const Rec c = v.toMap();
        if (typeOf(c) == QStringLiteral("C")) {
            callVolume += numField(c, "volume");
            callOi += numField(c, "oi");
        } else {
            putVolume += numField(c, "volume");
            putOi += numField(c, "oi");
        }
    }
    // `callVolume ? ... : null`: 0 and NaN are falsy
    auto truthy = [](double d) { return d != 0 && !std::isnan(d); };
    return Rec{{QStringLiteral("callVolume"), callVolume}, {QStringLiteral("putVolume"), putVolume}, {QStringLiteral("callOi"), callOi},
               {QStringLiteral("putOi"), putOi}, {QStringLiteral("pcVolume"), truthy(callVolume) ? QVariant(putVolume / callVolume) : null()},
               {QStringLiteral("pcOi"), truthy(callOi) ? QVariant(putOi / callOi) : null()}};
}

std::optional<double> maxPain(const List &cs)
{
    std::set<double> uniq;
    for (const QVariant &c : cs)
        uniq.insert(strikeOf(c));
    if (uniq.empty())
        return std::nullopt;
    double best = *uniq.begin(), bestPain = INFINITY;
    for (const double k : uniq) {
        double pain = 0;
        for (const QVariant &v : cs) {
            const Rec c = v.toMap();
            const double strike = strikeOf(v);
            pain += numField(c, "oi") * (typeOf(c) == QStringLiteral("C") ? std::max(0.0, k - strike) : std::max(0.0, strike - k));
        }
        if (pain < bestPain) {
            bestPain = pain;
            best = k;
        }
    }
    return best;
}

List termStructure(const Rec &chain)
{
    List out;
    const double price = toNumber(chain.value(QStringLiteral("price")));
    for (const QVariant &ev : chain.value(QStringLiteral("expirations")).toList()) {
        const Rec e = ev.toMap();
        const QString date = e.value(QStringLiteral("date")).toString();
        const auto iv = atmIv(contractsFor(chain, date), price);
        if (iv)
            out.append(Rec{{QStringLiteral("date"), date}, {QStringLiteral("dte"), e.value(QStringLiteral("dte"))}, {QStringLiteral("iv"), *iv}});
    }
    return out;
}

List strikeRows(const List &cs, double price, std::optional<int> n)
{
    QMap<double, Rec> map; // sorted by strike, as the rows are
    for (const QVariant &v : cs) {
        const Rec c = v.toMap();
        const double k = strikeOf(v);
        Rec &r = map[k];
        r.insert(QStringLiteral("strike"), k);
        r.insert(typeOf(c) == QStringLiteral("C") ? QStringLiteral("call") : QStringLiteral("put"), c);
    }
    List rows;
    for (const Rec &r : std::as_const(map))
        rows.append(r);
    if (!n)
        return rows;
    const auto atm = atmStrike(cs, price);
    qsizetype i = -1;
    for (qsizetype j = 0; j < rows.size(); ++j)
        if (atm && toNumber(rows[j].toMap().value(QStringLiteral("strike"))) == *atm) {
            i = j;
            break;
        }
    if (i < 0)
        return rows;
    const qsizetype from = std::max<qsizetype>(0, i - *n);
    const qsizetype to = std::min<qsizetype>(rows.size(), i + *n + 1);
    return from < to ? rows.mid(from, to - from) : List();
}

std::optional<double> historicalVol(const QVector<double> &closes, int days)
{
    const qsizetype take = std::min<qsizetype>(closes.size(), days + 1);
    const QVector<double> c = closes.mid(closes.size() - take);
    if (c.size() < 10)
        return std::nullopt;
    QVector<double> r;
    for (qsizetype i = 1; i < c.size(); ++i)
        r.append(std::log(c[i] / c[i - 1]));
    double m = 0;
    for (const double x : r)
        m += x;
    m /= r.size();
    double v = 0;
    for (const double x : r)
        v += (x - m) * (x - m);
    v /= r.size() - 1;
    return std::sqrt(v) * std::sqrt(252.0);
}

QString contractLabel(const Rec &c)
{
    static const char *const months[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
    const QDate d = QDate::fromString(c.value(QStringLiteral("exp")).toString(), QStringLiteral("yyyy-MM-dd"));
    const QString strike = jsString(strikeOf(c));
    const QString type = typeOf(c);
    if (!d.isValid())
        return QStringLiteral("Invalid Date NaN %1 %2").arg(strike, type);
    return QStringLiteral("%1 %2 %3 %4").arg(QLatin1String(months[d.month() - 1])).arg(d.day()).arg(strike, type);
}

} // namespace tl
