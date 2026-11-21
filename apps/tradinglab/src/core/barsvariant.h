#pragma once

// Bars to and from the {time, open, high, low, close, volume} records QML and the Claude tools use.

#include "bar.h"
#include "util.h"

namespace tl {

inline Bars barsFromVariant(const QVariantList &list)
{
    Bars out;
    out.reserve(list.size());
    for (const QVariant &v : list) {
        const QVariantMap m = v.toMap();
        out.push_back(Bar{m.value(QStringLiteral("time")).toLongLong(), m.value(QStringLiteral("open")).toDouble(), m.value(QStringLiteral("high")).toDouble(),
                          m.value(QStringLiteral("low")).toDouble(), m.value(QStringLiteral("close")).toDouble(), m.value(QStringLiteral("volume")).toDouble()});
    }
    return out;
}

inline QVariantMap barToVariant(const Bar &b)
{
    return {{QStringLiteral("time"), b.time}, {QStringLiteral("open"), b.open}, {QStringLiteral("high"), b.high},
            {QStringLiteral("low"), b.low}, {QStringLiteral("close"), b.close}, {QStringLiteral("volume"), b.volume}};
}

inline QVariantList barsToVariant(const Bars &bars)
{
    QVariantList out;
    out.reserve(int(bars.size()));
    for (const Bar &b : bars)
        out.append(barToVariant(b));
    return out;
}

} // namespace tl
