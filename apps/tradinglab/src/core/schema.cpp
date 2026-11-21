// Port of main/db.ts and main/schema.ts.
#include "schema.h"

#include "db.h"

namespace tl {

void initDatabase(const QString &path)
{
    Db::setPath(path);
    Db &db = Db::local();
    db.exec(QStringLiteral(R"(
    CREATE TABLE IF NOT EXISTS settings (
      key TEXT PRIMARY KEY,
      value TEXT NOT NULL
    );
    CREATE TABLE IF NOT EXISTS chats (
      id TEXT PRIMARY KEY,
      title TEXT NOT NULL,
      created_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP,
      updated_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP
    );
    CREATE TABLE IF NOT EXISTS chat_messages (
      id INTEGER PRIMARY KEY AUTOINCREMENT,
      chat_id TEXT NOT NULL REFERENCES chats(id) ON DELETE CASCADE,
      role TEXT NOT NULL CHECK (role IN ('user','assistant')),
      content TEXT NOT NULL,
      created_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP
    )
    )"));
    initTradingSchema();
    initAccountSchema();
    retireLegacyTables();
    initJournalSchema();
    initStrategySchema();
}

void initTradingSchema()
{
    Db::local().exec(QStringLiteral(R"(
    CREATE TABLE IF NOT EXISTS orders (
      id INTEGER PRIMARY KEY AUTOINCREMENT,
      group_id TEXT NOT NULL,
      parent_id INTEGER,
      role TEXT NOT NULL,
      symbol TEXT NOT NULL,
      side TEXT NOT NULL,
      qty INTEGER NOT NULL,
      type TEXT NOT NULL,
      limit_price REAL,
      stop_price REAL,
      trail_amount REAL,
      trail_unit TEXT,
      extreme REAL,
      triggered INTEGER NOT NULL DEFAULT 0,
      tif TEXT NOT NULL,
      status TEXT NOT NULL,
      reason TEXT,
      created_at INTEGER NOT NULL,
      start_at INTEGER NOT NULL,
      expires_at INTEGER,
      checked_to INTEGER NOT NULL DEFAULT 0,
      filled_at INTEGER,
      fill_price REAL,
      source TEXT NOT NULL DEFAULT 'user',
      note TEXT
    );
    CREATE INDEX IF NOT EXISTS orders_status ON orders(status);
    CREATE TABLE IF NOT EXISTS fills (
      id INTEGER PRIMARY KEY AUTOINCREMENT,
      order_id INTEGER NOT NULL,
      symbol TEXT NOT NULL,
      side TEXT NOT NULL,
      qty INTEGER NOT NULL,
      price REAL NOT NULL,
      time INTEGER NOT NULL,
      realized_pl REAL NOT NULL DEFAULT 0
    );
    CREATE TABLE IF NOT EXISTS positions (
      account_id INTEGER NOT NULL DEFAULT 1,
      symbol TEXT NOT NULL,
      qty INTEGER NOT NULL,
      avg_price REAL NOT NULL,
      PRIMARY KEY (account_id, symbol)
    )
    )"));
}

/**
 * Several paper accounts. Runs after initTradingSchema and is safe to repeat: it creates the accounts table, moves the old
 * single `account` row in as account 1, and adds account_id to orders, fills and positions (existing rows belong to account 1).
 */
void initAccountSchema()
{
    Db &db = Db::local();
    db.exec(QStringLiteral(R"(
    CREATE TABLE IF NOT EXISTS accounts (
      id INTEGER PRIMARY KEY AUTOINCREMENT,
      name TEXT NOT NULL,
      type TEXT NOT NULL DEFAULT 'margin',
      broker TEXT NOT NULL DEFAULT '',
      url TEXT NOT NULL DEFAULT '',
      cash REAL NOT NULL,
      starting_cash REAL NOT NULL,
      created_at INTEGER NOT NULL
    );
    CREATE TABLE IF NOT EXISTS account_transfers (
      id INTEGER PRIMARY KEY AUTOINCREMENT,
      account_id INTEGER NOT NULL,
      time INTEGER NOT NULL,
      amount REAL NOT NULL,
      note TEXT NOT NULL DEFAULT ''
    )
    )"));
    // The single-row `account` table of older versions becomes account 1. Copy and drop happen together, so a failure leaves the old table intact.
    const bool hasLegacy = db.hasTable(QStringLiteral("account"));
    db.tx([&] {
        if (db.one(QStringLiteral("SELECT COUNT(*) AS n FROM accounts"))->value(QStringLiteral("n")).toLongLong() == 0) {
            std::optional<Rec> legacy;
            if (hasLegacy)
                legacy = db.one(QStringLiteral("SELECT cash, starting_cash FROM account WHERE id = 1"));
            db.run(QStringLiteral("INSERT INTO accounts (id, name, cash, starting_cash, created_at) VALUES (1, 'Paper account', ?, ?, ?)"),
                   {legacy ? legacy->value(QStringLiteral("cash")) : QVariant(100000.0),
                    legacy ? legacy->value(QStringLiteral("starting_cash")) : QVariant(100000.0), nowSec()});
        }
        if (hasLegacy)
            db.exec(QStringLiteral("DROP TABLE account"));
    });
    if (!db.hasColumn(QStringLiteral("accounts"), QStringLiteral("type")))
        db.exec(QStringLiteral("ALTER TABLE accounts ADD COLUMN type TEXT NOT NULL DEFAULT 'margin'"));
    if (!db.hasColumn(QStringLiteral("orders"), QStringLiteral("account_id")))
        db.exec(QStringLiteral("ALTER TABLE orders ADD COLUMN account_id INTEGER NOT NULL DEFAULT 1"));
    if (!db.hasColumn(QStringLiteral("fills"), QStringLiteral("account_id")))
        db.exec(QStringLiteral("ALTER TABLE fills ADD COLUMN account_id INTEGER NOT NULL DEFAULT 1"));
    if (!db.hasColumn(QStringLiteral("positions"), QStringLiteral("account_id"))) {
        db.exec(QStringLiteral(R"(
      ALTER TABLE positions RENAME TO positions_old;
      CREATE TABLE positions (
        account_id INTEGER NOT NULL DEFAULT 1,
        symbol TEXT NOT NULL,
        qty INTEGER NOT NULL,
        avg_price REAL NOT NULL,
        PRIMARY KEY (account_id, symbol)
      );
      INSERT INTO positions (account_id, symbol, qty, avg_price) SELECT 1, symbol, qty, avg_price FROM positions_old;
      DROP TABLE positions_old
        )"));
    }
    db.exec(QStringLiteral("CREATE INDEX IF NOT EXISTS orders_account ON orders(account_id); CREATE INDEX IF NOT EXISTS fills_account ON fills(account_id)"));
}

void initJournalSchema()
{
    Db::local().exec(QStringLiteral(R"(
    CREATE TABLE IF NOT EXISTS journal_entries (
      id INTEGER PRIMARY KEY AUTOINCREMENT,
      created_at INTEGER NOT NULL,
      updated_at INTEGER NOT NULL,
      kind TEXT NOT NULL DEFAULT 'trade',
      status TEXT NOT NULL DEFAULT 'idea',
      source TEXT NOT NULL DEFAULT 'user',
      symbol TEXT,
      direction TEXT,
      title TEXT NOT NULL,
      body TEXT NOT NULL DEFAULT '',
      setup TEXT NOT NULL DEFAULT '',
      tags TEXT NOT NULL DEFAULT '[]',
      plan_entry REAL, plan_stop REAL, plan_target REAL, plan_qty INTEGER,
      emotions TEXT NOT NULL DEFAULT '[]',
      followed_plan TEXT,
      review TEXT NOT NULL DEFAULT '',
      lesson TEXT NOT NULL DEFAULT '',
      order_ids TEXT NOT NULL DEFAULT '[]',
      comments TEXT NOT NULL DEFAULT '[]',
      image TEXT
    );
    CREATE INDEX IF NOT EXISTS journal_symbol ON journal_entries(symbol)
    )"));
}

void initStrategySchema()
{
    Db::local().exec(QStringLiteral(R"(
    CREATE TABLE IF NOT EXISTS strategy_docs (
      id TEXT PRIMARY KEY,
      title TEXT NOT NULL,
      category TEXT NOT NULL DEFAULT 'My strategies',
      level TEXT NOT NULL DEFAULT 'beginner',
      minutes INTEGER NOT NULL DEFAULT 10,
      summary TEXT NOT NULL DEFAULT '',
      tags TEXT NOT NULL DEFAULT '[]',
      body TEXT NOT NULL DEFAULT '',
      quiz TEXT NOT NULL DEFAULT '[]',
      chart_setup TEXT,
      source TEXT NOT NULL DEFAULT 'user',
      created_at INTEGER NOT NULL,
      updated_at INTEGER NOT NULL
    );
    CREATE TABLE IF NOT EXISTS strategy_progress (
      id TEXT PRIMARY KEY,
      status TEXT NOT NULL DEFAULT 'new',
      note TEXT NOT NULL DEFAULT '',
      quiz_score INTEGER,
      updated_at INTEGER NOT NULL
    )
    )"));
}

/**
 * Drops tables that earlier versions created and nothing uses any more. `account` is retired inside initAccountSchema and
 * `watchlist` by the watchlists store (each right after its data has been moved). The old `trades` log was never read, so it is
 * only dropped when it is empty; one with rows in it is left alone rather than deleting a user's data.
 */
void retireLegacyTables()
{
    Db &db = Db::local();
    if (db.hasTable(QStringLiteral("trades")) && db.one(QStringLiteral("SELECT COUNT(*) AS n FROM trades"))->value(QStringLiteral("n")).toLongLong() == 0)
        db.exec(QStringLiteral("DROP TABLE trades"));
}

} // namespace tl
