// The price chart item. Port of renderer/src/components/Chart.tsx, chart/primitives.ts and chart/drawings.ts, with
// what lightweight-charts did for them (series, panes, scales, crosshair, zoom/scroll) drawn here with QPainter.

#include "chartview.h"

#include "core/barsvariant.h"
#include "core/indicators.h"
#include "core/studies.h"
#include "core/studycolors.h"
#include "core/util.h"

#include <QBuffer>
#include <QCursor>
#include <QDateTime>
#include <QFontMetricsF>
#include <QImage>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QLinearGradient>
#include <QPainter>
#include <QPainterPath>
#include <QRegularExpression>

#include <algorithm>
#include <cmath>

using namespace tl;

namespace {

// upstream's dark chart palette
const QColor BG(0x1e, 0x1e, 0x1e);
const QColor TEXT(0xcc, 0xcc, 0xcc);
const QColor SEPARATOR(0x3c, 0x3c, 0x3c);
const QColor GRID(0x2a, 0x2a, 0x2a);
const QColor CROSSHAIR(0x95, 0x98, 0xa1);
const QColor CROSSHAIR_LABEL(0x4c, 0x52, 0x5e);
const QColor LEVEL(0x55, 0x55, 0x55);
const QColor LINE_BLUE(0x21, 0x96, 0xf3);

constexpr double AXIS_W = 66;   // right price scale
constexpr double TIME_H = 24;   // time scale
constexpr double HANDLE_R = 5;
constexpr double TOL = 6;

bool isFin(double v) { return std::isfinite(v); }

// CSS colours as the studies give them: #rgb, #rrggbb, #rrggbbaa (Qt would read 8 digits as #aarrggbb), rgb(a)(...)
QColor cssColor(const QString &s)
{
    const QString c = s.trimmed();
    if (c.startsWith(QLatin1Char('#')) && c.size() == 9) {
        bool ok = false;
        const uint v = c.mid(1).toUInt(&ok, 16);
        if (ok)
            return QColor((v >> 24) & 0xff, (v >> 16) & 0xff, (v >> 8) & 0xff, v & 0xff);
    }
    if (c.startsWith(QLatin1String("rgb"))) {
        static const QRegularExpression re(QStringLiteral("rgba?\\(([^)]*)\\)"));
        const auto m = re.match(c);
        if (m.hasMatch()) {
            const QStringList parts = m.captured(1).split(QLatin1Char(','));
            if (parts.size() >= 3) {
                QColor col(parts[0].trimmed().toInt(), parts[1].trimmed().toInt(), parts[2].trimmed().toInt());
                if (parts.size() >= 4)
                    col.setAlphaF(float(parts[3].trimmed().toDouble()));
                return col;
            }
        }
    }
    if (c == QLatin1String("transparent"))
        return QColor(0, 0, 0, 0);
    QColor col(c);
    return col.isValid() ? col : LINE_BLUE;
}

QString defaultDrawingColor(const QString &type)
{
    if (type == QLatin1String("hline")) return QStringLiteral("#ffb300");
    if (type == QLatin1String("fib")) return QStringLiteral("#ab47bc");
    if (type == QLatin1String("rect")) return QStringLiteral("#42a5f5");
    if (type == QLatin1String("marker")) return QStringLiteral("#ffd54f");
    return QStringLiteral("#2196f3"); // trend, ray
}

struct FibLevel { double level; const char *color; };
const FibLevel FIB_LEVELS[] = {{0, "#9e9e9e"}, {0.236, "#ef5350"}, {0.382, "#ff9800"}, {0.5, "#4caf50"},
                               {0.618, "#26a69a"}, {0.786, "#00bcd4"}, {1, "#9e9e9e"}};

double niceStep(double raw)
{
    if (!(raw > 0) || !isFin(raw))
        return 1;
    const double e = std::pow(10.0, std::floor(std::log10(raw)));
    const double f = raw / e;
    const double n = f < 1.5 ? 1 : f < 2.25 ? 2 : f < 3.5 ? 2.5 : f < 7.5 ? 5 : 10;
    return n * e;
}

QString volumeText(double v)
{
    const double a = std::abs(v);
    const auto f = [](double x) { return QString::number(std::round(x * 100) / 100, 'f', 2).remove(QRegularExpression(QStringLiteral("\\.?0+$"))); };
    if (a >= 1e9) return f(v / 1e9) + QLatin1Char('B');
    if (a >= 1e6) return f(v / 1e6) + QLatin1Char('M');
    if (a >= 1e3) return f(v / 1e3) + QLatin1Char('K');
    return QString::number(std::round(v), 'f', 0);
}

QFont chartFont(int px = 11, bool bold = false)
{
    QFont f(QStringLiteral("Liberation Sans"));
    f.setPixelSize(px);
    f.setBold(bold);
    return f;
}

double distToSegment(double px, double py, double x1, double y1, double x2, double y2, bool ray)
{
    if (ray) {
        const double d = std::hypot(x2 - x1, y2 - y1);
        const double k = d == 0 ? 1 : d;
        x2 = x1 + ((x2 - x1) / k) * 1e5;
        y2 = y1 + ((y2 - y1) / k) * 1e5;
    }
    const double dx = x2 - x1, dy = y2 - y1;
    const double len2 = dx * dx + dy * dy;
    const double t = len2 == 0 ? 0 : std::max(0.0, std::min(1.0, ((px - x1) * dx + (py - y1) * dy) / len2));
    return std::hypot(px - (x1 + t * dx), py - (y1 + t * dy));
}

// text with a 3 px halo in the background colour (primitives.ts text())
void haloText(QPainter *p, const QString &t, double x, double y, const QColor &color, Qt::Alignment align = Qt::AlignLeft)
{
    const QFont f = chartFont();
    const double w = QFontMetricsF(f).horizontalAdvance(t);
    if (align & Qt::AlignRight) x -= w;
    else if (align & Qt::AlignHCenter) x -= w / 2;
    QPainterPath path;
    path.addText(QPointF(x, y), f, t);
    p->save();
    p->setPen(QPen(BG, 3, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    p->setBrush(Qt::NoBrush);
    p->drawPath(path);
    p->setPen(Qt::NoPen);
    p->setBrush(color);
    p->drawPath(path);
    p->restore();
}

QColor readable(const QColor &bg) { return bg.lightnessF() > 0.62 ? QColor(0, 0, 0) : QColor(255, 255, 255); }

// a box with a value on the price axis
void axisLabel(QPainter *p, double x, double y, double w, const QString &text, const QColor &bg)
{
    const QRectF r(x + 1, y - 9, w - 2, 18);
    QColor b = bg;
    b.setAlpha(255);
    p->fillRect(r, b);
    p->setPen(readable(b));
    p->drawText(r.adjusted(5, 0, -2, 0), Qt::AlignVCenter | Qt::AlignLeft, text);
}

} // namespace

ChartView::ChartView(QQuickItem *parent) : QQuickPaintedItem(parent)
{
    setAcceptedMouseButtons(Qt::LeftButton);
    setAcceptHoverEvents(true);
    setAntialiasing(true);
    setFlag(ItemIsFocusScope, false);
    setActiveFocusOnTab(false);
    m_up = cssColor(QStringLiteral("#26a69a"));
    m_down = cssColor(QStringLiteral("#ef5350"));
}

// ------------------------------------------------------------------ properties

void ChartView::setCandles(const QVariantList &c) { m_candlesIn = c; m_dirty = true; polish(); update(); emit candlesChanged(); }
void ChartView::setSettings(const QVariantMap &s) { if (s == m_settings) return; m_settings = s; m_dirty = true; polish(); update(); emit settingsChanged(); }
void ChartView::setDataKey(const QString &k) { if (k == m_dataKey) return; m_dataKey = k; m_dirty = true; polish(); update(); emit dataKeyChanged(); }
void ChartView::setSymbol(const QString &s) { if (s == m_symbol) return; m_symbol = s; update(); emit symbolChanged(); }
void ChartView::setIntraday(bool on) { if (on == m_intraday) return; m_intraday = on; m_dirty = true; polish(); update(); emit intradayChanged(); }
void ChartView::setTimeZone(const QString &tz) { if (tz == m_timeZone) return; m_timeZone = tz; m_dirty = true; polish(); update(); emit timeZoneChanged(); }
void ChartView::setHour12(bool on) { if (on == m_hour12) return; m_hour12 = on; update(); emit hour12Changed(); }

void ChartView::setTool(const QString &t)
{
    if (t == m_tool)
        return;
    m_tool = t;
    if (m_tool == QLatin1String("cursor")) {
        m_draft.reset();
        m_downAt.reset();
    }
    refreshBusy();
    if (m_hover)
        updateCursor(m_mouse.x(), m_mouse.y());
    update();
    emit toolChanged();
}

QVariantList ChartView::drawings() const
{
    QVariantList out;
    for (const Drawing &d : m_drawings)
        out << drawingToVariant(d);
    return out;
}

void ChartView::setDrawings(const QVariantList &list)
{
    std::vector<Drawing> next;
    for (const QVariant &v : list)
        next.push_back(drawingFromVariant(v));
    m_drawings = std::move(next);
    if (!m_selectedId.isEmpty() && std::none_of(m_drawings.begin(), m_drawings.end(), [&](const Drawing &d) { return d.id == m_selectedId; }))
        setSelected(QString());
    update();
    emit drawingsChanged();
}

void ChartView::setOrderLines(const QVariantList &l)
{
    m_orderLinesIn = l;
    m_orderLines.clear();
    for (const QVariant &v : l) {
        const QVariantMap m = v.toMap();
        OrderLine o;
        o.id = m.value(QStringLiteral("id")).toString();
        o.label = m.value(QStringLiteral("label")).toString();
        o.style = m.value(QStringLiteral("style")).toString();
        o.price = m.value(QStringLiteral("price")).toDouble();
        o.color = cssColor(m.value(QStringLiteral("color")).toString());
        o.draggable = m.value(QStringLiteral("draggable")).toBool();
        if (isFin(o.price))
            m_orderLines.push_back(o);
    }
    update();
    emit orderLinesChanged();
}

bool ChartView::busy() const { return m_busy; }

void ChartView::refreshBusy()
{
    const bool b = m_drag.has_value() || m_lineDrag.has_value() || m_draft.has_value();
    if (b != m_busy) {
        m_busy = b;
        emit busyChanged();
    }
}

void ChartView::setSelected(const QString &id)
{
    if (id == m_selectedId)
        return;
    m_selectedId = id;
    emit selectedIdChanged();
}

ChartView::Drawing ChartView::drawingFromVariant(const QVariant &v)
{
    QVariantMap m = v.toMap();
    Drawing d;
    d.id = m.take(QStringLiteral("id")).toString();
    d.type = m.take(QStringLiteral("type")).toString();
    d.color = m.take(QStringLiteral("color")).toString();
    if (d.color.isEmpty())
        d.color = defaultDrawingColor(d.type);
    const QVariant label = m.take(QStringLiteral("label"));
    d.label = isNull(label) ? QString() : label.toString();
    d.by = m.take(QStringLiteral("by")).toString();
    d.direction = m.take(QStringLiteral("direction")).toString();
    const QVariantMap p1 = m.take(QStringLiteral("p1")).toMap(), p2 = m.take(QStringLiteral("p2")).toMap();
    d.p1 = {p1.value(QStringLiteral("time")).toDouble(), p1.value(QStringLiteral("price")).toDouble()};
    d.p2 = p2.isEmpty() ? d.p1 : Point{p2.value(QStringLiteral("time")).toDouble(), p2.value(QStringLiteral("price")).toDouble()};
    d.extra = m;
    return d;
}

QVariantMap ChartView::drawingToVariant(const Drawing &d)
{
    QVariantMap m = d.extra;
    m.insert(QStringLiteral("id"), d.id);
    m.insert(QStringLiteral("type"), d.type);
    m.insert(QStringLiteral("p1"), QVariantMap{{QStringLiteral("time"), d.p1.time}, {QStringLiteral("price"), d.p1.price}});
    m.insert(QStringLiteral("p2"), QVariantMap{{QStringLiteral("time"), d.p2.time}, {QStringLiteral("price"), d.p2.price}});
    m.insert(QStringLiteral("color"), d.color);
    if (!d.label.isEmpty()) m.insert(QStringLiteral("label"), d.label);
    if (!d.by.isEmpty()) m.insert(QStringLiteral("by"), d.by);
    if (!d.direction.isEmpty()) m.insert(QStringLiteral("direction"), d.direction);
    return m;
}

// ------------------------------------------------------------------ building the series

void ChartView::updatePolish() { ensureBuilt(); }

void ChartView::ensureBuilt()
{
    if (m_dirty)
        rebuild();
}

void ChartView::rebuild()
{
    m_dirty = false;
    // where the user had scrolled and zoomed, kept across rebuilds (new bars arriving, a colour changing)
    struct Saved { QString key; double fromTime, width, offset; bool follow; };
    std::optional<Saved> saved;
    if (m_haveView && !m_bars.empty()) {
        const int n = int(m_bars.size());
        const int i = std::clamp(int(std::lround(m_from)), 0, n - 1);
        saved = Saved{m_builtKey, double(m_bars[size_t(i)].time), m_to - m_from, m_to - (n - 1), m_to >= n - 1 - 0.5};
    }

    const QVariantMap &s = m_settings;
    m_type = s.value(QStringLiteral("type"), QStringLiteral("candles")).toString();
    m_scale = s.value(QStringLiteral("scale"), QStringLiteral("normal")).toString();
    m_grid = s.value(QStringLiteral("grid"), true).toBool();
    m_magnet = s.value(QStringLiteral("crosshair")).toString() == QLatin1String("magnet");
    m_up = cssColor(s.value(QStringLiteral("upColor"), QStringLiteral("#26a69a")).toString());
    m_down = cssColor(s.value(QStringLiteral("downColor"), QStringLiteral("#ef5350")).toString());
    const List studies = s.value(QStringLiteral("studies")).toList();

    m_zone = QTimeZone(QTimeZone::UTC);
    if (m_intraday) {
        m_zone = m_timeZone.isEmpty() ? QTimeZone::systemTimeZone() : QTimeZone(m_timeZone.toUtf8());
        if (!m_zone.isValid())
            m_zone = QTimeZone::systemTimeZone();
    }

    m_bars = barsFromVariant(m_candlesIn);
    // Studies are always computed on the raw bars; only the main series is drawn from Heikin Ashi candles.
    m_display = m_type == QLatin1String("heikin") ? ind::heikinAshi(m_bars) : m_bars;
    const int N = int(m_bars.size());
    m_times.clear();
    for (const Bar &b : m_bars)
        m_times.push_back(double(b.time));

    // Studies with a positive shift (Ichimoku) plot into the future, so extend the time axis with whitespace.
    m_future = 0;
    for (const QVariant &v : studies) {
        const Rec inst = v.toMap();
        if (!inst.value(QStringLiteral("visible")).toBool())
            continue;
        const StudyDef *def = studyById(inst.value(QStringLiteral("studyId")).toString());
        if (def && def->id == QLatin1String("ichimoku"))
            m_future = std::max(m_future, int(fullParams(*def, inst.value(QStringLiteral("params")).toMap()).value(QStringLiteral("disp"))));
    }
    m_step = N > 1 ? m_times[size_t(N - 1)] - m_times[size_t(N - 2)] : 86400;
    const int nslots = N + m_future;

    m_panes.clear();
    m_panes.emplace_back();
    for (const QVariant &v : studies) {
        const Rec inst = v.toMap();
        if (!inst.value(QStringLiteral("visible")).toBool() || N == 0)
            continue;
        const StudyDef *def = studyById(inst.value(QStringLiteral("studyId")).toString());
        if (!def)
            continue;
        const StudyParams params = fullParams(*def, inst.value(QStringLiteral("params")).toMap());
        std::vector<StudyOutput> outs;
        try {
            outs = def->compute(m_bars, params);
        } catch (...) {
            continue;
        }
        int paneIdx = 0;
        if (def->pane != QLatin1String("overlay")) {
            m_panes.emplace_back();
            paneIdx = int(m_panes.size()) - 1;
        }
        Pane &pane = m_panes[size_t(paneIdx)];
        const bool volFmt = def->id == QLatin1String("volume") || def->id == QLatin1String("obv");
        if (paneIdx > 0 && volFmt)
            pane.volumeFormat = true;
        const int copy = copyIndex(studies, inst.value(QStringLiteral("uid")).toString());
        Study st;
        st.title = studyLabel(*def, params);
        QHash<QString, int> byName;
        for (int oi = 0; oi < int(outs.size()); ++oi) {
            const StudyOutput &out = outs[size_t(oi)];
            Series se;
            se.kind = out.kind;
            se.name = out.name;
            se.color = cssColor(lineColor(out, inst, copy, oi));
            se.steps = out.steps;
            se.volumeFormat = volFmt;
            const QStringList barColors = recolour(out, inst);
            se.values.assign(size_t(nslots), NAN);
            if (!barColors.isEmpty())
                se.colors.assign(size_t(nslots), se.color);
            for (int j = 0; j < nslots; ++j) {
                const int src = j - out.shift;
                if (src >= 0 && src < N && src < int(out.values.size())) {
                    se.values[size_t(j)] = out.values[size_t(src)];
                    if (!barColors.isEmpty() && src < barColors.size())
                        se.colors[size_t(j)] = cssColor(barColors[src]);
                }
            }
            byName.insert(out.name, int(pane.series.size()));
            st.series.push_back(int(pane.series.size()));
            pane.series.push_back(std::move(se));
        }
        for (double lv : def->levels)
            pane.levels.push_back(lv);
        if (def->cloud && byName.contains(def->cloud->upper) && byName.contains(def->cloud->lower))
            pane.clouds.push_back({byName.value(def->cloud->upper), byName.value(def->cloud->lower), cssColor(def->cloud->upColor), cssColor(def->cloud->downColor)});
        pane.studies.push_back(st);
    }

    // time-scale tick weights: how big a boundary each slot starts (year, month, day, hour)
    m_tickWeight.assign(size_t(nslots), 0);
    QDateTime prev;
    for (int i = 0; i < nslots; ++i) {
        const QDateTime dt = QDateTime::fromSecsSinceEpoch(qint64(slotTime(i)), m_zone);
        int w = 2;
        if (prev.isValid()) {
            const QDate a = prev.date(), b = dt.date();
            if (a.year() != b.year()) w = 4;
            else if (a.month() != b.month()) w = 3;
            else if (a.day() != b.day()) w = 2;
            else if (prev.time().hour() != dt.time().hour()) w = 1;
            else w = 0;
        }
        m_tickWeight[size_t(i)] = w;
        prev = dt;
    }

    // keep the user's zoom and scroll; follow the newest bar only if they were looking at it
    if (saved && saved->key == m_dataKey && N > 0) {
        if (saved->follow) {
            m_to = N - 1 + saved->offset;
            m_from = m_to - saved->width;
        } else {
            int i = -1;
            for (int k = 0; k < N; ++k)
                if (double(m_bars[size_t(k)].time) >= saved->fromTime) { i = k; break; }
            m_from = i < 0 ? std::max(0.0, N - 1 - saved->width) : i;
            m_to = m_from + saved->width;
        }
        clampRange();
    } else {
        m_from = -0.5;
        m_to = std::max(nslots, 1) - 0.5;
    }
    m_haveView = N > 0;
    m_builtKey = m_dataKey;
    // a drawing in progress or a drag belongs to the chart as it was
    if (m_drag) m_drag.reset();
    if (m_lineDrag) m_lineDrag.reset();
    refreshBusy();
}

void ChartView::fitContent()
{
    ensureBuilt();
    m_from = -0.5;
    m_to = std::max(int(m_bars.size()) + m_future, 1) - 0.5;
    update();
}

void ChartView::clampRange()
{
    const int nslots = std::max(int(m_bars.size()) + m_future, 1);
    double w = m_to - m_from;
    w = std::clamp(w, std::min(4.0, double(nslots)), std::max(nslots * 2.0, 10.0));
    if (m_to - m_from != w) {
        const double c = (m_from + m_to) / 2;
        m_from = c - w / 2;
        m_to = c + w / 2;
    }
    // keep a couple of bars on screen
    if (m_to < 1.5) { m_from += 1.5 - m_to; m_to = 1.5; }
    if (m_from > nslots - 2.5) { m_to -= m_from - (nslots - 2.5); m_from = nslots - 2.5; }
}

void ChartView::zoomAt(double x, double factor)
{
    const double anchor = logicalOfX(x);
    m_from = anchor - (anchor - m_from) * factor;
    m_to = anchor + (m_to - anchor) * factor;
    clampRange();
    update();
}

// ------------------------------------------------------------------ coordinates

double ChartView::plotWidth() const { return std::max(10.0, m_width - AXIS_W); }
double ChartView::xOfLogical(double l) const { return (l - m_from) / (m_to - m_from) * plotWidth(); }
double ChartView::logicalOfX(double x) const { return m_from + x / plotWidth() * (m_to - m_from); }
int ChartView::slotAt(double x) const
{
    const int nslots = int(m_bars.size()) + m_future;
    return std::clamp(int(std::lround(logicalOfX(x))), 0, std::max(0, nslots - 1));
}

double ChartView::slotTime(int slot) const
{
    const int N = int(m_times.size());
    if (N == 0) return 0;
    if (slot < N) return m_times[size_t(std::max(0, slot))];
    return m_times[size_t(N - 1)] + (slot - N + 1) * m_step;
}

double ChartView::toUnits(int pane, double v) const
{
    if (pane != 0 || !isFin(v)) return v;
    if (m_scale == QLatin1String("log")) return v > 0 ? std::log10(v) : NAN;
    if (m_scale == QLatin1String("percent")) return m_percentBase > 0 ? (v / m_percentBase - 1) * 100 : v;
    return v;
}

double ChartView::fromUnits(int pane, double u) const
{
    if (pane != 0) return u;
    if (m_scale == QLatin1String("log")) return std::pow(10.0, u);
    if (m_scale == QLatin1String("percent")) return m_percentBase > 0 ? (u / 100 + 1) * m_percentBase : u;
    return u;
}

std::optional<double> ChartView::yOf(int pane, double price) const
{
    if (pane < 0 || pane >= int(m_panes.size())) return std::nullopt;
    const Pane &p = m_panes[size_t(pane)];
    const double u = toUnits(pane, price);
    if (!isFin(u) || p.max == p.min) return std::nullopt;
    return p.rect.top() + (p.max - u) / (p.max - p.min) * p.rect.height();
}

std::optional<double> ChartView::priceAt(int pane, double y) const
{
    if (pane < 0 || pane >= int(m_panes.size())) return std::nullopt;
    const Pane &p = m_panes[size_t(pane)];
    if (p.rect.height() <= 0) return std::nullopt;
    const double u = p.max - (y - p.rect.top()) / p.rect.height() * (p.max - p.min);
    const double v = fromUnits(pane, u);
    return isFin(v) ? std::optional<double>(v) : std::nullopt;
}

int ChartView::paneAt(double y) const
{
    for (int i = 0; i < int(m_panes.size()); ++i)
        if (y >= m_panes[size_t(i)].rect.top() && y <= m_panes[size_t(i)].rect.bottom() + 1)
            return i;
    return -1;
}

// Candle times of the main series; drawings interpolate between them so they survive interval changes.
std::optional<double> ChartView::xOfTime(double time) const
{
    const auto &t = m_times;
    const int n = int(t.size());
    if (n < 2) {
        if (n == 1 && time == t[0]) return xOfLogical(0);
        return std::nullopt;
    }
    int lo = 0, hi = n - 1;
    while (hi - lo > 1) {
        const int m = (lo + hi) >> 1;
        if (t[size_t(m)] <= time) lo = m; else hi = m;
    }
    // beyond either end, extrapolate with the edge bar spacing
    const int a = time > t[size_t(hi)] ? hi - 1 : time < t[size_t(lo)] ? 0 : lo;
    const int b = a + 1;
    return xOfLogical(a + (time - t[size_t(a)]) / (t[size_t(b)] - t[size_t(a)]));
}

std::optional<ChartView::Point> ChartView::pointAt(double x, double y) const
{
    const int N = int(m_times.size());
    if (N < 2) return std::nullopt;
    const auto price = priceAt(0, y);
    if (!price) return std::nullopt;
    // fractional bar index -> time, extrapolating past either end of the data
    const double logical = logicalOfX(x);
    const int i = std::clamp(int(std::floor(logical)), 0, N - 2);
    return Point{m_times[size_t(i)] + (logical - i) * (m_times[size_t(i + 1)] - m_times[size_t(i)]), *price};
}

void ChartView::layout(double w, double h)
{
    m_width = w;
    m_height = h;
    const double plotH = std::max(10.0, h - TIME_H);
    const int n = int(m_panes.size());
    const double total = 4 + (n - 1);
    double y = 0;
    for (int i = 0; i < n; ++i) {
        const double ph = plotH * (i == 0 ? 4 : 1) / total;
        // a 1 px separator between panes
        m_panes[size_t(i)].rect = QRectF(0, y + (i > 0 ? 1 : 0), plotWidth(), ph - (i > 0 ? 1 : 0));
        y += ph;
    }
}

void ChartView::autoscale()
{
    const int N = int(m_bars.size());
    const int nslots = N + m_future;
    const int lo = std::max(0, int(std::floor(m_from))), hi = std::min(nslots - 1, int(std::ceil(m_to)));
    m_percentBase = N > 0 ? m_display[size_t(std::clamp(lo, 0, N - 1))].close : 0;
    for (int pi = 0; pi < int(m_panes.size()); ++pi) {
        Pane &p = m_panes[size_t(pi)];
        double mn = INFINITY, mx = -INFINITY;
        const auto take = [&](double v) {
            const double u = toUnits(pi, v);
            if (isFin(u)) { mn = std::min(mn, u); mx = std::max(mx, u); }
        };
        if (pi == 0) {
            const bool closeOnly = m_type == QLatin1String("line") || m_type == QLatin1String("area");
            for (int i = lo; i <= std::min(hi, N - 1); ++i) {
                const Bar &b = m_display[size_t(i)];
                if (closeOnly) take(b.close);
                else { take(b.low); take(b.high); }
            }
        }
        for (const Series &s : p.series) {
            for (int i = lo; i <= hi; ++i)
                take(s.values[size_t(i)]);
            if (s.kind == QLatin1String("histogram"))
                take(0);
        }
        if (!isFin(mn)) { mn = 0; mx = 1; }
        if (mx == mn) { const double d = std::abs(mx) * 0.01 + 0.5; mn -= d; mx += d; }
        // lightweight-charts' default margins: 20% above, 10% below
        const double total = (mx - mn) / 0.7;
        p.max = mx + total * 0.2;
        p.min = mn - total * 0.1;
        p.valid = true;
    }
}

// ------------------------------------------------------------------ drawings: geometry and hit testing

std::optional<ChartView::Geo> ChartView::geo(const Drawing &d) const
{
    const auto y1 = yOf(0, d.p1.price), y2 = yOf(0, d.p2.price);
    if (!y1 || !y2) return std::nullopt;
    if (d.type == QLatin1String("hline")) return Geo{0, *y1, plotWidth(), *y1};
    if (d.type == QLatin1String("marker")) {
        const auto x = xOfTime(d.p1.time);
        if (!x) return std::nullopt;
        return Geo{*x, *y1, *x, *y1};
    }
    const auto x1 = xOfTime(d.p1.time), x2 = xOfTime(d.p2.time);
    if (!x1 || !x2) return std::nullopt;
    return Geo{*x1, *y1, *x2, *y2};
}

std::vector<ChartView::FibLine> ChartView::fibLines(const Drawing &d) const
{
    std::vector<FibLine> out;
    for (const FibLevel &f : FIB_LEVELS) {
        const double price = d.p2.price + (d.p1.price - d.p2.price) * f.level;
        const auto y = yOf(0, price);
        if (y) out.push_back({f.level, price, *y, QColor(QString::fromLatin1(f.color))});
    }
    return out;
}

std::optional<ChartView::Hit> ChartView::pick(double px, double py) const
{
    // handles of the selected drawing win over everything else
    for (const Drawing &sel : m_drawings) {
        if (sel.id != m_selectedId || sel.type == QLatin1String("hline") || sel.type == QLatin1String("marker"))
            continue;
        if (const auto g = geo(sel)) {
            if (std::hypot(px - g->x1, py - g->y1) <= HANDLE_R + 3) return Hit{sel.id, QStringLiteral("p1")};
            if (std::hypot(px - g->x2, py - g->y2) <= HANDLE_R + 3) return Hit{sel.id, QStringLiteral("p2")};
        }
    }
    for (auto it = m_drawings.rbegin(); it != m_drawings.rend(); ++it) {
        const Drawing &d = *it;
        const auto g = geo(d);
        if (!g) continue;
        bool hit = false;
        if (d.type == QLatin1String("trend") || d.type == QLatin1String("ray"))
            hit = distToSegment(px, py, g->x1, g->y1, g->x2, g->y2, d.type == QLatin1String("ray")) <= TOL;
        else if (d.type == QLatin1String("hline"))
            hit = std::abs(py - g->y1) <= TOL;
        else if (d.type == QLatin1String("marker"))
            hit = std::abs(px - g->x1) <= 40 && (d.direction == QLatin1String("up") ? py >= g->y1 && py <= g->y1 + 34 : py <= g->y1 && py >= g->y1 - 34);
        else if (d.type == QLatin1String("rect"))
            hit = px >= std::min(g->x1, g->x2) && px <= std::max(g->x1, g->x2) && py >= std::min(g->y1, g->y2) && py <= std::max(g->y1, g->y2);
        else if (d.type == QLatin1String("fib")) {
            const double xa = std::min(g->x1, g->x2), xb = std::max(g->x1, g->x2);
            if (px >= xa - TOL && px <= xb + TOL)
                for (const FibLine &l : fibLines(d))
                    if (std::abs(py - l.y) <= TOL) { hit = true; break; }
        }
        if (hit) return Hit{d.id, QStringLiteral("body")};
    }
    return std::nullopt;
}

const ChartView::OrderLine *ChartView::pickOrderLine(double px, double py) const
{
    for (const OrderLine &l : m_orderLines) {
        if (!l.draggable) continue;
        const auto y = yOf(0, l.price);
        if (y && std::abs(py - *y) <= 5 && px >= 0 && px <= plotWidth()) return &l;
    }
    return nullptr;
}

void ChartView::commit(const std::vector<Drawing> &list)
{
    m_drawings = list;
    const QVariantList v = drawings();
    update();
    emit drawingsChanged();
    emit drawingsEdited(v);
}

void ChartView::finishDraft(const Point &pt)
{
    Drawing d = *m_draft;
    m_draft.reset();
    m_downAt.reset();
    refreshBusy();
    if (d.p1.time == pt.time && d.p1.price == pt.price) { update(); return; }
    d.p2 = pt;
    std::vector<Drawing> next = m_drawings;
    next.push_back(d);
    commit(next);
    setTool(QStringLiteral("cursor"));
}

double ChartView::priceToY(double price)
{
    ensureBuilt();
    layout(width(), height());
    if (m_bars.empty()) return -1;
    autoscale();
    const auto y = yOf(0, price);
    return y ? *y : -1;
}

void ChartView::deleteSelected()
{
    if (m_selectedId.isEmpty()) return;
    std::vector<Drawing> next;
    for (const Drawing &d : m_drawings)
        if (d.id != m_selectedId) next.push_back(d);
    setSelected(QString());
    commit(next);
}

// ------------------------------------------------------------------ input

void ChartView::geometryChange(const QRectF &n, const QRectF &o)
{
    QQuickPaintedItem::geometryChange(n, o);
    update();
}

void ChartView::updateCursor(double x, double y)
{
    const bool inPane = paneAt(y) == 0 && x >= 0 && x <= plotWidth();
    Qt::CursorShape c = Qt::ArrowCursor;
    if (m_tool != QLatin1String("cursor")) c = Qt::CrossCursor;
    else if (m_drag || m_lineDrag) c = m_lineDrag ? Qt::SizeVerCursor : Qt::PointingHandCursor;
    else if (inPane && pickOrderLine(x, y)) c = Qt::SizeVerCursor;
    else if (inPane && pick(x, y)) c = Qt::PointingHandCursor;
    else if (m_pan) c = Qt::ClosedHandCursor;
    if (cursor().shape() != c) setCursor(c);
}

void ChartView::mousePressEvent(QMouseEvent *e)
{
    if (e->button() != Qt::LeftButton) { e->ignore(); return; }
    forceActiveFocus(Qt::MouseFocusReason);
    ensureBuilt();
    layout(width(), height());
    autoscale();
    e->accept();
    const double x = e->position().x(), y = e->position().y();
    const double plotH = m_height - TIME_H;
    if (m_bars.empty() || x < 0 || x > plotWidth() || y < 0 || y > plotH) return;
    const int pane = paneAt(y);
    const bool cursorTool = m_tool == QLatin1String("cursor");
    if (pane == 0) {
        if (cursorTool) {
            if (const OrderLine *ol = pickOrderLine(x, y)) {
                m_lineDrag = LineDrag{ol->id, ol->price};
                refreshBusy();
                updateCursor(x, y);
                return;
            }
            const auto hit = pick(x, y);
            setSelected(hit ? hit->id : QString());
            update();
            if (hit) {
                for (const Drawing &d : m_drawings)
                    if (d.id == hit->id) { m_drag = Drag{hit->id, hit->part, QPointF(x, y), d, false}; break; }
                refreshBusy();
                return;
            }
        } else {
            const auto pt = pointAt(x, y);
            if (!pt) return;
            if (m_tool == QLatin1String("hline")) {
                std::vector<Drawing> next = m_drawings;
                Drawing d;
                d.id = uuid();
                d.type = QStringLiteral("hline");
                d.p1 = d.p2 = *pt;
                d.color = defaultDrawingColor(d.type);
                next.push_back(d);
                commit(next);
                setTool(QStringLiteral("cursor"));
            } else if (!m_draft) {
                Drawing d;
                d.id = uuid();
                d.type = m_tool;
                d.p1 = d.p2 = *pt;
                d.color = defaultDrawingColor(d.type);
                m_draft = d;
                m_downAt = QPointF(x, y);
                refreshBusy();
                update();
            } else {
                finishDraft(*pt);
            }
            return;
        }
    }
    // scrolling is off while a drawing tool is active
    if (!cursorTool) return;
    m_pan = Pan{x, m_from, m_to};
    updateCursor(x, y);
}

void ChartView::mouseMoveEvent(QMouseEvent *e)
{
    const double x = e->position().x(), y = e->position().y();
    m_mouse = e->position();
    m_hover = true;
    if (m_lineDrag) {
        if (const auto p = priceAt(0, y)) {
            m_lineDrag->price = std::round(*p * 100) / 100;
            update();
        }
        return;
    }
    if (m_drag) {
        if (const auto g = geo(m_drag->orig)) {
            const double dx = x - m_drag->start.x(), dy = y - m_drag->start.y();
            Drawing next = m_drag->orig;
            if (m_drag->part == QLatin1String("body")) {
                const auto a = pointAt(g->x1 + dx, g->y1 + dy), b = pointAt(g->x2 + dx, g->y2 + dy);
                if (a && b) { next.p1 = *a; next.p2 = *b; }
            } else if (const auto pt = pointAt(x, y)) {
                if (m_drag->part == QLatin1String("p1")) next.p1 = *pt; else next.p2 = *pt;
            }
            m_drag->moved = true;
            for (Drawing &d : m_drawings)
                if (d.id == m_drag->id) d = next;
            update();
        }
        return;
    }
    if (m_pan) {
        const double shift = (x - m_pan->x) / plotWidth() * (m_pan->to - m_pan->from);
        m_from = m_pan->from - shift;
        m_to = m_pan->to - shift;
        clampRange();
        update();
        return;
    }
    if (m_draft && paneAt(y) == 0)
        if (const auto pt = pointAt(x, y)) m_draft->p2 = *pt;
    update();
}

void ChartView::mouseReleaseEvent(QMouseEvent *e)
{
    const double x = e->position().x(), y = e->position().y();
    if (m_lineDrag) {
        const LineDrag l = *m_lineDrag;
        m_lineDrag.reset();
        refreshBusy();
        update();
        emit orderLineMoved(l.id, l.price);
        return;
    }
    if (m_drag) {
        const bool moved = m_drag->moved;
        m_drag.reset();
        refreshBusy();
        if (moved) commit(m_drawings);
        return;
    }
    if (m_pan) {
        m_pan.reset();
        updateCursor(x, y);
        return;
    }
    // click-drag drawing: releasing far from the start finishes the shape
    if (m_draft && m_downAt) {
        if (std::hypot(x - m_downAt->x(), y - m_downAt->y()) > 8) {
            if (const auto pt = pointAt(x, y)) { finishDraft(*pt); return; }
        }
        m_downAt.reset();
    }
}

void ChartView::hoverMoveEvent(QHoverEvent *e)
{
    m_mouse = e->position();
    m_hover = true;
    if (!m_dirty && !m_bars.empty()) {
        if (m_draft && paneAt(m_mouse.y()) == 0)
            if (const auto pt = pointAt(m_mouse.x(), m_mouse.y())) m_draft->p2 = *pt;
        updateCursor(m_mouse.x(), m_mouse.y());
    }
    update();
}

void ChartView::hoverLeaveEvent(QHoverEvent *)
{
    m_hover = false;
    update();
}

void ChartView::wheelEvent(QWheelEvent *e)
{
    // off while a drawing tool is active or something is being dragged
    if (m_tool != QLatin1String("cursor") || m_drag || m_lineDrag || m_bars.empty()) { e->ignore(); return; }
    e->accept();
    const QPoint d = e->angleDelta();
    const bool sideways = std::abs(d.x()) > std::abs(d.y()) || (e->modifiers() & Qt::ShiftModifier);
    if (sideways) {
        const double delta = std::abs(d.x()) > std::abs(d.y()) ? d.x() : d.y();
        const double shift = -delta / 120.0 * (m_to - m_from) * 0.1;
        m_from += shift;
        m_to += shift;
        clampRange();
        update();
    } else if (d.y() != 0) {
        zoomAt(std::min(e->position().x(), plotWidth()), std::pow(0.9, d.y() / 120.0));
    }
}

void ChartView::keyPressEvent(QKeyEvent *e)
{
    if (e->key() == Qt::Key_Escape) {
        m_draft.reset();
        m_downAt.reset();
        setSelected(QString());
        refreshBusy();
        setTool(QStringLiteral("cursor"));
        update();
        e->accept();
    } else if ((e->key() == Qt::Key_Delete || e->key() == Qt::Key_Backspace) && !m_selectedId.isEmpty()) {
        deleteSelected();
        e->accept();
    } else {
        e->ignore();
    }
}

// ------------------------------------------------------------------ painting

void ChartView::paint(QPainter *p)
{
    render(p, width(), height(), true);
}

QString ChartView::capturePng(int maxWidth)
{
    ensureBuilt();
    const int w = int(width()), h = int(height());
    if (w <= 0 || h <= 0 || m_bars.empty())
        return QString();
    QImage img(w, h, QImage::Format_ARGB32_Premultiplied);
    img.fill(BG);
    {
        QPainter p(&img);
        p.setRenderHint(QPainter::Antialiasing);
        render(&p, w, h, false);
    }
    if (maxWidth > 0 && w > maxWidth)
        img = img.scaledToWidth(maxWidth, Qt::SmoothTransformation);
    QByteArray bytes;
    QBuffer buf(&bytes);
    buf.open(QIODevice::WriteOnly);
    img.save(&buf, "PNG");
    return QString::fromLatin1(bytes.toBase64());
}

std::vector<int> ChartView::timeTicks() const
{
    const int nslots = int(m_bars.size()) + m_future;
    const int lo = std::max(0, int(std::ceil(m_from))), hi = std::min(nslots - 1, int(std::floor(m_to)));
    std::vector<int> picked;
    std::vector<double> xs;
    const double minDist = m_intraday ? 90 : 70;
    for (int w = 4; w >= 0; --w) {
        for (int i = lo; i <= hi; ++i) {
            if (m_tickWeight[size_t(i)] != w) continue;
            const double x = xOfLogical(i);
            if (x < 15 || x > plotWidth() - 15) continue;
            if (std::any_of(xs.begin(), xs.end(), [&](double a) { return std::abs(a - x) < minDist; })) continue;
            picked.push_back(i);
            xs.push_back(x);
        }
    }
    std::sort(picked.begin(), picked.end());
    return picked;
}

QString ChartView::tickLabel(int slot) const
{
    const QDateTime dt = QDateTime::fromSecsSinceEpoch(qint64(slotTime(slot)), m_zone);
    switch (m_tickWeight[size_t(slot)]) {
    case 4: return dt.toString(QStringLiteral("yyyy"));
    case 3: return dt.toString(QStringLiteral("MMM"));
    case 2: return dt.toString(QStringLiteral("d MMM"));
    default: return dt.toString(m_hour12 ? QStringLiteral("h:mm AP") : QStringLiteral("HH:mm"));
    }
}

QString ChartView::crosshairTime(int slot) const
{
    const QDateTime dt = QDateTime::fromSecsSinceEpoch(qint64(slotTime(slot)), m_zone);
    const QString date = dt.toString(QStringLiteral("ddd d MMM ''yy"));
    if (!m_intraday) return date;
    return date + QStringLiteral("  ") + dt.toString(m_hour12 ? QStringLiteral("h:mm AP") : QStringLiteral("HH:mm")) + QLatin1Char(' ') + m_zone.abbreviation(dt);
}

QString ChartView::formatPrice(int pane, double u) const
{
    const Pane &p = m_panes[size_t(pane)];
    if (pane == 0 && m_scale == QLatin1String("percent")) return QString::number(u, 'f', 2) + QLatin1Char('%');
    const double v = fromUnits(pane, u);
    if (p.volumeFormat) return volumeText(v);
    return QString::number(v, 'f', 2);
}

QString ChartView::formatValue(const Series &s, double v) const
{
    return s.volumeFormat ? volumeText(v) : QString::number(v, 'f', 2);
}

void ChartView::render(QPainter *p, double w, double h, bool withCrosshair)
{
    ensureBuilt();
    p->fillRect(QRectF(0, 0, w, h), BG);
    p->setFont(chartFont());
    layout(w, h);
    if (m_bars.empty()) {
        p->setPen(TEXT);
        p->drawText(QRectF(0, 0, w, h), Qt::AlignCenter, QStringLiteral("No data"));
        return;
    }
    autoscale();
    const double plotH = h - TIME_H;
    const std::vector<int> ticks = timeTicks();

    if (m_grid) {
        p->setPen(QPen(GRID, 1));
        for (int i : ticks) {
            const double x = std::round(xOfLogical(i)) + 0.5;
            for (const Pane &pane : m_panes)
                p->drawLine(QPointF(x, pane.rect.top()), QPointF(x, pane.rect.bottom()));
        }
    }
    for (int i = 0; i < int(m_panes.size()); ++i)
        paintPane(p, i);

    // separators, the scales' borders
    p->setPen(QPen(SEPARATOR, 1));
    for (int i = 1; i < int(m_panes.size()); ++i) {
        const double y = std::round(m_panes[size_t(i)].rect.top()) - 0.5;
        p->drawLine(QPointF(0, y), QPointF(w, y));
    }
    p->drawLine(QPointF(plotWidth() + 0.5, 0), QPointF(plotWidth() + 0.5, plotH));
    p->drawLine(QPointF(0, plotH + 0.5), QPointF(w, plotH + 0.5));

    for (int i = 0; i < int(m_panes.size()); ++i)
        paintPriceAxis(p, i);
    paintTimeAxis(p);
    paintLegend(p);
    if (withCrosshair)
        paintCrosshair(p);
}

void ChartView::paintPane(QPainter *p, int idx)
{
    Pane &pane = m_panes[size_t(idx)];
    p->save();
    p->setClipRect(pane.rect);
    // horizontal grid
    if (m_grid) {
        p->setPen(QPen(GRID, 1));
        const double step = niceStep((pane.max - pane.min) / std::max(1.0, pane.rect.height() / 40));
        for (double u = std::ceil(pane.min / step) * step; u <= pane.max; u += step) {
            const double y = std::round(pane.rect.top() + (pane.max - u) / (pane.max - pane.min) * pane.rect.height()) + 0.5;
            p->drawLine(QPointF(0, y), QPointF(pane.rect.right(), y));
        }
    }
    paintClouds(p, pane, idx);
    // study reference levels: dashed grey lines
    for (double lv : pane.levels) {
        if (const auto y = yOf(idx, lv)) {
            QPen pen(LEVEL, 1, Qt::CustomDashLine);
            pen.setDashPattern({4, 4});
            p->setPen(pen);
            p->drawLine(QPointF(0, *y), QPointF(pane.rect.right(), *y));
        }
    }
    if (idx == 0)
        paintMain(p);
    for (const Series &s : pane.series)
        paintSeries(p, pane, idx, s);
    if (idx == 0) {
        for (const Drawing &d : m_drawings)
            paintDrawing(p, d, d.id == m_selectedId);
        if (m_draft)
            paintDrawing(p, *m_draft, false);
        // order lines sit above the drawings
        for (const OrderLine &l : m_orderLines) {
            OrderLine shown = l;
            if (m_lineDrag && m_lineDrag->id == l.id) shown.price = m_lineDrag->price;
            paintOrderLine(p, shown);
        }
    }
    p->restore();
}

void ChartView::paintMain(QPainter *p)
{
    const int N = int(m_display.size());
    const int lo = std::max(0, int(std::floor(m_from)) - 1), hi = std::min(N - 1, int(std::ceil(m_to)) + 1);
    const double spacing = plotWidth() / (m_to - m_from);
    const double bodyW = std::max(1.0, std::floor(spacing * 0.8));
    const bool lineType = m_type == QLatin1String("line") || m_type == QLatin1String("area");

    if (lineType) {
        QPainterPath path;
        bool started = false;
        double firstX = 0, lastX = 0;
        for (int i = lo; i <= hi; ++i) {
            const auto y = yOf(0, m_display[size_t(i)].close);
            if (!y) continue;
            const double x = xOfLogical(i);
            if (!started) { path.moveTo(x, *y); firstX = x; started = true; }
            else path.lineTo(x, *y);
            lastX = x;
        }
        if (!started) return;
        if (m_type == QLatin1String("area")) {
            const QRectF r = m_panes[0].rect;
            QPainterPath fill = path;
            fill.lineTo(lastX, r.bottom());
            fill.lineTo(firstX, r.bottom());
            fill.closeSubpath();
            QLinearGradient g(0, r.top(), 0, r.bottom());
            g.setColorAt(0, cssColor(QStringLiteral("#2196f366")));
            g.setColorAt(1, cssColor(QStringLiteral("#2196f305")));
            p->fillPath(fill, g);
        }
        p->setPen(QPen(LINE_BLUE, 2, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        p->setBrush(Qt::NoBrush);
        p->drawPath(path);
    } else {
        const bool hollow = m_type == QLatin1String("hollow");
        const bool bars = m_type == QLatin1String("bars");
        for (int i = lo; i <= hi; ++i) {
            const Bar &b = m_display[size_t(i)];
            const auto yo = yOf(0, b.open), yh = yOf(0, b.high), yl = yOf(0, b.low), yc = yOf(0, b.close);
            if (!yo || !yh || !yl || !yc) continue;
            const double x = std::round(xOfLogical(i));
            const bool up = b.close >= b.open;
            const QColor c = up ? m_up : m_down;
            if (bars) {
                const double half = std::max(1.0, std::floor(bodyW / 2));
                p->setPen(QPen(c, spacing > 6 ? 1.5 : 1));
                p->drawLine(QPointF(x, *yh), QPointF(x, *yl));
                p->drawLine(QPointF(x - half, *yo), QPointF(x, *yo));
                p->drawLine(QPointF(x, *yc), QPointF(x + half, *yc));
                continue;
            }
            p->setPen(QPen(c, 1));
            const double top = std::min(*yo, *yc), bot = std::max(*yo, *yc);
            if (hollow && up) {
                // up candles: a border only, wicks outside the body
                p->drawLine(QPointF(x + 0.5, *yh), QPointF(x + 0.5, top));
                p->drawLine(QPointF(x + 0.5, bot), QPointF(x + 0.5, *yl));
                p->setBrush(Qt::NoBrush);
                p->drawRect(QRectF(x - std::floor(bodyW / 2) + 0.5, top + 0.5, std::max(1.0, bodyW - 1), std::max(1.0, bot - top - 1)));
            } else {
                p->drawLine(QPointF(x + 0.5, *yh), QPointF(x + 0.5, *yl));
                p->fillRect(QRectF(x - std::floor(bodyW / 2), top, bodyW, std::max(1.0, bot - top)), c);
            }
        }
    }
    // the last price: a dashed line in the last bar's colour
    const Bar &last = m_display.back();
    if (const auto y = yOf(0, last.close)) {
        QPen pen(lineType ? LINE_BLUE : (last.close >= last.open ? m_up : m_down), 1, Qt::CustomDashLine);
        pen.setDashPattern({2, 4});
        p->setPen(pen);
        p->drawLine(QPointF(0, std::round(*y) + 0.5), QPointF(plotWidth(), std::round(*y) + 0.5));
    }
}

void ChartView::paintSeries(QPainter *p, const Pane &pane, int paneIdx, const Series &s)
{
    const int nslots = int(s.values.size());
    const int lo = std::max(0, int(std::floor(m_from)) - 1), hi = std::min(nslots - 1, int(std::ceil(m_to)) + 1);
    const double spacing = plotWidth() / (m_to - m_from);
    if (s.kind == QLatin1String("histogram")) {
        const auto zero = yOf(paneIdx, 0);
        const double base = zero ? std::clamp(*zero, pane.rect.top(), pane.rect.bottom()) : pane.rect.bottom();
        const double bw = std::max(1.0, std::floor(spacing * 0.8));
        for (int i = lo; i <= hi; ++i) {
            const auto y = yOf(paneIdx, s.values[size_t(i)]);
            if (!y) continue;
            const double x = std::round(xOfLogical(i));
            const QColor c = s.colors.empty() ? s.color : s.colors[size_t(i)];
            p->fillRect(QRectF(x - std::floor(bw / 2), std::min(*y, base), bw, std::max(1.0, std::abs(base - *y))), c);
        }
        return;
    }
    if (s.kind == QLatin1String("dots")) {
        p->setPen(Qt::NoPen);
        for (int i = lo; i <= hi; ++i) {
            const auto y = yOf(paneIdx, s.values[size_t(i)]);
            if (!y) continue;
            p->setBrush(s.colors.empty() ? s.color : s.colors[size_t(i)]);
            p->drawEllipse(QPointF(xOfLogical(i), *y), 1.5, 1.5);
        }
        return;
    }
    // a line; gaps break it; stepped lines hold each value flat until the next
    QPainterPath path;
    bool open = false;
    double prevY = 0;
    for (int i = lo; i <= hi; ++i) {
        const auto y = yOf(paneIdx, s.values[size_t(i)]);
        if (!y) { open = false; continue; }
        const double x = xOfLogical(i);
        if (!open) { path.moveTo(x, *y); open = true; }
        else if (s.steps) { path.lineTo(x, prevY); path.lineTo(x, *y); }
        else path.lineTo(x, *y);
        prevY = *y;
    }
    p->setPen(QPen(s.color, 1, Qt::SolidLine, Qt::FlatCap, Qt::RoundJoin));
    p->setBrush(Qt::NoBrush);
    p->drawPath(path);
}

// Fills the area between two line series (Ichimoku cloud), underneath the other series.
void ChartView::paintClouds(QPainter *p, const Pane &pane, int paneIdx)
{
    for (const Cloud &c : pane.clouds) {
        const Series &a = pane.series[size_t(c.upper)], &b = pane.series[size_t(c.lower)];
        const int nslots = int(a.values.size());
        const int lo = std::max(0, int(std::floor(m_from)) - 1), hi = std::min(nslots - 2, int(std::ceil(m_to)) + 1);
        p->setPen(Qt::NoPen);
        for (int i = lo; i <= hi; ++i) {
            const double a0 = a.values[size_t(i)], a1 = a.values[size_t(i + 1)], b0 = b.values[size_t(i)], b1 = b.values[size_t(i + 1)];
            if (!isFin(a0) || !isFin(a1) || !isFin(b0) || !isFin(b1)) continue;
            const auto ya0 = yOf(paneIdx, a0), ya1 = yOf(paneIdx, a1), yb0 = yOf(paneIdx, b0), yb1 = yOf(paneIdx, b1);
            if (!ya0 || !ya1 || !yb0 || !yb1) continue;
            const double x0 = xOfLogical(i), x1 = xOfLogical(i + 1);
            const QPointF poly[4] = {{x0, *ya0}, {x1, *ya1}, {x1, *yb1}, {x0, *yb0}};
            p->setBrush(a0 + a1 >= b0 + b1 ? c.up : c.down);
            p->drawPolygon(poly, 4);
        }
    }
}

void ChartView::paintOrderLine(QPainter *p, const OrderLine &l)
{
    const auto yy = yOf(0, l.price);
    if (!yy) return;
    const double y = *yy, width = plotWidth();
    p->save();
    QPen pen(l.color, l.style == QLatin1String("position") ? 1.5 : 1.25);
    if (l.style == QLatin1String("draft")) { pen.setStyle(Qt::CustomDashLine); pen.setDashPattern({6 / 1.25, 4 / 1.25}); }
    else if (l.style == QLatin1String("position")) { pen.setStyle(Qt::CustomDashLine); pen.setDashPattern({2 / 1.5, 3 / 1.5}); }
    p->setPen(pen);
    p->drawLine(QPointF(0, y), QPointF(width, y));
    const QFont f = chartFont();
    p->setFont(f);
    const double w = QFontMetricsF(f).horizontalAdvance(l.label) + 12;
    const double x = width - w - 6;
    p->setPen(Qt::NoPen);
    p->setBrush(l.color);
    p->drawRoundedRect(QRectF(x, y - 9, w, 18), 3, 3);
    p->setPen(Qt::white);
    p->drawText(QRectF(x + 6, y - 9, w - 6, 18), Qt::AlignVCenter | Qt::AlignLeft, l.label);
    p->restore();
}

void ChartView::paintDrawing(QPainter *p, const Drawing &d, bool selected)
{
    const auto gg = geo(d);
    if (!gg) return;
    const Geo g = *gg;
    const QColor color = cssColor(d.color);
    p->save();
    p->setFont(chartFont());
    const QPen pen(color, selected ? 2 : 1.5, Qt::SolidLine, Qt::FlatCap, Qt::RoundJoin);
    p->setPen(pen);
    p->setBrush(Qt::NoBrush);
    const auto line = [&](double xa, double ya, double xb, double yb) { p->drawLine(QPointF(xa, ya), QPointF(xb, yb)); };
    const QString &label = d.label;

    if (d.type == QLatin1String("trend")) {
        line(g.x1, g.y1, g.x2, g.y2);
        if (!label.isEmpty()) haloText(p, label, std::max(g.x1, g.x2) - 4, (g.x1 > g.x2 ? g.y1 : g.y2) - 6, color, Qt::AlignRight);
    } else if (d.type == QLatin1String("marker")) {
        const bool down = d.direction != QLatin1String("up"); // a 'down' arrow sits above the bar
        const double tipY = g.y1 + (down ? -4 : 4), baseY = g.y1 + (down ? -14 : 14);
        const QPointF tri[3] = {{g.x1, tipY}, {g.x1 - 6, baseY}, {g.x1 + 6, baseY}};
        p->setPen(Qt::NoPen);
        p->setBrush(color);
        p->drawPolygon(tri, 3);
        if (!label.isEmpty()) haloText(p, label, g.x1, down ? baseY - 6 : baseY + 14, color, Qt::AlignHCenter);
    } else if (d.type == QLatin1String("ray")) {
        const double dist = std::hypot(g.x2 - g.x1, g.y2 - g.y1);
        const double k = dist == 0 ? 1 : dist;
        line(g.x1, g.y1, g.x1 + ((g.x2 - g.x1) / k) * 1e4, g.y1 + ((g.y2 - g.y1) / k) * 1e4);
        if (!label.isEmpty()) haloText(p, label, g.x2 + (g.x2 >= g.x1 ? 6 : -6), g.y2 - 6, color, g.x2 >= g.x1 ? Qt::AlignLeft : Qt::AlignRight);
    } else if (d.type == QLatin1String("hline")) {
        line(0, g.y1, plotWidth(), g.y1);
        haloText(p, (label.isEmpty() ? QString() : label + QStringLiteral("  ")) + QString::number(d.p1.price, 'f', 2), 8, g.y1 - 4, color);
    } else if (d.type == QLatin1String("rect")) {
        const QRectF r(std::min(g.x1, g.x2), std::min(g.y1, g.y2), std::abs(g.x2 - g.x1), std::abs(g.y2 - g.y1));
        QColor fill = color;
        fill.setAlphaF(float(color.alphaF() * 0.12));
        p->fillRect(r, fill);
        p->drawRect(r);
        if (!label.isEmpty()) haloText(p, label, r.left() + 4, r.top() + 13, color);
    } else if (d.type == QLatin1String("fib")) {
        const double xa = std::min(g.x1, g.x2), xb = std::max(g.x1, g.x2);
        const std::vector<FibLine> lines = fibLines(d);
        if (!label.isEmpty() && !lines.empty()) {
            double top = lines[0].y;
            for (const FibLine &l : lines) top = std::min(top, l.y);
            haloText(p, label, xa + 4, top - 16, color);
        }
        for (size_t i = 0; i < lines.size(); ++i) {
            const FibLine &l = lines[i];
            if (i + 1 < lines.size()) {
                QColor band = l.color;
                band.setAlphaF(0.07f);
                p->fillRect(QRectF(xa, std::min(l.y, lines[i + 1].y), xb - xa, std::abs(lines[i + 1].y - l.y)), band);
            }
            p->setPen(QPen(l.color, 1));
            line(xa, l.y, xb, l.y);
            p->drawText(QPointF(xa + 4, l.y - 3), QString::number(l.level) + QStringLiteral(" (") + QString::number(l.price, 'f', 2) + QLatin1Char(')'));
        }
        QPen dash(QColor(0x9e, 0x9e, 0x9e), 1, Qt::CustomDashLine);
        dash.setDashPattern({4, 4});
        p->setPen(dash);
        line(g.x1, g.y1, g.x2, g.y2);
    }

    if (selected && d.type != QLatin1String("hline") && d.type != QLatin1String("marker")) {
        p->setPen(QPen(color, 2));
        p->setBrush(BG);
        p->drawEllipse(QPointF(g.x1, g.y1), HANDLE_R, HANDLE_R);
        p->drawEllipse(QPointF(g.x2, g.y2), HANDLE_R, HANDLE_R);
    }
    p->restore();
}

void ChartView::paintPriceAxis(QPainter *p, int idx)
{
    const Pane &pane = m_panes[size_t(idx)];
    const double x = plotWidth();
    p->save();
    p->setClipRect(QRectF(x, pane.rect.top(), AXIS_W, pane.rect.height()));
    p->setFont(chartFont());
    p->setPen(TEXT);
    const QFontMetricsF fm(chartFont());
    // tick labels give way to the last-value labels
    std::vector<double> taken;
    if (idx == 0) {
        if (const auto y = yOf(0, m_display.back().close)) taken.push_back(*y);
    } else {
        for (const Series &s : pane.series)
            for (int i = int(s.values.size()) - 1; i >= 0; --i)
                if (isFin(s.values[size_t(i)])) { if (const auto y = yOf(idx, s.values[size_t(i)])) taken.push_back(*y); break; }
    }
    const auto roomAt = [&](double y) { return std::none_of(taken.begin(), taken.end(), [&](double t) { return std::abs(t - y) < 13; }); };
    const bool log = idx == 0 && m_scale == QLatin1String("log");
    if (log) {
        // nice prices, unevenly spaced
        const double lo = fromUnits(0, pane.min), hi = fromUnits(0, pane.max);
        const double step = niceStep((hi - lo) / std::max(1.0, pane.rect.height() / 40));
        double lastY = 1e9;
        for (double v = std::ceil(lo / step) * step; v <= hi; v += step) {
            const auto y = yOf(0, v);
            if (!y || std::abs(*y - lastY) < 18 || !roomAt(*y)) continue;
            lastY = *y;
            p->drawText(QPointF(x + 6, *y + fm.ascent() / 2 - 1), QString::number(v, 'f', 2));
        }
    } else {
        const double step = niceStep((pane.max - pane.min) / std::max(1.0, pane.rect.height() / 40));
        for (double u = std::ceil(pane.min / step) * step; u <= pane.max; u += step) {
            const double y = pane.rect.top() + (pane.max - u) / (pane.max - pane.min) * pane.rect.height();
            if (y < pane.rect.top() + 6 || y > pane.rect.bottom() - 4 || !roomAt(y)) continue;
            const double shown = std::abs(u) < step / 1e6 ? 0 : u;
            p->drawText(QPointF(x + 6, y + fm.ascent() / 2 - 1), formatPrice(idx, shown));
        }
    }
    // last values: the main series, and series in separate panes
    if (idx == 0) {
        const Bar &last = m_display.back();
        if (const auto y = yOf(0, last.close)) {
            const bool lineType = m_type == QLatin1String("line") || m_type == QLatin1String("area");
            axisLabel(p, x, *y, AXIS_W, formatPrice(0, toUnits(0, last.close)), lineType ? LINE_BLUE : (last.close >= last.open ? m_up : m_down));
        }
    } else {
        for (const Series &s : pane.series) {
            for (int i = int(s.values.size()) - 1; i >= 0; --i) {
                if (!isFin(s.values[size_t(i)])) continue;
                if (const auto y = yOf(idx, s.values[size_t(i)]))
                    axisLabel(p, x, *y, AXIS_W, formatValue(s, s.values[size_t(i)]), s.colors.empty() ? s.color : s.colors[size_t(i)]);
                break;
            }
        }
    }
    p->restore();
}

void ChartView::paintTimeAxis(QPainter *p)
{
    const double y = m_height - TIME_H;
    p->save();
    p->setClipRect(QRectF(0, y, plotWidth(), TIME_H));
    p->setFont(chartFont());
    p->setPen(TEXT);
    for (int i : timeTicks()) {
        const double x = xOfLogical(i);
        const QString t = tickLabel(i);
        p->drawText(QRectF(x - 50, y + 2, 100, TIME_H - 4), Qt::AlignCenter, t);
    }
    p->restore();
}

void ChartView::paintLegend(QPainter *p)
{
    const int N = int(m_bars.size());
    const int nslots = N + m_future;
    const bool hovering = m_hover && m_mouse.x() >= 0 && m_mouse.x() <= plotWidth() && m_mouse.y() >= 0 && m_mouse.y() <= m_height - TIME_H;
    const int slot = hovering ? slotAt(m_mouse.x()) : N - 1;
    const QFont f = chartFont(), bold = chartFont(11, true);
    const QFontMetricsF fm(f), fmb(bold);
    p->save();

    // the OHLC readout
    double x = 6, y = m_panes[0].rect.top() + 4 + fm.ascent();
    const auto put = [&](const QString &t, const QColor &c, bool b = false) {
        p->setFont(b ? bold : f);
        p->setPen(c);
        p->drawText(QPointF(x, y), t);
        x += (b ? fmb : fm).horizontalAdvance(t);
    };
    if (!m_symbol.isEmpty()) { put(m_symbol, TEXT, true); x += 8; }
    if (slot >= 0 && slot < N) {
        const Bar &b = m_display[size_t(slot)];
        const QColor c = b.close >= b.open ? m_up : m_down;
        const auto fmt = [](double v) { return QString::number(v, 'f', 2); };
        put(QStringLiteral("O "), TEXT); put(fmt(b.open), c); x += 6;
        put(QStringLiteral("H "), TEXT); put(fmt(b.high), c); x += 6;
        put(QStringLiteral("L "), TEXT); put(fmt(b.low), c); x += 6;
        put(QStringLiteral("C "), TEXT); put(fmt(b.close), c); x += 6;
        const double prev = slot > 0 ? m_bars[size_t(slot - 1)].close : m_bars[size_t(slot)].open;
        const double ch = m_bars[size_t(slot)].close - prev;
        if (prev != 0)
            put((ch >= 0 ? QStringLiteral("+") : QString()) + fmt(ch) + QStringLiteral(" (") + (ch >= 0 ? QStringLiteral("+") : QString())
                    + fmt(ch / prev * 100) + QStringLiteral("%)"), ch >= 0 ? m_up : m_down);
        x += 10;
        put(QStringLiteral("Vol "), TEXT); put(volumeText(m_bars[size_t(slot)].volume), TEXT);
    }
    // a line per study: its title, then each output's value in its colour
    for (int pi = 0; pi < int(m_panes.size()); ++pi) {
        const Pane &pane = m_panes[size_t(pi)];
        if (pi > 0) y = pane.rect.top() + 3 + fm.ascent();
        for (const Study &st : pane.studies) {
            if (pi == 0) y += fm.height() + 1;
            x = 6;
            put(st.title, TEXT);
            for (int si : st.series) {
                const Series &s = pane.series[size_t(si)];
                if (slot < 0 || slot >= nslots) continue;
                const double v = s.values[size_t(slot)];
                x += 6;
                put(isFin(v) ? formatValue(s, v) : QStringLiteral("—"), s.colors.empty() ? s.color : s.colors[size_t(slot)]);
            }
        }
    }
    p->restore();
}

void ChartView::paintCrosshair(QPainter *p)
{
    if (!m_hover || m_pan || m_bars.empty()) return;
    const double plotH = m_height - TIME_H;
    const double mx = m_mouse.x(), my = m_mouse.y();
    if (mx < 0 || mx > plotWidth() || my < 0 || my > plotH) return;
    const int N = int(m_bars.size());
    const int slot = slotAt(mx);
    const double x = std::round(xOfLogical(slot)) + 0.5;
    const int pane = paneAt(my);
    double y = my;
    if (m_magnet && pane == 0 && slot < N) {
        // snap to the nearest of the bar's open, high, low and close
        const Bar &b = m_display[size_t(slot)];
        double best = 1e9;
        const bool lineType = m_type == QLatin1String("line") || m_type == QLatin1String("area");
        for (double v : lineType ? std::vector<double>{b.close} : std::vector<double>{b.open, b.high, b.low, b.close})
            if (const auto yy = yOf(0, v); yy && std::abs(*yy - my) < best) { best = std::abs(*yy - my); y = *yy; }
    }
    QPen pen(CROSSHAIR, 1, Qt::CustomDashLine);
    pen.setDashPattern({6, 6});
    p->save();
    p->setPen(pen);
    p->drawLine(QPointF(x, 0), QPointF(x, plotH));
    if (pane >= 0) {
        const QRectF r = m_panes[size_t(pane)].rect;
        p->drawLine(QPointF(0, std::round(y) + 0.5), QPointF(r.right(), std::round(y) + 0.5));
        const double u = m_panes[size_t(pane)].max - (y - r.top()) / r.height() * (m_panes[size_t(pane)].max - m_panes[size_t(pane)].min);
        p->setFont(chartFont());
        axisLabel(p, plotWidth(), y, AXIS_W, formatPrice(pane, u), CROSSHAIR_LABEL);
    }
    const QString t = crosshairTime(slot);
    const QFontMetricsF fm(chartFont());
    const double w = fm.horizontalAdvance(t) + 14;
    const QRectF box(std::clamp(x - w / 2, 0.0, plotWidth() - w), plotH + 2, w, TIME_H - 4);
    p->fillRect(box, CROSSHAIR_LABEL);
    p->setPen(Qt::white);
    p->drawText(box, Qt::AlignCenter, t);
    p->restore();
}
