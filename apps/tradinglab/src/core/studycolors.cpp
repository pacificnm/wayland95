// Study output colours. Port of renderer/src/chart/colors.ts.

#include "studycolors.h"

#include <QHash>
#include <QRegularExpression>

#include <cmath>

namespace tl {

const QStringList &dupPalette()
{
    static const QStringList p{QStringLiteral("#ba68c8"), QStringLiteral("#81c784"), QStringLiteral("#ff7043"),
                               QStringLiteral("#fff176"), QStringLiteral("#4dd0e1"), QStringLiteral("#f06292")};
    return p;
}

static Bars dummy()
{
    Bars c;
    for (int i = 0; i < 260; ++i) {
        const double b = 100 + std::sin(i / 9.0) * 8 + i * 0.05;
        c.push_back(Bar{1'700'000'000 + qint64(i) * 86400, b, b + 2, b - 2, b + 0.6, 1000.0 + i});
    }
    return c;
}

const std::vector<StudyOutput> &studyOutputs(const StudyDef &def)
{
    // every study is run once, the first time any is asked for (thread-safe static init)
    static const QHash<QString, std::vector<StudyOutput>> cache = [] {
        QHash<QString, std::vector<StudyOutput>> h;
        const Bars c = dummy();
        for (const StudyDef &d : studies())
            h.insert(d.id, d.compute(c, fullParams(d, {})));
        return h;
    }();
    static const std::vector<StudyOutput> none;
    const auto it = cache.constFind(def.id);
    return it == cache.cend() ? none : *it;
}

int copyIndex(const List &studies, const QString &uid)
{
    QString studyId;
    bool found = false;
    for (const QVariant &v : studies) {
        const Rec s = v.toMap();
        if (s.value(QStringLiteral("uid")).toString() == uid) { studyId = s.value(QStringLiteral("studyId")).toString(); found = true; break; }
    }
    if (!found)
        return 0;
    int i = 0;
    for (const QVariant &v : studies) {
        const Rec s = v.toMap();
        if (s.value(QStringLiteral("studyId")).toString() != studyId)
            continue;
        if (s.value(QStringLiteral("uid")).toString() == uid)
            return i;
        ++i;
    }
    return -1;
}

static QString override(const Rec &inst, const QString &key)
{
    const QVariant v = inst.value(QStringLiteral("colors")).toMap().value(key);
    return isNull(v) ? QString() : v.toString();
}

QString lineColor(const StudyOutput &out, const Rec &inst, int copy, int outIndex)
{
    const QString o = override(inst, out.name);
    if (!o.isEmpty())
        return o;
    const QStringList &pal = dupPalette();
    return copy > 0 && out.kind == QLatin1String("line") && !out.tones ? pal.at((copy - 1 + outIndex) % pal.size()) : out.color;
}

static QString alphaOf(const QString &c) { return c.size() == 9 ? c.mid(7) : QString(); }

std::optional<StudyTones> toneColors(const StudyOutput &out, const Rec &inst)
{
    if (!out.tones)
        return std::nullopt;
    auto pick = [&](const QString &k, const QString &def) {
        const QString o = override(inst, out.name + QLatin1Char(':') + k);
        return !o.isEmpty() ? o.left(7) + alphaOf(def) : def;
    };
    return StudyTones{pick(QStringLiteral("up"), out.tones->up), pick(QStringLiteral("down"), out.tones->down)};
}

QStringList recolour(const StudyOutput &out, const Rec &inst)
{
    const auto t = toneColors(out, inst);
    if (out.colors.isEmpty() || !t || !out.tones)
        return out.colors;
    QStringList r;
    r.reserve(out.colors.size());
    for (const QString &c : out.colors)
        r << (c == out.tones->up ? t->up : c == out.tones->down ? t->down : c);
    return r;
}

bool isHex(const QVariant &v)
{
    static const QRegularExpression re(QStringLiteral("^#[0-9a-fA-F]{6}$"));
    return v.metaType().id() == QMetaType::QString && re.match(v.toString()).hasMatch();
}

} // namespace tl
