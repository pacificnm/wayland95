#include "db.h"

#include <QAtomicInt>
#include <QMutex>
#include <QSqlError>
#include <QSqlQuery>
#include <QSqlRecord>
#include <QThreadStorage>

namespace tl {

namespace {
QString g_path;
QAtomicInt g_counter;
QThreadStorage<Db *> g_local;

QVariant bindable(const QVariant &v)
{
    // a QString binds as text even when it is a null one (upstream bound ''); only null()/invalid is SQL NULL
    if (v.metaType().id() == QMetaType::QString)
        return QVariant(v.toString().isNull() ? QStringLiteral("") : v.toString());
    if (isNull(v))
        return QVariant(QMetaType(QMetaType::QString)); // a null value: binds as NULL
    if (v.metaType().id() == QMetaType::Bool)
        return v.toBool() ? 1 : 0;
    return v;
}

QSqlQuery prepared(QSqlDatabase &db, const QString &sql, const List &args)
{
    QSqlQuery q(db);
    q.setForwardOnly(true);
    if (!q.prepare(sql))
        throw Error(QStringLiteral("SQL error: %1 (%2)").arg(q.lastError().text(), sql.simplified().left(120)));
    for (const QVariant &a : args)
        q.addBindValue(bindable(a));
    if (!q.exec())
        throw Error(QStringLiteral("SQL error: %1 (%2)").arg(q.lastError().text(), sql.simplified().left(120)));
    return q;
}

Rec rowOf(const QSqlQuery &q)
{
    Rec r;
    const QSqlRecord rec = q.record();
    for (int i = 0; i < rec.count(); ++i) {
        const QVariant v = q.value(i);
        r.insert(rec.fieldName(i), v.isNull() ? null() : v);
    }
    return r;
}
}

void Db::setPath(const QString &path) { g_path = path; }
QString Db::path() { return g_path; }

Db &Db::local()
{
    if (!g_local.hasLocalData())
        g_local.setLocalData(new Db(QStringLiteral("tl-%1").arg(g_counter.fetchAndAddRelaxed(1))));
    return *g_local.localData();
}

void Db::closeLocal()
{
    if (g_local.hasLocalData()) {
        const QString name = g_local.localData()->m_db.connectionName();
        g_local.localData()->m_db.close();
        g_local.setLocalData(nullptr); // deletes it
        QSqlDatabase::removeDatabase(name);
    }
}

Db::Db(const QString &connection)
{
    m_db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connection);
    m_db.setDatabaseName(g_path);
    m_db.setConnectOptions(QStringLiteral("QSQLITE_BUSY_TIMEOUT=10000"));
    if (!m_db.open())
        throw Error(QStringLiteral("Cannot open the database %1: %2").arg(g_path, m_db.lastError().text()));
    QSqlQuery q(m_db);
    q.exec(QStringLiteral("PRAGMA journal_mode = WAL"));
    q.exec(QStringLiteral("PRAGMA foreign_keys = ON"));
}

std::optional<Rec> Db::one(const QString &sql, const List &args)
{
    QSqlQuery q = prepared(m_db, sql, args);
    if (q.next())
        return rowOf(q);
    return std::nullopt;
}

List Db::all(const QString &sql, const List &args)
{
    QSqlQuery q = prepared(m_db, sql, args);
    List out;
    while (q.next())
        out.append(rowOf(q));
    return out;
}

RunResult Db::run(const QString &sql, const List &args)
{
    QSqlQuery q = prepared(m_db, sql, args);
    RunResult r;
    r.changes = q.numRowsAffected();
    r.lastInsertRowid = q.lastInsertId().toLongLong();
    return r;
}

void Db::exec(const QString &sql)
{
    for (const QString &part : sql.split(QLatin1Char(';'))) {
        const QString s = part.trimmed();
        if (s.isEmpty())
            continue;
        QSqlQuery q(m_db);
        if (!q.exec(s))
            throw Error(QStringLiteral("SQL error: %1 (%2)").arg(q.lastError().text(), s.simplified().left(120)));
    }
}

bool Db::hasColumn(const QString &table, const QString &column)
{
    for (const QVariant &c : all(QStringLiteral("PRAGMA table_info(%1)").arg(table)))
        if (c.toMap().value(QStringLiteral("name")).toString() == column)
            return true;
    return false;
}

bool Db::hasTable(const QString &table)
{
    return one(QStringLiteral("SELECT 1 AS x FROM sqlite_master WHERE type = 'table' AND name = ?"), {table}).has_value();
}

QVariant getSetting(const QString &key, const QVariant &fallback)
{
    const auto r = Db::local().one(QStringLiteral("SELECT value FROM settings WHERE key = ?"), {key});
    if (!r)
        return fallback;
    return fromJson(r->value(QStringLiteral("value")).toString(), fallback);
}

void setSetting(const QString &key, const QVariant &value)
{
    Db::local().run(QStringLiteral("INSERT INTO settings (key, value) VALUES (?, ?) ON CONFLICT(key) DO UPDATE SET value = excluded.value"),
                    {key, toJson(value)});
}

} // namespace tl
