// Portfolio planning maths. Port of shared/portfolio.ts.

#include "portfolio.h"

#include <numeric>

namespace tl {

namespace {

double r2(double n) { return mathRound(n * 100) / 100; }
double r3(double n) { return mathRound(n * 1000) / 1000; }
double floorTo(double n, double step) { return r3(std::floor(n / step + 1e-9) * step); }
// `x || 0`
double orZero(double x) { return std::isnan(x) ? 0 : x; }
double stepOf(const PfItem &it, const Pf &p) { return stepFor(it.kind, p.fractional); }

QHash<QString, double> numberMap(const QVariant &v)
{
    QHash<QString, double> out;
    const Rec m = v.toMap();
    for (auto it = m.cbegin(); it != m.cend(); ++it)
        if (!isNull(it.value()))
            out.insert(it.key(), toNumber(it.value()));
    return out;
}

Rec numberRec(const QHash<QString, double> &h)
{
    Rec out;
    for (auto it = h.cbegin(); it != h.cend(); ++it)
        out.insert(it.key(), it.value());
    return out;
}

struct Priced
{
    const PfItem *it;
    std::optional<double> price;
};

QVector<Priced> pricedOf(const QVector<PfItem> &items, const Prices &live)
{
    QVector<Priced> out;
    out.reserve(items.size());
    for (const PfItem &it : items)
        out.append(Priced{&it, priceOf(it, live)});
    return out;
}

} // namespace

List kinds()
{
    return List{Rec{{QStringLiteral("id"), QStringLiteral("stock")}, {QStringLiteral("label"), QStringLiteral("Stock")}},
                Rec{{QStringLiteral("id"), QStringLiteral("etf")}, {QStringLiteral("label"), QStringLiteral("ETF")}},
                Rec{{QStringLiteral("id"), QStringLiteral("fund")}, {QStringLiteral("label"), QStringLiteral("Mutual fund")}}};
}

QStringList kindIds() { return {QStringLiteral("stock"), QStringLiteral("etf"), QStringLiteral("fund")}; }

const QRegularExpression &symbolRe()
{
    static const QRegularExpression re(QStringLiteral("^[A-Z0-9.^=-]{1,15}$"));
    return re;
}

QString guessKind(const QString &symbol)
{
    static const QRegularExpression re(QStringLiteral("^[A-Z]{4}X$"));
    return re.match(symbol.toUpper()).hasMatch() ? QStringLiteral("fund") : QStringLiteral("stock");
}

PfItem PfItem::fromRec(const Rec &r)
{
    PfItem it;
    it.id = qint64(toNumber(r.value(QStringLiteral("id"))));
    it.symbol = r.value(QStringLiteral("symbol")).toString();
    it.name = r.value(QStringLiteral("name")).toString();
    it.kind = r.value(QStringLiteral("kind")).toString();
    it.targetPct = toNumber(r.value(QStringLiteral("targetPct")));
    it.shares = toNumber(r.value(QStringLiteral("shares")));
    it.cost = toNumber(r.value(QStringLiteral("cost")));
    const QVariant mp = r.value(QStringLiteral("manualPrice"));
    if (!isNull(mp))
        it.manualPrice = toNumber(mp);
    if (r.contains(QStringLiteral("noBuy")) && !isNull(r.value(QStringLiteral("noBuy"))))
        it.noBuy = r.value(QStringLiteral("noBuy")).toBool();
    return it;
}

Rec PfItem::toRec() const
{
    Rec r{{QStringLiteral("id"), id}, {QStringLiteral("symbol"), symbol}, {QStringLiteral("name"), name}, {QStringLiteral("kind"), kind},
          {QStringLiteral("targetPct"), targetPct}, {QStringLiteral("shares"), shares}, {QStringLiteral("cost"), cost},
          {QStringLiteral("manualPrice"), orNull(manualPrice)}};
    if (noBuy)
        r.insert(QStringLiteral("noBuy"), *noBuy);
    return r;
}

Pf Pf::fromRec(const Rec &r)
{
    Pf p;
    p.id = qint64(toNumber(r.value(QStringLiteral("id"))));
    p.name = r.value(QStringLiteral("name")).toString();
    p.amount = toNumber(r.value(QStringLiteral("amount")));
    p.mode = r.value(QStringLiteral("mode")).toString();
    p.fractional = r.value(QStringLiteral("fractional")).toBool();
    p.leftover = r.value(QStringLiteral("leftover")).toBool();
    const QVariant a = r.value(QStringLiteral("accountId"));
    if (!isNull(a))
        p.accountId = qint64(toNumber(a));
    for (const QVariant &v : r.value(QStringLiteral("items")).toList())
        p.items.append(PfItem::fromRec(v.toMap()));
    return p;
}

Rec Pf::toRec() const
{
    List its;
    for (const PfItem &it : items)
        its.append(it.toRec());
    return Rec{{QStringLiteral("id"), id}, {QStringLiteral("name"), name}, {QStringLiteral("amount"), amount}, {QStringLiteral("mode"), mode},
               {QStringLiteral("fractional"), fractional}, {QStringLiteral("leftover"), leftover},
               {QStringLiteral("accountId"), accountId ? QVariant(*accountId) : null()}, {QStringLiteral("items"), its}};
}

Prices pricesFromRec(const Rec &live)
{
    Prices out;
    for (auto it = live.cbegin(); it != live.cend(); ++it)
        if (const auto n = num(it.value()))
            out.insert(it.key(), *n);
    return out;
}

std::optional<double> priceOf(const PfItem &it, const Prices &live)
{
    const auto f = live.constFind(it.symbol);
    if (f != live.cend() && std::isfinite(*f) && *f > 0)
        return *f;
    if (it.manualPrice && *it.manualPrice > 0)
        return *it.manualPrice;
    return std::nullopt;
}

QVariant priceOf(const Rec &item, const Rec &live) { return orNull(priceOf(PfItem::fromRec(item), pricesFromRec(live))); }

// ---------------------------------------------------------------------------------------------------------------- planning

Rec planPortfolio(const Pf &p, const Prices &live)
{
    const QVector<Priced> priced = pricedOf(p.items, live);
    const qsizetype n = priced.size();
    double pctSum = 0;
    for (const PfItem &it : p.items)
        pctSum += orZero(it.targetPct);
    const double pctTotal = r2(pctSum);
    auto curVal = [](const PfItem &it, std::optional<double> price) { return price ? it.shares * *price : 0.0; };
    double hv = 0;
    for (const Priced &x : priced)
        hv += curVal(*x.it, x.price);
    const double holdingsValue = r2(hv);
    const double amount = std::max(0.0, orZero(p.amount));
    const double budget = p.mode == QStringLiteral("total") ? std::max(0.0, r2(amount - holdingsValue)) : amount;

    // money wanted per item
    QVector<double> want(n, 0);
    if (p.mode == QStringLiteral("new")) {
        for (qsizetype i = 0; i < n; ++i)
            want[i] = (amount * orZero(priced[i].it->targetPct)) / 100;
    } else {
        // top up whatever is under target; if that needs more than the cash available, share the cash in proportion to the shortfalls
        QVector<double> need(n, 0);
        double sum = 0;
        for (qsizetype i = 0; i < n; ++i) {
            const Priced &x = priced[i];
            need[i] = x.it->noBuy.value_or(false) ? 0 : std::max(0.0, (amount * orZero(x.it->targetPct)) / 100 - curVal(*x.it, x.price));
            sum += need[i];
        }
        const double scale = sum > 0 ? std::min(1.0, budget / sum) : 0;
        for (qsizetype i = 0; i < n; ++i)
            want[i] = need[i] * scale;
    }
    // holdings that cannot be bought here still count toward the portfolio, but the plan never spends on them
    for (qsizetype i = 0; i < n; ++i)
        if (priced[i].it->noBuy.value_or(false))
            want[i] = 0;

    struct Row
    {
        double shares, extra, cost;
    };
    QVector<Row> rows(n);
    List out;
    for (qsizetype i = 0; i < n; ++i) {
        const Priced &x = priced[i];
        const double step = stepOf(*x.it, p);
        const double shares = x.price && want[i] > 0 ? floorTo(want[i] / *x.price, step) : 0;
        rows[i] = Row{shares, 0, x.price ? r2(shares * *x.price) : 0};
    }

    double spent = 0;
    for (const Row &r : rows)
        spent += r.cost;
    if (p.leftover) {
        // buy one more step of whichever holding is furthest below its wanted amount, while the cash lasts
        for (int guard = 0; guard < 100000; ++guard) {
            const double room = budget - spent;
            qsizetype best = -1;
            double bestGap = 0;
            for (qsizetype i = 0; i < n; ++i) {
                const Priced &x = priced[i];
                if (!x.price || x.it->noBuy.value_or(false))
                    continue;
                const double step = stepOf(*x.it, p);
                if (step < 1)
                    continue; // fractional holdings are already exact; only whole-share holdings leave real cash behind
                if (*x.price * step > room + 1e-9)
                    continue;
                const double gap = want[i] - rows[i].cost;
                if (gap > bestGap + 1e-9) {
                    bestGap = gap;
                    best = i;
                }
            }
            if (best < 0)
                break;
            Row &r = rows[best];
            const double step = stepOf(*priced[best].it, p);
            r.shares = r3(r.shares + step);
            r.extra = r3(r.extra + step);
            r.cost = r2(r.shares * *priced[best].price);
            spent = 0;
            for (const Row &x : rows)
                spent += x.cost;
        }
    }
    double total = 0;
    for (const Row &r : rows)
        total += r.cost;
    spent = r2(total);

    QStringList missing;
    for (qsizetype i = 0; i < n; ++i) {
        const Priced &x = priced[i];
        const double cv = curVal(*x.it, x.price);
        const double tv = (amount * orZero(x.it->targetPct)) / 100;
        const double over = p.mode == QStringLiteral("total") && x.price ? std::max(0.0, cv - tv) : 0;
        out.append(Rec{
            {QStringLiteral("id"), x.it->id},
            {QStringLiteral("symbol"), x.it->symbol},
            {QStringLiteral("targetPct"), orZero(x.it->targetPct)},
            {QStringLiteral("price"), orNull(x.price)},
            {QStringLiteral("currentValue"), r2(cv)},
            {QStringLiteral("dollars"), r2(want[i])},
            {QStringLiteral("shares"), rows[i].shares},
            {QStringLiteral("extra"), rows[i].extra},
            {QStringLiteral("cost"), rows[i].cost},
            {QStringLiteral("overShares"), x.price && over > 0 ? r3(over / *x.price) : 0},
            {QStringLiteral("overValue"), r2(over)},
        });
        if (!x.price)
            missing << x.it->symbol;
    }
    return Rec{{QStringLiteral("rows"), out}, {QStringLiteral("pctTotal"), pctTotal}, {QStringLiteral("budget"), r2(budget)},
               {QStringLiteral("spent"), spent}, {QStringLiteral("leftover"), r2(budget - spent)}, {QStringLiteral("missingPrice"), missing},
               {QStringLiteral("holdingsValue"), holdingsValue}};
}

// ---------------------------------------------------------------------------------------------------------------- holdings

Rec summarizeHoldings(const QVector<PfItem> &items, const Prices &live)
{
    const QVector<Priced> priced = pricedOf(items, live);
    double tv = 0, tc = 0;
    for (const Priced &x : priced) {
        tv += x.price ? x.it->shares * *x.price : 0;
        // cost of holdings that have a price, so the gain compares like with like
        tc += x.price ? x.it->cost : 0;
    }
    const double totalValue = r2(tv);
    const double totalCost = r2(tc);
    List rows;
    QStringList missing;
    for (const Priced &x : priced) {
        const PfItem &it = *x.it;
        const std::optional<double> value = x.price ? std::optional<double>(r2(it.shares * *x.price)) : std::nullopt;
        const std::optional<double> gain = value ? std::optional<double>(r2(*value - it.cost)) : std::nullopt;
        std::optional<double> weight;
        if (value)
            weight = totalValue > 0 ? mathRound((*value / totalValue) * 10000) / 100 : 0;
        rows.append(Rec{
            {QStringLiteral("id"), it.id},
            {QStringLiteral("symbol"), it.symbol},
            {QStringLiteral("shares"), it.shares},
            {QStringLiteral("cost"), it.cost},
            {QStringLiteral("avgCost"), it.shares > 0 && it.cost > 0 ? QVariant(r2(it.cost / it.shares)) : null()},
            {QStringLiteral("price"), orNull(x.price)},
            {QStringLiteral("value"), orNull(value)},
            {QStringLiteral("gain"), orNull(gain)},
            {QStringLiteral("gainPct"), gain && it.cost > 0 ? QVariant(mathRound((*gain / it.cost) * 10000) / 100) : null()},
            {QStringLiteral("weight"), orNull(weight)},
            {QStringLiteral("targetPct"), orZero(it.targetPct)},
            {QStringLiteral("drift"), weight ? QVariant(mathRound((*weight - orZero(it.targetPct)) * 100) / 100) : null()},
        });
        if (it.shares > 0 && !x.price)
            missing << it.symbol;
    }
    const double gain = r2(totalValue - totalCost);
    return Rec{{QStringLiteral("rows"), rows}, {QStringLiteral("totalValue"), totalValue}, {QStringLiteral("totalCost"), totalCost},
               {QStringLiteral("gain"), gain}, {QStringLiteral("gainPct"), totalCost > 0 ? QVariant(mathRound((gain / totalCost) * 10000) / 100) : null()},
               {QStringLiteral("missingPrice"), missing}};
}

Rec summarizeHoldings(const List &items, const Rec &live)
{
    QVector<PfItem> its;
    for (const QVariant &v : items)
        its.append(PfItem::fromRec(v.toMap()));
    return summarizeHoldings(its, pricesFromRec(live));
}

QVector<double> equalWeights(int n)
{
    if (n <= 0)
        return {};
    const double each = std::floor(10000.0 / n) / 100;
    QVector<double> out(n, each);
    out[n - 1] = r2(100 - each * (n - 1));
    return out;
}

QVector<double> normalizeWeights(const QVector<double> &w)
{
    const double sum = std::accumulate(w.cbegin(), w.cend(), 0.0);
    if (sum <= 0)
        return QVector<double>(w.size(), 0);
    QVector<double> out;
    for (const double x : w)
        out.append(std::floor((x / sum) * 10000) / 100);
    const double total = std::accumulate(out.cbegin(), out.cend(), 0.0);
    out.last() = r2(out.last() + (100 - total));
    return out;
}

List equalWeightsList(int n)
{
    List out;
    for (const double d : equalWeights(n))
        out.append(d);
    return out;
}

List normalizeWeights(const List &w)
{
    QVector<double> in;
    for (const QVariant &v : w)
        in.append(toNumber(v));
    List out;
    for (const double d : normalizeWeights(in))
        out.append(d);
    return out;
}

// ---------------------------------------------------------------------------------------------------------------- linked accounts

AccountLink AccountLink::fromRec(const Rec &r)
{
    AccountLink l;
    l.id = qint64(toNumber(r.value(QStringLiteral("id"))));
    l.name = r.value(QStringLiteral("name")).toString();
    l.cash = toNumber(r.value(QStringLiteral("cash")));
    l.buyingPower = toNumber(r.value(QStringLiteral("buyingPower")));
    for (const QVariant &v : r.value(QStringLiteral("positions")).toList()) {
        const Rec p = v.toMap();
        l.positions.append(Position{p.value(QStringLiteral("symbol")).toString(), toNumber(p.value(QStringLiteral("qty"))),
                                    toNumber(p.value(QStringLiteral("avg")))});
    }
    l.pendingBuys = numberMap(r.value(QStringLiteral("pendingBuys")));
    if (!isNull(r.value(QStringLiteral("pendingSells"))))
        l.pendingSells = numberMap(r.value(QStringLiteral("pendingSells")));
    return l;
}

Rec Effective::toRec() const
{
    return Rec{{QStringLiteral("pf"), pf.toRec()}, {QStringLiteral("availableCash"), availableCash}, {QStringLiteral("shorts"), shorts},
               {QStringLiteral("pending"), numberRec(pending)}, {QStringLiteral("pendingSold"), numberRec(pendingSold)},
               {QStringLiteral("sellable"), numberRec(sellable)}};
}

/*
 * The portfolio the way a linked one is planned and valued. Stocks and ETFs take their shares and cost from the account's positions (plus orders
 * already sent but not filled, so the plan never buys the same shares twice); mutual funds cannot trade in the paper account, so they keep the
 * numbers the user typed and are never bought here. The amount is holdings plus available cash in "total size" mode, which tops up whatever is
 * under target using only the cash on hand. An unlinked portfolio is returned unchanged.
 */
Effective effectivePortfolio(const Pf &p, const std::optional<AccountLink> &link, const Prices &live)
{
    Effective e;
    if (!link || !p.accountId) {
        e.pf = p;
        for (const PfItem &i : p.items)
            e.sellable.insert(i.symbol, i.shares);
        return e;
    }
    double pendingCost = 0;
    QVector<PfItem> items;
    for (const PfItem &src : p.items) {
        PfItem it = src;
        if (it.kind == QStringLiteral("fund")) {
            e.sellable.insert(it.symbol, 0);
            it.noBuy = true;
            items.append(it);
            continue;
        }
        const AccountLink::Position *pos = nullptr;
        for (const AccountLink::Position &x : link->positions)
            if (x.symbol == it.symbol) {
                pos = &x;
                break;
            }
        if (pos && pos->qty < 0)
            e.shorts << it.symbol;
        const double qty = std::max(0.0, pos ? pos->qty : 0);
        const double pend = std::max(0.0, link->pendingBuys.value(it.symbol, 0));
        const double sold = std::min(qty, std::max(0.0, link->pendingSells ? link->pendingSells->value(it.symbol, 0) : 0));
        const std::optional<double> price = priceOf(it, live);
        if (pend > 0) {
            e.pending.insert(it.symbol, pend);
            if (price)
                pendingCost += pend * *price;
        }
        if (sold > 0)
            e.pendingSold.insert(it.symbol, sold);
        e.sellable.insert(it.symbol, qty - sold);
        const double kept = qty - sold;
        it.shares = kept + pend;
        it.cost = r2(kept * (pos ? pos->avg : 0) + (price ? pend * *price : 0));
        it.noBuy = false;
        items.append(it);
    }
    e.availableCash = std::max(0.0, r2(std::min(link->cash, link->buyingPower) - pendingCost));
    double holdings = 0;
    for (const PfItem &it : items) {
        const auto pr = priceOf(it, live);
        holdings += pr ? it.shares * *pr : 0;
    }
    e.pf = p;
    e.pf.items = items;
    e.pf.mode = QStringLiteral("total");
    e.pf.fractional = false;
    e.pf.amount = r2(holdings + e.availableCash);
    return e;
}

Rec effectivePortfolio(const Rec &p, const QVariant &link, const Rec &live)
{
    std::optional<AccountLink> l;
    if (!isNull(link))
        l = AccountLink::fromRec(link.toMap());
    return effectivePortfolio(Pf::fromRec(p), l, pricesFromRec(live)).toRec();
}

// ---------------------------------------------------------------------------------------------------------------- selling

SellOptions SellOptions::fromRec(const Rec &r)
{
    SellOptions o;
    o.percent = toNumber(r.value(QStringLiteral("percent")));
    if (!isNumber(r.value(QStringLiteral("percent"))))
        o.percent = NAN; // Number.isFinite: a string is not a number
    o.method = r.value(QStringLiteral("method")).toString();
    o.onlyProfit = r.value(QStringLiteral("onlyProfit")).toBool();
    if (!isNull(r.value(QStringLiteral("sellable"))))
        o.sellable = numberMap(r.value(QStringLiteral("sellable")));
    return o;
}

QString money0(double n) { return money(n, 0); }

/*
 * How many shares to sell to raise `percent` of the value of what can be sold, taking profits. Shares are whole (or thousandths where fractions are
 * allowed), never beyond what is sellable, and the total never goes over the percentage asked for, so the result is a little under it.
 * `sellable` overrides the shares that can be sold (a linked portfolio passes the account's free shares); without it every share is sellable.
 */
Rec planSell(const Pf &p, const Prices &live, const SellOptions &o)
{
    const double pct = std::min(100.0, std::max(0.0, std::isfinite(o.percent) ? o.percent : 0));
    const QVector<Priced> priced = pricedOf(p.items, live);
    const qsizetype n = priced.size();
    auto canSell = [&](const Priced &x) -> double {
        if (!x.price || x.it->noBuy.value_or(false))
            return 0;
        return std::max(0.0, std::min(x.it->shares, o.sellable ? o.sellable->value(x.it->symbol, 0) : x.it->shares));
    };
    auto avg = [](const PfItem &it) -> std::optional<double> {
        return it.shares > 0 && it.cost > 0 ? std::optional<double>(it.cost / it.shares) : std::nullopt;
    };
    QVector<double> sellN(n), sellValue(n);
    double bv = 0, totalValue = 0;
    for (qsizetype i = 0; i < n; ++i) {
        sellN[i] = canSell(priced[i]);
        sellValue[i] = priced[i].price ? sellN[i] * *priced[i].price : 0;
        bv += sellValue[i];
        totalValue += priced[i].price ? priced[i].it->shares * *priced[i].price : 0;
    }
    const double baseValue = r2(bv);
    const double target = r2((baseValue * pct) / 100);

    QVector<QString> why(n); // null QString = undefined
    for (qsizetype i = 0; i < n; ++i) {
        const Priced &x = priced[i];
        if (x.it->shares <= 0)
            continue;
        if (!x.price)
            why[i] = QStringLiteral("no price");
        else if (x.it->noBuy.value_or(false))
            why[i] = QStringLiteral("can't sell here");
        else if (sellN[i] <= 0)
            why[i] = QStringLiteral("nothing free to sell");
        else if (o.onlyProfit) {
            const auto a = avg(*x.it);
            if (!a)
                why[i] = QStringLiteral("cost unknown");
            else if (*x.price <= *a)
                why[i] = QStringLiteral("not in profit");
        }
    }
    QVector<bool> cand(n);
    QVector<double> candValue(n);
    double candTotal = 0;
    for (qsizetype i = 0; i < n; ++i) {
        cand[i] = sellN[i] > 0 && why[i].isNull();
        candValue[i] = cand[i] ? sellValue[i] : 0;
        candTotal += candValue[i];
    }

    // dollars to take from each candidate
    QVector<double> dollars(n, 0);
    if (target > 0 && candTotal > 0) {
        if (o.method == QStringLiteral("proportional")) {
            const double f = std::min(1.0, target / candTotal);
            for (qsizetype i = 0; i < n; ++i)
                dollars[i] = candValue[i] * f;
        } else {
            QVector<double> excess(n, 0);
            double ex = 0;
            for (qsizetype i = 0; i < n; ++i) {
                const Priced &x = priced[i];
                if (cand[i] && x.price)
                    excess[i] = std::min(candValue[i], std::max(0.0, x.it->shares * *x.price - (orZero(x.it->targetPct) / 100) * totalValue));
                ex += excess[i];
            }
            if (ex >= target) {
                for (qsizetype i = 0; i < n; ++i)
                    dollars[i] = ex > 0 ? (excess[i] * target) / ex : 0;
            } else {
                QVector<double> room(n);
                double rt = 0;
                for (qsizetype i = 0; i < n; ++i) {
                    room[i] = candValue[i] - excess[i];
                    rt += room[i];
                }
                const double rest = std::min(target - ex, rt);
                for (qsizetype i = 0; i < n; ++i)
                    dollars[i] = excess[i] + (rt > 0 ? (room[i] * rest) / rt : 0);
            }
        }
    }

    // whole shares (or thousandths): round each holding down, then add one more step to the holdings with the largest remainders while the total
    // stays at or under the target
    QVector<double> steps(n), qty(n, 0);
    double raised = 0;
    for (qsizetype i = 0; i < n; ++i) {
        const Priced &x = priced[i];
        steps[i] = stepOf(*x.it, p);
        if (x.price && cand[i] && dollars[i] > 0)
            qty[i] = std::max(0.0, std::min(r3(std::floor(dollars[i] / *x.price / steps[i] + 1e-9) * steps[i]), sellN[i]));
        raised += x.price ? qty[i] * *x.price : 0;
    }
    for (int guard = 0; guard < 10000; ++guard) {
        qsizetype best = -1;
        double bestGap = 0;
        for (qsizetype i = 0; i < n; ++i) {
            const Priced &x = priced[i];
            if (!x.price || !cand[i])
                continue;
            const double gap = dollars[i] - qty[i] * *x.price;
            if (gap > bestGap + 1e-9 && qty[i] + steps[i] <= sellN[i] + 1e-9 && raised + *x.price * steps[i] <= target + 1e-9) {
                bestGap = gap;
                best = i;
            }
        }
        if (best < 0)
            break;
        qty[best] = r3(qty[best] + steps[best]);
        raised += *priced[best].price * steps[best];
    }

    QVector<double> proceedsOf(n), leftOf(n);
    QVector<std::optional<double>> gainOf(n);
    double proceedsSum = 0;
    for (qsizetype i = 0; i < n; ++i) {
        const Priced &x = priced[i];
        const auto a = avg(*x.it);
        proceedsOf[i] = x.price ? r2(qty[i] * *x.price) : 0;
        gainOf[i] = a && x.price ? std::optional<double>(r2(qty[i] * (*x.price - *a))) : std::nullopt;
        leftOf[i] = r3(x.it->shares - qty[i]);
        proceedsSum += proceedsOf[i];
    }
    const double proceeds = r2(proceedsSum);
    const double remaining = totalValue - proceeds;
    List rows;
    double gainSum = 0;
    QStringList missing;
    for (qsizetype i = 0; i < n; ++i) {
        const Priced &x = priced[i];
        const auto a = avg(*x.it);
        QVariant weightAfter = null();
        if (x.price)
            weightAfter = remaining > 0 ? mathRound(((leftOf[i] * *x.price) / remaining) * 10000) / 100 : 0.0;
        Rec r{
            {QStringLiteral("id"), x.it->id},
            {QStringLiteral("symbol"), x.it->symbol},
            {QStringLiteral("price"), orNull(x.price)},
            {QStringLiteral("sellable"), r3(sellN[i])},
            {QStringLiteral("shares"), qty[i]},
            {QStringLiteral("proceeds"), proceedsOf[i]},
            {QStringLiteral("basis"), a ? QVariant(r2(qty[i] * *a)) : null()},
            {QStringLiteral("gain"), orNull(gainOf[i])},
            {QStringLiteral("left"), leftOf[i]},
            {QStringLiteral("gainPctNow"), a && x.price ? QVariant(mathRound(((*x.price - *a) / *a) * 10000) / 100) : null()},
            {QStringLiteral("weightAfter"), weightAfter},
        };
        if (!why[i].isNull())
            r.insert(QStringLiteral("skip"), why[i]);
        rows.append(r);
        gainSum += gainOf[i].value_or(0);
        if (x.it->shares > 0 && !x.price)
            missing << x.it->symbol;
    }
    QStringList notes;
    if (pct > 0 && baseValue == 0)
        notes << QStringLiteral("There is nothing to sell: no holdings with shares and a price that can be sold from here.");
    else if (pct > 0 && target > 0 && candTotal + 1e-9 < target)
        notes << QStringLiteral("Only %1 of the %2 asked for can be raised from the holdings that qualify%3.")
                     .arg(money0(candTotal), money0(target), o.onlyProfit ? QStringLiteral(" (only those in profit are included)") : QString());
    else if (pct > 0 && target > 0 && proceeds == 0)
        notes << QStringLiteral("At this size no holding rounds to even one share. Raise the percentage.");
    return Rec{{QStringLiteral("rows"), rows}, {QStringLiteral("percent"), pct}, {QStringLiteral("baseValue"), baseValue},
               {QStringLiteral("target"), target}, {QStringLiteral("proceeds"), proceeds}, {QStringLiteral("gain"), r2(gainSum)},
               {QStringLiteral("actualPct"), baseValue > 0 ? mathRound((proceeds / baseValue) * 10000) / 100 : 0},
               {QStringLiteral("missingPrice"), missing}, {QStringLiteral("notes"), notes}};
}

} // namespace tl
