// Congressional trading logic. Port of renderer/src/data/congress.ts.

#include "congresslogic.h"

#include <QDateTime>
#include <QHash>
#include <QRegularExpression>
#include <QSet>
#include <QTimeZone>
#include <QStringList>

#include <algorithm>
#include <vector>

namespace tl {

namespace {

QString s(const Rec &t, const char *k) { return t.value(QLatin1String(k)).toString(); }

// Date.parse for the ISO date-only forms the feed uses (YYYY, YYYY-MM, YYYY-MM-DD: UTC midnight); NaN otherwise
double parseDate(const QString &d)
{
    static const QRegularExpression re(QStringLiteral("^(\\d{4})(?:-(\\d{2})(?:-(\\d{2}))?)?$"));
    const QRegularExpressionMatch m = re.match(d);
    if (!m.hasMatch())
        return NAN;
    const int y = m.captured(1).toInt();
    const int mo = m.captured(2).isEmpty() ? 1 : m.captured(2).toInt();
    const int day = m.captured(3).isEmpty() ? 1 : m.captured(3).toInt();
    const QDate date(y, mo, day);
    if (!date.isValid())
        return NAN;
    return double(QDateTime(date, QTime(0, 0), QTimeZone::UTC).toMSecsSinceEpoch());
}

QString memberKey(const Rec &t)
{
    const QString id = s(t, "memberId");
    return s(t, "chamber") + QLatin1Char(':') + (id.isEmpty() ? s(t, "member") : id);
}

// a Map kept in insertion order
template<typename V>
struct Ordered
{
    QStringList keys;
    QHash<QString, V> values;
    V &at(const QString &k, const V &init)
    {
        if (!values.contains(k)) {
            keys << k;
            values.insert(k, init);
        }
        return values[k];
    }
};

} // namespace

std::optional<AmountRange> amountRange(const QString &str)
{
    static const QRegularExpression re(QStringLiteral("\\$?[\\d,]+"));
    std::vector<double> nums;
    auto it = re.globalMatch(str);
    while (it.hasNext()) {
        QString x = it.next().captured(0);
        x.remove(QLatin1Char('$')).remove(QLatin1Char(','));
        const double n = x.isEmpty() ? 0 : x.toDouble();
        if (std::isfinite(n) && n > 0)
            nums.push_back(n);
    }
    if (nums.size() >= 2)
        return AmountRange{nums[0], nums[1], (nums[0] + nums[1]) / 2};
    if (nums.size() == 1)
        return AmountRange{nums[0], nums[0], nums[0]};
    return std::nullopt;
}

std::optional<double> delayDays(const Rec &t)
{
    const double a = parseDate(s(t, "traded")), b = parseDate(s(t, "disclosed"));
    if (!std::isfinite(a) || !std::isfinite(b))
        return std::nullopt;
    const double d = (b - a) / 86400000;
    return std::max(0.0, std::floor(d + 0.5));
}

bool isCongressBuy(const Rec &t) { return s(t, "type").contains(QStringLiteral("purchase"), Qt::CaseInsensitive); }
bool isCongressSell(const Rec &t) { return s(t, "type").contains(QStringLiteral("sale"), Qt::CaseInsensitive); }

Rec summarizeCongress(const List &list)
{
    int buys = 0, sells = 0;
    double buyValue = 0, sellValue = 0;
    std::vector<double> delays;
    struct Sym
    {
        QString symbol;
        int count = 0, buys = 0, sells = 0;
        QSet<QString> who;
    };
    struct Mem
    {
        QString member, memberId, chamber;
        int count = 0;
    };
    Ordered<Sym> sym;
    Ordered<Mem> mem;
    Ordered<int> types;
    for (const QVariant &v : list) {
        const Rec t = v.toMap();
        const auto range = amountRange(s(t, "amount"));
        const double val = range ? range->mid : 0;
        const bool buy = isCongressBuy(t), sell = isCongressSell(t);
        if (buy) {
            ++buys;
            buyValue += val;
        } else if (sell) {
            ++sells;
            sellValue += val;
        }
        if (const auto d = delayDays(t))
            delays.push_back(*d);
        const QString symbol = s(t, "symbol");
        if (!symbol.isEmpty()) {
            Sym &e = sym.at(symbol, Sym{symbol, 0, 0, 0, {}});
            e.who.insert(memberKey(t));
            ++e.count;
            if (buy)
                ++e.buys;
            if (sell)
                ++e.sells;
        }
        Mem &m = mem.at(memberKey(t), Mem{s(t, "member"), s(t, "memberId"), s(t, "chamber"), 0});
        ++m.count;
        QString type = s(t, "assetType");
        if (type.isEmpty())
            type = QStringLiteral("Other");
        types.at(type, 0) += 1;
    }
    std::sort(delays.begin(), delays.end());
    QStringList dates;
    for (const QVariant &v : list) {
        const QString d = s(v.toMap(), "traded");
        if (!d.isEmpty())
            dates << d;
    }
    std::sort(dates.begin(), dates.end());

    QStringList symKeys = sym.keys;
    std::stable_sort(symKeys.begin(), symKeys.end(), [&](const QString &a, const QString &b) { return sym.values[b].count < sym.values[a].count; });
    List topSymbols;
    for (const QString &k : symKeys.mid(0, 8)) {
        const Sym &e = sym.values[k];
        topSymbols.append(Rec{{QStringLiteral("symbol"), e.symbol}, {QStringLiteral("count"), e.count}, {QStringLiteral("buys"), e.buys},
                              {QStringLiteral("sells"), e.sells}, {QStringLiteral("members"), e.who.size()}});
    }
    QStringList memKeys = mem.keys;
    std::stable_sort(memKeys.begin(), memKeys.end(), [&](const QString &a, const QString &b) { return mem.values[b].count < mem.values[a].count; });
    List topMembers;
    for (const QString &k : memKeys.mid(0, 8)) {
        const Mem &m = mem.values[k];
        topMembers.append(Rec{{QStringLiteral("member"), m.member}, {QStringLiteral("memberId"), m.memberId}, {QStringLiteral("chamber"), m.chamber},
                              {QStringLiteral("count"), m.count}});
    }
    QStringList typeKeys = types.keys;
    std::stable_sort(typeKeys.begin(), typeKeys.end(), [&](const QString &a, const QString &b) { return types.values[b] < types.values[a]; });
    List assetTypes;
    for (const QString &k : typeKeys)
        assetTypes.append(Rec{{QStringLiteral("type"), k}, {QStringLiteral("count"), types.values[k]}});

    const qsizetype count = list.size();
    return Rec{
        {QStringLiteral("count"), count},
        {QStringLiteral("buys"), buys},
        {QStringLiteral("sells"), sells},
        {QStringLiteral("other"), count - buys - sells},
        {QStringLiteral("buyValue"), buyValue},
        {QStringLiteral("sellValue"), sellValue},
        {QStringLiteral("medianDelay"), delays.empty() ? null() : QVariant(delays[delays.size() / 2])},
        {QStringLiteral("maxDelay"), delays.empty() ? null() : QVariant(delays.back())},
        {QStringLiteral("topSymbols"), topSymbols},
        {QStringLiteral("topMembers"), topMembers},
        {QStringLiteral("assetTypes"), assetTypes},
        {QStringLiteral("from"), dates.isEmpty() ? null() : QVariant(dates.first())},
        {QStringLiteral("to"), dates.isEmpty() ? null() : QVariant(dates.last())},
    };
}

Rec defaultTradeFilter()
{
    return Rec{{QStringLiteral("kind"), QStringLiteral("all")}, {QStringLiteral("assetType"), QStringLiteral("all")}, {QStringLiteral("q"), QString()},
               {QStringLiteral("owner"), QStringLiteral("all")}};
}

List filterTrades(const List &list, const Rec &f)
{
    const QString q = s(f, "q").trimmed().toLower();
    const QString kind = s(f, "kind"), assetType = s(f, "assetType"), owner = s(f, "owner");
    List out;
    for (const QVariant &v : list) {
        const Rec t = v.toMap();
        if (kind != QStringLiteral("all") && !(kind == QStringLiteral("buys") ? isCongressBuy(t) : isCongressSell(t)))
            continue;
        const QString at = s(t, "assetType");
        if (assetType != QStringLiteral("all")
            && !(at.contains(QStringLiteral("stock"), Qt::CaseInsensitive) && !at.contains(QStringLiteral("option"), Qt::CaseInsensitive)))
            continue;
        if (owner != QStringLiteral("all") && s(t, "owner") != owner)
            continue;
        if (!q.isEmpty()
            && !QStringList{s(t, "member"), s(t, "symbol"), s(t, "asset"), s(t, "district")}.join(QLatin1Char(' ')).toLower().contains(q))
            continue;
        out.append(v);
    }
    return out;
}

List groupMembers(const List &list)
{
    Ordered<Rec> m;
    for (const QVariant &v : list) {
        const Rec t = v.toMap();
        const QString key = memberKey(t);
        Rec &e = m.at(key, Rec{{QStringLiteral("key"), key}, {QStringLiteral("member"), s(t, "member")}, {QStringLiteral("memberId"), s(t, "memberId")},
                               {QStringLiteral("chamber"), s(t, "chamber")}, {QStringLiteral("district"), s(t, "district")}, {QStringLiteral("count"), 0}});
        e.insert(QStringLiteral("count"), e.value(QStringLiteral("count")).toInt() + 1);
    }
    QStringList keys = m.keys;
    std::stable_sort(keys.begin(), keys.end(), [&](const QString &a, const QString &b) {
        return m.values[b].value(QStringLiteral("count")).toInt() < m.values[a].value(QStringLiteral("count")).toInt();
    });
    List out;
    for (const QString &k : keys)
        out.append(m.values[k]);
    return out;
}

} // namespace tl
