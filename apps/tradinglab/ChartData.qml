import QtQuick
import QtQuick.Window

// The chart's data: candles for the selected symbol/range/interval (real when an FMP key exists, sample otherwise) and
// the drawings saved for the symbol. Port of renderer/src/chart/useChartData.ts (useChartData + useDrawings).
// While the market is open and the window is visible, the latest bars are re-read on a timer that depends on the
// interval. Lives in Main.qml (app.chartData), so Claude's tools can read it while another screen is showing.
//
// API (used by ChartView.qml and the Claude tools):
//   candles [{time, open, high, low, close, volume}] or null, loadedKey ("SYMBOL|range|interval" of those candles, or
//   null), loading, error, checkedAt (ms), refreshError, sample (no FMP key), reload(), drawings [Drawing],
//   drawingsReady, updateDrawings(fn) (fn takes and returns the list; applied at once and saved), busy (set by the
//   chart while the user draws or drags, so a refresh waits), capture(cb) (cb gets a base64 PNG of the chart, or null
//   when the chart is not on screen), chartItem (the ChartView item on screen, set by ChartView.qml).
Item {
    id: root
    visible: false

    property var app

    readonly property bool live: app ? app.live : false
    readonly property string symbol: app ? app.symbol : ""
    readonly property string range: app ? app.chartSettings.range : "1Y"
    readonly property string interval: app ? app.chartSettings.interval : "1day"
    // chartKey(symbol, settings)
    readonly property string key: symbol + "|" + range + "|" + interval

    // ---- candles
    property var _real: null            // {id, key, candles}: the last completed load
    property var _fresh: null           // {from: load id, candles}: that load brought up to date
    property var _sample: null          // {key, candles}
    property int _seq: 0
    property var _error: null
    property var _checkedAt: null
    property var _refreshError: null

    // a refreshed copy belongs to the load it was made from; a new load (symbol, range, interval or Retry) starts clean
    readonly property var candles: live ? (_fresh && _real && _fresh.from === _real.id ? _fresh.candles : (_real ? _real.candles : null))
                                        : (_sample ? _sample.candles : null)
    readonly property var loadedKey: live ? (_real ? _real.key : null) : key
    property bool loading: false
    readonly property var error: live ? _error : null
    readonly property var checkedAt: live ? _checkedAt : null
    readonly property var refreshError: live ? _refreshError : null
    readonly property bool sample: !live

    // ---- drawings
    property string _drawSymbol: ""
    property var _drawList: []
    readonly property var drawings: _drawSymbol === symbol ? _drawList : []
    readonly property bool drawingsReady: _drawSymbol === symbol

    // ---- the chart on screen
    property bool busy: false
    property var chartItem: null

    function capture(cb) {
        const c = chartItem;
        const png = c && c.visible && c.width > 0 ? c.capturePng(1200) : "";
        cb(png ? png : null);
    }

    // ------------------------------------------------------------------ loading (data/fmp.ts loadCandles)
    function loadCandles(sym, rng, iv, cb) {
        const weekMonth = iv === "1week" || iv === "1month";
        api.invoke("fmp:bars", [{ symbol: sym, interval: core.callPure("fetchInterval", [iv]), from: core.callPure("loadFromDay", [rng, iv]) }], function (r) {
            if (!r || !r.ok) { cb(null, r && r.error ? r.error : "Could not load price data."); return; }
            if (!r.data || r.data.length === 0) { cb(null, "No price data returned for " + sym + "."); return; }
            const c = weekMonth ? core.callPure("aggregate", [r.data, iv === "1week" ? "week" : "month"]) : r.data;
            cb(core.callPure("lastBars", [c, rng, iv]), null);
        });
    }

    // The candles brought up to date, or null when nothing changed (so the chart is not rebuilt for nothing).
    // Intraday only re-reads from the day before the last candle and replaces the tail; daily and longer reload.
    function refreshCandles(sym, rng, iv, prev, cb) {
        const intraday = core.callPure("fetchInterval", [iv]) !== "1day";
        if (!intraday || prev.length === 0) {
            loadCandles(sym, rng, iv, function (next, err) {
                if (err) cb(null, err);
                else cb(core.callPure("sameCandles", [prev, next]) ? null : next, null);
            });
            return;
        }
        api.invoke("fmp:bars", [{ symbol: sym, interval: iv, from: core.callPure("refreshFromDay", [prev]) }], function (r) {
            if (!r || !r.ok) { cb(null, r && r.error ? r.error : "Update failed"); return; }
            if (!r.data || r.data.length === 0) { cb(null, null); return; }
            const next = core.callPure("mergeRefreshed", [prev, r.data, rng, iv]);
            cb(core.callPure("sameCandles", [prev, next]) ? null : next, null);
        });
    }

    function reload() {
        if (!live) return;
        const id = ++_seq;
        const k = key, sym = symbol, rng = range, iv = interval;
        loading = true;
        _error = null;
        refreshTimer.stop();
        loadCandles(sym, rng, iv, function (c, err) {
            if (id !== root._seq) return;
            root.loading = false;
            if (err) { root._error = err; return; }
            root._real = { id: id, key: k, candles: c };
            root._checkedAt = Date.now();
            root._refreshError = null;
            root.armRefresh(core.refreshEveryMs(iv));
        });
    }

    function makeSample() {
        if (live || !symbol) return;
        if (_sample && _sample.key === key) return;
        _sample = { key: key, candles: core.callPure("sampleCandles", [symbol, range, interval]) };
    }

    // ------------------------------------------------------------------ the refresh timer
    readonly property bool windowVisible: app ? app.visible && app.visibility !== Window.Minimized && app.visibility !== Window.Hidden : true
    property real _lastTick: 0

    function armRefresh(wait) {
        refreshTimer.stop();
        if (!live || !_real || _real.key !== key) return;
        _lastTick = Date.now();
        refreshTimer.interval = wait;
        refreshTimer.start();
    }

    function tick() {
        if (!live || !_real || _real.key !== key) return;
        _lastTick = Date.now();
        const every = core.refreshEveryMs(interval);
        if (windowVisible && core.sessionActive(Date.now() / 1000)) {
            if (busy) { armRefresh(5000); return; } // mid-drawing or dragging: do not rebuild the chart under the user's hand
            const base = _real;
            const sym = symbol, rng = range, iv = interval;
            refreshCandles(sym, rng, iv, candles || base.candles, function (next, err) {
                if (!root._real || root._real.id !== base.id || root.key !== base.key) return; // a new load took over
                if (err) root._refreshError = err;
                else {
                    if (next) root._fresh = { from: base.id, candles: next };
                    root._checkedAt = Date.now();
                    root._refreshError = null;
                }
                root.armRefresh(every);
            });
            return;
        }
        armRefresh(every);
    }

    Timer { id: refreshTimer; repeat: false; onTriggered: root.tick() }

    // coming back to the window after a while: catch up now instead of waiting out the timer
    onWindowVisibleChanged: {
        if (windowVisible && live && _real && _real.key === key && Date.now() - _lastTick >= core.refreshEveryMs(interval)) {
            refreshTimer.stop();
            tick();
        }
    }

    onKeyChanged: { if (live) reload(); else makeSample(); }
    onLiveChanged: { if (live) reload(); else { refreshTimer.stop(); loading = false; makeSample(); } }

    // ------------------------------------------------------------------ drawings (useDrawings)
    function loadDrawings() {
        const sym = symbol;
        if (!sym) return;
        api.invoke("settings:get", ["drawings:" + sym], function (d) {
            if (sym !== root.symbol) return;
            root._drawList = Array.isArray(d) ? d : [];
            root._drawSymbol = sym;
        });
    }
    // applies at once, so several of Claude's tool calls in a row compose
    function updateDrawings(fn) {
        const sym = symbol;
        const next = fn(_drawSymbol === sym ? _drawList.slice() : []) || [];
        _drawList = next;
        _drawSymbol = sym;
        api.invoke("settings:set", ["drawings:" + sym, next]);
    }
    onSymbolChanged: loadDrawings()

    Component.onCompleted: {
        if (live) reload(); else makeSample();
        loadDrawings();
    }
}
