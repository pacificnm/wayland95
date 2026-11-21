#pragma once

// File > Back Up Data / Restore From Backup. Port of main/backup.ts: VACUUM INTO a single file without API keys or
// the market-data cache; a restore validates first, saves a safety copy, then swaps the file (the app restarts).

#include "util.h"

namespace tl {

// Settings rows that held API keys upstream. Keys now live in the desktop keyring, but old databases (and backups
// made by the Electron app) may still carry these rows, so they are still stripped and kept the same way.
extern const QStringList SECRET_KEYS;

// {accounts, orders, journal, watchlists, portfolios, chats, createdAt, appVersion}
Rec createBackup(const QString &dest, const QString &appVersion);
// {ok:true, summary} | {ok:false, error}
Rec inspectBackup(const QString &path, const QString &appVersion);
// replaces the live database (the caller restarts the app); returns the safety copy's path
QString restoreBackup(const QString &livePath, const QString &backupPath, const QString &safetyDir, const QString &appVersion);
QString describeBackup(const Rec &summary);

} // namespace tl
