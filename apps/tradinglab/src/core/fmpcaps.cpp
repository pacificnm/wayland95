// FMP plan capabilities and the stored plan check. Port of shared/fmpCaps.ts and main/capsStore.ts.

#include "fmpcaps.h"

#include "db.h"

#include <QSet>

#include <cmath>

namespace tl {

const std::vector<CapDef> CAPS = {
    {QStringLiteral("quotes"), QStringLiteral("Live quotes"), QStringLiteral("live prices, watchlist prices and the paper account's marks")},
    {QStringLiteral("indices"), QStringLiteral("Index quotes"), QStringLiteral("the index ticker along the top")},
    {QStringLiteral("daily"), QStringLiteral("Daily bars"), QStringLiteral("daily, weekly and monthly charts")},
    {QStringLiteral("bars1min"), QStringLiteral("1-minute bars"), QStringLiteral("1-minute charts (and precise order fills)")},
    {QStringLiteral("bars5min"), QStringLiteral("5-minute bars"), QStringLiteral("5-minute charts")},
    {QStringLiteral("bars15min"), QStringLiteral("15-minute bars"), QStringLiteral("15-minute charts")},
    {QStringLiteral("bars30min"), QStringLiteral("30-minute bars"), QStringLiteral("30-minute charts")},
    {QStringLiteral("bars1hour"), QStringLiteral("1-hour bars"), QStringLiteral("1-hour charts")},
    {QStringLiteral("bars4hour"), QStringLiteral("4-hour bars"), QStringLiteral("4-hour charts")},
    {QStringLiteral("search"), QStringLiteral("Symbol search"), QStringLiteral("searching for symbols by name")},
    {QStringLiteral("news"), QStringLiteral("Stock news"), QStringLiteral("news for a symbol")},
    {QStringLiteral("marketNews"), QStringLiteral("Market news"), QStringLiteral("general market news")},
    {QStringLiteral("profile"), QStringLiteral("Company profile"), QStringLiteral("company details on the quote screen")},
    {QStringLiteral("analyst"), QStringLiteral("Analyst ratings"), QStringLiteral("ratings, price targets and grades")},
    {QStringLiteral("estimates"), QStringLiteral("Analyst estimates"), QStringLiteral("the estimates section of Analyst Reports")},
    {QStringLiteral("aftermarket"), QStringLiteral("After-hours quotes"), QStringLiteral("after-hours prices on the quote screen")},
    {QStringLiteral("sectors"), QStringLiteral("Sector performance"), QStringLiteral("sector and industry performance")},
    {QStringLiteral("movers"), QStringLiteral("Market movers"), QStringLiteral("the biggest gainers and losers")},
    {QStringLiteral("screener"), QStringLiteral("Stock screener"), QStringLiteral("the Stock Screener")},
    {QStringLiteral("congress"), QStringLiteral("Senate and House trades"), QStringLiteral("the congressional trades screen")},
    {QStringLiteral("fundamentals"), QStringLiteral("Fundamentals"), QStringLiteral("ratios, key metrics and financial statements")},
};

QString capLabel(const QString &id)
{
    for (const CapDef &c : CAPS)
        if (c.id == id)
            return c.label;
    return id;
}

bool missingAll(const QStringList &unavailable, const QStringList &needs)
{
    const QSet<QString> set(unavailable.cbegin(), unavailable.cend());
    if (needs.isEmpty())
        return false;
    for (const QString &n : needs)
        if (!set.contains(n))
            return false;
    return true;
}

const QMap<QString, QStringList> SCREEN_CAPS = {
    {QStringLiteral("market"), {QStringLiteral("sectors"), QStringLiteral("movers")}},
    {QStringLiteral("congress"), {QStringLiteral("congress")}},
    {QStringLiteral("marketnews"), {QStringLiteral("news"), QStringLiteral("marketNews")}},
    {QStringLiteral("screener"), {QStringLiteral("screener")}},
    {QStringLiteral("news"), {QStringLiteral("news")}},
    {QStringLiteral("analyst"), {QStringLiteral("analyst"), QStringLiteral("estimates")}},
    {QStringLiteral("fundamentals"), {QStringLiteral("fundamentals")}},
};

const QMap<QString, QStringList> TOOL_CAPS = {
    {QStringLiteral("get_news"), {QStringLiteral("news"), QStringLiteral("marketNews")}},
    {QStringLiteral("get_analyst_ratings"), {QStringLiteral("analyst")}},
    {QStringLiteral("run_screener"), {QStringLiteral("screener")}},
    {QStringLiteral("get_congress_trades"), {QStringLiteral("congress")}},
    {QStringLiteral("get_market_performance"), {QStringLiteral("sectors"), QStringLiteral("movers")}},
    {QStringLiteral("get_quote"), {QStringLiteral("quotes")}},
};

const QMap<QString, QString> INTERVAL_CAP = {
    {QStringLiteral("1min"), QStringLiteral("bars1min")}, {QStringLiteral("5min"), QStringLiteral("bars5min")},
    {QStringLiteral("15min"), QStringLiteral("bars15min")}, {QStringLiteral("30min"), QStringLiteral("bars30min")},
    {QStringLiteral("1hour"), QStringLiteral("bars1hour")}, {QStringLiteral("4hour"), QStringLiteral("bars4hour")},
};

namespace {
struct Grain
{
    FillGrain g;
    QString cap;
};
const std::vector<Grain> &grains()
{
    static const std::vector<Grain> list = {
        {{QStringLiteral("1min"), 60, 0, QStringLiteral("1-minute")}, QStringLiteral("bars1min")},
        {{QStringLiteral("5min"), 300, 0, QStringLiteral("5-minute")}, QStringLiteral("bars5min")},
        {{QStringLiteral("15min"), 900, 0, QStringLiteral("15-minute")}, QStringLiteral("bars15min")},
        {{QStringLiteral("30min"), 1800, 0, QStringLiteral("30-minute")}, QStringLiteral("bars30min")},
        {{QStringLiteral("1hour"), 3600, 1800, QStringLiteral("1-hour")}, QStringLiteral("bars1hour")},
    };
    return list;
}
}

Rec toRec(const FillGrain &g)
{
    return Rec{{QStringLiteral("interval"), g.interval}, {QStringLiteral("seconds"), g.seconds},
               {QStringLiteral("offset"), g.offset}, {QStringLiteral("label"), g.label}};
}

std::optional<FillGrain> fillGrain(const QStringList &unavailable)
{
    for (const Grain &x : grains())
        if (!unavailable.contains(x.cap))
            return x.g;
    return std::nullopt;
}

qint64 nextBar(qint64 t, const FillGrain &g)
{
    return qint64(std::ceil(double(t - g.offset) / g.seconds)) * g.seconds + g.offset;
}

QString planNote(const QStringList &unavailable)
{
    QStringList lost;
    for (const CapDef &c : CAPS)
        if (unavailable.contains(c.id))
            lost << c.label.toLower();
    if (lost.isEmpty())
        return QString();
    return QStringLiteral("\n\n## The user's market-data plan\n\nThe user's FMP plan does not include: %1. The screens, chart intervals "
                          "and tools that depend on them are switched off, so do not try to use them or offer them. If the user asks for "
                          "that data, say plainly that their plan does not include it. Work with the intervals and data you can actually "
                          "get; set_chart will refuse an interval that is not available.")
        .arg(lost.join(QStringLiteral(", ")));
}

std::optional<Rec> readCaps()
{
    const QVariant v = getSetting(QStringLiteral("fmpCaps"));
    if (v.metaType().id() != QMetaType::QVariantMap)
        return std::nullopt;
    return v.toMap();
}

void saveCaps(const std::optional<Rec> &caps)
{
    if (caps)
        setSetting(QStringLiteral("fmpCaps"), *caps);
    else
        Db::local().run(QStringLiteral("DELETE FROM settings WHERE key = 'fmpCaps'"));
}

QStringList unavailableOf(const std::optional<Rec> &caps)
{
    if (!caps)
        return {};
    QStringList out;
    for (const QVariant &v : caps->value(QStringLiteral("unavailable")).toList())
        out << v.toString();
    return out;
}

} // namespace tl
