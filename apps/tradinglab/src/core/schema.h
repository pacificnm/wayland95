#pragma once

// The database file and its tables. Port of main/db.ts and main/schema.ts: CREATE TABLE IF NOT EXISTS,
// idempotent ALTERs, no migration framework. Watchlists, portfolios and the FMP cache create their own tables.

#include <QString>

namespace tl {

// Opens (creating it if need be) the database at `path` on this thread and brings every table up to date.
void initDatabase(const QString &path);

void initTradingSchema();
void initAccountSchema();
void initJournalSchema();
void initStrategySchema();
void retireLegacyTables();

} // namespace tl
