// Ports of main/journal.ts, main/watchlists.ts and main/strategies.ts.
#include "stores.h"

#include "db.h"
#include "trade.h"

#include <QRegularExpression>
#include <QSet>

#include <algorithm>
#include <cmath>

namespace tl {

namespace {
Db &db() { return Db::local(); }
QString str(const QVariant &v, int max) { return isNull(v) ? QString() : v.toString().left(max); }
QVariant numOrNull(const QVariant &v) { return isNumber(v) ? v : null(); }
QVariant json(const QVariant &text, const QVariant &dflt)
{
    if (isNull(text) || text.toString().isEmpty())
        return dflt;
    return fromJson(text.toString(), dflt);
}
QStringList cleanTags(const QVariant &v, int max)
{
    if (v.metaType().id() != QMetaType::QVariantList && v.metaType().id() != QMetaType::QStringList)
        return {};
    QStringList out;
    for (const QVariant &t : v.toList()) {
        QString s = t.toString().trimmed();
        if (s.startsWith(QLatin1Char('#')))
            s.remove(0, 1);
        s = s.left(30);
        if (!s.isEmpty() && !out.contains(s))
            out << s;
    }
    return out.mid(0, max);
}
QVariantList integers(const QVariant &v)
{
    QVariantList out;
    for (const QVariant &x : v.toList())
        if (isInteger(x))
            out.append(x.toLongLong());
    return out;
}
bool isList(const QVariant &v) { return v.metaType().id() == QMetaType::QVariantList || v.metaType().id() == QMetaType::QStringList; }
}

// =========================================================================================== journal
namespace journal {

const QStringList EMOTIONS{QStringLiteral("Calm"), QStringLiteral("Confident"), QStringLiteral("Patient"), QStringLiteral("Anxious"), QStringLiteral("FOMO"),
                           QStringLiteral("Greedy"), QStringLiteral("Fearful"), QStringLiteral("Frustrated"), QStringLiteral("Impulsive")};

static const QString COLS = QStringLiteral("id, created_at, updated_at, kind, status, source, symbol, direction, title, body, setup, tags, plan_entry, plan_stop, plan_target, plan_qty, emotions, followed_plan, review, lesson, order_ids, comments, image IS NOT NULL AS has_image");

static Rec toEntry(const Rec &r)
{
    Rec e = r;
    e.insert(QStringLiteral("tags"), json(r.value(QStringLiteral("tags")), QVariantList()));
    e.insert(QStringLiteral("emotions"), json(r.value(QStringLiteral("emotions")), QVariantList()));
    e.insert(QStringLiteral("order_ids"), json(r.value(QStringLiteral("order_ids")), QVariantList()));
    e.insert(QStringLiteral("comments"), json(r.value(QStringLiteral("comments")), QVariantList()));
    e.insert(QStringLiteral("has_image"), r.value(QStringLiteral("has_image")).toLongLong() != 0);
    return e;
}

/**
 * What became of the trade: starts at the first fill of the linked orders, then follows every fill in that
 * symbol until the position is flat again. So exits made by any route (stop, target, manual close) count.
 */
static Rec tradeInfo(const Rec &e)
{
    Rec none{{QStringLiteral("state"), QStringLiteral("none")}, {QStringLiteral("qty"), null()}, {QStringLiteral("entryPrice"), null()},
             {QStringLiteral("exitPrice"), null()}, {QStringLiteral("realizedPl"), null()}, {QStringLiteral("plannedRisk"), null()},
             {QStringLiteral("rMultiple"), null()}, {QStringLiteral("openedAt"), null()}, {QStringLiteral("closedAt"), null()}};
    const List ids = e.value(QStringLiteral("order_ids")).toList();
    if (ids.isEmpty())
        return none;
    QStringList marks;
    for (int k = 0; k < ids.size(); ++k)
        marks << QStringLiteral("?");
    const QString in = marks.join(QLatin1Char(','));
    const auto first = db().one(QStringLiteral("SELECT * FROM fills WHERE order_id IN (%1) ORDER BY time, id LIMIT 1").arg(in), ids);
    if (!first) {
        const bool active = db().one(QStringLiteral("SELECT 1 AS x FROM orders WHERE id IN (%1) AND status IN ('working','pending')").arg(in), ids).has_value();
        none.insert(QStringLiteral("state"), active ? QStringLiteral("working") : QStringLiteral("cancelled"));
        return none;
    }
    const qint64 ft = first->value(QStringLiteral("time")).toLongLong(), fid = first->value(QStringLiteral("id")).toLongLong();
    const List fills = db().all(QStringLiteral("SELECT * FROM fills WHERE symbol = ? AND (time > ? OR (time = ? AND id >= ?)) ORDER BY time, id"),
                                {first->value(QStringLiteral("symbol")), ft, ft, fid});
    double pos = 0, realized = 0, openQty = 0, openCost = 0, closeQty = 0, closeCost = 0;
    QVariant closedAt = null();
    for (const QVariant &fv : fills) {
        const Rec f = fv.toMap();
        const QString side = f.value(QStringLiteral("side")).toString();
        const double qty = f.value(QStringLiteral("qty")).toDouble(), price = f.value(QStringLiteral("price")).toDouble();
        pos += isBuy(side) ? qty : -qty;
        realized += f.value(QStringLiteral("realized_pl")).toDouble();
        if (opensPosition(side)) { openQty += qty; openCost += qty * price; } else { closeQty += qty; closeCost += qty * price; }
        if (pos == 0) { closedAt = f.value(QStringLiteral("time")); break; }
    }
    const std::optional<double> entryPrice = openQty ? std::optional<double>(openCost / openQty) : std::nullopt;
    const std::optional<double> planEntry = num(e.value(QStringLiteral("plan_entry"))), planStop = num(e.value(QStringLiteral("plan_stop")));
    const std::optional<double> ref = planEntry ? planEntry : entryPrice;
    const std::optional<double> plannedRisk = ref && planStop ? std::optional<double>(std::fabs(*ref - *planStop) * openQty) : std::nullopt;
    const bool closed = pos == 0;
    return Rec{{QStringLiteral("state"), closed ? QStringLiteral("closed") : QStringLiteral("open")}, {QStringLiteral("qty"), openQty},
               {QStringLiteral("entryPrice"), orNull(entryPrice)}, {QStringLiteral("exitPrice"), closeQty ? QVariant(closeCost / closeQty) : null()},
               {QStringLiteral("realizedPl"), realized}, {QStringLiteral("plannedRisk"), orNull(plannedRisk)},
               {QStringLiteral("rMultiple"), closed && plannedRisk && *plannedRisk != 0 ? QVariant(realized / *plannedRisk) : null()},
               {QStringLiteral("openedAt"), ft}, {QStringLiteral("closedAt"), closedAt}};
}

static Rec withTrade(const Rec &r)
{
    Rec e = toEntry(r);
    e.insert(QStringLiteral("trade"), tradeInfo(e));
    return e;
}

List list(const Rec &f)
{
    QStringList where;
    List args;
    if (!f.value(QStringLiteral("status")).toString().isEmpty()) { where << QStringLiteral("status = ?"); args << f.value(QStringLiteral("status")).toString(); }
    if (!f.value(QStringLiteral("symbol")).toString().isEmpty()) { where << QStringLiteral("symbol = ?"); args << f.value(QStringLiteral("symbol")).toString().toUpper(); }
    if (!f.value(QStringLiteral("source")).toString().isEmpty()) { where << QStringLiteral("source = ?"); args << f.value(QStringLiteral("source")).toString(); }
    const QString q = f.value(QStringLiteral("q")).toString().trimmed();
    if (!q.isEmpty()) {
        where << QStringLiteral("(title LIKE ? OR body LIKE ? OR setup LIKE ? OR tags LIKE ? OR symbol LIKE ? OR review LIKE ? OR lesson LIKE ?)");
        const QString like = QLatin1Char('%') + q + QLatin1Char('%');
        for (int k = 0; k < 7; ++k)
            args << like;
    }
    const int limit = std::max(1, std::min(500, isNumber(f.value(QStringLiteral("limit"))) ? f.value(QStringLiteral("limit")).toInt() : 200));
    List out;
    for (const QVariant &r : db().all(QStringLiteral("SELECT %1 FROM journal_entries %2 ORDER BY created_at DESC, id DESC LIMIT %3")
                                          .arg(COLS, where.isEmpty() ? QString() : QStringLiteral("WHERE ") + where.join(QStringLiteral(" AND "))).arg(limit), args))
        out.append(withTrade(r.toMap()));
    return out;
}

QVariant get(qint64 id)
{
    const auto r = db().one(QStringLiteral("SELECT %1 FROM journal_entries WHERE id = ?").arg(COLS), {id});
    return r ? QVariant(withTrade(*r)) : null();
}

QVariant image(qint64 id)
{
    const auto r = db().one(QStringLiteral("SELECT image FROM journal_entries WHERE id = ?"), {id});
    return r ? r->value(QStringLiteral("image")) : null();
}

Rec create(const Rec &input)
{
    const qint64 now = nowSec();
    const QVariant sym = input.value(QStringLiteral("symbol"));
    const QString symbol = !isNull(sym) && !sym.toString().isEmpty() ? sym.toString().trimmed().toUpper() : QString();
    const QString kind = input.value(QStringLiteral("kind")).toString();
    QString title = str(input.value(QStringLiteral("title")), 160).trimmed();
    if (title.isEmpty())
        title = !symbol.isEmpty() ? QStringLiteral("%1 %2").arg(symbol, kind == QLatin1String("note") ? QStringLiteral("note") : QStringLiteral("trade idea")) : QStringLiteral("Untitled entry");
    const QString direction = input.value(QStringLiteral("direction")).toString();
    const QVariant status = input.value(QStringLiteral("status"));
    const QVariant followed = input.value(QStringLiteral("followed_plan"));
    const QVariant img = input.value(QStringLiteral("image"));
    const RunResult r = db().run(QStringLiteral(
        "INSERT INTO journal_entries (created_at, updated_at, kind, status, source, symbol, direction, title, body, setup, tags, plan_entry, plan_stop, plan_target, plan_qty, emotions, followed_plan, review, lesson, order_ids, image) "
        "VALUES (?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?)"),
        {now, now, kind == QLatin1String("note") ? QStringLiteral("note") : QStringLiteral("trade"), isNull(status) ? QVariant(QStringLiteral("idea")) : status,
         input.value(QStringLiteral("source")).toString() == QLatin1String("claude") ? QStringLiteral("claude") : QStringLiteral("user"),
         symbol.isEmpty() ? null() : QVariant(symbol),
         direction == QLatin1String("long") || direction == QLatin1String("short") ? QVariant(direction) : null(), title,
         str(input.value(QStringLiteral("body")), 20000), str(input.value(QStringLiteral("setup")), 80),
         toJson(cleanTags(input.value(QStringLiteral("tags")), 12)), numOrNull(input.value(QStringLiteral("plan_entry"))),
         numOrNull(input.value(QStringLiteral("plan_stop"))), numOrNull(input.value(QStringLiteral("plan_target"))), numOrNull(input.value(QStringLiteral("plan_qty"))),
         toJson(cleanTags(input.value(QStringLiteral("emotions")), 12)), isNull(followed) ? null() : followed,
         str(input.value(QStringLiteral("review")), 20000), str(input.value(QStringLiteral("lesson")), 5000),
         toJson(isList(input.value(QStringLiteral("order_ids"))) ? integers(input.value(QStringLiteral("order_ids"))) : QVariantList()),
         isNull(img) ? null() : img});
    return get(r.lastInsertRowid).toMap();
}

QVariant update(qint64 id, const Rec &p)
{
    if (!db().one(QStringLiteral("SELECT 1 AS x FROM journal_entries WHERE id = ?"), {id}))
        return null();
    QStringList set{QStringLiteral("updated_at = ?")};
    List args{nowSec()};
    auto put = [&](const QString &col, const QVariant &v) { set << col + QStringLiteral(" = ?"); args << v; };
    auto in = [&](const char *k) { return p.contains(QLatin1String(k)); };
    auto v = [&](const char *k) { return p.value(QLatin1String(k)); };
    if (in("kind")) put(QStringLiteral("kind"), v("kind").toString() == QLatin1String("note") ? QStringLiteral("note") : QStringLiteral("trade"));
    if (in("status") && QStringList{QStringLiteral("idea"), QStringLiteral("taken"), QStringLiteral("skipped")}.contains(v("status").toString())) put(QStringLiteral("status"), v("status").toString());
    if (in("symbol")) put(QStringLiteral("symbol"), !isNull(v("symbol")) && !v("symbol").toString().isEmpty() ? QVariant(v("symbol").toString().trimmed().toUpper()) : null());
    if (in("direction")) put(QStringLiteral("direction"), v("direction").toString() == QLatin1String("long") || v("direction").toString() == QLatin1String("short") ? v("direction") : null());
    if (in("title")) { const QString t = str(v("title"), 160).trimmed(); put(QStringLiteral("title"), t.isEmpty() ? QStringLiteral("Untitled entry") : t); }
    if (in("body")) put(QStringLiteral("body"), str(v("body"), 20000));
    if (in("setup")) put(QStringLiteral("setup"), str(v("setup"), 80));
    if (in("tags")) put(QStringLiteral("tags"), toJson(cleanTags(v("tags"), 12)));
    for (const char *k : {"plan_entry", "plan_stop", "plan_target", "plan_qty"})
        if (in(k)) put(QLatin1String(k), numOrNull(v(k)));
    if (in("emotions")) put(QStringLiteral("emotions"), toJson(cleanTags(v("emotions"), 12)));
    if (in("followed_plan")) put(QStringLiteral("followed_plan"), QStringList{QStringLiteral("yes"), QStringLiteral("partly"), QStringLiteral("no")}.contains(v("followed_plan").toString()) ? v("followed_plan") : null());
    if (in("review")) put(QStringLiteral("review"), str(v("review"), 20000));
    if (in("lesson")) put(QStringLiteral("lesson"), str(v("lesson"), 5000));
    if (in("order_ids")) put(QStringLiteral("order_ids"), toJson(integers(v("order_ids"))));
    if (in("image")) put(QStringLiteral("image"), isNull(v("image")) ? null() : v("image"));
    args << id;
    db().run(QStringLiteral("UPDATE journal_entries SET %1 WHERE id = ?").arg(set.join(QStringLiteral(", "))), args);
    return get(id);
}

QVariant comment(qint64 id, const Rec &c)
{
    const auto r = db().one(QStringLiteral("SELECT comments FROM journal_entries WHERE id = ?"), {id});
    if (!r)
        return null();
    List l = json(r->value(QStringLiteral("comments")), QVariantList()).toList();
    l.append(Rec{{QStringLiteral("by"), c.value(QStringLiteral("by")).toString() == QLatin1String("claude") ? QStringLiteral("claude") : QStringLiteral("user")},
                 {QStringLiteral("at"), nowSec()}, {QStringLiteral("text"), str(c.value(QStringLiteral("text")), 8000)}});
    db().run(QStringLiteral("UPDATE journal_entries SET comments = ?, updated_at = ? WHERE id = ?"), {toJson(l), nowSec(), id});
    return get(id);
}

void remove(qint64 id) { db().run(QStringLiteral("DELETE FROM journal_entries WHERE id = ?"), {id}); }

} // namespace journal

// =========================================================================================== watchlists
namespace watchlists {

bool validSymbol(const QString &symbol)
{
    static const QRegularExpression re(QStringLiteral("^[A-Z0-9.^=-]{1,15}$"));
    return re.match(symbol).hasMatch();
}

void init()
{
    db().exec(QStringLiteral(R"(
    CREATE TABLE IF NOT EXISTS watchlists (
      id INTEGER PRIMARY KEY AUTOINCREMENT,
      name TEXT NOT NULL COLLATE NOCASE UNIQUE,
      created_at INTEGER NOT NULL
    );
    CREATE TABLE IF NOT EXISTS watchlist_items (
      list_id INTEGER NOT NULL REFERENCES watchlists(id) ON DELETE CASCADE,
      symbol TEXT NOT NULL,
      added_at INTEGER NOT NULL,
      PRIMARY KEY (list_id, symbol)
    )
    )"));
    // First run with lists: carry over the single legacy watchlist (or start with a few popular symbols).
    if (db().one(QStringLiteral("SELECT COUNT(*) AS n FROM watchlists"))->value(QStringLiteral("n")).toLongLong() == 0) {
        const qint64 id = db().run(QStringLiteral("INSERT INTO watchlists (name, created_at) VALUES (?, ?)"), {QStringLiteral("Watchlist"), nowSec()}).lastInsertRowid;
        QStringList legacy;
        try {
            for (const QVariant &r : db().all(QStringLiteral("SELECT symbol FROM watchlist ORDER BY added_at, rowid")))
                legacy << r.toMap().value(QStringLiteral("symbol")).toString();
        } catch (const Error &) {
        }
        const QStringList defaults{QStringLiteral("AAPL"), QStringLiteral("MSFT"), QStringLiteral("NVDA"), QStringLiteral("TSLA"), QStringLiteral("SPY")};
        for (const QString &s : legacy.isEmpty() ? defaults : legacy)
            db().run(QStringLiteral("INSERT OR IGNORE INTO watchlist_items (list_id, symbol, added_at) VALUES (?,?,?)"), {id, s, nowSec()});
    }
    // the old single list has been carried over (or there was none); nothing reads it again
    db().exec(QStringLiteral("DROP TABLE IF EXISTS watchlist"));
}

static bool exists(qint64 id) { return db().one(QStringLiteral("SELECT 1 AS x FROM watchlists WHERE id = ?"), {id}).has_value(); }
static QString cleanName(const QString &n) { return n.trimmed().replace(QRegularExpression(QStringLiteral("\\s+")), QStringLiteral(" ")).left(40); }
static bool nameTaken(const QString &name, qint64 exceptId = -1)
{
    return db().one(QStringLiteral("SELECT 1 AS x FROM watchlists WHERE name = ? AND id != ?"), {name, exceptId}).has_value();
}

List lists()
{
    const List items = db().all(QStringLiteral("SELECT list_id, symbol FROM watchlist_items ORDER BY added_at, rowid"));
    List out;
    for (const QVariant &lv : db().all(QStringLiteral("SELECT id, name FROM watchlists ORDER BY id"))) {
        const Rec l = lv.toMap();
        QStringList symbols;
        for (const QVariant &iv : items)
            if (iv.toMap().value(QStringLiteral("list_id")) == l.value(QStringLiteral("id")))
                symbols << iv.toMap().value(QStringLiteral("symbol")).toString();
        out.append(Rec{{QStringLiteral("id"), l.value(QStringLiteral("id"))}, {QStringLiteral("name"), l.value(QStringLiteral("name"))}, {QStringLiteral("symbols"), symbols}});
    }
    return out;
}

Rec create(const QString &nameIn)
{
    const QString name = cleanName(nameIn);
    if (name.isEmpty()) return failError(QStringLiteral("Give the list a name."));
    if (nameTaken(name)) return failError(QStringLiteral("You already have a list called “%1”.").arg(name));
    return ok(Rec{{QStringLiteral("id"), db().run(QStringLiteral("INSERT INTO watchlists (name, created_at) VALUES (?, ?)"), {name, nowSec()}).lastInsertRowid}});
}

Rec rename(qint64 id, const QString &nameIn)
{
    const QString name = cleanName(nameIn);
    if (!exists(id)) return failError(QStringLiteral("That list no longer exists."));
    if (name.isEmpty()) return failError(QStringLiteral("Give the list a name."));
    if (nameTaken(name, id)) return failError(QStringLiteral("You already have a list called “%1”.").arg(name));
    db().run(QStringLiteral("UPDATE watchlists SET name = ? WHERE id = ?"), {name, id});
    return ok();
}

Rec remove(qint64 id)
{
    if (!exists(id)) return failError(QStringLiteral("That list no longer exists."));
    if (db().one(QStringLiteral("SELECT COUNT(*) AS n FROM watchlists"))->value(QStringLiteral("n")).toLongLong() <= 1) return failError(QStringLiteral("You need at least one watchlist."));
    db().run(QStringLiteral("DELETE FROM watchlist_items WHERE list_id = ?"), {id}); // explicit, in case foreign keys are off
    db().run(QStringLiteral("DELETE FROM watchlists WHERE id = ?"), {id});
    return ok();
}

Rec add(qint64 id, const QString &symbolIn)
{
    const QString symbol = symbolIn.trimmed().toUpper();
    if (!exists(id)) return failError(QStringLiteral("That list no longer exists."));
    if (!validSymbol(symbol)) return failError(QStringLiteral("“%1” is not a valid symbol.").arg(symbolIn));
    const RunResult r = db().run(QStringLiteral("INSERT OR IGNORE INTO watchlist_items (list_id, symbol, added_at) VALUES (?,?,?)"), {id, symbol, nowSec()});
    return ok(Rec{{QStringLiteral("symbol"), symbol}, {QStringLiteral("added"), r.changes > 0}});
}

Rec removeSymbol(qint64 id, const QString &symbol)
{
    db().run(QStringLiteral("DELETE FROM watchlist_items WHERE list_id = ? AND symbol = ?"), {id, symbol.trimmed().toUpper()});
    return ok();
}

} // namespace watchlists

// =========================================================================================== strategies
namespace strategies {

const QStringList CATEGORIES{QStringLiteral("Foundations"), QStringLiteral("Trend"), QStringLiteral("Breakouts"), QStringLiteral("Chart patterns"),
                             QStringLiteral("Mean reversion & momentum"), QStringLiteral("Intraday"), QStringLiteral("Events & options"),
                             QStringLiteral("Long-term investing"), QStringLiteral("Process & psychology"), QStringLiteral("My strategies")};
static const QStringList STATUSES{QStringLiteral("new"), QStringLiteral("learning"), QStringLiteral("practiced"), QStringLiteral("confident")};
static const QStringList LEVELS{QStringLiteral("beginner"), QStringLiteral("intermediate"), QStringLiteral("advanced")};

static Rec toDoc(const Rec &r)
{
    return Rec{{QStringLiteral("id"), r.value(QStringLiteral("id"))}, {QStringLiteral("title"), r.value(QStringLiteral("title"))},
               {QStringLiteral("category"), r.value(QStringLiteral("category"))}, {QStringLiteral("level"), r.value(QStringLiteral("level"))},
               {QStringLiteral("minutes"), r.value(QStringLiteral("minutes"))}, {QStringLiteral("summary"), r.value(QStringLiteral("summary"))},
               {QStringLiteral("tags"), json(r.value(QStringLiteral("tags")), QVariantList())}, {QStringLiteral("body"), r.value(QStringLiteral("body"))},
               {QStringLiteral("quiz"), json(r.value(QStringLiteral("quiz")), QVariantList())}, {QStringLiteral("chartSetup"), json(r.value(QStringLiteral("chart_setup")), null())},
               {QStringLiteral("source"), r.value(QStringLiteral("source"))}, {QStringLiteral("created_at"), r.value(QStringLiteral("created_at"))},
               {QStringLiteral("updated_at"), r.value(QStringLiteral("updated_at"))}};
}

static QVariantList cleanQuiz(const QVariant &v)
{
    QVariantList out;
    if (!isList(v))
        return out;
    for (const QVariant &x : v.toList()) {
        const Rec o = x.toMap();
        if (x.metaType().id() == QMetaType::QVariantMap && !o.value(QStringLiteral("q")).toString().isEmpty() && !o.value(QStringLiteral("a")).toString().isEmpty())
            out.append(Rec{{QStringLiteral("q"), str(o.value(QStringLiteral("q")), 400)}, {QStringLiteral("a"), str(o.value(QStringLiteral("a")), 1200)}});
    }
    return out.mid(0, 12);
}

static QVariant cleanSetup(const QVariant &v)
{
    if (v.metaType().id() != QMetaType::QVariantMap)
        return null();
    const Rec o = v.toMap();
    if (o.value(QStringLiteral("range")).metaType().id() != QMetaType::QString || o.value(QStringLiteral("interval")).metaType().id() != QMetaType::QString
        || !isList(o.value(QStringLiteral("studies"))))
        return null();
    Rec out{{QStringLiteral("range"), o.value(QStringLiteral("range"))}, {QStringLiteral("interval"), o.value(QStringLiteral("interval"))}};
    if (o.value(QStringLiteral("type")).metaType().id() == QMetaType::QString)
        out.insert(QStringLiteral("type"), o.value(QStringLiteral("type")));
    QVariantList studies;
    for (const QVariant &sv : o.value(QStringLiteral("studies")).toList()) {
        const Rec s = sv.toMap();
        if (s.value(QStringLiteral("study")).metaType().id() != QMetaType::QString)
            continue;
        Rec st{{QStringLiteral("study"), s.value(QStringLiteral("study"))}};
        if (s.value(QStringLiteral("params")).metaType().id() == QMetaType::QVariantMap)
            st.insert(QStringLiteral("params"), s.value(QStringLiteral("params")));
        studies.append(st);
    }
    out.insert(QStringLiteral("studies"), studies.mid(0, 10));
    return out;
}

List list()
{
    List out;
    for (const QVariant &r : db().all(QStringLiteral("SELECT * FROM strategy_docs ORDER BY updated_at DESC")))
        out.append(toDoc(r.toMap()));
    return out;
}

QVariant get(const QString &id)
{
    const auto r = db().one(QStringLiteral("SELECT * FROM strategy_docs WHERE id = ?"), {id});
    return r ? QVariant(toDoc(*r)) : null();
}

static int minutesOf(const QVariant &v) { return std::max(1, std::min(240, int(std::lround(isNumber(v) ? v.toDouble() : 10)))); }

Rec create(const Rec &input)
{
    const qint64 now = nowSec();
    const QString id = QStringLiteral("custom-") + uuid();
    QString title = str(input.value(QStringLiteral("title")), 120).trimmed();
    if (title.isEmpty()) title = QStringLiteral("Untitled strategy");
    const QString cat = input.value(QStringLiteral("category")).toString();
    const QString level = input.value(QStringLiteral("level")).toString();
    const QVariant setup = cleanSetup(input.value(QStringLiteral("chartSetup")));
    db().run(QStringLiteral("INSERT INTO strategy_docs (id, title, category, level, minutes, summary, tags, body, quiz, chart_setup, source, created_at, updated_at) VALUES (?,?,?,?,?,?,?,?,?,?,?,?,?)"),
             {id, title, CATEGORIES.contains(cat) ? cat : QStringLiteral("My strategies"), LEVELS.contains(level) ? level : QStringLiteral("beginner"),
              minutesOf(input.value(QStringLiteral("minutes"))), str(input.value(QStringLiteral("summary")), 400),
              toJson(cleanTags(input.value(QStringLiteral("tags")), 10)), str(input.value(QStringLiteral("body")), 60000),
              toJson(cleanQuiz(input.value(QStringLiteral("quiz")))), isNull(setup) ? null() : QVariant(toJson(setup)),
              input.value(QStringLiteral("source")).toString() == QLatin1String("claude") ? QStringLiteral("claude") : QStringLiteral("user"), now, now});
    return get(id).toMap();
}

QVariant update(const QString &id, const Rec &p)
{
    if (isNull(get(id)))
        return null();
    QStringList set{QStringLiteral("updated_at = ?")};
    List args{nowSec()};
    auto put = [&](const QString &c, const QVariant &v) { set << c + QStringLiteral(" = ?"); args << v; };
    auto in = [&](const char *k) { return p.contains(QLatin1String(k)); };
    auto v = [&](const char *k) { return p.value(QLatin1String(k)); };
    if (in("title")) { const QString t = str(v("title"), 120).trimmed(); put(QStringLiteral("title"), t.isEmpty() ? QStringLiteral("Untitled strategy") : t); }
    if (in("category") && CATEGORIES.contains(v("category").toString())) put(QStringLiteral("category"), v("category").toString());
    if (in("level") && LEVELS.contains(v("level").toString())) put(QStringLiteral("level"), v("level").toString());
    if (in("minutes")) put(QStringLiteral("minutes"), minutesOf(v("minutes")));
    if (in("summary")) put(QStringLiteral("summary"), str(v("summary"), 400));
    if (in("tags")) put(QStringLiteral("tags"), toJson(cleanTags(v("tags"), 10)));
    if (in("body")) put(QStringLiteral("body"), str(v("body"), 60000));
    if (in("quiz")) put(QStringLiteral("quiz"), toJson(cleanQuiz(v("quiz"))));
    if (in("chartSetup")) { const QVariant s = cleanSetup(v("chartSetup")); put(QStringLiteral("chart_setup"), isNull(s) ? null() : QVariant(toJson(s))); }
    args << id;
    db().run(QStringLiteral("UPDATE strategy_docs SET %1 WHERE id = ?").arg(set.join(QStringLiteral(", "))), args);
    return get(id);
}

void remove(const QString &id)
{
    db().run(QStringLiteral("DELETE FROM strategy_docs WHERE id = ?"), {id});
    db().run(QStringLiteral("DELETE FROM strategy_progress WHERE id = ?"), {id});
}

static Rec toProgress(const Rec &r)
{
    return Rec{{QStringLiteral("id"), r.value(QStringLiteral("id"))}, {QStringLiteral("status"), r.value(QStringLiteral("status"))},
               {QStringLiteral("note"), r.value(QStringLiteral("note"))}, {QStringLiteral("quiz_score"), r.value(QStringLiteral("quiz_score"))},
               {QStringLiteral("updated_at"), r.value(QStringLiteral("updated_at"))}};
}

List progress()
{
    List out;
    for (const QVariant &r : db().all(QStringLiteral("SELECT * FROM strategy_progress")))
        out.append(toProgress(r.toMap()));
    return out;
}

Rec setProgress(const QString &id, const Rec &p)
{
    const auto cur = db().one(QStringLiteral("SELECT * FROM strategy_progress WHERE id = ?"), {id});
    const QString status = p.contains(QStringLiteral("status")) && STATUSES.contains(p.value(QStringLiteral("status")).toString())
        ? p.value(QStringLiteral("status")).toString() : (cur ? cur->value(QStringLiteral("status")).toString() : QStringLiteral("new"));
    const QString note = p.contains(QStringLiteral("note")) ? str(p.value(QStringLiteral("note")), 8000) : (cur ? cur->value(QStringLiteral("note")).toString() : QString());
    QVariant score = cur ? cur->value(QStringLiteral("quiz_score")) : null();
    if (p.contains(QStringLiteral("quiz_score"))) {
        const QVariant q = p.value(QStringLiteral("quiz_score"));
        score = isNull(q) ? null() : QVariant(std::max(0, std::min(100, int(std::lround(q.toDouble())))));
    }
    db().run(QStringLiteral("INSERT INTO strategy_progress (id, status, note, quiz_score, updated_at) VALUES (?,?,?,?,?) ON CONFLICT(id) DO UPDATE SET status = excluded.status, note = excluded.note, quiz_score = excluded.quiz_score, updated_at = excluded.updated_at"),
             {id, status, note, score, nowSec()});
    return toProgress(*db().one(QStringLiteral("SELECT * FROM strategy_progress WHERE id = ?"), {id}));
}

} // namespace strategies

} // namespace tl
