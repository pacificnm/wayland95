#pragma once

// Small helpers shared by the Trading Lab core. Records that the upstream TypeScript passed around as plain objects
// are QVariantMaps here, with the same field names, so the QML screens and the Claude tools read them the same way.

#include <QDateTime>
#include <QJsonDocument>
#include <QString>
#include <QVariant>
#include <QVariantList>
#include <QVariantMap>

#include <cmath>
#include <optional>
#include <stdexcept>

namespace tl {

using Rec = QVariantMap;
using List = QVariantList;

// what upstream threw as `new Error(message)`: handlers catch it and report the message
class Error : public std::runtime_error
{
public:
    explicit Error(const QString &message)
        : std::runtime_error(message.toStdString())
    {
    }
    QString message() const { return QString::fromStdString(what()); }
};

inline qint64 nowSec() { return QDateTime::currentSecsSinceEpoch(); }
inline qint64 nowMs() { return QDateTime::currentMSecsSinceEpoch(); }

// JSON null (also what a NULL column becomes)
inline QVariant null() { return QVariant::fromValue(nullptr); }
inline bool isNull(const QVariant &v) { return !v.isValid() || v.isNull() || v.metaType().id() == QMetaType::Nullptr; }
inline QVariant orNull(const std::optional<double> &v) { return v ? QVariant(*v) : null(); }

// `typeof v === 'number' && Number.isFinite(v)`
inline bool isNumber(const QVariant &v)
{
    if (isNull(v))
        return false;
    switch (v.metaType().id()) {
    case QMetaType::Int: case QMetaType::UInt: case QMetaType::LongLong: case QMetaType::ULongLong:
    case QMetaType::Double: case QMetaType::Float: case QMetaType::Long: case QMetaType::ULong:
    case QMetaType::Short: case QMetaType::UShort:
        return std::isfinite(v.toDouble());
    default:
        return false;
    }
}
inline std::optional<double> num(const QVariant &v) { return isNumber(v) ? std::optional<double>(v.toDouble()) : std::nullopt; }
// a finite number > 0 (upstream's `pos`)
inline bool isPos(const QVariant &v) { return isNumber(v) && v.toDouble() > 0; }
inline bool isInteger(const QVariant &v) { return isNumber(v) && std::floor(v.toDouble()) == v.toDouble(); }
// `Number(v)` for strings and numbers (NaN when it is not one)
inline double toNumber(const QVariant &v)
{
    if (isNull(v))
        return NAN;
    if (v.metaType().id() == QMetaType::QString) {
        bool ok = false;
        const QString s = v.toString().trimmed();
        if (s.isEmpty())
            return 0;
        const double d = s.toDouble(&ok);
        return ok ? d : NAN;
    }
    if (v.metaType().id() == QMetaType::Bool)
        return v.toBool() ? 1 : 0;
    bool ok = false;
    const double d = v.toDouble(&ok);
    return ok ? d : NAN;
}

// `n.toFixed(d)`
inline QString fixed(double n, int d) { return QString::number(n, 'f', d); }
// en-US currency, e.g. $1,234.56 and -$12.00 (`toLocaleString('en-US', {style: 'currency'})`)
QString money(double n, int decimals = 2);
// en-US grouping without a currency sign, e.g. 1,234,567
QString grouped(double n, int decimals = 0);

QString toJson(const QVariant &v);
QVariant fromJson(const QString &text, const QVariant &fallback = QVariant());
// like JSON.parse(text) as T with a default when it is missing or not JSON
inline QVariantList jsonList(const QVariant &text)
{
    const QVariant v = fromJson(text.toString());
    return v.metaType().id() == QMetaType::QVariantList ? v.toList() : QVariantList();
}

QString uuid();

// a converted content file (apps/tradinglab/content, compiled in at :/tradinglab/content/), parsed once
QVariant content(const QString &file);
QString contentText(const QString &file);

// {ok: false, errors: [...]} and {ok: false, error: "..."} as upstream's handlers returned them
inline Rec failErrors(const QStringList &errors) { return Rec{{QStringLiteral("ok"), false}, {QStringLiteral("errors"), errors}}; }
inline Rec failError(const QString &error) { return Rec{{QStringLiteral("ok"), false}, {QStringLiteral("error"), error}}; }
inline Rec ok(Rec r = {}) { r.insert(QStringLiteral("ok"), true); return r; }

} // namespace tl
