// Candlestick pattern detectors. Port of the detector half of shared/candlePatterns.ts (DETECT, its helpers and findPattern).
// Every expression keeps upstream's order of operations so a bar series matches exactly the same bars as in the Electron app.

#include "candlepatterns.h"

#include <algorithm>
#include <cmath>
#include <initializer_list>
#include <optional>

namespace tl {

namespace {

enum class Trend { None, Up, Down, Flat };

double range(const Bar &b) { return b.high - b.low; }
double body(const Bar &b) { return std::abs(b.close - b.open); }
double top(const Bar &b) { return std::max(b.open, b.close); }
double bot(const Bar &b) { return std::min(b.open, b.close); }
bool green(const Bar &b) { return b.close > b.open; }
bool red(const Bar &b) { return b.close < b.open; }

// Average range of the (up to 10) bars before index s.
std::optional<double> avgRange(const Bars &c, int s)
{
    if (s < 5)
        return std::nullopt;
    double sum = 0;
    int n = 0;
    for (int k = std::max(0, s - 10); k < s; k++) {
        sum += range(c[k]);
        n++;
    }
    if (n && sum > 0)
        return sum / n;
    return std::nullopt;
}

// Direction of the four bars ending just before index s: a move bigger than one average bar counts as a trend.
Trend trendBefore(const Bars &c, int s)
{
    const auto avg = avgRange(c, s);
    if (!avg)
        return Trend::None;
    const double ch = c[s - 1].close - c[s - 5].close;
    return ch > *avg ? Trend::Up : ch < -*avg ? Trend::Down : Trend::Flat;
}

struct Shape
{
    double r, body, up, lo;
};
Shape shape(const Bar &b)
{
    const double r = range(b);
    return {r, tl::body(b), b.high - top(b), bot(b) - b.low};
}

template<typename F>
bool every(std::initializer_list<Bar> bars, F f)
{
    return std::all_of(bars.begin(), bars.end(), f);
}

using Detector = bool (*)(const Bars &c, int i, double avg);

// ---- single candles ---------------------------------------------------------------------------

bool doji(const Bars &c, int i, double avg)
{
    const Shape s = shape(c[i]);
    return s.r >= avg * 0.5 && s.body <= 0.1 * s.r && s.up >= 0.25 * s.r && s.lo >= 0.25 * s.r
        && !(s.up >= 0.4 * s.r && s.lo >= 0.4 * s.r && s.r >= 1.5 * avg);
}
bool longLeggedDoji(const Bars &c, int i, double avg)
{
    const Shape s = shape(c[i]);
    return s.r >= avg * 1.5 && s.body <= 0.1 * s.r && s.up >= 0.4 * s.r && s.lo >= 0.4 * s.r;
}
bool dragonflyDoji(const Bars &c, int i, double avg)
{
    const Shape s = shape(c[i]);
    return s.r >= avg * 0.5 && s.body <= 0.1 * s.r && s.up <= 0.1 * s.r && s.lo >= 0.6 * s.r;
}
bool gravestoneDoji(const Bars &c, int i, double avg)
{
    const Shape s = shape(c[i]);
    return s.r >= avg * 0.5 && s.body <= 0.1 * s.r && s.lo <= 0.1 * s.r && s.up >= 0.6 * s.r;
}
bool spinningTop(const Bars &c, int i, double avg)
{
    const Shape s = shape(c[i]);
    return s.r >= avg * 0.5 && s.body > 0.1 * s.r && s.body <= 0.3 * s.r && s.up >= s.body && s.lo >= s.body;
}
bool hammer(const Bars &c, int i, double avg)
{
    const Shape s = shape(c[i]);
    return trendBefore(c, i) == Trend::Down && s.r >= avg * 0.5 && s.body > 0.05 * s.r && s.lo >= 2 * s.body && s.up <= 0.15 * s.r;
}
bool hangingMan(const Bars &c, int i, double avg)
{
    const Shape s = shape(c[i]);
    return trendBefore(c, i) == Trend::Up && s.r >= avg * 0.5 && s.body > 0.05 * s.r && s.lo >= 2 * s.body && s.up <= 0.15 * s.r;
}
bool invertedHammer(const Bars &c, int i, double avg)
{
    const Shape s = shape(c[i]);
    return trendBefore(c, i) == Trend::Down && s.r >= avg * 0.5 && s.body > 0.05 * s.r && s.up >= 2 * s.body && s.lo <= 0.15 * s.r;
}
bool shootingStar(const Bars &c, int i, double avg)
{
    const Shape s = shape(c[i]);
    return trendBefore(c, i) == Trend::Up && s.r >= avg * 0.5 && s.body > 0.05 * s.r && s.up >= 2 * s.body && s.lo <= 0.15 * s.r;
}
bool bullishMarubozu(const Bars &c, int i, double avg)
{
    const Shape s = shape(c[i]);
    return green(c[i]) && s.r >= avg * 0.9 && s.body >= 0.9 * s.r;
}
bool bearishMarubozu(const Bars &c, int i, double avg)
{
    const Shape s = shape(c[i]);
    return red(c[i]) && s.r >= avg * 0.9 && s.body >= 0.9 * s.r;
}
bool bullishBeltHold(const Bars &c, int i, double avg)
{
    const Shape s = shape(c[i]);
    return trendBefore(c, i) == Trend::Down && green(c[i]) && s.r >= avg && s.lo <= 0.05 * s.r && s.body >= 0.6 * s.r && s.body < 0.9 * s.r;
}
bool bearishBeltHold(const Bars &c, int i, double avg)
{
    const Shape s = shape(c[i]);
    return trendBefore(c, i) == Trend::Up && red(c[i]) && s.r >= avg && s.up <= 0.05 * s.r && s.body >= 0.6 * s.r && s.body < 0.9 * s.r;
}
bool longWhiteCandle(const Bars &c, int i, double avg)
{
    const Shape s = shape(c[i]);
    return green(c[i]) && s.r >= avg * 1.3 && s.body >= 0.6 * s.r && s.body < 0.9 * s.r;
}
bool longBlackCandle(const Bars &c, int i, double avg)
{
    const Shape s = shape(c[i]);
    return red(c[i]) && s.r >= avg * 1.3 && s.body >= 0.6 * s.r && s.body < 0.9 * s.r;
}
bool highWave(const Bars &c, int i, double avg)
{
    const Shape s = shape(c[i]);
    return s.r >= avg * 1.5 && s.body > 0.1 * s.r && s.body <= 0.3 * s.r && s.up >= 0.3 * s.r && s.lo >= 0.3 * s.r;
}

// ---- two candles ------------------------------------------------------------------------------

bool bullishEngulfing(const Bars &c, int i, double avg)
{
    if (i < 1)
        return false;
    const Bar &p = c[i - 1], &n = c[i];
    return trendBefore(c, i - 1) == Trend::Down && red(p) && green(n) && n.open <= p.close && n.close >= p.open && body(n) > body(p) && body(p) >= avg * 0.2;
}
bool bearishEngulfing(const Bars &c, int i, double avg)
{
    if (i < 1)
        return false;
    const Bar &p = c[i - 1], &n = c[i];
    return trendBefore(c, i - 1) == Trend::Up && green(p) && red(n) && n.open >= p.close && n.close <= p.open && body(n) > body(p) && body(p) >= avg * 0.2;
}
bool bullishHarami(const Bars &c, int i, double avg)
{
    if (i < 1)
        return false;
    const Bar &p = c[i - 1], &n = c[i];
    return trendBefore(c, i - 1) == Trend::Down && red(p) && green(n) && body(p) >= avg * 0.6 && top(n) <= p.open && bot(n) >= p.close && body(n) <= 0.5 * body(p);
}
bool bearishHarami(const Bars &c, int i, double avg)
{
    if (i < 1)
        return false;
    const Bar &p = c[i - 1], &n = c[i];
    return trendBefore(c, i - 1) == Trend::Up && green(p) && red(n) && body(p) >= avg * 0.6 && top(n) <= p.close && bot(n) >= p.open && body(n) <= 0.5 * body(p);
}
bool piercingLine(const Bars &c, int i, double avg)
{
    if (i < 1)
        return false;
    const Bar &p = c[i - 1], &n = c[i];
    return trendBefore(c, i - 1) == Trend::Down && red(p) && green(n) && body(p) >= avg * 0.5 && n.open < p.low && n.close > (p.open + p.close) / 2 && n.close < p.open;
}
bool darkCloudCover(const Bars &c, int i, double avg)
{
    if (i < 1)
        return false;
    const Bar &p = c[i - 1], &n = c[i];
    return trendBefore(c, i - 1) == Trend::Up && green(p) && red(n) && body(p) >= avg * 0.5 && n.open > p.high && n.close < (p.open + p.close) / 2 && n.close > p.open;
}
bool tweezerBottom(const Bars &c, int i, double avg)
{
    if (i < 1)
        return false;
    const Bar &p = c[i - 1], &n = c[i];
    return trendBefore(c, i - 1) == Trend::Down && red(p) && green(n) && std::abs(p.low - n.low) <= 0.1 * avg;
}
bool tweezerTop(const Bars &c, int i, double avg)
{
    if (i < 1)
        return false;
    const Bar &p = c[i - 1], &n = c[i];
    return trendBefore(c, i - 1) == Trend::Up && green(p) && red(n) && std::abs(p.high - n.high) <= 0.1 * avg;
}
bool insideBar(const Bars &c, int i, double avg)
{
    if (i < 1)
        return false;
    const Bar &p = c[i - 1], &n = c[i];
    return range(p) >= avg * 0.8 && n.high < p.high && n.low > p.low;
}
bool risingWindow(const Bars &c, int i, double avg)
{
    return i >= 1 && trendBefore(c, i - 1) == Trend::Up && c[i].low - c[i - 1].high >= 0.25 * avg;
}
bool fallingWindow(const Bars &c, int i, double avg)
{
    return i >= 1 && trendBefore(c, i - 1) == Trend::Down && c[i - 1].low - c[i].high >= 0.25 * avg;
}
bool matchingLow(const Bars &c, int i, double avg)
{
    if (i < 1)
        return false;
    const Bar &p = c[i - 1], &n = c[i];
    return trendBefore(c, i - 1) == Trend::Down && red(p) && red(n) && body(p) >= 0.3 * avg && body(n) >= 0.3 * avg && std::abs(p.close - n.close) <= 0.1 * avg;
}
bool bullishKicker(const Bars &c, int i, double avg)
{
    if (i < 1)
        return false;
    const Bar &a = c[i - 1], &b = c[i];
    return trendBefore(c, i - 1) == Trend::Down && red(a) && green(b) && body(a) >= 0.5 * avg && body(b) >= 0.5 * avg && b.open > a.open && b.low > a.high;
}
bool bearishKicker(const Bars &c, int i, double avg)
{
    if (i < 1)
        return false;
    const Bar &a = c[i - 1], &b = c[i];
    return trendBefore(c, i - 1) == Trend::Up && green(a) && red(b) && body(a) >= 0.5 * avg && body(b) >= 0.5 * avg && b.open < a.open && b.high < a.low;
}
bool bullishHaramiCross(const Bars &c, int i, double avg)
{
    if (i < 1)
        return false;
    const Bar &p = c[i - 1], &n = c[i];
    return trendBefore(c, i - 1) == Trend::Down && red(p) && body(p) >= avg * 0.6 && range(n) > 0 && body(n) <= 0.1 * range(n) && top(n) <= p.open && bot(n) >= p.close;
}
bool bearishHaramiCross(const Bars &c, int i, double avg)
{
    if (i < 1)
        return false;
    const Bar &p = c[i - 1], &n = c[i];
    return trendBefore(c, i - 1) == Trend::Up && green(p) && body(p) >= avg * 0.6 && range(n) > 0 && body(n) <= 0.1 * range(n) && top(n) <= p.close && bot(n) >= p.open;
}
bool onNeck(const Bars &c, int i, double avg)
{
    if (i < 1)
        return false;
    const Bar &p = c[i - 1], &n = c[i];
    return trendBefore(c, i - 1) == Trend::Down && red(p) && body(p) >= avg * 0.6 && green(n) && n.open < p.low && n.close <= p.close && std::abs(n.close - p.low) <= 0.1 * avg;
}
bool inNeck(const Bars &c, int i, double avg)
{
    if (i < 1)
        return false;
    const Bar &p = c[i - 1], &n = c[i];
    return trendBefore(c, i - 1) == Trend::Down && red(p) && body(p) >= avg * 0.6 && green(n) && n.open < p.low && n.close > p.close && n.close <= p.close + 0.15 * body(p);
}
bool thrustingLine(const Bars &c, int i, double avg)
{
    if (i < 1)
        return false;
    const Bar &p = c[i - 1], &n = c[i];
    return trendBefore(c, i - 1) == Trend::Down && red(p) && body(p) >= avg * 0.6 && green(n) && n.open < p.low && n.close > p.close + 0.15 * body(p) && n.close < (p.open + p.close) / 2;
}
bool bullishSeparatingLines(const Bars &c, int i, double avg)
{
    if (i < 1)
        return false;
    const Bar &p = c[i - 1], &n = c[i];
    return trendBefore(c, i - 1) == Trend::Up && red(p) && green(n) && body(p) >= 0.5 * avg && body(n) >= 0.5 * avg && std::abs(n.open - p.open) <= 0.1 * avg;
}
bool bearishSeparatingLines(const Bars &c, int i, double avg)
{
    if (i < 1)
        return false;
    const Bar &p = c[i - 1], &n = c[i];
    return trendBefore(c, i - 1) == Trend::Down && green(p) && red(n) && body(p) >= 0.5 * avg && body(n) >= 0.5 * avg && std::abs(n.open - p.open) <= 0.1 * avg;
}
bool bullishCounterattack(const Bars &c, int i, double avg)
{
    if (i < 1)
        return false;
    const Bar &p = c[i - 1], &n = c[i];
    return trendBefore(c, i - 1) == Trend::Down && red(p) && green(n) && body(p) >= 0.6 * avg && body(n) >= 0.6 * avg && n.open < p.close - 0.3 * avg && std::abs(n.close - p.close) <= 0.1 * avg;
}
bool bearishCounterattack(const Bars &c, int i, double avg)
{
    if (i < 1)
        return false;
    const Bar &p = c[i - 1], &n = c[i];
    return trendBefore(c, i - 1) == Trend::Up && green(p) && red(n) && body(p) >= 0.6 * avg && body(n) >= 0.6 * avg && n.open > p.close + 0.3 * avg && std::abs(n.close - p.close) <= 0.1 * avg;
}
bool homingPigeon(const Bars &c, int i, double avg)
{
    if (i < 1)
        return false;
    const Bar &p = c[i - 1], &n = c[i];
    return trendBefore(c, i - 1) == Trend::Down && red(p) && red(n) && body(p) >= 0.6 * avg && body(n) < body(p) && n.open < p.open && n.close > p.close;
}
bool matchingHigh(const Bars &c, int i, double avg)
{
    if (i < 1)
        return false;
    const Bar &p = c[i - 1], &n = c[i];
    return trendBefore(c, i - 1) == Trend::Up && green(p) && green(n) && body(p) >= 0.3 * avg && body(n) >= 0.3 * avg && std::abs(p.close - n.close) <= 0.1 * avg;
}

// ---- three candles ----------------------------------------------------------------------------

bool morningStar(const Bars &c, int i, double avg)
{
    if (i < 2)
        return false;
    const Bar &a = c[i - 2], &b = c[i - 1], &d = c[i];
    return trendBefore(c, i - 2) == Trend::Down && red(a) && body(a) >= avg * 0.5 && body(b) <= 0.3 * body(a) && top(b) < a.close && green(d) && d.close > (a.open + a.close) / 2;
}
bool eveningStar(const Bars &c, int i, double avg)
{
    if (i < 2)
        return false;
    const Bar &a = c[i - 2], &b = c[i - 1], &d = c[i];
    return trendBefore(c, i - 2) == Trend::Up && green(a) && body(a) >= avg * 0.5 && body(b) <= 0.3 * body(a) && bot(b) > a.close && red(d) && d.close < (a.open + a.close) / 2;
}
bool threeInsideUp(const Bars &c, int i, double avg)
{
    return i >= 2 && bullishHarami(c, i - 1, avg) && green(c[i]) && c[i].close > c[i - 2].open;
}
bool threeInsideDown(const Bars &c, int i, double avg)
{
    return i >= 2 && bearishHarami(c, i - 1, avg) && red(c[i]) && c[i].close < c[i - 2].open;
}
bool threeOutsideUp(const Bars &c, int i, double avg)
{
    return i >= 2 && bullishEngulfing(c, i - 1, avg) && green(c[i]) && c[i].close > c[i - 1].close;
}
bool threeOutsideDown(const Bars &c, int i, double avg)
{
    return i >= 2 && bearishEngulfing(c, i - 1, avg) && red(c[i]) && c[i].close < c[i - 1].close;
}
bool bullishAbandonedBaby(const Bars &c, int i, double avg)
{
    if (i < 2)
        return false;
    const Bar &a = c[i - 2], &b = c[i - 1], &d = c[i];
    return trendBefore(c, i - 2) == Trend::Down && red(a) && body(a) >= 0.5 * avg && range(b) > 0 && body(b) <= 0.1 * range(b) && b.high < a.low && green(d) && d.low > b.high;
}
bool bearishAbandonedBaby(const Bars &c, int i, double avg)
{
    if (i < 2)
        return false;
    const Bar &a = c[i - 2], &b = c[i - 1], &d = c[i];
    return trendBefore(c, i - 2) == Trend::Up && green(a) && body(a) >= 0.5 * avg && range(b) > 0 && body(b) <= 0.1 * range(b) && b.low > a.high && red(d) && d.high < b.low;
}
bool threeWhiteSoldiers(const Bars &c, int i, double avg)
{
    if (i < 2 || trendBefore(c, i - 2) == Trend::Up)
        return false;
    const Bar t[3] = {c[i - 2], c[i - 1], c[i]};
    return every({t[0], t[1], t[2]}, [avg](const Bar &b) { return green(b) && body(b) >= avg * 0.5 && b.high - b.close <= 0.3 * body(b); })
        && t[1].open > t[0].open && t[1].open <= t[0].close && t[2].open > t[1].open && t[2].open <= t[1].close && t[1].close > t[0].close && t[2].close > t[1].close;
}
bool threeBlackCrows(const Bars &c, int i, double avg)
{
    if (i < 2 || trendBefore(c, i - 2) == Trend::Down)
        return false;
    const Bar t[3] = {c[i - 2], c[i - 1], c[i]};
    return every({t[0], t[1], t[2]}, [avg](const Bar &b) { return red(b) && body(b) >= avg * 0.5 && b.close - b.low <= 0.3 * body(b); })
        && t[1].open < t[0].open && t[1].open >= t[0].close && t[2].open < t[1].open && t[2].open >= t[1].close && t[1].close < t[0].close && t[2].close < t[1].close;
}
bool morningDojiStar(const Bars &c, int i, double avg)
{
    if (i < 2)
        return false;
    const Bar &a = c[i - 2], &b = c[i - 1], &d = c[i];
    return trendBefore(c, i - 2) == Trend::Down && red(a) && body(a) >= avg * 0.5 && range(b) > 0 && body(b) <= 0.1 * range(b) && top(b) < a.close && green(d) && d.close > (a.open + a.close) / 2;
}
bool eveningDojiStar(const Bars &c, int i, double avg)
{
    if (i < 2)
        return false;
    const Bar &a = c[i - 2], &b = c[i - 1], &d = c[i];
    return trendBefore(c, i - 2) == Trend::Up && green(a) && body(a) >= avg * 0.5 && range(b) > 0 && body(b) <= 0.1 * range(b) && bot(b) > a.close && red(d) && d.close < (a.open + a.close) / 2;
}
bool triDoji(const Bar &x, double avg)
{
    return range(x) >= 0.4 * avg && body(x) <= 0.1 * range(x);
}
bool bullishTriStar(const Bars &c, int i, double avg)
{
    if (i < 2)
        return false;
    const Bar &a = c[i - 2], &b = c[i - 1], &d = c[i];
    return trendBefore(c, i - 2) == Trend::Down && triDoji(a, avg) && triDoji(b, avg) && triDoji(d, avg) && b.high < a.low && b.high < d.low;
}
bool bearishTriStar(const Bars &c, int i, double avg)
{
    if (i < 2)
        return false;
    const Bar &a = c[i - 2], &b = c[i - 1], &d = c[i];
    return trendBefore(c, i - 2) == Trend::Up && triDoji(a, avg) && triDoji(b, avg) && triDoji(d, avg) && b.low > a.high && b.low > d.high;
}
bool twoCrows(const Bars &c, int i, double avg)
{
    if (i < 2)
        return false;
    const Bar &a = c[i - 2], &b = c[i - 1], &d = c[i];
    return trendBefore(c, i - 2) == Trend::Up && green(a) && body(a) >= 0.6 * avg && red(b) && b.open > a.close && b.close > a.close && red(d) && d.open < b.open && d.open > b.close && d.close < a.close && d.close > a.open;
}
bool advanceBlock(const Bars &c, int i, double avg)
{
    if (i < 2 || trendBefore(c, i - 2) != Trend::Up)
        return false;
    const Bar &a = c[i - 2], &b = c[i - 1], &d = c[i];
    const auto upw = [](const Bar &x) { return x.high - x.close; };
    return every({a, b, d}, [avg](const Bar &x) { return green(x) && body(x) >= 0.3 * avg; })
        && b.open > a.open && b.open <= a.close && d.open > b.open && d.open <= b.close && b.close > a.close && d.close > b.close
        && body(b) < body(a) && body(d) < body(b) && upw(b) > upw(a) && upw(d) > upw(b);
}
bool stalledPattern(const Bars &c, int i, double avg)
{
    if (i < 2)
        return false;
    const Bar &a = c[i - 2], &b = c[i - 1], &d = c[i];
    return trendBefore(c, i - 2) == Trend::Up && green(a) && green(b) && green(d) && body(a) >= 0.6 * avg && body(b) >= 0.6 * avg && b.open > a.open && b.open <= a.close && b.close > a.close
        && body(d) <= 0.4 * body(b) && d.open >= b.close - 0.1 * avg && d.close > b.close;
}
bool upsideTasukiGap(const Bars &c, int i, double avg)
{
    if (i < 2)
        return false;
    const Bar &a = c[i - 2], &b = c[i - 1], &d = c[i];
    return trendBefore(c, i - 2) == Trend::Up && green(a) && green(b) && body(a) >= 0.5 * avg && body(b) >= 0.5 * avg && b.low > a.high && red(d) && d.open < b.close && d.open > b.open && d.close < b.open && d.close > a.high;
}
bool downsideTasukiGap(const Bars &c, int i, double avg)
{
    if (i < 2)
        return false;
    const Bar &a = c[i - 2], &b = c[i - 1], &d = c[i];
    return trendBefore(c, i - 2) == Trend::Down && red(a) && red(b) && body(a) >= 0.5 * avg && body(b) >= 0.5 * avg && b.high < a.low && green(d) && d.open > b.close && d.open < b.open && d.close > b.open && d.close < a.low;
}

// ---- four or five candles ---------------------------------------------------------------------

bool risingThreeMethods(const Bars &c, int i, double avg)
{
    if (i < 4 || trendBefore(c, i - 4) != Trend::Up)
        return false;
    const Bar &a = c[i - 4], &e = c[i];
    return green(a) && body(a) >= 0.6 * avg
        && every({c[i - 3], c[i - 2], c[i - 1]}, [&a](const Bar &m) { return body(m) <= 0.5 * body(a) && m.high <= a.high && m.low >= a.low; })
        && green(e) && body(e) >= 0.6 * avg && e.close > a.close;
}
bool fallingThreeMethods(const Bars &c, int i, double avg)
{
    if (i < 4 || trendBefore(c, i - 4) != Trend::Down)
        return false;
    const Bar &a = c[i - 4], &e = c[i];
    return red(a) && body(a) >= 0.6 * avg
        && every({c[i - 3], c[i - 2], c[i - 1]}, [&a](const Bar &m) { return body(m) <= 0.5 * body(a) && m.high <= a.high && m.low >= a.low; })
        && red(e) && body(e) >= 0.6 * avg && e.close < a.close;
}
bool bullishThreeLineStrike(const Bars &c, int i, double avg)
{
    if (i < 3 || trendBefore(c, i - 3) != Trend::Down)
        return false;
    const Bar t[3] = {c[i - 3], c[i - 2], c[i - 1]};
    return every({t[0], t[1], t[2]}, [avg](const Bar &b) { return red(b) && body(b) >= 0.3 * avg; })
        && t[1].close < t[0].close && t[2].close < t[1].close && green(c[i]) && c[i].open < t[2].close && c[i].close > t[0].open;
}
bool bearishThreeLineStrike(const Bars &c, int i, double avg)
{
    if (i < 3 || trendBefore(c, i - 3) != Trend::Up)
        return false;
    const Bar t[3] = {c[i - 3], c[i - 2], c[i - 1]};
    return every({t[0], t[1], t[2]}, [avg](const Bar &b) { return green(b) && body(b) >= 0.3 * avg; })
        && t[1].close > t[0].close && t[2].close > t[1].close && red(c[i]) && c[i].open > t[2].close && c[i].close < t[0].open;
}
bool bullishBreakaway(const Bars &c, int i, double avg)
{
    if (i < 4 || trendBefore(c, i - 4) != Trend::Down)
        return false;
    const Bar &a = c[i - 4], &b = c[i - 3], &m = c[i - 2], &n = c[i - 1], &e = c[i];
    return red(a) && body(a) >= 0.6 * avg && red(b) && b.open < a.close && m.close < b.close && n.close < m.close && green(e) && body(e) >= 0.6 * avg && e.close > b.open && e.close < a.close;
}
bool bearishBreakaway(const Bars &c, int i, double avg)
{
    if (i < 4 || trendBefore(c, i - 4) != Trend::Up)
        return false;
    const Bar &a = c[i - 4], &b = c[i - 3], &m = c[i - 2], &n = c[i - 1], &e = c[i];
    return green(a) && body(a) >= 0.6 * avg && green(b) && b.open > a.close && m.close > b.close && n.close > m.close && red(e) && body(e) >= 0.6 * avg && e.close < b.open && e.close > a.close;
}

struct Def
{
    const char *id;
    int size;
    Detector detect;
};

// Library order (upstream RAW / PATTERN_IDS); `size` is the pattern's candle count from the teaching entry.
const Def DEFS[] = {
    {"doji", 1, doji},
    {"dragonfly-doji", 1, dragonflyDoji},
    {"gravestone-doji", 1, gravestoneDoji},
    {"spinning-top", 1, spinningTop},
    {"hammer", 1, hammer},
    {"hanging-man", 1, hangingMan},
    {"inverted-hammer", 1, invertedHammer},
    {"shooting-star", 1, shootingStar},
    {"bullish-marubozu", 1, bullishMarubozu},
    {"bearish-marubozu", 1, bearishMarubozu},
    {"bullish-engulfing", 2, bullishEngulfing},
    {"bearish-engulfing", 2, bearishEngulfing},
    {"bullish-harami", 2, bullishHarami},
    {"bearish-harami", 2, bearishHarami},
    {"piercing-line", 2, piercingLine},
    {"dark-cloud-cover", 2, darkCloudCover},
    {"tweezer-bottom", 2, tweezerBottom},
    {"tweezer-top", 2, tweezerTop},
    {"inside-bar", 2, insideBar},
    {"morning-star", 3, morningStar},
    {"evening-star", 3, eveningStar},
    {"three-white-soldiers", 3, threeWhiteSoldiers},
    {"three-black-crows", 3, threeBlackCrows},
    {"long-legged-doji", 1, longLeggedDoji},
    {"bullish-belt-hold", 1, bullishBeltHold},
    {"bearish-belt-hold", 1, bearishBeltHold},
    {"rising-window", 2, risingWindow},
    {"falling-window", 2, fallingWindow},
    {"matching-low", 2, matchingLow},
    {"bullish-kicker", 2, bullishKicker},
    {"bearish-kicker", 2, bearishKicker},
    {"three-inside-up", 3, threeInsideUp},
    {"three-inside-down", 3, threeInsideDown},
    {"three-outside-up", 3, threeOutsideUp},
    {"three-outside-down", 3, threeOutsideDown},
    {"bullish-abandoned-baby", 3, bullishAbandonedBaby},
    {"bearish-abandoned-baby", 3, bearishAbandonedBaby},
    {"rising-three-methods", 5, risingThreeMethods},
    {"falling-three-methods", 5, fallingThreeMethods},
    {"bullish-three-line-strike", 4, bullishThreeLineStrike},
    {"bearish-three-line-strike", 4, bearishThreeLineStrike},
    {"long-white-candle", 1, longWhiteCandle},
    {"long-black-candle", 1, longBlackCandle},
    {"high-wave", 1, highWave},
    {"bullish-harami-cross", 2, bullishHaramiCross},
    {"bearish-harami-cross", 2, bearishHaramiCross},
    {"on-neck", 2, onNeck},
    {"in-neck", 2, inNeck},
    {"thrusting-line", 2, thrustingLine},
    {"bullish-separating-lines", 2, bullishSeparatingLines},
    {"bearish-separating-lines", 2, bearishSeparatingLines},
    {"bullish-counterattack", 2, bullishCounterattack},
    {"bearish-counterattack", 2, bearishCounterattack},
    {"homing-pigeon", 2, homingPigeon},
    {"matching-high", 2, matchingHigh},
    {"morning-doji-star", 3, morningDojiStar},
    {"evening-doji-star", 3, eveningDojiStar},
    {"bullish-tri-star", 3, bullishTriStar},
    {"bearish-tri-star", 3, bearishTriStar},
    {"two-crows", 3, twoCrows},
    {"advance-block", 3, advanceBlock},
    {"stalled-pattern", 3, stalledPattern},
    {"upside-tasuki-gap", 3, upsideTasukiGap},
    {"downside-tasuki-gap", 3, downsideTasukiGap},
    {"bullish-breakaway", 5, bullishBreakaway},
    {"bearish-breakaway", 5, bearishBreakaway},
};

const Def *defOf(const QString &id)
{
    static const QHash<QString, const Def *> byId = [] {
        QHash<QString, const Def *> h;
        for (const Def &d : DEFS)
            h.insert(QString::fromLatin1(d.id), &d);
        return h;
    }();
    return byId.value(id, nullptr);
}

// The check findPattern makes at one index: scale from the bars before the pattern's first candle.
bool detectAt(const Def &d, const Bars &c, int i)
{
    const auto avg = avgRange(c, i - d.size + 1);
    return avg && d.detect(c, i, *avg);
}

} // namespace

const QStringList &patternIds()
{
    static const QStringList ids = [] {
        QStringList l;
        for (const Def &d : DEFS)
            l << QString::fromLatin1(d.id);
        return l;
    }();
    return ids;
}

bool isPatternId(const QString &id)
{
    return defOf(id) != nullptr;
}

int patternSize(const QString &id)
{
    const Def *d = defOf(id);
    return d ? d->size : 0;
}

bool detect(const QString &id, const Bars &bars, int index)
{
    const Def *d = defOf(id);
    if (!d || index < std::max(5, d->size - 1) || index >= int(bars.size()))
        return false;
    return detectAt(*d, bars, index);
}

std::vector<int> findPattern(const QString &id, const Bars &bars)
{
    std::vector<int> out;
    const Def *d = defOf(id);
    if (!d)
        return out;
    for (int i = std::max(5, d->size - 1); i < int(bars.size()); i++) {
        if (detectAt(*d, bars, i))
            out.push_back(i);
    }
    return out;
}

QHash<QString, int> countPatterns(const Bars &bars)
{
    QHash<QString, int> counts;
    for (const Def &d : DEFS) {
        int n = 0;
        for (int i = std::max(5, d.size - 1); i < int(bars.size()); i++) {
            if (detectAt(d, bars, i))
                n++;
        }
        counts.insert(QString::fromLatin1(d.id), n);
    }
    return counts;
}

} // namespace tl
