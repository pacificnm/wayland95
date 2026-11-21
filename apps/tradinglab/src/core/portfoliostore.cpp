// The portfolio store. Port of main/portfolio.ts.

#include "portfoliostore.h"

#include "db.h"
#include "normalize.h"
#include "portfolio.h"

#include <QHash>
#include <QRegularExpression>

namespace tl {

namespace {

const QString kError = QStringLiteral("error");

QString jsStr(const QVariant &v) { return jsValueString(v); }
// a bound QString that is null becomes SQL NULL, so text bound for a NOT NULL column is never a null QString
QString text(const QString &s) { return s.isNull() ? QStringLiteral("") : s; }
// `String(v ?? '')`
QString strOr(const QVariant &v) { return text(isNull(v) ? QString() : jsStr(v)); }

// upstream's `num`: '' / null / undefined -> null, otherwise Number(v) when finite
std::optional<double> numIn(const QVariant &v)
{
    if (isNull(v) || (v.metaType().id() == QMetaType::QString && v.toString().isEmpty()))
        return std::nullopt;
    const double d = toNumber(v);
    return std::isfinite(d) ? std::optional<double>(d) : std::nullopt;
}

bool validKind(const QVariant &k) { return k.metaType().id() == QMetaType::QString && kindIds().contains(k.toString()); }

QString cleanName(const QVariant &n)
{
    static const QRegularExpression ws(QStringLiteral("\\s+"));
    return strOr(n).trimmed().replace(ws, QStringLiteral(" ")).left(40);
}

Db &db() { return Db::local(); }

bool accountExists(double id)
{
    try {
        return db().one(QStringLiteral("SELECT 1 AS x FROM accounts WHERE id = ?"), {id}).has_value();
    } catch (const Error &) {
        return false;
    }
}
bool has(qint64 id) { return db().one(QStringLiteral("SELECT 1 AS x FROM portfolios WHERE id = ?"), {id}).has_value(); }
bool taken(const QString &name, qint64 except = -1)
{
    return db().one(QStringLiteral("SELECT 1 AS x FROM portfolios WHERE name = ? AND id != ?"), {name, except}).has_value();
}
std::optional<Rec> itemRow(qint64 id) { return db().one(QStringLiteral("SELECT id, portfolio_id FROM portfolio_items WHERE id = ?"), {id}); }

QString alreadyCalled(const QString &name) { return QStringLiteral("You already have a portfolio called “%1”.").arg(name); }

} // namespace

PortfolioStore::PortfolioStore(Clock clock)
    : m_clock(std::move(clock))
{
    initSchema();
}

void PortfolioStore::initSchema()
{
    db().exec(QStringLiteral(R"(
    CREATE TABLE IF NOT EXISTS portfolios (
      id INTEGER PRIMARY KEY AUTOINCREMENT,
      name TEXT NOT NULL COLLATE NOCASE UNIQUE,
      amount REAL NOT NULL DEFAULT 10000,
      mode TEXT NOT NULL DEFAULT 'new',
      fractional INTEGER NOT NULL DEFAULT 0,
      leftover INTEGER NOT NULL DEFAULT 0,
      created_at INTEGER NOT NULL
    );
    CREATE TABLE IF NOT EXISTS portfolio_items (
      id INTEGER PRIMARY KEY AUTOINCREMENT,
      portfolio_id INTEGER NOT NULL REFERENCES portfolios(id) ON DELETE CASCADE,
      symbol TEXT NOT NULL,
      name TEXT NOT NULL DEFAULT '',
      kind TEXT NOT NULL DEFAULT 'stock',
      target_pct REAL NOT NULL DEFAULT 0,
      shares REAL NOT NULL DEFAULT 0,
      cost REAL NOT NULL DEFAULT 0,
      manual_price REAL,
      added_at INTEGER NOT NULL,
      UNIQUE (portfolio_id, symbol)
    );
  )"));

    // the link to a paper account was added later: an idempotent ALTER keeps databases that already have the table working
    if (!db().hasColumn(QStringLiteral("portfolios"), QStringLiteral("account_id")))
        db().exec(QStringLiteral("ALTER TABLE portfolios ADD COLUMN account_id INTEGER"));
}

List PortfolioStore::list()
{
    // an account that was deleted leaves its portfolios unlinked, with the numbers they had typed
    try {
        db().exec(QStringLiteral("UPDATE portfolios SET account_id = NULL WHERE account_id IS NOT NULL AND account_id NOT IN (SELECT id FROM accounts)"));
    } catch (const Error &) {
        // no accounts table in a bare test database
    }
    const List items = db().all(QStringLiteral("SELECT * FROM portfolio_items ORDER BY added_at, id"));
    List out;
    for (const QVariant &pv : db().all(QStringLiteral("SELECT * FROM portfolios ORDER BY id"))) {
        const Rec p = pv.toMap();
        const double pid = toNumber(p.value(QStringLiteral("id")));
        List its;
        for (const QVariant &iv : items) {
            const Rec i = iv.toMap();
            if (toNumber(i.value(QStringLiteral("portfolio_id"))) != pid)
                continue;
            const QVariant kind = i.value(QStringLiteral("kind"));
            const QVariant mp = i.value(QStringLiteral("manual_price"));
            its.append(Rec{
                {QStringLiteral("id"), toNumber(i.value(QStringLiteral("id")))},
                {QStringLiteral("symbol"), jsStr(i.value(QStringLiteral("symbol")))},
                {QStringLiteral("name"), jsStr(i.value(QStringLiteral("name")))},
                {QStringLiteral("kind"), validKind(kind) ? kind.toString() : QStringLiteral("stock")},
                {QStringLiteral("targetPct"), toNumber(i.value(QStringLiteral("target_pct")))},
                {QStringLiteral("shares"), toNumber(i.value(QStringLiteral("shares")))},
                {QStringLiteral("cost"), toNumber(i.value(QStringLiteral("cost")))},
                {QStringLiteral("manualPrice"), isNull(mp) ? null() : QVariant(toNumber(mp))},
            });
        }
        const QVariant acc = p.value(QStringLiteral("account_id"));
        out.append(Rec{
            {QStringLiteral("id"), pid},
            {QStringLiteral("name"), jsStr(p.value(QStringLiteral("name")))},
            {QStringLiteral("amount"), toNumber(p.value(QStringLiteral("amount")))},
            {QStringLiteral("mode"), p.value(QStringLiteral("mode")).toString() == QStringLiteral("total") ? QStringLiteral("total") : QStringLiteral("new")},
            {QStringLiteral("fractional"), toNumber(p.value(QStringLiteral("fractional"))) != 0},
            {QStringLiteral("leftover"), toNumber(p.value(QStringLiteral("leftover"))) != 0},
            {QStringLiteral("accountId"), isNull(acc) ? null() : QVariant(toNumber(acc))},
            {QStringLiteral("items"), its},
        });
    }
    return out;
}

std::optional<Rec> PortfolioStore::get(qint64 id)
{
    for (const QVariant &p : list())
        if (toNumber(p.toMap().value(QStringLiteral("id"))) == double(id))
            return p.toMap();
    return std::nullopt;
}

Rec PortfolioStore::create(const QVariant &nameIn, const QVariant &amountIn)
{
    const QString name = cleanName(nameIn);
    if (name.isEmpty())
        return failError(QStringLiteral("Give the portfolio a name."));
    if (taken(name))
        return failError(alreadyCalled(name));
    const std::optional<double> amount = numIn(amountIn);
    if (amount && *amount < 0)
        return failError(QStringLiteral("The investment amount cannot be negative."));
    const RunResult r = db().run(QStringLiteral("INSERT INTO portfolios (name, amount, created_at) VALUES (?, ?, ?)"),
                                 {name, amount.value_or(10000), m_clock()});
    return ok(Rec{{QStringLiteral("id"), r.lastInsertRowid}});
}

Rec PortfolioStore::update(qint64 id, const Rec &patch)
{
    if (!has(id))
        return failError(QStringLiteral("That portfolio no longer exists."));
    if (patch.contains(QStringLiteral("name"))) {
        const QString name = cleanName(patch.value(QStringLiteral("name")));
        if (name.isEmpty())
            return failError(QStringLiteral("Give the portfolio a name."));
        if (taken(name, id))
            return failError(alreadyCalled(name));
        db().run(QStringLiteral("UPDATE portfolios SET name = ? WHERE id = ?"), {name, id});
    }
    if (patch.contains(QStringLiteral("amount"))) {
        const std::optional<double> a = numIn(patch.value(QStringLiteral("amount")));
        if (!a || *a < 0 || *a > 1e12)
            return failError(QStringLiteral("Enter an investment amount of zero or more."));
        db().run(QStringLiteral("UPDATE portfolios SET amount = ? WHERE id = ?"), {*a, id});
    }
    if (patch.contains(QStringLiteral("mode")))
        db().run(QStringLiteral("UPDATE portfolios SET mode = ? WHERE id = ?"),
                 {patch.value(QStringLiteral("mode")).toString() == QStringLiteral("total") ? QStringLiteral("total") : QStringLiteral("new"), id});
    if (patch.contains(QStringLiteral("fractional")))
        db().run(QStringLiteral("UPDATE portfolios SET fractional = ? WHERE id = ?"), {patch.value(QStringLiteral("fractional")).toBool() ? 1 : 0, id});
    if (patch.contains(QStringLiteral("leftover")))
        db().run(QStringLiteral("UPDATE portfolios SET leftover = ? WHERE id = ?"), {patch.value(QStringLiteral("leftover")).toBool() ? 1 : 0, id});
    if (patch.contains(QStringLiteral("accountId"))) {
        const QVariant acc = patch.value(QStringLiteral("accountId"));
        const bool unlink = isNull(acc);
        if (!unlink && !accountExists(toNumber(acc)))
            return failError(QStringLiteral("That paper account no longer exists."));
        db().run(QStringLiteral("UPDATE portfolios SET account_id = ? WHERE id = ?"), {unlink ? null() : QVariant(qint64(toNumber(acc))), id});
    }
    return ok();
}

Rec PortfolioStore::remove(qint64 id)
{
    if (!has(id))
        return failError(QStringLiteral("That portfolio no longer exists."));
    db().run(QStringLiteral("DELETE FROM portfolio_items WHERE portfolio_id = ?"), {id}); // explicit, in case foreign keys are off
    db().run(QStringLiteral("DELETE FROM portfolios WHERE id = ?"), {id});
    return ok();
}

Rec PortfolioStore::addItem(qint64 pid, const Rec &input)
{
    const QString symbol = strOr(input.value(QStringLiteral("symbol"))).trimmed().toUpper();
    if (!has(pid))
        return failError(QStringLiteral("That portfolio no longer exists."));
    if (!symbolRe().match(symbol).hasMatch())
        return failError(QStringLiteral("“%1” is not a valid symbol.").arg(jsStr(input.value(QStringLiteral("symbol")))));
    if (db().one(QStringLiteral("SELECT 1 AS x FROM portfolio_items WHERE portfolio_id = ? AND symbol = ?"), {pid, symbol}))
        return failError(QStringLiteral("%1 is already in this portfolio.").arg(symbol));
    const QVariant k = input.value(QStringLiteral("kind"));
    const QString kind = validKind(k) ? k.toString() : guessKind(symbol);
    const RunResult r = db().run(QStringLiteral("INSERT INTO portfolio_items (portfolio_id, symbol, name, kind, added_at) VALUES (?,?,?,?,?)"),
                                 {pid, symbol, text(strOr(input.value(QStringLiteral("name"))).left(120)), kind, m_clock()});
    return ok(Rec{{QStringLiteral("id"), r.lastInsertRowid}, {QStringLiteral("symbol"), symbol}});
}

Rec PortfolioStore::updateItem(qint64 id, const Rec &patch)
{
    if (!itemRow(id))
        return failError(QStringLiteral("That holding no longer exists."));
    if (patch.contains(QStringLiteral("targetPct"))) {
        const auto v = numIn(patch.value(QStringLiteral("targetPct")));
        if (!v || *v < 0 || *v > 100)
            return failError(QStringLiteral("A target percentage must be between 0 and 100."));
        db().run(QStringLiteral("UPDATE portfolio_items SET target_pct = ? WHERE id = ?"), {mathRound(*v * 100) / 100, id});
    }
    if (patch.contains(QStringLiteral("shares"))) {
        const auto v = numIn(patch.value(QStringLiteral("shares")));
        if (!v || *v < 0 || *v > 1e12)
            return failError(QStringLiteral("Shares must be zero or more."));
        db().run(QStringLiteral("UPDATE portfolio_items SET shares = ? WHERE id = ?"), {mathRound(*v * 1e6) / 1e6, id});
    }
    if (patch.contains(QStringLiteral("cost"))) {
        const auto v = numIn(patch.value(QStringLiteral("cost")));
        if (!v || *v < 0 || *v > 1e13)
            return failError(QStringLiteral("Cost must be zero or more."));
        db().run(QStringLiteral("UPDATE portfolio_items SET cost = ? WHERE id = ?"), {mathRound(*v * 100) / 100, id});
    }
    if (patch.contains(QStringLiteral("manualPrice"))) {
        const QVariant mp = patch.value(QStringLiteral("manualPrice"));
        const std::optional<double> v = isNull(mp) ? std::nullopt : numIn(mp);
        if (v && (*v <= 0 || *v > 1e9))
            return failError(QStringLiteral("A price must be more than zero."));
        db().run(QStringLiteral("UPDATE portfolio_items SET manual_price = ? WHERE id = ?"), {orNull(v), id});
    }
    if (patch.contains(QStringLiteral("kind")) && validKind(patch.value(QStringLiteral("kind"))))
        db().run(QStringLiteral("UPDATE portfolio_items SET kind = ? WHERE id = ?"), {patch.value(QStringLiteral("kind")).toString(), id});
    if (patch.contains(QStringLiteral("name")))
        db().run(QStringLiteral("UPDATE portfolio_items SET name = ? WHERE id = ?"), {text(jsStr(patch.value(QStringLiteral("name"))).left(120)), id});
    return ok();
}

Rec PortfolioStore::removeItem(qint64 id)
{
    if (!itemRow(id))
        return ok();
    db().run(QStringLiteral("DELETE FROM portfolio_items WHERE id = ?"), {id});
    return ok();
}

Rec PortfolioStore::setTargets(qint64 pid, const List &targets)
{
    if (!has(pid))
        return failError(QStringLiteral("That portfolio no longer exists."));
    for (const QVariant &tv : targets) {
        const QVariant pct = tv.toMap().value(QStringLiteral("pct"));
        if (!(isNumber(pct) && pct.toDouble() >= 0 && pct.toDouble() <= 100))
            return failError(QStringLiteral("A target percentage must be between 0 and 100."));
    }
    for (const QVariant &tv : targets) {
        const Rec t = tv.toMap();
        db().run(QStringLiteral("UPDATE portfolio_items SET target_pct = ? WHERE id = ? AND portfolio_id = ?"),
                 {mathRound(t.value(QStringLiteral("pct")).toDouble() * 100) / 100, t.value(QStringLiteral("id")), pid});
    }
    return ok();
}

Rec PortfolioStore::recordBuys(qint64 pid, const List &buys)
{
    if (!has(pid))
        return failError(QStringLiteral("That portfolio no longer exists."));
    List todo;
    for (const QVariant &b : buys)
        if (toNumber(b.toMap().value(QStringLiteral("shares"))) > 0)
            todo.append(b);
    for (const QVariant &bv : todo) {
        const Rec b = bv.toMap();
        const QVariant shares = b.value(QStringLiteral("shares")), price = b.value(QStringLiteral("price"));
        if (!(isNumber(shares) && isNumber(price) && price.toDouble() > 0))
            return failError(QStringLiteral("Every purchase needs a share count and a price."));
        const auto row = db().one(QStringLiteral("SELECT portfolio_id FROM portfolio_items WHERE id = ?"), {b.value(QStringLiteral("id"))});
        if (!row || row->value(QStringLiteral("portfolio_id")).toLongLong() != pid)
            return failError(QStringLiteral("One of those holdings no longer exists."));
    }
    for (const QVariant &bv : todo) {
        const Rec b = bv.toMap();
        const double shares = b.value(QStringLiteral("shares")).toDouble(), price = b.value(QStringLiteral("price")).toDouble();
        db().run(QStringLiteral("UPDATE portfolio_items SET shares = ROUND(shares + ?, 6), cost = ROUND(cost + ?, 2) WHERE id = ?"),
                 {shares, shares * price, b.value(QStringLiteral("id"))});
    }
    return ok(Rec{{QStringLiteral("recorded"), todo.size()}});
}

Rec PortfolioStore::recordSells(qint64 pid, const List &sells)
{
    if (!has(pid))
        return failError(QStringLiteral("That portfolio no longer exists."));
    List todo;
    for (const QVariant &s : sells)
        if (toNumber(s.toMap().value(QStringLiteral("shares"))) > 0)
            todo.append(s);
    struct Held
    {
        double shares, cost;
    };
    QHash<qint64, Held> rows;
    for (const QVariant &sv : todo) {
        const Rec s = sv.toMap();
        const QVariant shares = s.value(QStringLiteral("shares"));
        if (!isNumber(shares))
            return failError(QStringLiteral("Every sale needs a share count."));
        const auto row = db().one(QStringLiteral("SELECT portfolio_id, shares, cost FROM portfolio_items WHERE id = ?"), {s.value(QStringLiteral("id"))});
        if (!row || row->value(QStringLiteral("portfolio_id")).toLongLong() != pid)
            return failError(QStringLiteral("One of those holdings no longer exists."));
        const double held = toNumber(row->value(QStringLiteral("shares")));
        if (shares.toDouble() > held + 1e-9)
            return failError(QStringLiteral("You cannot sell more shares than you hold."));
        rows.insert(s.value(QStringLiteral("id")).toLongLong(), Held{held, toNumber(row->value(QStringLiteral("cost")))});
    }
    for (const QVariant &sv : todo) {
        const Rec s = sv.toMap();
        const Held r = rows.value(s.value(QStringLiteral("id")).toLongLong());
        const double left = std::max(0.0, mathRound((r.shares - s.value(QStringLiteral("shares")).toDouble()) * 1e6) / 1e6);
        db().run(QStringLiteral("UPDATE portfolio_items SET shares = ?, cost = ? WHERE id = ?"),
                 {left, left == 0 || r.shares == 0 ? 0.0 : mathRound(r.cost * (left / r.shares) * 100) / 100, s.value(QStringLiteral("id"))});
    }
    return ok(Rec{{QStringLiteral("recorded"), todo.size()}});
}

} // namespace tl
