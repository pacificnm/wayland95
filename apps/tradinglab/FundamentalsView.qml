import QtQuick
import Win95Ui

// Fundamentals: valuation ratios, profitability, the balance sheet and the annual statements for the chart's symbol.
// Port of upstream renderer/src/views/FundamentalsView.tsx (fmp:fundamentals); sample numbers without an FMP key.
Item {
    id: view
    property var app

    readonly property string symbol: app ? app.symbol : ""
    readonly property bool live: app ? app.live : false
    property var result: null
    property string error: ""
    property bool loading: false
    property int seq: 0

    function pick(o, ...names) {
        if (!o) return null;
        for (const n of names) { const v = o[n]; if (v !== undefined && v !== null && v !== "" && isFinite(Number(v))) return Number(v); }
        return null;
    }
    function pickStr(o, ...names) {
        if (!o) return "";
        for (const n of names) { const v = o[n]; if (v !== undefined && v !== null && v !== "") return String(v); }
        return "";
    }
    function dash(v, fmt) { return v === null || v === undefined ? "—" : fmt(v); }
    function x2(n) { return n.toFixed(2); }
    function x1(n) { return n.toFixed(1); }
    function pc(n) { return (n * 100).toFixed(1) + "%"; } // FMP ratios are fractions (0.466 is 46.6%), including ones above 100% such as ROE

    function build(f) {
        const r = f.ratios, m = f.metrics, b = (f.balance || [])[0] || null;
        return {
            valuation: {
                "Market cap": dash(pick(m, "marketCap"), Util.compact),
                "P/E (TTM)": dash(pick(r, "priceToEarningsRatioTTM", "peRatioTTM"), x1),
                "P/S": dash(pick(r, "priceToSalesRatioTTM"), x1),
                "P/B": dash(pick(r, "priceToBookRatioTTM"), x1),
                "EV/EBITDA": dash(pick(m, "evToEBITDATTM", "enterpriseValueOverEBITDATTM"), x1),
                "PEG": dash(pick(r, "priceToEarningsGrowthRatioTTM", "pegRatioTTM"), x2)
            },
            profitability: {
                "Gross margin": dash(pick(r, "grossProfitMarginTTM"), pc),
                "Operating margin": dash(pick(r, "operatingProfitMarginTTM"), pc),
                "Net margin": dash(pick(r, "netProfitMarginTTM"), pc),
                "ROE": dash(pick(m, "returnOnEquityTTM"), pc),
                "ROA": dash(pick(m, "returnOnAssetsTTM"), pc)
            },
            balance: {
                "Cash": dash(pick(b, "cashAndCashEquivalents", "cashAndShortTermInvestments"), Util.compact),
                "Total debt": dash(pick(b, "totalDebt"), Util.compact),
                "Debt / equity": dash(pick(r, "debtToEquityRatioTTM", "debtEquityRatioTTM"), x2),
                "Current ratio": dash(pick(r, "currentRatioTTM"), x2),
                "Total equity": dash(pick(b, "totalStockholdersEquity", "totalEquity"), Util.compact)
            },
            years: (f.income || []).map(function (i) {
                const date = pickStr(i, "date");
                const cf = (f.cashflow || []).find(function (c) { return pickStr(c, "date") === date; }) || null;
                return { year: pickStr(i, "fiscalYear", "calendarYear") || date.slice(0, 4), revenue: pick(i, "revenue"), gross: pick(i, "grossProfit"), operating: pick(i, "operatingIncome"),
                         net: pick(i, "netIncome"), eps: pick(i, "epsDiluted", "epsdiluted", "eps"), fcf: pick(cf, "freeCashFlow") };
            })
        };
    }

    function fromSample(sym) {
        const s = sample.fundamentals(sym);
        return { valuation: s.valuation, profitability: s.profitability, balance: s.balance,
                 years: s.years.map(function (y) { return { year: String(y.year), revenue: y.revenue, gross: y.gross, operating: y.operating, net: y.net, eps: y.eps, fcf: y.fcf }; }) };
    }

    function load() {
        if (!live || !symbol) return;
        const id = ++seq;
        loading = true;
        error = "";
        api.invoke("fmp:fundamentals", [symbol], function (r) {
            if (id !== seq) return;
            loading = false;
            if (!r || !r.ok) { error = r ? r.error : "No answer from the data service."; return; }
            result = build(r.data);
        });
    }
    onSymbolChanged: { result = null; load(); }
    onLiveChanged: load()

    readonly property var v: live ? result : (symbol ? fromSample(symbol) : null)

    SvSample { id: sample }

    SymbolHeader { id: header; app: view.app; width: view.width }

    SvPage {
        id: page
        y: header.height + 2
        width: view.width
        height: view.height - y
        sample: view.live ? "" : "Sample data — add your FMP key in File → Settings."
        loading: view.live && view.loading && !view.v
        error: view.error
        onRetry: view.load()

        Grid {
            id: grid
            visible: !!view.v
            width: page.contentWidth
            columns: 3
            spacing: 8
            readonly property real cardWidth: (width - 2 * spacing) / 3
            readonly property real tallest: Math.max(c1.implicitHeight, c2.implicitHeight, c3.implicitHeight)
            SvCard { id: c1; width: grid.cardWidth; height: grid.tallest; title: "Valuation"; SvKV { width: parent.width; entries: view.v ? view.v.valuation : {} } }
            SvCard { id: c2; width: grid.cardWidth; height: grid.tallest; title: "Profitability"; SvKV { width: parent.width; entries: view.v ? view.v.profitability : {} } }
            SvCard { id: c3; width: grid.cardWidth; height: grid.tallest; title: "Balance sheet"; SvKV { width: parent.width; entries: view.v ? view.v.balance : {} } }
        }

        SvCard {
            visible: !!view.v
            width: page.contentWidth
            title: "Annual results"
            SvTable {
                width: parent.width
                model: view.v ? view.v.years : []
                emptyText: "No statements available."
                columns: [
                    { title: "Year", w: 60, bold: true, text: function (y) { return y.year; } },
                    { title: "Revenue", flex: true, min: 70, right: true, text: function (y) { return view.dash(y.revenue, Util.compact); } },
                    { title: "Gross profit", flex: true, min: 80, right: true, text: function (y) { return view.dash(y.gross, Util.compact); } },
                    { title: "Operating income", flex: true, min: 100, right: true, text: function (y) { return view.dash(y.operating, Util.compact); } },
                    { title: "Net income", flex: true, min: 80, right: true, text: function (y) { return view.dash(y.net, Util.compact); }, color: function (y) { return y.net !== null && y.net < 0 ? "#b00000" : Theme.text; } },
                    { title: "EPS", w: 70, right: true, text: function (y) { return view.dash(y.eps, Util.usd); } },
                    { title: "Free cash flow", flex: true, min: 90, right: true, text: function (y) { return view.dash(y.fcf, Util.compact); } }
                ]
            }
        }
        Graph95 {
            visible: !!view.v && view.v.years.length > 1
            width: page.contentWidth
            height: 220
            type: "Column"
            title: "Annual results"
            legend: "Bottom"
            labels: view.v ? view.v.years.slice().reverse().map(function (y) { return y.year; }) : []
            series: view.v ? [
                { name: "Revenue", values: view.v.years.slice().reverse().map(function (y) { return y.revenue ?? 0; }) },
                { name: "Net income", values: view.v.years.slice().reverse().map(function (y) { return y.net ?? 0; }), color: "#008000" },
                { name: "Free cash flow", values: view.v.years.slice().reverse().map(function (y) { return y.fcf ?? 0; }), color: "#808000" }
            ] : []
        }
    }
}
