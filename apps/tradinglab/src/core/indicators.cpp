// Technical indicators. Port of renderer/src/chart/indicators.ts; see indicators.h for the NAN = null convention.

#include "indicators.h"

#include <algorithm>
#include <cmath>

namespace tl::ind {

namespace {
Series nulls(size_t n) { return Series(n, NAN); }
}

Series sma(const Series &v, int n)
{
    Series out = nulls(v.size());
    for (size_t i = 0; i < v.size(); ++i) {
        if (int(i) < n - 1)
            continue;
        double sum = 0;
        bool ok = true;
        for (int j = int(i) - n + 1; j <= int(i); ++j) {
            if (!has(v[j])) { ok = false; break; }
            sum += v[j];
        }
        if (ok)
            out[i] = sum / n;
    }
    return out;
}

Series ema(const Series &v, int n)
{
    Series out = nulls(v.size());
    const double k = 2.0 / (n + 1);
    double prev = NAN;
    int count = 0;
    double sum = 0;
    for (size_t i = 0; i < v.size(); ++i) {
        if (!has(v[i]))
            continue;
        if (!has(prev)) {
            sum += v[i];
            count++;
            if (count == n) { prev = sum / n; out[i] = prev; }
        } else {
            prev = v[i] * k + prev * (1 - k);
            out[i] = prev;
        }
    }
    return out;
}

Series rma(const Series &v, int n)
{
    Series out = nulls(v.size());
    double prev = NAN;
    int count = 0;
    double sum = 0;
    for (size_t i = 0; i < v.size(); ++i) {
        if (!has(v[i]))
            continue;
        if (!has(prev)) {
            sum += v[i];
            count++;
            if (count == n) { prev = sum / n; out[i] = prev; }
        } else {
            prev = (prev * (n - 1) + v[i]) / n;
            out[i] = prev;
        }
    }
    return out;
}

Series wma(const Series &v, int n)
{
    const double denom = (double(n) * (n + 1)) / 2;
    Series out = nulls(v.size());
    for (size_t i = 0; i < v.size(); ++i) {
        if (int(i) < n - 1)
            continue;
        double s = 0;
        bool ok = true;
        for (int j = 0; j < n; ++j) {
            const double x = v[i - n + 1 + j];
            if (!has(x)) { ok = false; break; }
            s += x * (j + 1);
        }
        if (ok)
            out[i] = s / denom;
    }
    return out;
}

Series stdev(const Series &v, int n)
{
    const Series m = sma(v, n);
    Series out = nulls(v.size());
    for (size_t i = 0; i < v.size(); ++i) {
        if (!has(m[i]))
            continue;
        double s = 0;
        for (int j = int(i) - n + 1; j <= int(i); ++j)
            { const double d = v[j] - m[i]; s += d * d; }
        out[i] = std::sqrt(s / n);
    }
    return out;
}

// Math.max(...slice) / Math.min(...slice): NaN anywhere gives NaN; an empty window gives -/+Infinity
static Series extreme(const Series &v, int n, bool max)
{
    Series out = nulls(v.size());
    for (size_t i = 0; i < v.size(); ++i) {
        if (int(i) < n - 1)
            continue;
        double r = max ? -INFINITY : INFINITY;
        for (int j = std::max(0, int(i) - n + 1); j <= int(i); ++j) {
            if (!has(v[j])) { r = NAN; break; }
            r = max ? std::max(r, v[j]) : std::min(r, v[j]);
        }
        out[i] = r;
    }
    return out;
}
Series highest(const Series &v, int n) { return extreme(v, n, true); }
Series lowest(const Series &v, int n) { return extreme(v, n, false); }

Series closes(const Bars &c) { Series s; s.reserve(c.size()); for (const Bar &k : c) s.push_back(k.close); return s; }
Series highs(const Bars &c) { Series s; s.reserve(c.size()); for (const Bar &k : c) s.push_back(k.high); return s; }
Series lows(const Bars &c) { Series s; s.reserve(c.size()); for (const Bar &k : c) s.push_back(k.low); return s; }

Series trueRange(const Bars &c)
{
    Series out(c.size());
    for (size_t i = 0; i < c.size(); ++i) {
        const Bar &k = c[i];
        out[i] = i == 0 ? k.high - k.low
                        : std::max({k.high - k.low, std::abs(k.high - c[i - 1].close), std::abs(k.low - c[i - 1].close)});
    }
    return out;
}

Series atr(const Bars &c, int n) { return rma(trueRange(c), n); }

Series rsi(const Series &close, int n)
{
    Series gain, loss; // from the second bar on (upstream's gain.slice(1))
    for (size_t i = 1; i < close.size(); ++i) {
        gain.push_back(std::max(close[i] - close[i - 1], 0.0));
        loss.push_back(std::max(close[i - 1] - close[i], 0.0));
    }
    const Series ag = rma(gain, n), al = rma(loss, n);
    Series out{NAN};
    for (size_t i = 0; i < ag.size(); ++i) {
        const double g = ag[i];
        if (!has(g) || !has(al[i]))
            out.push_back(NAN);
        else
            out.push_back(al[i] == 0 ? 100 : 100 - 100 / (1 + g / al[i]));
    }
    return out;
}

Series vwap(const Bars &c)
{
    double pv = 0, vol = 0;
    Series out;
    out.reserve(c.size());
    for (const Bar &k : c) {
        pv += ((k.high + k.low + k.close) / 3) * k.volume;
        vol += k.volume;
        out.push_back(vol != 0 && has(vol) ? pv / vol : k.close);
    }
    return out;
}

Series obv(const Bars &c)
{
    double acc = 0;
    Series out;
    out.reserve(c.size());
    for (size_t i = 0; i < c.size(); ++i) {
        if (i > 0)
            acc += c[i].close > c[i - 1].close ? c[i].volume : c[i].close < c[i - 1].close ? -c[i].volume : 0;
        out.push_back(acc);
    }
    return out;
}

AdxResult adx(const Bars &c, int n)
{
    Series pdm{0}, mdm{0};
    for (size_t i = 1; i < c.size(); ++i) {
        const double up = c[i].high - c[i - 1].high, dn = c[i - 1].low - c[i].low;
        pdm.push_back(up > dn && up > 0 ? up : 0);
        mdm.push_back(dn > up && dn > 0 ? dn : 0);
    }
    const Series tr = rma(trueRange(c), n), p = rma(pdm, n), m = rma(mdm, n);
    AdxResult r;
    r.plus = nulls(tr.size());
    r.minus = nulls(tr.size());
    for (size_t i = 0; i < tr.size(); ++i) {
        const double t = tr[i];
        const bool truthy = has(t) && t != 0; // `t && ...`
        if (truthy && has(p[i])) r.plus[i] = (100 * p[i]) / t;
        if (truthy && has(m[i])) r.minus[i] = (100 * m[i]) / t;
    }
    Series dx = nulls(tr.size());
    for (size_t i = 0; i < tr.size(); ++i) {
        const double pv = r.plus[i], mv = r.minus[i];
        if (!has(pv) || !has(mv) || pv + mv == 0)
            continue;
        dx[i] = (100 * std::abs(pv - mv)) / (pv + mv);
    }
    r.adx = rma(dx, n);
    return r;
}

Series mfi(const Bars &c, int n)
{
    Series tp;
    tp.reserve(c.size());
    for (const Bar &k : c) tp.push_back((k.high + k.low + k.close) / 3);
    Series out = nulls(c.size());
    for (size_t i = 0; i < c.size(); ++i) {
        if (int(i) < n)
            continue;
        double pos = 0, neg = 0;
        for (int j = int(i) - n + 1; j <= int(i); ++j) {
            if (j < 1)
                continue; // tp[-1] is undefined upstream: neither comparison holds
            const double f = tp[j] * c[j].volume;
            if (tp[j] > tp[j - 1]) pos += f;
            else if (tp[j] < tp[j - 1]) neg += f;
        }
        out[i] = neg == 0 ? 100 : 100 - 100 / (1 + pos / neg);
    }
    return out;
}

Series cci(const Bars &c, int n)
{
    Series tp;
    tp.reserve(c.size());
    for (const Bar &k : c) tp.push_back((k.high + k.low + k.close) / 3);
    const Series m = sma(tp, n);
    Series out = nulls(c.size());
    for (size_t i = 0; i < tp.size(); ++i) {
        if (!has(m[i]))
            continue;
        double dev = 0;
        for (int j = int(i) - n + 1; j <= int(i); ++j)
            dev += std::abs(tp[j] - m[i]);
        dev /= n;
        out[i] = dev == 0 ? 0 : (tp[i] - m[i]) / (0.015 * dev);
    }
    return out;
}

Series parabolicSar(const Bars &c, double step, double max)
{
    Series out = nulls(c.size());
    if (c.size() < 2)
        return out;
    bool up = c[1].close >= c[0].close;
    double sar = up ? c[0].low : c[0].high;
    double ep = up ? c[1].high : c[1].low;
    double af = step;
    out[1] = sar;
    for (size_t i = 2; i < c.size(); ++i) {
        sar = sar + af * (ep - sar);
        if (up) {
            sar = std::min({sar, c[i - 1].low, c[i - 2].low});
            if (c[i].low < sar) { up = false; sar = ep; ep = c[i].low; af = step; }
            else if (c[i].high > ep) { ep = c[i].high; af = std::min(af + step, max); }
        } else {
            sar = std::max({sar, c[i - 1].high, c[i - 2].high});
            if (c[i].high > sar) { up = true; sar = ep; ep = c[i].high; af = step; }
            else if (c[i].low < ep) { ep = c[i].low; af = std::min(af + step, max); }
        }
        out[i] = sar;
    }
    return out;
}

Bars heikinAshi(const Bars &c)
{
    Bars out;
    out.reserve(c.size());
    for (size_t i = 0; i < c.size(); ++i) {
        const Bar &k = c[i];
        const double close = (k.open + k.high + k.low + k.close) / 4;
        const double open = i == 0 ? (k.open + k.close) / 2 : (out[i - 1].open + out[i - 1].close) / 2;
        out.push_back(Bar{k.time, open, std::max({k.high, open, close}), std::min({k.low, open, close}), close, k.volume});
    }
    return out;
}

SupertrendResult supertrend(const Bars &c, int n, double mult)
{
    const Series a = atr(c, n);
    SupertrendResult r{nulls(c.size()), std::vector<bool>(c.size(), true)};
    double fu = 0, fl = 0;
    int dir = 1;
    bool started = false;
    for (size_t i = 0; i < c.size(); ++i) {
        if (!has(a[i]))
            continue;
        const double hl2 = (c[i].high + c[i].low) / 2;
        const double bu = hl2 + mult * a[i], bl = hl2 - mult * a[i];
        if (!started) { fu = bu; fl = bl; dir = 1; started = true; }
        else {
            const double pc = c[i - 1].close;
            fu = bu < fu || pc > fu ? bu : fu;
            fl = bl > fl || pc < fl ? bl : fl;
            if (dir == -1 && c[i].close > fu) dir = 1;
            else if (dir == 1 && c[i].close < fl) dir = -1;
        }
        r.values[i] = dir == 1 ? fl : fu;
        r.up[i] = dir == 1;
    }
    return r;
}

IchimokuResult ichimoku(const Bars &c, int conv, int base, int spanB)
{
    const Series hi = highs(c), lo = lows(c);
    auto mid = [&](int n) {
        const Series h = highest(hi, n), l = lowest(lo, n);
        Series out = nulls(h.size());
        for (size_t i = 0; i < h.size(); ++i)
            if (has(h[i])) out[i] = (h[i] + l[i]) / 2;
        return out;
    };
    IchimokuResult r{mid(conv), mid(base), {}, mid(spanB)};
    r.spanA = nulls(c.size());
    for (size_t i = 0; i < c.size(); ++i)
        if (has(r.tenkan[i]) && has(r.kijun[i])) r.spanA[i] = (r.tenkan[i] + r.kijun[i]) / 2;
    return r;
}

PivotsResult pivots(const Bars &c, int period, bool fib)
{
    PivotsResult r;
    for (Series &s : r.levels) s = nulls(c.size());
    if (period < 1)
        return r;
    for (size_t start = period; start < c.size(); start += period) {
        double H = -INFINITY, L = INFINITY;
        for (size_t j = start - period; j < start; ++j) { H = std::max(H, c[j].high); L = std::min(L, c[j].low); }
        const double C = c[start - 1].close;
        const double P = (H + L + C) / 3, rg = H - L;
        const std::array<double, 7> lv = fib
            ? std::array<double, 7>{P, P + 0.382 * rg, P + 0.618 * rg, P + rg, P - 0.382 * rg, P - 0.618 * rg, P - rg}
            : std::array<double, 7>{P, 2 * P - L, P + rg, H + 2 * (P - L), 2 * P - H, P - rg, L - 2 * (H - P)};
        for (size_t i = start; i < std::min(start + period, c.size()); ++i)
            for (size_t k = 0; k < 7; ++k) r.levels[k][i] = lv[k];
    }
    return r;
}

} // namespace tl::ind
