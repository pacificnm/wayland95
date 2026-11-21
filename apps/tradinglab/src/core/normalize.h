#pragma once

// FMP JSON records -> the app's row shapes. Port of shared/screener.ts and shared/congress.ts.
//   ScreenerRow    {symbol, name, marketCap, sector, industry, beta, price, dividend, volume, avgVolume, exchange, country, isEtf, isFund, active}
//   CongressTrade  {chamber, memberId, member, district, owner, symbol, asset, assetType, type, amount, disclosed, traded, link}

#include "util.h"

namespace tl {

// One row of FMP's company-screener response.
Rec normalizeScreenerRow(const Rec &r);
// FMP's Senate and House disclosure rows share one shape. Note the House rows also use the field name `senateID`.
// chamber: 'senate' | 'house'
Rec normalizeTrade(const QString &chamber, const Rec &r);

// `String(v)` as a template string prints it (numbers in JS form, booleans as words)
QString jsValueString(const QVariant &v);

} // namespace tl
