#pragma once

// CRUD for paper accounts and the "active" one. Port of main/accounts.ts. Trading data (orders, fills, positions)
// is scoped to an account id in the engine. Errors are thrown as tl::Error with upstream's messages.

#include "util.h"

namespace tl::accounts {

List list(); // AccountInfo records
Rec get(qint64 id);
qint64 activeId();
void setActive(qint64 id);
Rec create(const Rec &input, const QVariant &balance);
Rec update(qint64 id, const Rec &patch);
// Sets the cash balance to match the real account; the difference is booked as a deposit or withdrawal.
Rec setBalance(qint64 id, const QVariant &cash, const QString &note = QString());
void remove(qint64 id);
List transfers(qint64 id, int limit = 8);

} // namespace tl::accounts
