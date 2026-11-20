import QtQuick
import Win95Ui

// Graph: a graph (Graph95's) of a table's records, or of what code gives
// it. Its Graph property (the Graph Editor: Type, Display, Data) is one
// object:
//
//   type       Column, Bar, Line, Area, Pie, Doughnut, Scatter,
//              Candlestick, Heikin-Ashi, OHLC, HLC, Price (stock graphs)
//   (display)  title, legend (None, Right, Bottom, Top), xTitle, yTitle,
//              gridLines, showValues, stacked, markers, smooth, palette,
//              yMin, yMax ("": from the data), decimals (-1: as needed),
//              prefix, suffix
//   (stock)    overlays: ["SMA 20", "EMA 50", "Bollinger 20 2", "VWAP"],
//              studies: ["Volume", "RSI 14", "MACD 12 26 9", "Stochastic 14 3"],
//              candleColors: "Classic" or "Green and red", lastPrice, showLast (0: all)
//   (data)     source: "table" or "code"
//              table, labelField ("": one bar for all of it; a stock graph: a bar for each record),
//              groupDates: "", "minute", "hour", "day", "week", "month" or "year" (a date label field),
//              series: [{ field, aggregate: sum, count, average, min, max, name?, color? }],
//              (a stock graph) openField, highField, lowField, closeField, volumeField: a bar
//                  for each label, its first open, highest high, lowest low, last close and
//                  all the volume (just a close field: the prices of trades, made into bars),
//              filterField, filterOp (=, !=, >, >=, <, <=, contains, empty, notEmpty), filterValue,
//              sort: "", "label", "labelDesc", "value", "valueDesc" (the first series), limit (0: all)
//
// It's read again when its table changes. In code:
//
//   graph1.refresh()
//   graph1.setData(["Jan", "Feb"], [{ name: "Sales", values: [3, 5] }])   (or one list of values)
//   graph1.labels  series  graphType  title  stacked  showValues  legend  itemAt(x, y)
//   clicked: graph1.clickedLabel, clickedValue, clickedSeries (its name), clickedIndex, clickedItem
//   (stock) graph1.setData(labels, [{ name: "ACME", open: [...], high: [...], low: [...], close: [...], volume: [...] }])
//           graph1.tick("10:31", 101.25, 300)   a trade: the same label, the same bar
//           graph1.addBar("10:31", open, high, low, close, volume)
//           graph1.maxBars  overlays  studies  indicator("RSI 14")
AppElement {
    id: element

    property var graph: ({})
    signal clicked()

    property string clickedLabel: ""
    property var clickedValue: null
    property string clickedSeries: ""
    property int clickedIndex: -1
    // (all of it: a stock graph's bar has open, high, low, close, volume, change)
    property var clickedItem: null

    // the database it reads (an app's, or the Graph Editor's preview's)
    property var database: form && !designMode ? form.db : null

    readonly property var defaults: ({
        type: "Column", title: "", legend: "Right", xTitle: "", yTitle: "", gridLines: true, showValues: false,
        stacked: false, markers: true, smooth: false, palette: "Windows", yMin: "", yMax: "", decimals: -1,
        prefix: "", suffix: "", source: "table", table: "", labelField: "", groupDates: "", series: [],
        filterField: "", filterOp: "=", filterValue: "", sort: "", limit: 0,
        overlays: [], studies: [], candleColors: "Classic", lastPrice: true, showLast: 0,
        openField: "", highField: "", lowField: "", closeField: "", volumeField: ""
    })
    readonly property var settings: Object.assign({}, defaults, graph || {})

    // (code can change these while the app runs)
    property string graphType: settings.type
    property string title: settings.title
    property string legend: settings.legend
    property bool stacked: settings.stacked
    property bool showValues: settings.showValues
    property var overlays: settings.overlays
    property var studies: settings.studies
    // tick() and addBar() keep this many bars (0: all of them)
    property int maxBars: 0
    readonly property bool isStock: drawing.stockTypes.indexOf(graphType) >= 0

    property var labels: []
    property var series: []
    property bool fromCode: false
    // (the Graph Editor's: what went wrong reading the table)
    property string problem: ""

    // the colors: BackColor (around it) and PlotColor (behind the bars,
    // lines and candles); blank, the desktop's, as the form's is
    property string plotColor: ""
    readonly property color plotFill: plotColor !== "" ? plotColor : Theme.window

    function refresh() {
        problem = "";
        if (fromCode)
            return;
        var s = settings;
        if (designMode || !database || s.source === "code" || !s.table) {
            sampleData();
            return;
        }
        var info = database.tableInfo(s.table);
        if (!info || !info.name) {
            problem = "There's no table called " + s.table + ".";
            labels = [];
            series = [];
            return;
        }
        var rows = database.select(s.table, {});
        rows = rows.filter(function (r) { return passes(r, s); });

        var labelInfo = s.labelField ? database.fieldInfo(s.table, s.labelField) : ({});
        if (drawing.stockTypes.indexOf(s.type) >= 0) {
            stockFrom(rows, s, labelInfo);
            return;
        }
        var specs = (s.series || []).filter(function (x) { return x && (x.field || x.aggregate === "count"); });
        if (specs.length === 0)
            specs = [{ field: "", aggregate: "count" }];

        // the records in groups, by label
        var groups = {}, order = [];
        rows.forEach(function (r) {
            var key = keyOf(r, s, labelInfo);
            if (groups[key] === undefined) {
                groups[key] = { key: key, label: labelOf(key, r, s, labelInfo), rows: [] };
                order.push(key);
            }
            groups[key].rows.push(r);
        });
        var list = order.map(function (k) { return groups[k]; });
        list.forEach(function (gr) {
            gr.values = specs.map(function (spec) { return summary(gr.rows, spec); });
        });

        // in order, the first so many
        var sort = s.sort || (s.groupDates ? "label" : "");
        if (sort === "label" || sort === "labelDesc") {
            list.sort(function (a, b) { return compare(a.key, b.key); });
            if (sort === "labelDesc")
                list.reverse();
        } else if (sort === "value" || sort === "valueDesc") {
            list.sort(function (a, b) { return (a.values[0] || 0) - (b.values[0] || 0); });
            if (sort === "valueDesc")
                list.reverse();
        }
        if (s.limit > 0)
            list = list.slice(0, s.limit);

        labels = list.map(function (gr) { return gr.label; });
        series = specs.map(function (spec, i) {
            return { name: spec.name || seriesName(spec), color: spec.color || "",
                     values: list.map(function (gr) { return gr.values[i]; }) };
        });
    }

    // a stock graph's bars: one for each label, in time order (or each record, without one)
    function stockFrom(rows, s, labelInfo) {
        if (!s.closeField) {
            problem = "Which field is the price? (Data: Close)";
            labels = [];
            series = [];
            return;
        }
        var lf = s.labelField;
        rows = rows.slice().sort(function (a, b) {
            var c = lf ? compare(a[lf] === null || a[lf] === undefined ? "" : a[lf], b[lf] === null || b[lf] === undefined ? "" : b[lf]) : 0;
            return c !== 0 ? c : compare(a.id, b.id);
        });
        var groups = [], byKey = {};
        rows.forEach(function (r, i) {
            var key = lf ? keyOf(r, s, labelInfo) : "#" + i;
            if (byKey[key] === undefined) {
                byKey[key] = { label: lf ? labelOf(key, r, s, labelInfo) : String(i + 1), rows: [] };
                groups.push(byKey[key]);
            }
            byKey[key].rows.push(r);
        });
        function nums(list, field) {
            var out = [];
            list.forEach(function (r) { var n = number(r[field]); if (n !== null) out.push(n); });
            return out;
        }
        var bars = groups.map(function (gr) {
            var closes = nums(gr.rows, s.closeField), many = closes.length > 1;
            var opens = s.openField ? nums(gr.rows, s.openField) : many ? closes : [];
            var highs = s.highField ? nums(gr.rows, s.highField) : many ? closes : [];
            var lows = s.lowField ? nums(gr.rows, s.lowField) : many ? closes : [];
            var vols = s.volumeField ? nums(gr.rows, s.volumeField) : [];
            return { label: gr.label,
                     open: opens.length ? opens[0] : null,
                     high: highs.length ? Math.max.apply(null, highs) : null,
                     low: lows.length ? Math.min.apply(null, lows) : null,
                     close: closes.length ? closes[closes.length - 1] : null,
                     volume: vols.length ? vols.reduce(function (a, b) { return a + b; }, 0) : null };
        });
        labels = bars.map(function (b) { return b.label; });
        var one = { name: s.filterOp === "=" && s.filterField && String(s.filterValue) !== "" ? String(s.filterValue) : s.table };
        ["open", "high", "low", "close"].forEach(function (k) { one[k] = bars.map(function (b) { return b[k]; }); });
        if (s.volumeField)
            one.volume = bars.map(function (b) { return b.volume; });
        series = [one];
    }

    // what's drawn at a point of it: { series, index, label, value, name } or null
    // (a stock graph's bar: and open, high, low, close, volume, change, changePercent)
    function itemAt(x, y) { return drawing.itemAt(x, y); }

    // (a stock graph, from code) a trade as it happens: the same label as the last bar, that
    // bar's (its high, low, close and volume); a new label, a new bar
    function tick(label, price, volume) {
        startFromCode();
        var r = drawing.withTick(labels, series, label, price, volume, maxBars);
        labels = r.labels;
        series = r.series;
    }
    // a whole bar (the same label as the last: it, again)
    function addBar(label, open, high, low, close, volume) {
        startFromCode();
        var r = drawing.withBar(labels, series, label, open, high, low, close, volume, maxBars);
        labels = r.labels;
        series = r.series;
    }
    // (the first from code: not added to the made-up values, or the table's)
    function startFromCode() {
        if (fromCode)
            return;
        fromCode = true;
        labels = [];
        series = [];
    }
    // what an indicator ("SMA 20", "RSI 14", "MACD", ...) works out to, a value for each label
    function indicator(name) { return drawing.indicator(name); }

    // the code's own data (the table isn't read again)
    function setData(newLabels, newSeries) {
        fromCode = true;
        labels = (newLabels || []).map(function (l) { return String(l); });
        var list = newSeries || [];
        if (list.length > 0 && typeof list[0] !== "object")
            list = [{ name: "", values: list }];
        series = list.map(function (one, i) {
            var out = { name: one.name !== undefined ? String(one.name) : "Series " + (i + 1), color: one.color || "",
                        values: (one.values || []).slice() };
            ["open", "high", "low", "close", "volume"].forEach(function (k) {
                if (one[k])
                    out[k] = one[k].slice();
            });
            return out;
        });
    }

    function seriesName(spec) {
        var how = { sum: "", count: "Count of ", average: "Average ", min: "Least ", max: "Most " }[spec.aggregate || "sum"] || "";
        if (spec.aggregate === "count" && !spec.field)
            return "Records";
        return how + spec.field;
    }

    function sampleData() {
        var s = settings;
        if (drawing.stockTypes.indexOf(s.type) >= 0) {
            // (a made-up stock: forty days of it)
            var seed = 11, p = 50, day = new Date(2026, 2, 2), one = { name: s.table || "Sample", open: [], high: [], low: [], close: [], volume: [] };
            var lbls = [];
            function rnd() { seed = (seed * 16807) % 2147483647; return seed / 2147483647; }
            for (var d = 0; d < 40; ++d) {
                var o = p, c = Math.max(1, o + (rnd() - 0.47) * 2);
                one.open.push(o);
                one.close.push(Math.round(c * 100) / 100);
                one.high.push(Math.round((Math.max(o, c) + rnd()) * 100) / 100);
                one.low.push(Math.round((Math.min(o, c) - rnd()) * 100) / 100);
                one.volume.push(Math.round(2000 + rnd() * 8000));
                lbls.push(Qt.formatDate(day, "d MMM"));
                day.setDate(day.getDate() + (day.getDay() === 5 ? 3 : 1));
                p = one.close[one.close.length - 1];
            }
            labels = lbls;
            series = [one];
            return;
        }
        labels = s.type === "Pie" || s.type === "Doughnut" ? ["North", "South", "East", "West"]
               : s.type === "Scatter" ? ["1", "2", "3", "4", "5"] : ["Jan", "Feb", "Mar", "Apr"];
        var specs = (s.series || []).filter(function (x) { return x && (x.field || x.aggregate === "count"); });
        if (specs.length === 0)
            specs = [{ field: "", aggregate: "count" }];
        var shapes = [[3, 5, 4, 7, 6], [2, 3, 5, 4, 5], [4, 2, 3, 2, 4], [1, 4, 2, 5, 3]];
        series = specs.map(function (spec, i) {
            return { name: spec.name || (s.table ? seriesName(spec) : "Series " + (i + 1)), color: spec.color || "",
                     values: shapes[i % shapes.length].slice(0, labels.length) };
        });
    }

    function number(v) {
        if (v === null || v === undefined || v === "")
            return null;
        if (typeof v === "boolean")
            return v ? 1 : 0;
        var n = Number(v);
        return isFinite(n) ? n : null;
    }
    function compare(a, b) {
        var na = Number(a), nb = Number(b);
        if (String(a).trim() !== "" && String(b).trim() !== "" && isFinite(na) && isFinite(nb))
            return na - nb;
        return String(a).localeCompare(String(b));
    }

    function passes(r, s) {
        if (!s.filterField)
            return true;
        var v = r[s.filterField], want = s.filterValue;
        var empty = v === null || v === undefined || v === "";
        switch (s.filterOp || "=") {
        case "empty": return empty;
        case "notEmpty": return !empty;
        case "contains": return !empty && String(v).toLowerCase().indexOf(String(want).toLowerCase()) >= 0;
        }
        if (empty)
            return false;
        var c;
        if (typeof v === "boolean")
            c = (v ? 1 : 0) - (/^(yes|true|1)$/i.test(String(want)) ? 1 : 0);
        else
            c = compare(v, want);
        switch (s.filterOp || "=") {
        case "=": return c === 0;
        case "!=": return c !== 0;
        case ">": return c > 0;
        case ">=": return c >= 0;
        case "<": return c < 0;
        case "<=": return c <= 0;
        }
        return true;
    }

    // what a record's label is grouped by
    function keyOf(r, s, info) {
        if (!s.labelField)
            return s.table;
        var v = r[s.labelField];
        if (v === null || v === undefined || v === "")
            return "";
        if (s.groupDates && (info.type === "date" || info.type === "datetime")) {
            var t = String(v);
            switch (s.groupDates) {
            case "year": return t.substring(0, 4);
            case "month": return t.substring(0, 7);
            case "hour": return t.substring(0, 13);
            case "minute": return t.substring(0, 16);
            case "week":
                // (the Monday it starts on)
                var day = new Date(Number(t.substring(0, 4)), Number(t.substring(5, 7)) - 1, Number(t.substring(8, 10)));
                if (isNaN(day.getTime()))
                    return t.substring(0, 10);
                day.setDate(day.getDate() - (day.getDay() + 6) % 7);
                return Qt.formatDate(day, "yyyy-MM-dd");
            }
            return t.substring(0, 10);
        }
        return typeof v === "boolean" ? (v ? "1" : "0") : String(v);
    }
    // and as it shows
    function labelOf(key, r, s, info) {
        if (!s.labelField)
            return s.table;
        if (key === "")
            return "(none)";
        if (info.type === "lookup")
            return database.lookupLabel(info.lookupTable, r[s.labelField]) || key;
        if (info.type === "boolean")
            return key === "1" ? "Yes" : "No";
        if (s.groupDates === "month" && /^\d{4}-\d\d$/.test(key))
            return Qt.formatDate(new Date(Number(key.substring(0, 4)), Number(key.substring(5, 7)) - 1, 1), "MMM yyyy");
        if ((s.groupDates === "day" || s.groupDates === "week") && /^\d{4}-\d\d-\d\d$/.test(key))
            return Qt.formatDate(new Date(Number(key.substring(0, 4)), Number(key.substring(5, 7)) - 1, Number(key.substring(8, 10))), "d MMM yyyy");
        if (s.groupDates === "hour" && /^\d{4}-\d\d-\d\dT\d\d$/.test(key))
            return Qt.formatDate(new Date(Number(key.substring(0, 4)), Number(key.substring(5, 7)) - 1, Number(key.substring(8, 10))), "d MMM")
                   + " " + key.substring(11, 13) + ":00";
        if (s.groupDates === "minute" && /^\d{4}-\d\d-\d\dT\d\d:\d\d$/.test(key))
            return key.substring(11, 16);
        return key;
    }

    function summary(rows, spec) {
        var how = spec.aggregate || "sum";
        if (how === "count") {
            if (!spec.field)
                return rows.length;
            return rows.filter(function (r) { return r[spec.field] !== null && r[spec.field] !== undefined && r[spec.field] !== ""; }).length;
        }
        var nums = [];
        rows.forEach(function (r) {
            var n = number(r[spec.field]);
            if (n !== null)
                nums.push(n);
        });
        if (nums.length === 0)
            return null;
        if (how === "average")
            return nums.reduce(function (a, b) { return a + b; }, 0) / nums.length;
        if (how === "min")
            return Math.min.apply(null, nums);
        if (how === "max")
            return Math.max.apply(null, nums);
        return nums.reduce(function (a, b) { return a + b; }, 0);
    }

    // (read again when the settings change, its table changes, or it opens)
    property bool pending: false
    function later() {
        if (pending)
            return;
        pending = true;
        Qt.callLater(function () {
            element.pending = false;
            element.refresh();
        });
    }
    onSettingsChanged: later()
    onDatabaseChanged: later()
    onDesignModeChanged: later()
    Component.onCompleted: refresh()
    Connections {
        target: element.database
        ignoreUnknownSignals: true
        function onChanged(table) {
            if (table === element.settings.table)
                element.later();
        }
    }

    Graph95 {
        id: drawing
        anchors.fill: parent
        type: element.graphType
        labels: element.labels
        series: element.series
        title: element.title
        legend: element.legend
        xTitle: element.settings.xTitle
        yTitle: element.settings.yTitle
        gridLines: element.settings.gridLines
        showValues: element.showValues
        stacked: element.stacked
        markers: element.settings.markers
        smooth: element.settings.smooth
        palette: element.settings.palette
        yMin: element.settings.yMin
        yMax: element.settings.yMax
        decimals: element.settings.decimals
        prefix: element.settings.prefix
        suffix: element.settings.suffix
        overlays: element.overlays
        studies: element.studies
        candleColors: element.settings.candleColors
        lastPrice: element.settings.lastPrice
        showLast: element.settings.showLast
        backColor: element.fillColor
        plotColor: element.plotFill
        textColor: element.textColor
        fontFamily: element.family
        fontSize: element.pixelSize
        emptyText: element.problem !== "" ? element.problem : "No data"
        tips: !element.designMode
        enabled: !element.designMode
        onClicked: (item) => {
            element.forceActiveFocus();
            element.clickedLabel = item ? String(item.label) : "";
            element.clickedValue = item ? item.value : null;
            element.clickedSeries = item ? item.name : "";
            element.clickedIndex = item ? item.index : -1;
            element.clickedItem = item;
            element.clicked();
        }
    }
}
