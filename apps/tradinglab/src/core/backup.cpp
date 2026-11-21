// Port of main/backup.ts.
#include "backup.h"

#include "db.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QSqlRecord>
#include <QTimeZone>

namespace tl {

const QStringList SECRET_KEYS{QStringLiteral("fmp_key"), QStringLiteral("anthropic_key")};
// what a file must contain to be accepted as a Trading Lab database
static const QStringList REQUIRED{QStringLiteral("settings"), QStringLiteral("accounts"), QStringLiteral("orders"), QStringLiteral("journal_entries")};

namespace {
// a second connection to some other file, closed when it goes out of scope
class Other
{
public:
    explicit Other(const QString &path, bool readOnly = false)
        : m_name(QStringLiteral("tl-other-") + uuid())
    {
        QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), m_name);
        db.setDatabaseName(path);
        if (readOnly)
            db.setConnectOptions(QStringLiteral("QSQLITE_OPEN_READONLY"));
        if (!db.open())
            throw Error(db.lastError().text());
    }
    ~Other()
    {
        { QSqlDatabase::database(m_name).close(); }
        QSqlDatabase::removeDatabase(m_name);
    }
    void exec(const QString &sql, const List &args = {})
    {
        QSqlQuery q(QSqlDatabase::database(m_name));
        q.prepare(sql);
        for (const QVariant &a : args)
            q.addBindValue(a);
        if (!q.exec())
            throw Error(q.lastError().text());
    }
    List all(const QString &sql)
    {
        QSqlQuery q(QSqlDatabase::database(m_name));
        if (!q.exec(sql))
            throw Error(q.lastError().text());
        List out;
        while (q.next()) {
            Rec r;
            for (int i = 0; i < q.record().count(); ++i)
                r.insert(q.record().fieldName(i), q.value(i));
            out.append(r);
        }
        return out;
    }
    int count(const QString &table)
    {
        try { return all(QStringLiteral("SELECT COUNT(*) AS n FROM %1").arg(table)).value(0).toMap().value(QStringLiteral("n")).toInt(); } catch (...) { return 0; }
    }

private:
    QString m_name;
};

Rec summaryOf(Other &c, const QString &createdAt, const QString &appVersion)
{
    return Rec{{QStringLiteral("accounts"), c.count(QStringLiteral("accounts"))}, {QStringLiteral("orders"), c.count(QStringLiteral("orders"))},
               {QStringLiteral("journal"), c.count(QStringLiteral("journal_entries"))}, {QStringLiteral("watchlists"), c.count(QStringLiteral("watchlists"))},
               {QStringLiteral("portfolios"), c.count(QStringLiteral("portfolios"))}, {QStringLiteral("chats"), c.count(QStringLiteral("chats"))},
               {QStringLiteral("createdAt"), createdAt}, {QStringLiteral("appVersion"), appVersion}};
}

QString isoNow() { return QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs); }

int compareVersions(const QString &a, const QString &b)
{
    const QStringList x = a.split(QLatin1Char('.')), y = b.split(QLatin1Char('.'));
    for (int i = 0; i < std::max(x.size(), y.size()); ++i) {
        const int p = x.value(i).toInt(), q = y.value(i).toInt();
        if (p != q)
            return p < q ? -1 : 1;
    }
    return 0;
}
}

QString describeBackup(const Rec &b)
{
    const int journal = b.value(QStringLiteral("journal")).toInt();
    return QStringLiteral("%1 account(s), %2 order(s), %3 journal entr%4, %5 watchlist(s), %6 portfolio(s)")
        .arg(b.value(QStringLiteral("accounts")).toInt()).arg(b.value(QStringLiteral("orders")).toInt()).arg(journal)
        .arg(journal == 1 ? QStringLiteral("y") : QStringLiteral("ies")).arg(b.value(QStringLiteral("watchlists")).toInt())
        .arg(b.value(QStringLiteral("portfolios")).toInt());
}

// Writes a consistent single-file copy of the live database to dest, without keys or the market-data cache.
Rec createBackup(const QString &dest, const QString &appVersion)
{
    // build beside the target and rename, so a failure never replaces an existing good backup with half a file
    const QString part = dest + QStringLiteral(".part");
    QFile::remove(part);
    Db::local().run(QStringLiteral("VACUUM INTO ?"), {part});
    Rec summary;
    try {
        Other c(part);
        QStringList quoted;
        for (const QString &k : SECRET_KEYS)
            quoted << QLatin1Char('\'') + k + QLatin1Char('\'');
        c.exec(QStringLiteral("DELETE FROM settings WHERE key IN (%1)").arg(quoted.join(QLatin1Char(','))));
        try { c.exec(QStringLiteral("DELETE FROM fmp_cache")); } catch (...) { /* table only exists once the feed was used */ }
        c.exec(QStringLiteral("CREATE TABLE IF NOT EXISTS backup_meta (key TEXT PRIMARY KEY, value TEXT NOT NULL)"));
        const QString createdAt = isoNow();
        const QString put = QStringLiteral("INSERT INTO backup_meta (key, value) VALUES (?, ?) ON CONFLICT(key) DO UPDATE SET value = excluded.value");
        c.exec(put, {QStringLiteral("app_version"), appVersion});
        c.exec(put, {QStringLiteral("created_at"), createdAt});
        c.exec(QStringLiteral("VACUUM"));
        c.exec(QStringLiteral("PRAGMA journal_mode = DELETE"));
        summary = summaryOf(c, createdAt, appVersion);
    } catch (...) {
        QFile::remove(part);
        throw;
    }
    QFile::remove(dest);
    if (!QFile::rename(part, dest))
        throw Error(QStringLiteral("Could not write %1.").arg(dest));
    return summary;
}

// Opens a candidate file read-only and decides whether it is a usable Trading Lab backup.
Rec inspectBackup(const QString &path, const QString &appVersion)
{
    QFile probe(path);
    if (!probe.open(QIODevice::ReadOnly))
        return failError(QStringLiteral("Could not read the file: %1").arg(probe.errorString()));
    if (!probe.read(16).startsWith("SQLite format 3"))
        return failError(QStringLiteral("That file is not a database."));
    probe.close();
    try {
        Other c(path, true);
        if (c.all(QStringLiteral("PRAGMA integrity_check")).value(0).toMap().values().value(0).toString() != QLatin1String("ok"))
            return failError(QStringLiteral("The file is damaged (it failed SQLite's integrity check)."));
        QStringList tables;
        for (const QVariant &t : c.all(QStringLiteral("SELECT name FROM sqlite_master WHERE type = 'table'")))
            tables << t.toMap().value(QStringLiteral("name")).toString();
        for (const QString &t : REQUIRED)
            if (!tables.contains(t))
                return failError(QStringLiteral("This is a database, but not a Trading Lab one."));
        QHash<QString, QString> meta;
        if (tables.contains(QStringLiteral("backup_meta")))
            for (const QVariant &r : c.all(QStringLiteral("SELECT key, value FROM backup_meta")))
                meta.insert(r.toMap().value(QStringLiteral("key")).toString(), r.toMap().value(QStringLiteral("value")).toString());
        const QString made = meta.value(QStringLiteral("app_version"));
        if (!made.isEmpty() && compareVersions(made, appVersion) > 0)
            return failError(QStringLiteral("This backup was made by a newer version (%1). Update Trading Lab first (you have %2).").arg(made, appVersion));
        return ok(Rec{{QStringLiteral("summary"), summaryOf(c, meta.value(QStringLiteral("created_at")), made)}});
    } catch (const Error &e) {
        return failError(QStringLiteral("Could not read the file: %1").arg(e.message()));
    }
}

/**
 * Replaces the live database with a backup. The caller must restart the app afterwards: this thread's handle is closed here.
 * Order matters: everything that can fail happens before the live file is touched, and the current data is saved first.
 */
QString restoreBackup(const QString &livePath, const QString &backupPath, const QString &safetyDir, const QString &appVersion)
{
    QDir().mkpath(safetyDir);
    const QString stamp = QDateTime::currentDateTimeUtc().toString(QStringLiteral("yyyy-MM-ddTHH-mm-ss"));
    const QString safetyCopy = QDir(safetyDir).filePath(QStringLiteral("before-restore-%1.db").arg(stamp));
    createBackup(safetyCopy, appVersion);
    // keep only the 5 newest safety copies
    QStringList old = QDir(safetyDir).entryList({QStringLiteral("before-restore-*.db")}, QDir::Files, QDir::Name | QDir::Reversed);
    for (const QString &f : old.mid(5))
        QFile::remove(QDir(safetyDir).filePath(f));

    const QString stage = livePath + QStringLiteral(".restoring");
    QFile::remove(stage);
    try {
        if (!QFile::copy(backupPath, stage))
            throw Error(QStringLiteral("Could not copy the backup."));
        Other s(stage);
        // this computer's own key rows survive a restore, whatever the backup came from
        for (const QVariant &r : Db::local().all(QStringLiteral("SELECT key, value FROM settings WHERE key IN (?, ?)"), {SECRET_KEYS[0], SECRET_KEYS[1]}))
            s.exec(QStringLiteral("INSERT INTO settings (key, value) VALUES (?, ?) ON CONFLICT(key) DO UPDATE SET value = excluded.value"),
                   {r.toMap().value(QStringLiteral("key")), r.toMap().value(QStringLiteral("value"))});
        s.exec(QStringLiteral("PRAGMA journal_mode = DELETE"));
    } catch (...) {
        QFile::remove(stage);
        throw;
    }
    Db::closeLocal();
    QFile::remove(livePath + QStringLiteral("-wal"));
    QFile::remove(livePath + QStringLiteral("-shm"));
    QFile::remove(livePath);
    QFile::rename(stage, livePath);
    return safetyCopy;
}

} // namespace tl
