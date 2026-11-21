#pragma once

// The small SQLite stores: journal, watchlists and strategy documents/progress.
// Ports of main/journal.ts, main/watchlists.ts and main/strategies.ts (same tables, same results, same messages).

#include "util.h"

namespace tl {

namespace journal {
// filter {status?, symbol?, q?, source?, limit?}; items are JournalItem records (entry fields plus `trade`)
List list(const Rec &filter = {});
QVariant get(qint64 id); // item or null
QVariant image(qint64 id); // data URL string or null
Rec create(const Rec &input);
QVariant update(qint64 id, const Rec &patch);
QVariant comment(qint64 id, const Rec &c); // {by, text}
void remove(qint64 id);
extern const QStringList EMOTIONS;
}

namespace watchlists {
void init(); // creates the tables and the first list
List lists(); // [{id, name, symbols}]
Rec create(const QString &name); // {ok, id} | {ok:false, error}
Rec rename(qint64 id, const QString &name);
Rec remove(qint64 id);
Rec add(qint64 id, const QString &symbol); // {ok, symbol, added}
Rec removeSymbol(qint64 id, const QString &symbol);
bool validSymbol(const QString &symbol); // SYMBOL_RE
}

namespace strategies {
extern const QStringList CATEGORIES;
List list(); // user and Claude documents, newest first
QVariant get(const QString &id);
Rec create(const Rec &input);
QVariant update(const QString &id, const Rec &patch);
void remove(const QString &id);
List progress();
Rec setProgress(const QString &id, const Rec &p);
}

} // namespace tl
