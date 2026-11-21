// Chart settings defaults. Port of renderer/src/chart/settings.ts.

#include "chartsettings.h"

namespace tl {

const QStringList &chartTypes()
{
    static const QStringList l{QStringLiteral("candles"), QStringLiteral("hollow"), QStringLiteral("bars"),
                               QStringLiteral("line"), QStringLiteral("area"), QStringLiteral("heikin")};
    return l;
}

const QStringList &scaleModes()
{
    static const QStringList l{QStringLiteral("normal"), QStringLiteral("log"), QStringLiteral("percent")};
    return l;
}

const QStringList &crosshairModes()
{
    static const QStringList l{QStringLiteral("normal"), QStringLiteral("magnet")};
    return l;
}

Rec defaultChartSettings()
{
    const Rec volume{{QStringLiteral("uid"), QStringLiteral("default-volume")}, {QStringLiteral("studyId"), QStringLiteral("volume")},
                     {QStringLiteral("params"), Rec()}, {QStringLiteral("visible"), true}};
    return Rec{
        {QStringLiteral("range"), QStringLiteral("1Y")},
        {QStringLiteral("interval"), QStringLiteral("1day")},
        {QStringLiteral("type"), QStringLiteral("candles")},
        {QStringLiteral("scale"), QStringLiteral("normal")},
        {QStringLiteral("grid"), true},
        {QStringLiteral("crosshair"), QStringLiteral("normal")},
        {QStringLiteral("upColor"), QStringLiteral("#26a69a")},
        {QStringLiteral("downColor"), QStringLiteral("#ef5350")},
        {QStringLiteral("studies"), List{volume}},
    };
}

Rec normalizeChartSettings(const QVariant &stored)
{
    Rec r = defaultChartSettings();
    if (stored.metaType().id() != QMetaType::QVariantMap)
        return r;
    const Rec s = stored.toMap();
    for (auto it = s.cbegin(); it != s.cend(); ++it)
        r.insert(it.key(), it.value());
    return r;
}

Rec newStudyInstance(const QString &studyId, const Rec &params)
{
    return Rec{{QStringLiteral("uid"), uuid()}, {QStringLiteral("studyId"), studyId}, {QStringLiteral("params"), params}, {QStringLiteral("visible"), true}};
}

} // namespace tl
