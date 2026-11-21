// Port of main/accounts.ts.
#include "accounts.h"

#include "accountsinfo.h"
#include "db.h"

#include <cmath>

namespace tl::accounts {

namespace {
Db &db() { return Db::local(); }

Rec info(const Rec &r)
{
    return Rec{{QStringLiteral("id"), r.value(QStringLiteral("id"))}, {QStringLiteral("name"), r.value(QStringLiteral("name"))},
               {QStringLiteral("type"), r.value(QStringLiteral("type")).toString() == QLatin1String("cash") ? QStringLiteral("cash") : QStringLiteral("margin")},
               {QStringLiteral("broker"), r.value(QStringLiteral("broker"))}, {QStringLiteral("url"), r.value(QStringLiteral("url"))},
               {QStringLiteral("cash"), r.value(QStringLiteral("cash"))}, {QStringLiteral("startingCash"), r.value(QStringLiteral("starting_cash"))},
               {QStringLiteral("createdAt"), r.value(QStringLiteral("created_at"))}};
}
List rows() { return db().all(QStringLiteral("SELECT * FROM accounts ORDER BY id")); }
std::optional<Rec> row(qint64 id) { return db().one(QStringLiteral("SELECT * FROM accounts WHERE id = ?"), {id}); }
Rec must(qint64 id)
{
    const auto r = row(id);
    if (!r)
        throw Error(QStringLiteral("Account not found."));
    return *r;
}
double money(const QVariant &n, const QString &what)
{
    if (!isNumber(n) || n.toDouble() < 0 || n.toDouble() > 1e12)
        throw Error(QStringLiteral("%1 must be a number of 0 or more.").arg(what));
    return std::round(n.toDouble() * 100) / 100;
}
// only the fields that were given, trimmed
Rec clean(const Rec &i)
{
    const QString e = checkAccountInput(i);
    if (!e.isEmpty())
        throw Error(e);
    Rec c;
    for (const char *k : {"name", "broker", "url"})
        if (i.contains(QLatin1String(k)) && !isNull(i.value(QLatin1String(k))))
            c.insert(QLatin1String(k), i.value(QLatin1String(k)).toString().trimmed());
    if (i.contains(QStringLiteral("type")) && !isNull(i.value(QStringLiteral("type"))))
        c.insert(QStringLiteral("type"), i.value(QStringLiteral("type")));
    return c;
}
}

List list()
{
    List out;
    for (const QVariant &r : rows())
        out.append(info(r.toMap()));
    return out;
}

Rec get(qint64 id) { return info(must(id)); }

qint64 activeId()
{
    const QVariant v = getSetting(QStringLiteral("activeAccount"));
    const qint64 id = isNumber(v) ? v.toLongLong() : -1;
    if (id >= 0 && row(id))
        return id;
    return rows().value(0).toMap().value(QStringLiteral("id")).toLongLong();
}

void setActive(qint64 id)
{
    must(id);
    setSetting(QStringLiteral("activeAccount"), id);
}

Rec create(const Rec &input, const QVariant &balance)
{
    const Rec c = clean(input);
    if (c.value(QStringLiteral("name")).toString().isEmpty())
        throw Error(QStringLiteral("Give the account a name."));
    const double cash = money(balance, QStringLiteral("The starting balance"));
    const RunResult r = db().run(QStringLiteral("INSERT INTO accounts (name, type, broker, url, cash, starting_cash, created_at) VALUES (?,?,?,?,?,?,?)"),
                                 {c.value(QStringLiteral("name")), c.value(QStringLiteral("type"), QStringLiteral("margin")), c.value(QStringLiteral("broker"), QString()),
                                  c.value(QStringLiteral("url"), QString()), cash, cash, nowSec()});
    return info(must(r.lastInsertRowid));
}

Rec update(qint64 id, const Rec &patch)
{
    must(id);
    const Rec c = clean(patch);
    for (const char *k : {"name", "type", "broker", "url"})
        if (c.contains(QLatin1String(k)))
            db().run(QStringLiteral("UPDATE accounts SET %1 = ? WHERE id = ?").arg(QLatin1String(k)), {c.value(QLatin1String(k)), id});
    return info(must(id));
}

/**
 * Sets the cash balance to match the real account. The difference is booked as a deposit or withdrawal (the starting
 * balance moves with it) so total return still measures trading results, not money added or taken out.
 */
Rec setBalance(qint64 id, const QVariant &cash, const QString &note)
{
    const Rec cur = must(id);
    const double target = money(cash, QStringLiteral("The balance"));
    const double delta = std::round((target - cur.value(QStringLiteral("cash")).toDouble()) * 100) / 100;
    if (delta != 0) {
        db().tx([&] {
            db().run(QStringLiteral("UPDATE accounts SET cash = ?, starting_cash = starting_cash + ? WHERE id = ?"), {target, delta, id});
            db().run(QStringLiteral("INSERT INTO account_transfers (account_id, time, amount, note) VALUES (?,?,?,?)"), {id, nowSec(), delta, note.left(120)});
        });
    }
    return info(must(id));
}

void remove(qint64 id)
{
    must(id);
    if (rows().size() <= 1)
        throw Error(QStringLiteral("You need at least one account."));
    db().tx([&] {
        for (const char *t : {"orders", "fills", "positions", "account_transfers"})
            db().run(QStringLiteral("DELETE FROM %1 WHERE account_id = ?").arg(QLatin1String(t)), {id});
        db().run(QStringLiteral("DELETE FROM accounts WHERE id = ?"), {id});
    });
}

List transfers(qint64 id, int limit)
{
    return db().all(QStringLiteral("SELECT id, time, amount, note FROM account_transfers WHERE account_id = ? ORDER BY id DESC LIMIT ?"), {id, limit});
}

} // namespace tl::accounts
