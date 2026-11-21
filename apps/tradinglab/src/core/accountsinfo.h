#pragma once

// Paper accounts: each one mirrors a real-world account (name, brokerage, link, balance) but only ever holds simulated money.
// The pure part: type labels, buying power, the brokerage list and input checks. Port of shared/accounts.ts.
// cash: you can only spend the cash you have and cannot short. margin: Reg T style 2:1 leverage, shorting allowed.

#include "util.h"

#include <QStringList>

namespace tl {

// ACCOUNT_TYPE_LABEL: {cash: 'Cash (no margin, no short)', margin: 'Margin (2:1, can short)'}
Rec accountTypeLabels();
QString accountTypeLabel(const QString &type);
constexpr double MARGIN_MULTIPLE = 2;

// Buying power before working orders are set aside. `gross` is the combined market value of all long and short positions.
double buyingPowerFor(const QString &type, double equity, double cash, double gross);

// BROKERS
QStringList brokers();

// input: a partial AccountInput {name?, broker?, url?, type?}. Returns an error message, or an empty string when the input is acceptable.
// The link must be a web URL, since it is opened in the browser.
QString checkAccountInput(const Rec &input);

} // namespace tl
