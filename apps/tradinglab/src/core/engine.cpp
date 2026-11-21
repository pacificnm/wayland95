// Port of main/trading.ts.
#include "engine.h"

#include "db.h"
#include "http.h"
#include "nytime.h"

#include <algorithm>
#include <cmath>
#include <map>

namespace tl {

namespace {
const QString ACTIVE = QStringLiteral("('pending','working')");
const FillGrain ONE_MINUTE{QStringLiteral("1min"), 60, 0, QStringLiteral("1-minute")};

QString s(const Rec &r, const char *k) { return r.value(QLatin1String(k)).toString(); }
qint64 i(const Rec &r, const char *k) { return r.value(QLatin1String(k)).toLongLong(); }
double f(const Rec &r, const char *k) { return r.value(QLatin1String(k)).toDouble(); }
bool has(const Rec &r, const char *k) { return !isNull(r.value(QLatin1String(k))); }
QVariant opt(const QVariant &v) { return v.isValid() && !isNull(v) ? v : null(); }

double buyingPowerFor(const QString &type, double equity, double cash, double gross)
{
    // cash: you can only spend the cash you have. margin: Reg T style 2:1.
    return std::max(0.0, type == QLatin1String("cash") ? cash : 2 * equity - gross);
}

Db &db() { return Db::local(); }
}

Engine::Engine(Market market, std::function<qint64()> activeAccount, std::function<qint64()> clock)
    : m_market(std::move(market))
    , m_active(std::move(activeAccount))
    , m_clock(std::move(clock))
{
}

std::optional<FillGrain> Engine::grain() const { return m_market.grain ? m_market.grain() : std::optional<FillGrain>(ONE_MINUTE); }
qint64 Engine::nextBarTime(qint64 t) const { return nextBar(t, grain().value_or(ONE_MINUTE)); }

std::optional<double> Engine::quoteOrNull(const QString &symbol)
{
    try { return m_market.quote(symbol); } catch (...) { return std::nullopt; }
}

// Every trading row belongs to an account. New orders go to the active account; working orders in all accounts keep filling.
Rec Engine::account(qint64 id)
{
    const auto r = db().one(QStringLiteral("SELECT id, name, type, broker, url, cash, starting_cash FROM accounts WHERE id = ?"), {id});
    if (!r)
        throw Error(QStringLiteral("Account not found."));
    return *r;
}

double Engine::positionQtyOf(qint64 accountId, const QString &symbol)
{
    const auto r = db().one(QStringLiteral("SELECT qty FROM positions WHERE account_id = ? AND symbol = ?"), {accountId, symbol});
    return r ? f(*r, "qty") : 0;
}

Rec Engine::values(qint64 accountId)
{
    std::lock_guard<std::recursive_mutex> g(m_lock);
    const Rec acc = account(accountId);
    const List rows = db().all(QStringLiteral("SELECT symbol, qty, avg_price FROM positions WHERE account_id = ? ORDER BY symbol"), {accountId});
    std::vector<std::optional<double>> marks(rows.size());
    std::vector<std::function<void()>> tasks;
    for (int k = 0; k < rows.size(); ++k)
        tasks.push_back([this, &marks, &rows, k] { marks[k] = quoteOrNull(s(rows[k].toMap(), "symbol")); });
    parallel(tasks);
    double mv = 0, gross = 0, unreal = 0;
    List positions;
    for (int k = 0; k < rows.size(); ++k) {
        const Rec r = rows[k].toMap();
        const double qty = f(r, "qty"), avg = f(r, "avg_price");
        const auto mark = marks[k];
        const double m = mark.value_or(avg);
        mv += qty * m;
        gross += std::fabs(qty * m);
        std::optional<double> u;
        if (mark) { u = (*mark - avg) * qty; unreal += *u; }
        positions.append(Rec{{QStringLiteral("symbol"), s(r, "symbol")}, {QStringLiteral("qty"), qty}, {QStringLiteral("avg"), avg},
                             {QStringLiteral("mark"), orNull(mark)}, {QStringLiteral("marketValue"), mark ? QVariant(qty * *mark) : null()},
                             {QStringLiteral("unrealized"), orNull(u)},
                             {QStringLiteral("unrealizedPct"), u ? QVariant((*u / std::fabs(avg * qty)) * 100) : null()}});
    }
    const double cash = f(acc, "cash");
    const double equity = cash + mv;
    const auto rs = db().one(QStringLiteral("SELECT SUM(realized_pl) AS s FROM fills WHERE account_id = ?"), {accountId});
    const double realized = rs && has(*rs, "s") ? f(*rs, "s") : 0;
    return Rec{{QStringLiteral("cash"), cash}, {QStringLiteral("startingCash"), f(acc, "starting_cash")}, {QStringLiteral("equity"), equity},
               {QStringLiteral("buyingPower"), buyingPowerFor(s(acc, "type"), equity, cash, gross)}, {QStringLiteral("positions"), positions},
               {QStringLiteral("realized"), realized}, {QStringLiteral("unrealized"), unreal}};
}

double Engine::reserved(qint64 accountId)
{
    double sum = 0;
    for (const QVariant &v : db().all(QStringLiteral("SELECT * FROM orders WHERE status = 'working' AND account_id = ?"), {accountId})) {
        const Rec o = v.toMap();
        if (!opensPosition(s(o, "side")))
            continue;
        const double price = has(o, "limit_price") ? f(o, "limit_price") : has(o, "stop_price") ? f(o, "stop_price") : 0;
        sum += f(o, "qty") * price;
    }
    return sum;
}

Rec Engine::snapshot()
{
    std::lock_guard<std::recursive_mutex> g(m_lock);
    const qint64 id = m_active();
    const Rec v = values(id);
    const Rec acc = account(id);
    const double held = reserved(id);
    List orders = db().all(QStringLiteral("SELECT * FROM orders WHERE status IN %1 AND account_id = ? ORDER BY id DESC").arg(ACTIVE), {id});
    orders += db().all(QStringLiteral("SELECT * FROM orders WHERE status NOT IN %1 AND account_id = ? ORDER BY id DESC LIMIT 80").arg(ACTIVE), {id});
    const List fills = db().all(QStringLiteral("SELECT * FROM fills WHERE account_id = ? ORDER BY id DESC LIMIT 80"), {id});
    const double startingCash = f(v, "startingCash"), equity = f(v, "equity");
    const Rec accountView{
        {QStringLiteral("id"), id}, {QStringLiteral("name"), s(acc, "name")}, {QStringLiteral("broker"), s(acc, "broker")}, {QStringLiteral("url"), s(acc, "url")},
        {QStringLiteral("type"), s(acc, "type") == QLatin1String("cash") ? QStringLiteral("cash") : QStringLiteral("margin")},
        {QStringLiteral("cash"), f(v, "cash")}, {QStringLiteral("startingCash"), startingCash}, {QStringLiteral("equity"), equity},
        {QStringLiteral("buyingPower"), std::max(0.0, f(v, "buyingPower") - held)}, {QStringLiteral("reserved"), held},
        {QStringLiteral("realizedPl"), f(v, "realized")}, {QStringLiteral("unrealizedPl"), f(v, "unrealized")},
        {QStringLiteral("totalReturnPct"), startingCash != 0 ? ((equity - startingCash) / startingCash) * 100 : 0.0}};
    const auto gr = grain();
    return Rec{{QStringLiteral("account"), accountView}, {QStringLiteral("positions"), v.value(QStringLiteral("positions"))},
               {QStringLiteral("orders"), orders}, {QStringLiteral("fills"), fills}, {QStringLiteral("at"), m_clock()},
               {QStringLiteral("fillBars"), gr ? QVariant(Rec{{QStringLiteral("label"), gr->label}, {QStringLiteral("seconds"), gr->seconds}}) : null()}};
}

Rec Engine::view(qint64 accountId)
{
    std::lock_guard<std::recursive_mutex> g(m_lock);
    const Rec acc = account(accountId);
    const Rec v = values(accountId);
    auto bySymbol = [&](const char *side) {
        Rec out;
        for (const QVariant &r : db().all(QStringLiteral("SELECT symbol, SUM(qty) AS qty FROM orders WHERE account_id = ? AND status IN ('pending','working') AND side = '%1' AND type = 'market' AND role = 'single' GROUP BY symbol").arg(QLatin1String(side)), {accountId}))
            out.insert(s(r.toMap(), "symbol"), r.toMap().value(QStringLiteral("qty")));
        return out;
    };
    List positions;
    for (const QVariant &p : v.value(QStringLiteral("positions")).toList()) {
        const Rec pr = p.toMap();
        positions.append(Rec{{QStringLiteral("symbol"), pr.value(QStringLiteral("symbol"))}, {QStringLiteral("qty"), pr.value(QStringLiteral("qty"))}, {QStringLiteral("avg"), pr.value(QStringLiteral("avg"))}});
    }
    return Rec{{QStringLiteral("id"), accountId}, {QStringLiteral("name"), s(acc, "name")}, {QStringLiteral("type"), s(acc, "type")},
               {QStringLiteral("cash"), v.value(QStringLiteral("cash"))}, {QStringLiteral("equity"), v.value(QStringLiteral("equity"))},
               {QStringLiteral("buyingPower"), std::max(0.0, f(v, "buyingPower") - reserved(accountId))},
               {QStringLiteral("positions"), positions}, {QStringLiteral("pendingBuys"), bySymbol("buy")}, {QStringLiteral("pendingSells"), bySymbol("sell")}};
}

// ---------------------------------------------------------------- placing
qint64 Engine::insert(qint64 accountId, const QString &group, const OrderSpec &spec, const Rec &o, qint64 now)
{
    const RunResult r = db().run(QStringLiteral(
        "INSERT INTO orders (account_id, group_id, parent_id, role, symbol, side, qty, type, limit_price, stop_price, trail_amount, trail_unit, extreme, tif, status, created_at, start_at, expires_at, source, note) "
        "VALUES (?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?)"),
        {accountId, group, opt(o.value(QStringLiteral("parent_id"))), o.value(QStringLiteral("role")), spec.symbol, o.value(QStringLiteral("side")), spec.qtyInt(),
         o.value(QStringLiteral("type")), opt(o.value(QStringLiteral("limit_price"))), opt(o.value(QStringLiteral("stop_price"))),
         opt(o.value(QStringLiteral("trail_amount"))), o.value(QStringLiteral("trail_unit")).toString().isEmpty() ? null() : o.value(QStringLiteral("trail_unit")),
         opt(o.value(QStringLiteral("extreme"))), o.value(QStringLiteral("tif")), o.value(QStringLiteral("status")), now, nextBarTime(now),
         opt(o.value(QStringLiteral("expires_at"))), spec.source.isEmpty() ? QStringLiteral("user") : spec.source, spec.note.isEmpty() ? null() : QVariant(spec.note)});
    return r.lastInsertRowid;
}

Rec Engine::place(const Rec &specIn)
{
    std::lock_guard<std::recursive_mutex> g(m_lock);
    OrderSpec spec = OrderSpec::fromRec(specIn);
    spec.symbol = spec.symbol.trimmed().toUpper();
    const auto last = quoteOrNull(spec.symbol);
    if (!last)
        return failErrors({QStringLiteral("No live price for %1. Check the symbol and your market-data connection.").arg(spec.symbol)});
    const qint64 accountId = m_active();
    const Rec v = values(accountId);
    AnalysisCtx ctx;
    ctx.last = last;
    ctx.positionQty = positionQtyOf(accountId, spec.symbol);
    ctx.buyingPower = f(v, "buyingPower");
    ctx.reserved = reserved(accountId);
    ctx.canShort = s(account(accountId), "type") != QLatin1String("cash");
    const Analysis a = analyze(spec, ctx);
    if (!a.errors.isEmpty())
        return failErrors(a.errors);

    if (!grain() && (spec.type != QLatin1String("market") || spec.strategy != QLatin1String("single")))
        return failErrors({QStringLiteral("Your market-data plan has no intraday bars, so only plain market orders can be filled. Limit, stop, bracket and OCO orders need intraday data.")});
    if (!grain() && !isMarketOpen(m_clock()))
        return failErrors({QStringLiteral("The market is closed and your plan has no intraday bars, so a market order cannot be filled until the market opens.")});
    const qint64 now = m_clock();
    const QString group = uuid();
    const QVariant expires = spec.tif == QLatin1String("day") ? QVariant(sessionCloseAfter(now)) : null();
    QVariantList ids;
    db().tx([&] {
        const QString role = spec.strategy == QLatin1String("bracket") ? QStringLiteral("entry") : spec.strategy == QLatin1String("oco") ? QStringLiteral("oco_a") : QStringLiteral("single");
        const qint64 mainId = insert(accountId, group, spec, Rec{
            {QStringLiteral("role"), role}, {QStringLiteral("side"), spec.side}, {QStringLiteral("type"), spec.type}, {QStringLiteral("tif"), spec.tif},
            {QStringLiteral("status"), QStringLiteral("working")}, {QStringLiteral("limit_price"), spec.limit}, {QStringLiteral("stop_price"), spec.stop},
            {QStringLiteral("trail_amount"), spec.trailAmount}, {QStringLiteral("trail_unit"), spec.trailUnit},
            {QStringLiteral("extreme"), spec.type == QLatin1String("trailing_stop") ? QVariant(*last) : QVariant()}, {QStringLiteral("expires_at"), expires}}, now);
        ids.append(mainId);
        if (spec.strategy == QLatin1String("bracket")) {
            const QString exit = exitSide(spec.side);
            ids.append(insert(accountId, group, spec, Rec{{QStringLiteral("role"), QStringLiteral("target")}, {QStringLiteral("parent_id"), mainId}, {QStringLiteral("side"), exit},
                                                          {QStringLiteral("type"), QStringLiteral("limit")}, {QStringLiteral("tif"), QStringLiteral("gtc")}, {QStringLiteral("status"), QStringLiteral("pending")},
                                                          {QStringLiteral("limit_price"), spec.target}}, now));
            if (spec.protect) {
                const StopLeg &p = *spec.protect;
                ids.append(insert(accountId, group, spec, Rec{{QStringLiteral("role"), QStringLiteral("stop")}, {QStringLiteral("parent_id"), mainId}, {QStringLiteral("side"), exit},
                                                              {QStringLiteral("type"), p.type}, {QStringLiteral("tif"), QStringLiteral("gtc")}, {QStringLiteral("status"), QStringLiteral("pending")},
                                                              {QStringLiteral("stop_price"), p.stop}, {QStringLiteral("limit_price"), p.limit},
                                                              {QStringLiteral("trail_amount"), p.trailAmount}, {QStringLiteral("trail_unit"), p.trailUnit}}, now));
            }
        } else if (spec.strategy == QLatin1String("oco") && spec.protect) {
            const StopLeg &p = *spec.protect;
            ids.append(insert(accountId, group, spec, Rec{{QStringLiteral("role"), QStringLiteral("oco_b")}, {QStringLiteral("side"), spec.side}, {QStringLiteral("type"), p.type},
                                                          {QStringLiteral("tif"), spec.tif}, {QStringLiteral("status"), QStringLiteral("working")},
                                                          {QStringLiteral("stop_price"), p.stop}, {QStringLiteral("limit_price"), p.limit},
                                                          {QStringLiteral("trail_amount"), p.trailAmount}, {QStringLiteral("trail_unit"), p.trailUnit},
                                                          {QStringLiteral("extreme"), p.type == QLatin1String("trailing_stop") ? QVariant(*last) : QVariant()},
                                                          {QStringLiteral("expires_at"), expires}}, now));
        }
    });
    const List events = process(spec.symbol);
    bool filledNow = false;
    for (const QVariant &e : events)
        if (e.toMap().value(QStringLiteral("text")).toString().contains(QLatin1String("filled")))
            filledNow = true;
    return ok(Rec{{QStringLiteral("orderIds"), ids}, {QStringLiteral("filledNow"), filledNow}, {QStringLiteral("events"), events}});
}

// ---------------------------------------------------------------- filling
void Engine::cancelRow(qint64 id, const QString &reason, const QString &status)
{
    db().run(QStringLiteral("UPDATE orders SET status = ?, reason = ? WHERE id = ? AND status IN %1").arg(ACTIVE), {status, reason, id});
    for (const QVariant &c : db().all(QStringLiteral("SELECT * FROM orders WHERE parent_id = ? AND status IN %1").arg(ACTIVE), {id}))
        cancelRow(i(c.toMap(), "id"), QStringLiteral("Parent order %1").arg(status), QStringLiteral("cancelled"));
}

static Rec event(const QString &text) { return Rec{{QStringLiteral("text"), text}}; }

bool Engine::fill(const Rec &o, double price, qint64 time, List &events)
{
    const QString side = s(o, "side"), symbol = s(o, "symbol"), role = s(o, "role");
    const bool buy = isBuy(side);
    const qint64 accountId = i(o, "account_id"), id = i(o, "id");
    const double qty = f(o, "qty");
    const auto pos = db().one(QStringLiteral("SELECT qty, avg_price FROM positions WHERE account_id = ? AND symbol = ?"), {accountId, symbol});
    const double held = pos ? f(*pos, "qty") : 0;
    const QString qtyS = QString::number(qty);
    // exits must match a real position (a leftover stop after the position was closed elsewhere)
    if (side == QLatin1String("sell") && held < qty) {
        cancelRow(id, QStringLiteral("No shares to sell"), QStringLiteral("rejected"));
        events.append(event(QStringLiteral("%1: %2 %3 rejected (no position).").arg(symbol, sideLabel(side), qtyS)));
        return false;
    }
    if (side == QLatin1String("buy_to_cover") && -held < qty) {
        cancelRow(id, QStringLiteral("No short position to cover"), QStringLiteral("rejected"));
        events.append(event(QStringLiteral("%1: %2 %3 rejected (no short position).").arg(symbol, sideLabel(side), qtyS)));
        return false;
    }
    double realized = 0;
    db().tx([&] {
        const double signedQty = buy ? qty : -qty;
        const double newQty = held + signedQty;
        if (opensPosition(side)) {
            const double avg = ((std::fabs(held) * (pos ? f(*pos, "avg_price") : 0)) + qty * price) / std::fabs(newQty);
            db().run(QStringLiteral("INSERT INTO positions (account_id, symbol, qty, avg_price) VALUES (?,?,?,?) ON CONFLICT(account_id, symbol) DO UPDATE SET qty = excluded.qty, avg_price = excluded.avg_price"),
                     {accountId, symbol, qint64(newQty), avg});
        } else {
            const double avgPrice = pos ? f(*pos, "avg_price") : 0;
            realized = (buy ? (avgPrice - price) : (price - avgPrice)) * qty;
            if (newQty == 0)
                db().run(QStringLiteral("DELETE FROM positions WHERE account_id = ? AND symbol = ?"), {accountId, symbol});
            else
                db().run(QStringLiteral("UPDATE positions SET qty = ? WHERE account_id = ? AND symbol = ?"), {qint64(newQty), accountId, symbol});
        }
        db().run(QStringLiteral("UPDATE accounts SET cash = cash + ? WHERE id = ?"), {buy ? -qty * price : qty * price, accountId});
        db().run(QStringLiteral("INSERT INTO fills (account_id, order_id, symbol, side, qty, price, time, realized_pl) VALUES (?,?,?,?,?,?,?,?)"),
                 {accountId, id, symbol, side, qint64(qty), price, time, realized});
        db().run(QStringLiteral("UPDATE orders SET status = 'filled', filled_at = ?, fill_price = ? WHERE id = ?"), {time, price, id});
        static const QStringList exits{QStringLiteral("target"), QStringLiteral("stop"), QStringLiteral("oco_a"), QStringLiteral("oco_b")};
        if (exits.contains(role))
            for (const QVariant &sv : db().all(QStringLiteral("SELECT * FROM orders WHERE group_id = ? AND id != ? AND status IN %1").arg(ACTIVE), {s(o, "group_id"), id}))
                if (exits.contains(s(sv.toMap(), "role")))
                    cancelRow(i(sv.toMap(), "id"), QStringLiteral("Other leg of the OCO filled"));
        if (role == QLatin1String("entry"))
            db().run(QStringLiteral("UPDATE orders SET status = 'working', start_at = ?, extreme = CASE WHEN type = 'trailing_stop' THEN ? ELSE extreme END WHERE parent_id = ? AND status = 'pending'"),
                     {nextBarTime(time + 1), price, id});
    });
    QString pl;
    if (!opensPosition(side))
        pl = QStringLiteral(" (P/L %1$%2)").arg(realized >= 0 ? QStringLiteral("+") : QStringLiteral("-"), fixed(std::fabs(realized), 2));
    events.append(event(QStringLiteral("%1: %2 %3 filled @ %4%5").arg(symbol, sideLabel(side), qtyS, fixed(price, 2), pl)));
    return true;
}

// Price at which `o` fills inside `bar`, or nothing. May advance trailing/trigger state.
std::optional<double> Engine::tryBar(const Rec &o, const Bar &bar, St &st, bool closed)
{
    const bool buy = isBuy(s(o, "side"));
    auto stopHit = [&](double sp) -> std::optional<double> {
        if (buy) return bar.high >= sp ? std::optional<double>(std::max(sp, bar.open)) : std::nullopt;
        return bar.low <= sp ? std::optional<double>(std::min(sp, bar.open)) : std::nullopt;
    };
    auto limitFill = [&](double l) -> std::optional<double> {
        if (buy) return bar.low <= l ? std::optional<double>(std::min(l, bar.open)) : std::nullopt;
        return bar.high >= l ? std::optional<double>(std::max(l, bar.open)) : std::nullopt;
    };
    const QString type = s(o, "type");
    if (type == QLatin1String("market"))
        return bar.open;
    if (type == QLatin1String("limit"))
        return limitFill(f(o, "limit_price"));
    if (type == QLatin1String("stop"))
        return stopHit(f(o, "stop_price"));
    if (type == QLatin1String("stop_limit")) {
        if (!st.triggered) {
            const auto t = stopHit(f(o, "stop_price"));
            if (!t)
                return std::nullopt;
            st.triggered = true;
            const double l = f(o, "limit_price");
            if (buy ? *t <= l : *t >= l)
                return t;
            return (buy ? bar.low <= l : bar.high >= l) ? std::optional<double>(l) : std::nullopt;
        }
        return limitFill(f(o, "limit_price"));
    }
    if (type == QLatin1String("trailing_stop")) {
        if (!st.extreme)
            st.extreme = bar.open;
        const double dist = s(o, "trail_unit") == QLatin1String("%") ? (*st.extreme * f(o, "trail_amount")) / 100 : f(o, "trail_amount");
        const double trigger = buy ? *st.extreme + dist : *st.extreme - dist;
        const auto hit = stopHit(trigger);
        if (hit)
            return hit;
        if (closed)
            st.extreme = buy ? std::min(*st.extreme, bar.low) : std::max(*st.extreme, bar.high);
        return std::nullopt;
    }
    return std::nullopt;
}

// ---------------------------------------------------------------- processing
List Engine::process(const QString &symbol)
{
    std::lock_guard<std::recursive_mutex> g(m_lock);
    List events;
    const qint64 now = m_clock();
    for (const QVariant &ov : db().all(QStringLiteral("SELECT * FROM orders WHERE symbol = ? AND status IN %1 AND expires_at IS NOT NULL AND expires_at < ?").arg(ACTIVE), {symbol, now})) {
        const Rec o = ov.toMap();
        cancelRow(i(o, "id"), QStringLiteral("Day order expired at the close"), QStringLiteral("expired"));
        events.append(event(QStringLiteral("%1: %2 %3 expired.").arg(s(o, "symbol"), sideLabel(s(o, "side")), QString::number(f(o, "qty")))));
    }
    auto working = [&] { return db().all(QStringLiteral("SELECT * FROM orders WHERE symbol = ? AND status = 'working' ORDER BY id"), {symbol}); };
    if (working().isEmpty())
        return events;

    // fresh market orders during the session fill at the live price right away; queued ones (placed while
    // closed, or long ago) fall through to the bars below and fill at the first available open
    if (isMarketOpen(now)) {
        List markets;
        for (const QVariant &ov : working())
            if (s(ov.toMap(), "type") == QLatin1String("market") && now - i(ov.toMap(), "created_at") <= 120)
                markets.append(ov);
        if (!markets.isEmpty()) {
            const auto price = quoteOrNull(symbol);
            if (price)
                for (const QVariant &ov : markets)
                    fill(ov.toMap(), *price, now, events);
        }
    }

    const List live = working();
    if (live.isEmpty())
        return events;
    const auto gr = grain();
    if (!gr)
        return events; // no intraday bars on this plan: only fresh market orders (above) can fill
    qint64 since = std::numeric_limits<qint64>::max();
    for (const QVariant &ov : live)
        since = std::min(since, i(ov.toMap(), "start_at"));
    Bars bars;
    try { bars = m_market.bars(symbol, since); } catch (...) { bars.clear(); }
    std::map<qint64, St> state;
    std::map<qint64, qint64> checked;
    for (const QVariant &ov : live) {
        const Rec o = ov.toMap();
        state[i(o, "id")] = St{has(o, "extreme") ? std::optional<double>(f(o, "extreme")) : std::nullopt, i(o, "triggered") != 0};
        checked[i(o, "id")] = i(o, "checked_to");
    }

    for (const Bar &bar : bars) {
        const bool closed = bar.time + gr->seconds <= now;
        // stops are evaluated before targets, so an ambiguous bar counts as the stop being hit first
        List ws = working();
        std::stable_sort(ws.begin(), ws.end(), [](const QVariant &a, const QVariant &b) {
            return (s(a.toMap(), "role") == QLatin1String("target") ? 1 : 0) < (s(b.toMap(), "role") == QLatin1String("target") ? 1 : 0);
        });
        for (const QVariant &ov : ws) {
            const Rec o = ov.toMap();
            const qint64 id = i(o, "id");
            const qint64 chk = checked.count(id) ? checked[id] : 0;
            if (bar.time < i(o, "start_at") || bar.time <= chk)
                continue;
            if (!state.count(id))
                state[id] = St{has(o, "extreme") ? std::optional<double>(f(o, "extreme")) : std::nullopt, i(o, "triggered") != 0};
            const auto price = tryBar(o, bar, state[id], closed);
            if (price && fill(o, *price, bar.time, events))
                continue;
            if (closed)
                checked[id] = bar.time;
        }
    }
    for (const QVariant &ov : working()) {
        const Rec o = ov.toMap();
        const qint64 id = i(o, "id");
        if (!state.count(id))
            continue;
        const St &st = state[id];
        db().run(QStringLiteral("UPDATE orders SET extreme = ?, triggered = ?, checked_to = ? WHERE id = ?"),
                 {orNull(st.extreme), st.triggered ? 1 : 0, checked.count(id) ? checked[id] : i(o, "checked_to"), id});
    }
    return events;
}

// ---------------------------------------------------------------- other operations
Rec Engine::cancel(qint64 id)
{
    std::lock_guard<std::recursive_mutex> g(m_lock);
    const auto o = db().one(QStringLiteral("SELECT * FROM orders WHERE id = ?"), {id});
    if (!o)
        return failErrors({QStringLiteral("Order not found.")});
    const QString st = s(*o, "status");
    if (st != QLatin1String("working") && st != QLatin1String("pending"))
        return failErrors({QStringLiteral("Order is already %1.").arg(st)});
    cancelRow(id, QStringLiteral("Cancelled by user"));
    return ok();
}

Rec Engine::modify(qint64 id, const Rec &patch)
{
    std::lock_guard<std::recursive_mutex> g(m_lock);
    const auto o = db().one(QStringLiteral("SELECT * FROM orders WHERE id = ?"), {id});
    if (!o)
        return failErrors({QStringLiteral("Order not found.")});
    const QString st = s(*o, "status");
    if (st != QLatin1String("working") && st != QLatin1String("pending"))
        return failErrors({QStringLiteral("Order is already %1.").arg(st)});
    const bool hasLimit = patch.contains(QStringLiteral("limit")), hasStop = patch.contains(QStringLiteral("stop"));
    const bool hasTrail = patch.contains(QStringLiteral("trailAmount")), hasQty = patch.contains(QStringLiteral("qty"));
    const QVariant limit = patch.value(QStringLiteral("limit")), stop = patch.value(QStringLiteral("stop"));
    const QVariant trail = patch.value(QStringLiteral("trailAmount")), qty = patch.value(QStringLiteral("qty"));
    if (hasLimit && (!isPos(limit) || !has(*o, "limit_price"))) return failErrors({QStringLiteral("This order has no limit price to change.")});
    if (hasStop && (!isPos(stop) || !has(*o, "stop_price"))) return failErrors({QStringLiteral("This order has no stop price to change.")});
    if (hasTrail && (!isPos(trail) || !has(*o, "trail_amount"))) return failErrors({QStringLiteral("This order has no trailing amount to change.")});
    if (hasQty && (!isInteger(qty) || qty.toDouble() < 1)) return failErrors({QStringLiteral("Quantity must be a whole number, 1 or more.")});
    db().tx([&] {
        if (hasLimit) db().run(QStringLiteral("UPDATE orders SET limit_price = ? WHERE id = ?"), {limit.toDouble(), id});
        if (hasStop) db().run(QStringLiteral("UPDATE orders SET stop_price = ?, triggered = 0 WHERE id = ?"), {stop.toDouble(), id});
        if (hasTrail) db().run(QStringLiteral("UPDATE orders SET trail_amount = ? WHERE id = ?"), {trail.toDouble(), id});
        if (hasQty) db().run(QStringLiteral("UPDATE orders SET qty = ? WHERE (id = ? OR parent_id = ?) AND status IN %1").arg(ACTIVE), {qty.toLongLong(), id, id});
    });
    return ok();
}

Rec Engine::closePosition(const QString &symbol)
{
    std::lock_guard<std::recursive_mutex> g(m_lock);
    const double qty = positionQtyOf(m_active(), symbol);
    if (qty == 0)
        return failErrors({QStringLiteral("No position in %1.").arg(symbol)});
    for (const QVariant &ov : db().all(QStringLiteral("SELECT * FROM orders WHERE symbol = ? AND account_id = ? AND status IN %1").arg(ACTIVE), {symbol, m_active()}))
        cancelRow(i(ov.toMap(), "id"), QStringLiteral("Position closed"));
    const Rec r = place(Rec{{QStringLiteral("symbol"), symbol}, {QStringLiteral("side"), qty > 0 ? QStringLiteral("sell") : QStringLiteral("buy_to_cover")},
                            {QStringLiteral("qty"), std::fabs(qty)}, {QStringLiteral("type"), QStringLiteral("market")}, {QStringLiteral("tif"), QStringLiteral("day")},
                            {QStringLiteral("strategy"), QStringLiteral("single")}, {QStringLiteral("source"), QStringLiteral("user")}, {QStringLiteral("note"), QStringLiteral("Close position")}});
    if (!r.value(QStringLiteral("ok")).toBool())
        return r;
    return ok(Rec{{QStringLiteral("orderIds"), r.value(QStringLiteral("orderIds"))}});
}

void Engine::reset(double startingCash)
{
    std::lock_guard<std::recursive_mutex> g(m_lock);
    const qint64 id = m_active();
    db().tx([&] {
        db().run(QStringLiteral("DELETE FROM orders WHERE account_id = ?"), {id});
        db().run(QStringLiteral("DELETE FROM fills WHERE account_id = ?"), {id});
        db().run(QStringLiteral("DELETE FROM positions WHERE account_id = ?"), {id});
        db().run(QStringLiteral("DELETE FROM account_transfers WHERE account_id = ?"), {id});
        db().run(QStringLiteral("UPDATE accounts SET cash = ?, starting_cash = ? WHERE id = ?"), {startingCash, startingCash, id});
    });
}

List Engine::tick()
{
    std::lock_guard<std::recursive_mutex> g(m_lock);
    List events;
    for (const QVariant &r : db().all(QStringLiteral("SELECT DISTINCT symbol FROM orders WHERE status = 'working'"))) {
        try { events += process(s(r.toMap(), "symbol")); } catch (...) { }
    }
    return events;
}

double Engine::positionQty(const QString &symbol)
{
    std::lock_guard<std::recursive_mutex> g(m_lock);
    return positionQtyOf(m_active(), symbol);
}

} // namespace tl
