// Study registry and computation. Port of renderer/src/chart/studies.ts.

#include "studies.h"

#include "core/jsnum.h"
#include "core/studycolors.h"
#include "core/util.h"

namespace tl {

using namespace ind;

namespace {

StudyParam len(double d) { return {QStringLiteral("length"), QStringLiteral("Length"), d, 0}; }
// lengths are bar counts (see indicators.h)
int L(const StudyParams &p, const char *key) { return int(p.value(QLatin1String(key))); }
double P(const StudyParams &p, const char *key) { return p.value(QLatin1String(key)); }

StudyOutput line(const QString &name, const QString &color, Series values)
{
    StudyOutput o;
    o.name = name;
    o.kind = QStringLiteral("line");
    o.color = color;
    o.values = std::move(values);
    return o;
}

Series zipNull(const Series &a, const Series &b, const std::function<double(double, double)> &f)
{
    Series out(a.size(), NAN);
    for (size_t i = 0; i < a.size(); ++i)
        if (has(a[i]) && has(b[i])) out[i] = f(a[i], b[i]);
    return out;
}

std::vector<StudyDef> build()
{
    std::vector<StudyDef> s;
    const QString up = QStringLiteral("#26a69a"), down = QStringLiteral("#ef5350");

    s.push_back({QStringLiteral("sma"), QStringLiteral("Moving Average (SMA)"), QStringLiteral("Overlay"), QStringLiteral("overlay"), {len(20)}, {}, {},
        [](const Bars &c, const StudyParams &p) { return std::vector<StudyOutput>{line(QStringLiteral("SMA"), QStringLiteral("#f5a623"), sma(closes(c), L(p, "length")))}; }});
    s.push_back({QStringLiteral("ema"), QStringLiteral("Exponential MA (EMA)"), QStringLiteral("Overlay"), QStringLiteral("overlay"), {len(20)}, {}, {},
        [](const Bars &c, const StudyParams &p) { return std::vector<StudyOutput>{line(QStringLiteral("EMA"), QStringLiteral("#4fc3f7"), ema(closes(c), L(p, "length")))}; }});
    s.push_back({QStringLiteral("wma"), QStringLiteral("Weighted MA (WMA)"), QStringLiteral("Overlay"), QStringLiteral("overlay"), {len(20)}, {}, {},
        [](const Bars &c, const StudyParams &p) { return std::vector<StudyOutput>{line(QStringLiteral("WMA"), QStringLiteral("#ba68c8"), wma(closes(c), L(p, "length")))}; }});
    s.push_back({QStringLiteral("bb"), QStringLiteral("Bollinger Bands"), QStringLiteral("Volatility"), QStringLiteral("overlay"),
        {len(20), {QStringLiteral("mult"), QStringLiteral("StdDev"), 2, 0.1}}, {}, {},
        [](const Bars &c, const StudyParams &p) {
            const Series cl = closes(c), mid = sma(cl, L(p, "length")), sd = stdev(cl, L(p, "length"));
            const double mult = P(p, "mult");
            return std::vector<StudyOutput>{
                line(QStringLiteral("Upper"), QStringLiteral("#2196f3"), zipNull(mid, sd, [&](double m, double d) { return m + mult * d; })),
                line(QStringLiteral("Basis"), QStringLiteral("#ff9800"), mid),
                line(QStringLiteral("Lower"), QStringLiteral("#2196f3"), zipNull(mid, sd, [&](double m, double d) { return m - mult * d; }))};
        }});
    s.push_back({QStringLiteral("keltner"), QStringLiteral("Keltner Channels"), QStringLiteral("Volatility"), QStringLiteral("overlay"),
        {len(20), {QStringLiteral("atrLen"), QStringLiteral("ATR length"), 10, 0}, {QStringLiteral("mult"), QStringLiteral("Multiplier"), 2, 0.1}}, {}, {},
        [](const Bars &c, const StudyParams &p) {
            const Series mid = ema(closes(c), L(p, "length")), a = atr(c, L(p, "atrLen"));
            const double mult = P(p, "mult");
            return std::vector<StudyOutput>{
                line(QStringLiteral("Upper"), QStringLiteral("#26a69a"), zipNull(mid, a, [&](double m, double x) { return m + mult * x; })),
                line(QStringLiteral("Basis"), QStringLiteral("#9e9e9e"), mid),
                line(QStringLiteral("Lower"), QStringLiteral("#26a69a"), zipNull(mid, a, [&](double m, double x) { return m - mult * x; }))};
        }});
    s.push_back({QStringLiteral("donchian"), QStringLiteral("Donchian Channels"), QStringLiteral("Volatility"), QStringLiteral("overlay"), {len(20)}, {}, {},
        [](const Bars &c, const StudyParams &p) {
            const Series hi = highest(highs(c), L(p, "length")), lo = lowest(lows(c), L(p, "length"));
            Series middle(hi.size(), NAN);
            for (size_t i = 0; i < hi.size(); ++i)
                if (has(hi[i])) middle[i] = (hi[i] + lo[i]) / 2;
            return std::vector<StudyOutput>{line(QStringLiteral("Upper"), QStringLiteral("#42a5f5"), hi),
                                            line(QStringLiteral("Middle"), QStringLiteral("#ff9800"), middle),
                                            line(QStringLiteral("Lower"), QStringLiteral("#42a5f5"), lo)};
        }});
    s.push_back({QStringLiteral("vwap"), QStringLiteral("VWAP"), QStringLiteral("Overlay"), QStringLiteral("overlay"), {}, {}, {},
        [](const Bars &c, const StudyParams &) { return std::vector<StudyOutput>{line(QStringLiteral("VWAP"), QStringLiteral("#ffeb3b"), vwap(c))}; }});
    s.push_back({QStringLiteral("psar"), QStringLiteral("Parabolic SAR"), QStringLiteral("Trend"), QStringLiteral("overlay"),
        {{QStringLiteral("step"), QStringLiteral("Step"), 0.02, 0.01}, {QStringLiteral("max"), QStringLiteral("Max"), 0.2, 0.01}}, {}, {},
        [](const Bars &c, const StudyParams &p) {
            StudyOutput o = line(QStringLiteral("SAR"), QStringLiteral("#e0e0e0"), parabolicSar(c, P(p, "step"), P(p, "max")));
            o.kind = QStringLiteral("dots");
            return std::vector<StudyOutput>{o};
        }});
    s.push_back({QStringLiteral("supertrend"), QStringLiteral("Supertrend"), QStringLiteral("Trend"), QStringLiteral("overlay"),
        {{QStringLiteral("length"), QStringLiteral("ATR length"), 10, 0}, {QStringLiteral("mult"), QStringLiteral("Factor"), 3, 0.1}}, {}, {},
        [up, down](const Bars &c, const StudyParams &p) {
            const SupertrendResult r = supertrend(c, L(p, "length"), P(p, "mult"));
            StudyOutput o = line(QStringLiteral("Supertrend"), up, r.values);
            o.tones = StudyTones{up, down};
            for (bool u : r.up) o.colors << (u ? up : down);
            return std::vector<StudyOutput>{o};
        }});
    s.push_back({QStringLiteral("ichimoku"), QStringLiteral("Ichimoku Cloud"), QStringLiteral("Trend"), QStringLiteral("overlay"),
        {{QStringLiteral("conv"), QStringLiteral("Conversion"), 9, 0}, {QStringLiteral("base"), QStringLiteral("Base"), 26, 0},
         {QStringLiteral("spanB"), QStringLiteral("Leading span B"), 52, 0}, {QStringLiteral("disp"), QStringLiteral("Displacement"), 26, 0}},
        {},
        StudyCloud{QStringLiteral("Leading Span A"), QStringLiteral("Leading Span B"), QStringLiteral("rgba(38,166,154,0.18)"), QStringLiteral("rgba(239,83,80,0.18)")},
        [](const Bars &c, const StudyParams &p) {
            const IchimokuResult r = ichimoku(c, L(p, "conv"), L(p, "base"), L(p, "spanB"));
            const int disp = L(p, "disp");
            StudyOutput lag = line(QStringLiteral("Lagging Span"), QStringLiteral("#43a047"), closes(c));
            lag.shift = -disp;
            StudyOutput a = line(QStringLiteral("Leading Span A"), QStringLiteral("#66bb6a"), r.spanA);
            a.shift = disp;
            StudyOutput b = line(QStringLiteral("Leading Span B"), QStringLiteral("#ef5350"), r.spanB);
            b.shift = disp;
            return std::vector<StudyOutput>{line(QStringLiteral("Conversion"), QStringLiteral("#2962ff"), r.tenkan),
                                            line(QStringLiteral("Base"), QStringLiteral("#b71c1c"), r.kijun), lag, a, b};
        }});
    for (const bool fib : {false, true}) {
        s.push_back({fib ? QStringLiteral("pivots_fib") : QStringLiteral("pivots"),
            fib ? QStringLiteral("Pivot Points (Fibonacci)") : QStringLiteral("Pivot Points (Classic)"), QStringLiteral("Overlay"), QStringLiteral("overlay"),
            {{QStringLiteral("period"), QStringLiteral("Bars per period"), 5, 0}}, {}, {},
            [fib](const Bars &c, const StudyParams &p) {
                const PivotsResult r = pivots(c, L(p, "period"), fib);
                static const char *colors[7] = {"#ffeb3b", "#ef5350", "#ef5350", "#ef5350", "#26a69a", "#26a69a", "#26a69a"};
                std::vector<StudyOutput> out;
                for (size_t k = 0; k < 7; ++k) {
                    StudyOutput o = line(QLatin1String(PivotsResult::keys[k]), QLatin1String(colors[k]), r.levels[k]);
                    o.steps = true;
                    out.push_back(o);
                }
                return out;
            }});
    }

    s.push_back({QStringLiteral("volume"), QStringLiteral("Volume"), QStringLiteral("Volume"), QStringLiteral("separate"), {}, {}, {},
        [](const Bars &c, const StudyParams &) {
            StudyOutput o;
            o.name = QStringLiteral("Volume");
            o.kind = QStringLiteral("histogram");
            o.color = QStringLiteral("#26a69a");
            const QString u = QStringLiteral("#26a69a80"), d = QStringLiteral("#ef535080");
            o.tones = StudyTones{u, d};
            for (const Bar &k : c) {
                o.values.push_back(k.volume);
                o.colors << (k.close >= k.open ? u : d);
            }
            return std::vector<StudyOutput>{o};
        }});
    s.push_back({QStringLiteral("obv"), QStringLiteral("On Balance Volume"), QStringLiteral("Volume"), QStringLiteral("separate"), {}, {}, {},
        [](const Bars &c, const StudyParams &) { return std::vector<StudyOutput>{line(QStringLiteral("OBV"), QStringLiteral("#26c6da"), obv(c))}; }});
    s.push_back({QStringLiteral("mfi"), QStringLiteral("Money Flow Index"), QStringLiteral("Volume"), QStringLiteral("separate"), {len(14)}, {20, 80}, {},
        [](const Bars &c, const StudyParams &p) { return std::vector<StudyOutput>{line(QStringLiteral("MFI"), QStringLiteral("#7e57c2"), mfi(c, L(p, "length")))}; }});

    s.push_back({QStringLiteral("rsi"), QStringLiteral("Relative Strength Index (RSI)"), QStringLiteral("Momentum"), QStringLiteral("separate"), {len(14)}, {30, 70}, {},
        [](const Bars &c, const StudyParams &p) { return std::vector<StudyOutput>{line(QStringLiteral("RSI"), QStringLiteral("#7e57c2"), rsi(closes(c), L(p, "length")))}; }});
    s.push_back({QStringLiteral("macd"), QStringLiteral("MACD"), QStringLiteral("Momentum"), QStringLiteral("separate"),
        {{QStringLiteral("fast"), QStringLiteral("Fast"), 12, 0}, {QStringLiteral("slow"), QStringLiteral("Slow"), 26, 0}, {QStringLiteral("signal"), QStringLiteral("Signal"), 9, 0}}, {}, {},
        [up, down](const Bars &c, const StudyParams &p) {
            const Series f = ema(closes(c), L(p, "fast")), sl = ema(closes(c), L(p, "slow"));
            const Series macd = zipNull(f, sl, [](double x, double y) { return x - y; });
            const Series sig = ema(macd, L(p, "signal"));
            const Series hist = zipNull(macd, sig, [](double m, double g) { return m - g; });
            StudyOutput h;
            h.name = QStringLiteral("Histogram");
            h.kind = QStringLiteral("histogram");
            h.color = up;
            h.values = hist;
            h.tones = StudyTones{up, down};
            for (double x : hist) h.colors << ((has(x) ? x : 0) >= 0 ? up : down);
            return std::vector<StudyOutput>{h, line(QStringLiteral("MACD"), QStringLiteral("#2196f3"), macd), line(QStringLiteral("Signal"), QStringLiteral("#ff9800"), sig)};
        }});
    s.push_back({QStringLiteral("stoch"), QStringLiteral("Stochastic"), QStringLiteral("Momentum"), QStringLiteral("separate"),
        {{QStringLiteral("k"), QStringLiteral("%K length"), 14, 0}, {QStringLiteral("smoothK"), QStringLiteral("%K smoothing"), 3, 0}, {QStringLiteral("d"), QStringLiteral("%D smoothing"), 3, 0}},
        {20, 80}, {},
        [](const Bars &c, const StudyParams &p) {
            const Series hh = highest(highs(c), L(p, "k")), ll = lowest(lows(c), L(p, "k"));
            Series raw(c.size(), NAN);
            for (size_t i = 0; i < c.size(); ++i)
                if (has(hh[i])) raw[i] = hh[i] == ll[i] ? 50 : (100 * (c[i].close - ll[i])) / (hh[i] - ll[i]);
            const Series k = sma(raw, L(p, "smoothK"));
            return std::vector<StudyOutput>{line(QStringLiteral("%K"), QStringLiteral("#2196f3"), k), line(QStringLiteral("%D"), QStringLiteral("#ff9800"), sma(k, L(p, "d")))};
        }});
    s.push_back({QStringLiteral("cci"), QStringLiteral("Commodity Channel Index"), QStringLiteral("Momentum"), QStringLiteral("separate"), {len(20)}, {-100, 100}, {},
        [](const Bars &c, const StudyParams &p) { return std::vector<StudyOutput>{line(QStringLiteral("CCI"), QStringLiteral("#26a69a"), cci(c, L(p, "length")))}; }});
    s.push_back({QStringLiteral("willr"), QStringLiteral("Williams %R"), QStringLiteral("Momentum"), QStringLiteral("separate"), {len(14)}, {-80, -20}, {},
        [](const Bars &c, const StudyParams &p) {
            const Series hh = highest(highs(c), L(p, "length")), ll = lowest(lows(c), L(p, "length"));
            Series r(c.size(), NAN);
            for (size_t i = 0; i < c.size(); ++i)
                if (has(hh[i])) r[i] = hh[i] == ll[i] ? -50 : (-100 * (hh[i] - c[i].close)) / (hh[i] - ll[i]);
            return std::vector<StudyOutput>{line(QStringLiteral("%R"), QStringLiteral("#ec407a"), r)};
        }});
    s.push_back({QStringLiteral("roc"), QStringLiteral("Rate of Change"), QStringLiteral("Momentum"), QStringLiteral("separate"), {len(12)}, {0}, {},
        [](const Bars &c, const StudyParams &p) {
            const int n = L(p, "length");
            Series r(c.size(), NAN);
            for (size_t i = 0; i < c.size(); ++i)
                if (int(i) >= n && int(i) - n < int(c.size())) r[i] = (100 * (c[i].close - c[i - n].close)) / c[i - n].close;
            return std::vector<StudyOutput>{line(QStringLiteral("ROC"), QStringLiteral("#29b6f6"), r)};
        }});

    s.push_back({QStringLiteral("adx"), QStringLiteral("Average Directional Index (ADX)"), QStringLiteral("Trend"), QStringLiteral("separate"), {len(14)}, {25}, {},
        [](const Bars &c, const StudyParams &p) {
            const AdxResult r = adx(c, L(p, "length"));
            return std::vector<StudyOutput>{line(QStringLiteral("ADX"), QStringLiteral("#f44336"), r.adx), line(QStringLiteral("+DI"), QStringLiteral("#26a69a"), r.plus),
                                            line(QStringLiteral("-DI"), QStringLiteral("#ef5350"), r.minus)};
        }});
    s.push_back({QStringLiteral("atr"), QStringLiteral("Average True Range (ATR)"), QStringLiteral("Volatility"), QStringLiteral("separate"), {len(14)}, {}, {},
        [](const Bars &c, const StudyParams &p) { return std::vector<StudyOutput>{line(QStringLiteral("ATR"), QStringLiteral("#ff7043"), atr(c, L(p, "length")))}; }});
    return s;
}

QVariant val(double v) { return has(v) ? QVariant(v) : null(); }

} // namespace

const std::vector<StudyDef> &studies()
{
    static const std::vector<StudyDef> list = build();
    return list;
}

const StudyDef *studyById(const QString &id)
{
    for (const StudyDef &d : studies())
        if (d.id == id)
            return &d;
    return nullptr;
}

StudyParams fullParams(const StudyDef &def, const QVariantMap &given)
{
    StudyParams p;
    for (const StudyParam &x : def.params) {
        const auto v = num(given.value(x.key));
        p.insert(x.key, v ? *v : x.def);
    }
    return p;
}

std::vector<StudyOutput> compute(const QString &studyId, const QVariantMap &params, const Bars &bars)
{
    const StudyDef *def = studyById(studyId);
    if (!def)
        throw Error(QStringLiteral("Unknown study \"%1\".").arg(studyId));
    return def->compute(bars, fullParams(*def, params));
}

QString studyLabel(const StudyDef &def, const StudyParams &params)
{
    QString label = def.id.toUpper();
    for (const StudyParam &p : def.params)
        label += QLatin1Char(' ') + jsString(params.value(p.key, p.def));
    return label;
}

QStringList colorKeys(const StudyDef &def)
{
    QStringList names;
    for (const StudyOutput &o : studyOutputs(def)) {
        if (o.tones) names << o.name + QStringLiteral(":up") << o.name + QStringLiteral(":down");
        else names << o.name;
    }
    return names;
}

QVariantList outputsAsVariant(const std::vector<StudyOutput> &outputs, bool withValues)
{
    QVariantList list;
    for (const StudyOutput &o : outputs) {
        Rec r{{QStringLiteral("name"), o.name}, {QStringLiteral("kind"), o.kind}, {QStringLiteral("color"), o.color}};
        if (o.tones)
            r.insert(QStringLiteral("tones"), Rec{{QStringLiteral("up"), o.tones->up}, {QStringLiteral("down"), o.tones->down}});
        if (o.shift)
            r.insert(QStringLiteral("shift"), o.shift);
        if (o.steps)
            r.insert(QStringLiteral("steps"), true);
        if (withValues) {
            QVariantList values;
            values.reserve(qsizetype(o.values.size()));
            for (double v : o.values) values << val(v);
            r.insert(QStringLiteral("values"), values);
            if (!o.colors.isEmpty())
                r.insert(QStringLiteral("colors"), o.colors);
        }
        list << r;
    }
    return list;
}

QVariantMap studyAsVariant(const StudyDef &def)
{
    QVariantList params;
    for (const StudyParam &p : def.params) {
        Rec r{{QStringLiteral("key"), p.key}, {QStringLiteral("label"), p.label}, {QStringLiteral("default"), p.def}};
        if (p.step) r.insert(QStringLiteral("step"), p.step);
        params << r;
    }
    Rec r{{QStringLiteral("id"), def.id}, {QStringLiteral("name"), def.name}, {QStringLiteral("category"), def.category},
          {QStringLiteral("pane"), def.pane}, {QStringLiteral("params"), params},
          {QStringLiteral("outputs"), outputsAsVariant(studyOutputs(def), false)}, {QStringLiteral("colorKeys"), colorKeys(def)}};
    if (!def.levels.empty()) {
        QVariantList lv;
        for (double l : def.levels) lv << l;
        r.insert(QStringLiteral("levels"), lv);
    }
    if (def.cloud)
        r.insert(QStringLiteral("cloud"), Rec{{QStringLiteral("upper"), def.cloud->upper}, {QStringLiteral("lower"), def.cloud->lower},
                                              {QStringLiteral("upColor"), def.cloud->upColor}, {QStringLiteral("downColor"), def.cloud->downColor}});
    return r;
}

QVariantList studiesAsVariant()
{
    QVariantList list;
    for (const StudyDef &d : studies()) list << studyAsVariant(d);
    return list;
}

} // namespace tl
