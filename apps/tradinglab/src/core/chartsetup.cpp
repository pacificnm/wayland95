// Strategy chart setups. Port of renderer/src/chart/setup.ts.

#include "chartsetup.h"

#include "core/chartsettings.h"
#include "core/jsnum.h"
#include "core/studies.h"
#include "core/timeframe.h"

#include <QRegularExpression>

#include <algorithm>

namespace tl {

SetupResult chartSettingsFromSetup(const Rec &current, const Rec &setup)
{
    SetupResult res;
    const QString sr = setup.value(QStringLiteral("range")).toString(), si = setup.value(QStringLiteral("interval")).toString();
    const QString range = isRange(sr) ? sr : current.value(QStringLiteral("range")).toString();
    const QString wanted = isInterval(si) ? si : current.value(QStringLiteral("interval")).toString();
    const QString interval = coerceInterval(range, wanted);
    List studies;
    for (const QVariant &v : setup.value(QStringLiteral("studies")).toList()) {
        const Rec s = v.toMap();
        const QString study = s.value(QStringLiteral("study")).toString();
        const StudyDef *def = studyById(study);
        if (!def) {
            res.dropped << QStringLiteral("study \"%1\"").arg(study);
            continue;
        }
        Rec params;
        const Rec given = s.value(QStringLiteral("params")).toMap();
        for (auto it = given.cbegin(); it != given.cend(); ++it) {
            const bool known = std::any_of(def->params.begin(), def->params.end(), [&](const StudyParam &p) { return p.key == it.key(); });
            if (known && isPos(it.value()))
                params.insert(it.key(), it.value().toDouble());
            else
                res.dropped << QStringLiteral("%1 parameter \"%2\"").arg(study, it.key());
        }
        studies << Rec{{QStringLiteral("uid"), uuid()}, {QStringLiteral("studyId"), def->id}, {QStringLiteral("params"), params}, {QStringLiteral("visible"), true}};
    }
    const QString st = setup.value(QStringLiteral("type")).toString();
    const QString type = chartTypes().contains(st) ? st : current.value(QStringLiteral("type")).toString();
    res.settings = current;
    res.settings.insert(QStringLiteral("range"), range);
    res.settings.insert(QStringLiteral("interval"), interval);
    res.settings.insert(QStringLiteral("type"), type);
    res.settings.insert(QStringLiteral("studies"), studies);
    return res;
}

Rec setupFromSettings(const Rec &s)
{
    List studies;
    for (const QVariant &v : s.value(QStringLiteral("studies")).toList()) {
        const Rec x = v.toMap();
        if (!x.value(QStringLiteral("visible")).toBool())
            continue;
        Rec r{{QStringLiteral("study"), x.value(QStringLiteral("studyId"))}};
        const Rec params = x.value(QStringLiteral("params")).toMap();
        if (!params.isEmpty())
            r.insert(QStringLiteral("params"), params);
        studies << r;
    }
    return Rec{{QStringLiteral("range"), s.value(QStringLiteral("range"))}, {QStringLiteral("interval"), s.value(QStringLiteral("interval"))},
               {QStringLiteral("type"), s.value(QStringLiteral("type"))}, {QStringLiteral("studies"), studies}};
}

Rec describeSetup(const Rec &c)
{
    const QString rid = c.value(QStringLiteral("range")).toString(), iid = c.value(QStringLiteral("interval")).toString();
    QString range = rid, interval = iid;
    for (const RangeInfo &r : ranges())
        if (r.id == rid) range = r.label;
    for (const IntervalInfo &i : intervals())
        if (i.id == iid) interval = i.label;
    static const QRegularExpression paren(QStringLiteral(" \\(.*\\)"));
    QStringList studies;
    for (const QVariant &v : c.value(QStringLiteral("studies")).toList()) {
        const Rec s = v.toMap();
        const QString id = s.value(QStringLiteral("study")).toString();
        const StudyDef *d = studyById(id);
        if (!d) {
            studies << id;
            continue;
        }
        const Rec given = s.value(QStringLiteral("params")).toMap();
        QStringList p;
        QStringList seen;
        auto show = [](const QVariant &x) { return isNumber(x) ? jsString(x.toDouble()) : x.toString(); };
        for (const StudyParam &sp : d->params)
            if (given.contains(sp.key)) { p << show(given.value(sp.key)); seen << sp.key; }
        for (auto it = given.cbegin(); it != given.cend(); ++it)
            if (!seen.contains(it.key())) p << show(it.value());
        QString name = d->name;
        const QRegularExpressionMatch m = paren.match(name);
        if (m.hasMatch())
            name.remove(m.capturedStart(), m.capturedLength());
        studies << name + (p.isEmpty() ? QString() : QStringLiteral(" (%1)").arg(p.join(QStringLiteral(", "))));
    }
    return Rec{{QStringLiteral("range"), range}, {QStringLiteral("interval"), interval}, {QStringLiteral("studies"), studies}};
}

} // namespace tl
