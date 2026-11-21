#include "util.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>
#include <QUuid>
#include <QFile>
#include <QHash>
#include <QMutex>

namespace tl {

QString grouped(double n, int decimals)
{
    const bool neg = n < 0;
    QString s = QString::number(std::fabs(n), 'f', decimals);
    const int dot = s.indexOf(QLatin1Char('.'));
    int intEnd = dot < 0 ? s.size() : dot;
    for (int i = intEnd - 3; i > 0; i -= 3)
        s.insert(i, QLatin1Char(','));
    return neg ? QLatin1Char('-') + s : s;
}

QString money(double n, int decimals)
{
    if (!std::isfinite(n))
        return QStringLiteral("$NaN");
    const double r = std::round(n * std::pow(10, decimals)) / std::pow(10, decimals);
    const QString body = grouped(std::fabs(r), decimals);
    return (r < 0 ? QStringLiteral("-$") : QStringLiteral("$")) + body;
}

static QJsonValue toJsonValue(const QVariant &v)
{
    if (isNull(v))
        return QJsonValue(QJsonValue::Null);
    switch (v.metaType().id()) {
    case QMetaType::QVariantMap: {
        QJsonObject o;
        const QVariantMap m = v.toMap();
        for (auto it = m.cbegin(); it != m.cend(); ++it)
            o.insert(it.key(), toJsonValue(it.value()));
        return o;
    }
    case QMetaType::QVariantList: case QMetaType::QStringList: {
        QJsonArray a;
        for (const QVariant &x : v.toList())
            a.append(toJsonValue(x));
        return a;
    }
    case QMetaType::Double: case QMetaType::Float: {
        const double d = v.toDouble();
        return std::isfinite(d) ? QJsonValue(d) : QJsonValue(QJsonValue::Null);
    }
    default:
        return QJsonValue::fromVariant(v);
    }
}

QString toJson(const QVariant &v)
{
    const QJsonValue j = toJsonValue(v);
    if (j.isObject())
        return QString::fromUtf8(QJsonDocument(j.toObject()).toJson(QJsonDocument::Compact));
    if (j.isArray())
        return QString::fromUtf8(QJsonDocument(j.toArray()).toJson(QJsonDocument::Compact));
    // a scalar: wrap and strip, since QJsonDocument only takes objects and arrays
    QString s = QString::fromUtf8(QJsonDocument(QJsonArray{j}).toJson(QJsonDocument::Compact));
    return s.mid(1, s.size() - 2);
}

static QVariant fromJsonValue(const QJsonValue &j)
{
    switch (j.type()) {
    case QJsonValue::Null: case QJsonValue::Undefined: return null();
    case QJsonValue::Bool: return j.toBool();
    case QJsonValue::Double: return j.toDouble();
    case QJsonValue::String: return j.toString();
    case QJsonValue::Array: {
        QVariantList l;
        for (const QJsonValue &x : j.toArray())
            l.append(fromJsonValue(x));
        return l;
    }
    case QJsonValue::Object: {
        QVariantMap m;
        const QJsonObject o = j.toObject();
        for (auto it = o.begin(); it != o.end(); ++it)
            m.insert(it.key(), fromJsonValue(it.value()));
        return m;
    }
    }
    return null();
}

QVariant fromJson(const QString &text, const QVariant &fallback)
{
    if (text.isEmpty())
        return fallback;
    // QJsonDocument only parses objects and arrays: wrap scalars
    QJsonParseError err;
    const QJsonDocument d = QJsonDocument::fromJson((QStringLiteral("[") + text + QStringLiteral("]")).toUtf8(), &err);
    if (err.error != QJsonParseError::NoError || !d.isArray() || d.array().size() != 1)
        return fallback;
    return fromJsonValue(d.array().at(0));
}

QString uuid() { return QUuid::createUuid().toString(QUuid::WithoutBraces); }

QString contentText(const QString &file)
{
    QFile f(QStringLiteral(":/tradinglab/content/") + file);
    if (!f.open(QIODevice::ReadOnly))
        return {};
    return QString::fromUtf8(f.readAll());
}

QVariant content(const QString &file)
{
    static QMutex m;
    static QHash<QString, QVariant> cache;
    QMutexLocker g(&m);
    auto it = cache.find(file);
    if (it == cache.end())
        it = cache.insert(file, fromJson(contentText(file)));
    return *it;
}

} // namespace tl
