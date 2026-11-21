// Portfolio buy/sell orders, guarded. Port of main/portfolioOrders.ts.

#include "portfolioorders.h"

#include "jsnum.h"

#include <QSet>
#include <QStringList>

namespace tl {

namespace {

QString symbolOf(const QVariant &v)
{
    if (isNull(v))
        return QString();
    if (isNumber(v))
        return jsString(v.toDouble()).trimmed().toUpper();
    return v.toString().trimmed().toUpper();
}

} // namespace

/*
 * Sends paper market orders for a linked portfolio's buy or sell plan. It exists for two buttons the user clicks (after a confirmation), and no
 * Claude tool calls it. The checks are made here, not in the screen: the portfolio must follow the account that is active (orders always go to the
 * active account), every symbol must be in the portfolio, mutual funds are refused because the paper account cannot trade them, and sizes must be
 * whole shares. A sell is only ever a plain `sell` of shares the account holds (the engine refuses anything else), never a short sale.
 */
Rec placePortfolioOrders(const OrderDeps &deps, qint64 portfolioId, const QString &side, const List &orders)
{
    if (side != QStringLiteral("buy") && side != QStringLiteral("sell"))
        return failError(QStringLiteral("An order is either a buy or a sell."));
    const std::optional<Rec> p = deps.portfolio(portfolioId);
    if (!p)
        return failError(QStringLiteral("That portfolio no longer exists."));
    const QVariant accountId = p->value(QStringLiteral("accountId"));
    if (isNull(accountId))
        return failError(QStringLiteral("This portfolio is not linked to a paper account."));
    if (double(deps.activeAccountId()) != toNumber(accountId))
        return failError(QStringLiteral("This portfolio follows a different paper account from the active one. Make that account active first, so the orders go where you expect."));
    List todo;
    for (const QVariant &b : orders)
        if (toNumber(b.toMap().value(QStringLiteral("qty"))) > 0)
            todo.append(b);
    if (todo.isEmpty())
        return failError(QStringLiteral("There is nothing to %1.").arg(side));
    const List items = p->value(QStringLiteral("items")).toList();
    QSet<QString> seen;
    for (const QVariant &bv : todo) {
        const Rec b = bv.toMap();
        const QString sym = symbolOf(b.value(QStringLiteral("symbol")));
        Rec item;
        bool found = false;
        for (const QVariant &iv : items)
            if (iv.toMap().value(QStringLiteral("symbol")).toString() == sym) {
                item = iv.toMap();
                found = true;
                break;
            }
        if (!found)
            return failError(QStringLiteral("%1 is not in this portfolio.").arg(sym.isEmpty() ? QStringLiteral("A symbol") : sym));
        if (item.value(QStringLiteral("kind")).toString() == QStringLiteral("fund"))
            return failError(QStringLiteral("%1 is a mutual fund, and the paper account cannot trade mutual funds.").arg(sym));
        const QVariant qty = b.value(QStringLiteral("qty"));
        if (!isInteger(qty) || qty.toDouble() > 1000000)
            return failError(QStringLiteral("%1: the paper account trades whole shares.").arg(sym));
        if (seen.contains(sym))
            return failError(QStringLiteral("%1 appears twice in the request.").arg(sym));
        seen.insert(sym);
    }
    const QString name = p->value(QStringLiteral("name")).toString();
    List outcomes, events;
    for (const QVariant &bv : todo) {
        const Rec b = bv.toMap();
        const QString symbol = symbolOf(b.value(QStringLiteral("symbol")));
        const QVariant qty = b.value(QStringLiteral("qty"));
        const Rec r = deps.place(Rec{
            {QStringLiteral("symbol"), symbol},
            {QStringLiteral("side"), side},
            {QStringLiteral("qty"), qty},
            {QStringLiteral("type"), QStringLiteral("market")},
            {QStringLiteral("tif"), QStringLiteral("day")},
            {QStringLiteral("strategy"), QStringLiteral("single")},
            {QStringLiteral("source"), QStringLiteral("user")},
            {QStringLiteral("note"), QStringLiteral("Portfolio %1: %2").arg(side == QStringLiteral("buy") ? QStringLiteral("plan") : QStringLiteral("sale"), name)},
        });
        if (r.value(QStringLiteral("ok")).toBool()) {
            events.append(r.value(QStringLiteral("events")).toList());
            outcomes.append(Rec{{QStringLiteral("symbol"), symbol}, {QStringLiteral("qty"), qty}, {QStringLiteral("ok"), true},
                                {QStringLiteral("filledNow"), r.value(QStringLiteral("filledNow"))}});
        } else {
            outcomes.append(Rec{{QStringLiteral("symbol"), symbol}, {QStringLiteral("qty"), qty}, {QStringLiteral("ok"), false},
                                {QStringLiteral("error"), r.value(QStringLiteral("errors")).toStringList().join(QLatin1Char(' '))}});
        }
    }
    return ok(Rec{{QStringLiteral("outcomes"), outcomes}, {QStringLiteral("events"), events}});
}

} // namespace tl
