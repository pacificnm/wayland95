#pragma once

// The price chart (QML type PriceChart): candles (or hollow candles, OHLC bars, a line, an area, Heikin Ashi), overlay studies on the price
// pane and one pane per separate study, Ichimoku's cloud, study reference levels, normal/log/percent price scales, a
// shared time scale, a crosshair with an OHLC readout and legend, wheel/drag zoom and scroll that survive refreshes,
// the user's (and Claude's) drawings with select/drag/delete, and draggable order lines.
// Replaces lightweight-charts and upstream's components/Chart.tsx, chart/primitives.ts and chart/drawings.ts.
//
// The plot is upstream's dark chart palette (a sunken well in the Win95 window); everything around it is Win95.
//
// QML gives it: candles [{time, open, high, low, close, volume}], settings (chart/settings.ts ChartSettings), dataKey
// ("SYMBOL|range|interval": the view survives a rebuild only within the same key), intraday, timeZone ("" = local),
// hour12, drawings [Drawing], orderLines [OrderLine]. It owns `tool` (cursor|trend|ray|hline|fib|rect): QML sets it
// from the toolbar, the chart puts it back to "cursor" after a shape is placed or on Esc.
// It tells QML: drawingsEdited(list) (save it), orderLineMoved(id, price), and `busy` while drawing or dragging.

#include "core/bar.h"

#include <QColor>
#include <QPointF>
#include <QQuickPaintedItem>
#include <QRectF>
#include <QTimeZone>
#include <QVariant>
#include <QtQml/qqmlregistration.h>

#include <optional>
#include <vector>

class ChartView : public QQuickPaintedItem
{
    Q_OBJECT
    QML_NAMED_ELEMENT(PriceChart)
    Q_PROPERTY(QVariantList candles READ candles WRITE setCandles NOTIFY candlesChanged)
    Q_PROPERTY(QVariantMap settings READ settings WRITE setSettings NOTIFY settingsChanged)
    Q_PROPERTY(QString dataKey READ dataKey WRITE setDataKey NOTIFY dataKeyChanged)
    Q_PROPERTY(QString symbol READ symbol WRITE setSymbol NOTIFY symbolChanged)
    Q_PROPERTY(bool intraday READ intraday WRITE setIntraday NOTIFY intradayChanged)
    Q_PROPERTY(QString timeZone READ timeZone WRITE setTimeZone NOTIFY timeZoneChanged)
    Q_PROPERTY(bool hour12 READ hour12 WRITE setHour12 NOTIFY hour12Changed)
    Q_PROPERTY(QString tool READ tool WRITE setTool NOTIFY toolChanged)
    Q_PROPERTY(QVariantList drawings READ drawings WRITE setDrawings NOTIFY drawingsChanged)
    Q_PROPERTY(QVariantList orderLines READ orderLines WRITE setOrderLines NOTIFY orderLinesChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
    Q_PROPERTY(QString selectedId READ selectedId NOTIFY selectedIdChanged)

public:
    explicit ChartView(QQuickItem *parent = nullptr);

    QVariantList candles() const { return m_candlesIn; }
    void setCandles(const QVariantList &c);
    QVariantMap settings() const { return m_settings; }
    void setSettings(const QVariantMap &s);
    QString dataKey() const { return m_dataKey; }
    void setDataKey(const QString &k);
    QString symbol() const { return m_symbol; }
    void setSymbol(const QString &s);
    bool intraday() const { return m_intraday; }
    void setIntraday(bool on);
    QString timeZone() const { return m_timeZone; }
    void setTimeZone(const QString &tz);
    bool hour12() const { return m_hour12; }
    void setHour12(bool on);
    QString tool() const { return m_tool; }
    void setTool(const QString &t);
    QVariantList drawings() const;
    void setDrawings(const QVariantList &d);
    QVariantList orderLines() const { return m_orderLinesIn; }
    void setOrderLines(const QVariantList &l);
    bool busy() const;
    QString selectedId() const { return m_selectedId; }

    // A PNG of the chart scaled to at most maxWidth pixels wide, base64 (for Claude's capture_chart tool); "" when
    // there is nothing to show.
    Q_INVOKABLE QString capturePng(int maxWidth = 1200);
    // show all the bars (as a new symbol does)
    Q_INVOKABLE void fitContent();
    // where a price is on the price pane (y in item pixels), or -1 when off the scale
    Q_INVOKABLE double priceToY(double price);
    // removes the selected drawing (as Delete does)
    Q_INVOKABLE void deleteSelected();

    void paint(QPainter *p) override;

signals:
    void candlesChanged();
    void settingsChanged();
    void dataKeyChanged();
    void symbolChanged();
    void intradayChanged();
    void timeZoneChanged();
    void hour12Changed();
    void toolChanged();
    void drawingsChanged();
    void orderLinesChanged();
    void busyChanged();
    void selectedIdChanged();
    void drawingsEdited(const QVariantList &drawings);
    void orderLineMoved(const QString &id, double price);

protected:
    void updatePolish() override;
    void geometryChange(const QRectF &newGeometry, const QRectF &oldGeometry) override;
    void mousePressEvent(QMouseEvent *e) override;
    void mouseMoveEvent(QMouseEvent *e) override;
    void mouseReleaseEvent(QMouseEvent *e) override;
    void hoverMoveEvent(QHoverEvent *e) override;
    void hoverLeaveEvent(QHoverEvent *e) override;
    void wheelEvent(QWheelEvent *e) override;
    void keyPressEvent(QKeyEvent *e) override;

private:
    // ---- the built model
    struct Series
    {
        QString kind;                 // line | histogram | dots
        QColor color;
        std::vector<double> values;   // one per slot (bars + future), NAN = gap
        std::vector<QColor> colors;   // per slot, or empty
        bool steps = false;
        bool volumeFormat = false;
        QString name;
    };
    struct Cloud { int upper, lower; QColor up, down; };
    struct Study { QString title; std::vector<int> series; }; // series indexes in its pane
    struct Pane
    {
        std::vector<Series> series;
        std::vector<Cloud> clouds;
        std::vector<double> levels;
        std::vector<Study> studies;
        bool volumeFormat = false;
        // layout and scale, recomputed for each paint
        QRectF rect;
        double min = 0, max = 1; // in transformed units
        bool valid = false;
    };
    struct Point { double time = 0, price = 0; };
    struct Drawing
    {
        QString id, type, color, label, by, direction;
        Point p1, p2;
        QVariantMap extra; // fields this port does not know, kept as they were
    };
    struct OrderLine { QString id, label, style; double price = 0; QColor color; bool draggable = false; };
    struct Geo { double x1, y1, x2, y2; };
    struct Hit { QString id, part; };

    void rebuild();
    void ensureBuilt();
    void layout(double w, double h);
    void autoscale();
    void clampRange();
    void zoomAt(double x, double factor);

    // coordinates
    double plotWidth() const;
    double xOfLogical(double logical) const;
    double logicalOfX(double x) const;
    int slotAt(double x) const;
    double toUnits(int pane, double v) const;
    double fromUnits(int pane, double u) const;
    std::optional<double> yOf(int pane, double price) const;
    std::optional<double> priceAt(int pane, double y) const;
    int paneAt(double y) const;
    std::optional<double> xOfTime(double time) const;
    std::optional<Point> pointAt(double x, double y) const;

    // drawings
    std::optional<Geo> geo(const Drawing &d) const;
    struct FibLine { double level, price, y; QColor color; };
    std::vector<FibLine> fibLines(const Drawing &d) const;
    std::optional<Hit> pick(double x, double y) const;
    const OrderLine *pickOrderLine(double x, double y) const;
    void commit(const std::vector<Drawing> &list);
    void finishDraft(const Point &pt);
    static Drawing drawingFromVariant(const QVariant &v);
    static QVariantMap drawingToVariant(const Drawing &d);

    // painting
    void render(QPainter *p, double w, double h, bool withCrosshair);
    void paintPane(QPainter *p, int pane);
    void paintMain(QPainter *p);
    void paintSeries(QPainter *p, const Pane &pane, int paneIdx, const Series &s);
    void paintClouds(QPainter *p, const Pane &pane, int paneIdx);
    void paintDrawing(QPainter *p, const Drawing &d, bool selected);
    void paintOrderLine(QPainter *p, const OrderLine &l);
    void paintPriceAxis(QPainter *p, int pane);
    void paintTimeAxis(QPainter *p);
    void paintLegend(QPainter *p);
    void paintCrosshair(QPainter *p);
    QString formatPrice(int pane, double value) const;
    QString formatValue(const Series &s, double v) const;
    QString tickLabel(int slot) const;
    QString crosshairTime(int slot) const;
    double slotTime(int slot) const;
    void refreshBusy();
    std::vector<int> timeTicks() const;
    void setSelected(const QString &id);
    void updateCursor(double x, double y);

    // ---- inputs
    QVariantList m_candlesIn;
    QVariantMap m_settings;
    QString m_dataKey, m_symbol, m_timeZone, m_tool = QStringLiteral("cursor");
    bool m_intraday = false, m_hour12 = true;
    QVariantList m_orderLinesIn;

    // ---- built from the inputs
    bool m_dirty = true;
    tl::Bars m_bars;          // raw
    tl::Bars m_display;       // what the main series draws (Heikin Ashi or raw)
    std::vector<double> m_times; // bar times (no future slots)
    int m_future = 0;
    double m_step = 86400;
    std::vector<Pane> m_panes;
    std::vector<int> m_tickWeight; // per slot: 4 year, 3 month, 2 day, 1 hour, 0 other
    QString m_type = QStringLiteral("candles"), m_scale = QStringLiteral("normal");
    QColor m_up, m_down;
    bool m_grid = true, m_magnet = false;
    QString m_builtKey;
    QTimeZone m_zone = QTimeZone::UTC;
    std::vector<OrderLine> m_orderLines;
    std::vector<Drawing> m_drawings;

    // ---- view
    double m_from = 0, m_to = 1; // visible logical range (bar indexes; bar i is centred on i)
    bool m_haveView = false;
    double m_percentBase = 0;
    double m_width = 0, m_height = 0;

    // ---- interaction
    bool m_hover = false;
    QPointF m_mouse;
    QString m_selectedId;
    std::optional<Drawing> m_draft;
    std::optional<QPointF> m_downAt;
    struct Drag { QString id, part; QPointF start; Drawing orig; bool moved = false; };
    std::optional<Drag> m_drag;
    struct LineDrag { QString id; double price; };
    std::optional<LineDrag> m_lineDrag;
    struct Pan { double x, from, to; };
    std::optional<Pan> m_pan;
    bool m_busy = false;
};
