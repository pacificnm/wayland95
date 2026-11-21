// FMP record normalisation. Port of shared/screener.ts and shared/congress.ts.

#include "normalize.h"

#include "jsnum.h"

namespace tl {

QString jsValueString(const QVariant &v)
{
    if (!v.isValid())
        return QStringLiteral("undefined");
    if (isNull(v))
        return QStringLiteral("null");
    if (v.metaType().id() == QMetaType::Bool)
        return v.toBool() ? QStringLiteral("true") : QStringLiteral("false");
    if (isNumber(v) || v.metaType().id() == QMetaType::Double || v.metaType().id() == QMetaType::Float)
        return jsString(v.toDouble());
    if (v.metaType().id() == QMetaType::QVariantMap)
        return QStringLiteral("[object Object]");
    if (v.metaType().id() == QMetaType::QVariantList) {
        QStringList parts;
        for (const QVariant &x : v.toList())
            parts << (isNull(x) ? QString() : jsValueString(x));
        return parts.join(QLatin1Char(','));
    }
    return v.toString();
}

Rec normalizeScreenerRow(const Rec &r)
{
    auto n = [&](const char *k) -> QVariant {
        const QVariant v = r.value(QLatin1String(k));
        if (isNull(v) || (v.metaType().id() == QMetaType::QString && v.toString().isEmpty()))
            return null();
        const double d = toNumber(v);
        return std::isfinite(d) ? QVariant(d) : null();
    };
    auto s = [&](const char *k) -> QString {
        const QVariant v = r.value(QLatin1String(k));
        return isNull(v) ? QString() : jsValueString(v);
    };
    auto isTrue = [&](const char *k) {
        const QVariant v = r.value(QLatin1String(k));
        return v.metaType().id() == QMetaType::Bool && v.toBool();
    };
    const QVariant act = r.value(QStringLiteral("isActivelyTrading"));
    const bool notActive = act.metaType().id() == QMetaType::Bool && !act.toBool();
    QString exchange = s("exchangeShortName");
    if (exchange.isEmpty())
        exchange = s("exchange");
    return Rec{
        {QStringLiteral("symbol"), s("symbol")},
        {QStringLiteral("name"), s("companyName")},
        {QStringLiteral("marketCap"), n("marketCap")},
        {QStringLiteral("sector"), s("sector")},
        {QStringLiteral("industry"), s("industry")},
        {QStringLiteral("beta"), n("beta")},
        {QStringLiteral("price"), n("price")},
        {QStringLiteral("dividend"), n("lastAnnualDividend")},
        {QStringLiteral("volume"), n("volume")},
        {QStringLiteral("avgVolume"), n("avgVolume")},
        {QStringLiteral("exchange"), exchange},
        {QStringLiteral("country"), s("country")},
        {QStringLiteral("isEtf"), isTrue("isEtf")},
        {QStringLiteral("isFund"), isTrue("isFund")},
        {QStringLiteral("active"), !notActive},
    };
}

Rec normalizeTrade(const QString &chamber, const Rec &r)
{
    auto str = [&](const char *k) -> QString {
        const QVariant v = r.value(QLatin1String(k));
        return isNull(v) ? QString() : jsValueString(v);
    };
    QString member = str("office");
    if (member.isEmpty())
        member = (str("firstName") + QLatin1Char(' ') + str("lastName")).trimmed();
    return Rec{
        {QStringLiteral("chamber"), chamber},
        {QStringLiteral("memberId"), str("senateID")},
        {QStringLiteral("member"), member},
        {QStringLiteral("district"), str("district")},
        {QStringLiteral("owner"), str("owner")},
        {QStringLiteral("symbol"), str("symbol")},
        {QStringLiteral("asset"), str("assetDescription")},
        {QStringLiteral("assetType"), str("assetType")},
        {QStringLiteral("type"), str("type")},
        {QStringLiteral("amount"), str("amount")},
        {QStringLiteral("disclosed"), str("disclosureDate").left(10)},
        {QStringLiteral("traded"), str("transactionDate").left(10)},
        {QStringLiteral("link"), str("link")},
    };
}

} // namespace tl
