#pragma once

// The SQLite database (trading.db), in the shape of node:sqlite's DatabaseSync that upstream used:
// one(sql, args) is prepare(sql).get(...), all() is .all(), run() is .run(), exec() runs several statements.
// Every thread gets its own connection to the same file (WAL), so handlers can run on a thread pool.

#include "util.h"

#include <QSqlDatabase>
#include <QString>

#include <optional>

namespace tl {

struct RunResult
{
    qint64 changes = 0;
    qint64 lastInsertRowid = 0;
};

class Db
{
public:
    // the file every connection opens; set once before the first use
    static void setPath(const QString &path);
    static QString path();
    // this thread's connection (opened on first use)
    static Db &local();

    std::optional<Rec> one(const QString &sql, const List &args = {});
    List all(const QString &sql, const List &args = {});
    RunResult run(const QString &sql, const List &args = {});
    // several statements separated by semicolons (no semicolons inside string literals)
    void exec(const QString &sql);
    bool hasColumn(const QString &table, const QString &column);
    bool hasTable(const QString &table);

    // db.exec('BEGIN') ... COMMIT, or ROLLBACK when fn throws. Nested calls join the outer transaction.
    template<typename F>
    auto tx(F &&fn) -> decltype(fn())
    {
        if (m_depth > 0) {
            ++m_depth;
            struct Leave { int &d; ~Leave() { --d; } } leave{m_depth};
            return fn();
        }
        exec(QStringLiteral("BEGIN IMMEDIATE"));
        m_depth = 1;
        try {
            if constexpr (std::is_void_v<decltype(fn())>) {
                fn();
                m_depth = 0;
                exec(QStringLiteral("COMMIT"));
            } else {
                auto r = fn();
                m_depth = 0;
                exec(QStringLiteral("COMMIT"));
                return r;
            }
        } catch (...) {
            m_depth = 0;
            exec(QStringLiteral("ROLLBACK"));
            throw;
        }
    }

    QSqlDatabase &handle() { return m_db; }
    // closes this thread's connection (backup restore swaps the file)
    static void closeLocal();

private:
    explicit Db(const QString &connection);
    QSqlDatabase m_db;
    int m_depth = 0;
};

// settings table helpers (values are JSON, as upstream stored them)
QVariant getSetting(const QString &key, const QVariant &fallback = QVariant());
void setSetting(const QString &key, const QVariant &value);

} // namespace tl
