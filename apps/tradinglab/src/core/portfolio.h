#pragma once

// Portfolio planning: split an investment amount by target percentages, say how many shares that buys, and value what you hold.
// Pure (no database, no UI), shared by the Portfolio screen, the store and Claude's read-only tool. Nothing here trades: it is
// arithmetic on prices the caller supplies. Port of shared/portfolio.ts.
//
// Records exchanged with QML keep upstream's field names:
//   Pf      {id, name, amount, mode: 'new'|'total', fractional, leftover, accountId (or null), items: [PfItem]}
//   PfItem  {id, symbol, name, kind: 'stock'|'etf'|'fund', targetPct, shares, cost, manualPrice (or null), noBuy?}
//   Prices  {SYMBOL: price | null}
//   AccountLink {id, name, cash, buyingPower, positions: [{symbol, qty, avg}], pendingBuys: {SYM: n}, pendingSells?: {SYM: n}}
// The maths runs on the structs below; the Rec overloads convert at the edge.

#include "util.h"

#include <QHash>
#include <QRegularExpression>
#include <QStringList>
#include <QVector>

#include <optional>

namespace tl {

// JavaScript's Math.round (halves go up, towards +infinity)
inline double mathRound(double x)
{
    if (!std::isfinite(x))
        return x;
    const double f = std::floor(x);
    return x - f >= 0.5 ? f + 1 : f;
}

// KINDS: [{id, label}] = stock "Stock", etf "ETF", fund "Mutual fund"
List kinds();
QStringList kindIds();
// SYMBOL_RE = /^[A-Z0-9.^=-]{1,15}$/
const QRegularExpression &symbolRe();
// US mutual fund tickers are five letters ending in X (VFIAX, FXAIX). A good first guess, and the user can change it.
QString guessKind(const QString &symbol);

struct PfItem
{
    qint64 id = 0;
    QString symbol;
    QString name;
    QString kind = QStringLiteral("stock"); // stock | etf | fund
    // target share of the portfolio, 0-100
    double targetPct = 0;
    // what the user holds now (0 for a planned position)
    double shares = 0;
    // total money paid for those shares
    double cost = 0;
    // price the user typed, used only when no live price exists
    std::optional<double> manualPrice;
    // set on the fly for a linked portfolio: this holding cannot be bought here (mutual funds are not tradable in the paper account)
    std::optional<bool> noBuy;

    static PfItem fromRec(const Rec &r);
    Rec toRec() const;
};

struct Pf
{
    qint64 id = 0;
    QString name;
    double amount = 0;
    // 'new': the amount is new money to invest now. 'total': the amount is the size you want the whole portfolio to reach.
    QString mode = QStringLiteral("new");
    // allow fractional shares of stocks and ETFs (mutual funds always allow them)
    bool fractional = false;
    // spend what is left after rounding down on extra whole shares, most underweight first
    bool leftover = false;
    // the paper account this portfolio follows, or none when the user types everything by hand
    std::optional<qint64> accountId;
    QVector<PfItem> items;

    static Pf fromRec(const Rec &r);
    Rec toRec() const;
};

// symbol -> live price; only finite numbers are kept (null/undefined entries are simply absent)
using Prices = QHash<QString, double>;
Prices pricesFromRec(const Rec &live);

// The price used for a holding: the live one, else the one the user typed.
std::optional<double> priceOf(const PfItem &it, const Prices &live);
QVariant priceOf(const Rec &item, const Rec &live); // number or null

// Smallest amount you can buy: a whole share, or a thousandth of a share for funds and when fractions are allowed.
inline double stepFor(const QString &kind, bool fractional) { return kind == QStringLiteral("fund") || fractional ? 0.001 : 1; }

// {rows: [{id, symbol, targetPct, price, currentValue, dollars, shares, extra, cost, overShares, overValue}],
//  pctTotal, budget, spent, leftover, missingPrice: [symbol], holdingsValue}
Rec planPortfolio(const Pf &p, const Prices &live);
inline Rec planPortfolio(const Rec &p, const Rec &live) { return planPortfolio(Pf::fromRec(p), pricesFromRec(live)); }

// {rows: [{id, symbol, shares, cost, avgCost, price, value, gain, gainPct, weight, targetPct, drift}], totalValue, totalCost, gain, gainPct, missingPrice}
Rec summarizeHoldings(const QVector<PfItem> &items, const Prices &live);
Rec summarizeHoldings(const List &items, const Rec &live);

// Equal weights that add up to exactly 100 (the last item takes the rounding).
QVector<double> equalWeights(int n);
// The same proportions scaled so they add up to 100. All zeros stay zero.
QVector<double> normalizeWeights(const QVector<double> &w);
List equalWeightsList(int n);
List normalizeWeights(const List &w);

// What the paper account says about itself, as the Portfolio screen needs it.
struct AccountLink
{
    struct Position
    {
        QString symbol;
        double qty = 0;
        double avg = 0;
    };
    qint64 id = 0;
    QString name;
    double cash = 0;
    // free to open positions: already net of working orders that reserve money
    double buyingPower = 0;
    // long and short positions; shorts are ignored here
    QVector<Position> positions;
    // shares of market buy orders that have been sent but not filled yet, by symbol
    QHash<QString, double> pendingBuys;
    // shares of market sell orders that have been sent but not filled yet, by symbol
    std::optional<QHash<QString, double>> pendingSells;

    static AccountLink fromRec(const Rec &r);
};

struct Effective
{
    Pf pf;
    // cash the plan may spend: the account's cash and buying power, less what unfilled market buys are about to use
    double availableCash = 0;
    // short positions the account holds in this portfolio's symbols (not counted)
    QStringList shorts;
    // shares from unfilled market buys that are counted as already held
    QHash<QString, double> pending;
    // shares in unfilled market sells that are counted as already gone
    QHash<QString, double> pendingSold;
    // shares that can still be sold from here: the account's position less sells already waiting; funds are 0
    QHash<QString, double> sellable;

    // {pf, availableCash, shorts, pending, pendingSold, sellable}
    Rec toRec() const;
};

// The portfolio the way a linked one is planned and valued (see the .cpp). An unlinked portfolio is returned unchanged.
Effective effectivePortfolio(const Pf &p, const std::optional<AccountLink> &link, const Prices &live);
// `link` may be null
Rec effectivePortfolio(const Rec &p, const QVariant &link, const Rec &live);

// proportional: the same share of every holding. overweight: trim what is above its target first, then the rest proportionally.
struct SellOptions
{
    double percent = 0;
    QString method = QStringLiteral("proportional"); // proportional | overweight
    bool onlyProfit = false;
    std::optional<QHash<QString, double>> sellable;

    static SellOptions fromRec(const Rec &r); // {percent, method, onlyProfit, sellable?}
};

// {rows: [{id, symbol, price, sellable, shares, proceeds, basis, gain, left, gainPctNow, weightAfter, skip?}],
//  percent, baseValue, target, proceeds, gain, actualPct, missingPrice, notes}
Rec planSell(const Pf &p, const Prices &live, const SellOptions &o);
inline Rec planSell(const Rec &p, const Rec &live, const Rec &o) { return planSell(Pf::fromRec(p), pricesFromRec(live), SellOptions::fromRec(o)); }

// whole-dollar en-US currency, e.g. $1,235
QString money0(double n);

} // namespace tl
