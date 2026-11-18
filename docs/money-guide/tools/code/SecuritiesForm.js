//// (General)
readonly property var home: App.form("HomeForm")
property var ranges: [30, 91, 182, 365, 1826]

// the security's price history as candles
function showChart() {
    if (form.isNew) {
        chart.setData([], []);
        return;
    }
    var from = home.addDays(home.today(), -ranges[Math.max(0, rangeCombo.currentIndex)]);
    var labels = [], bars = { name: form.record.Symbol, open: [], high: [], low: [], close: [], volume: [] };
    Db.select("Prices", { where: { Security: form.recordId, Day: { op: ">=", value: from } }, orderBy: "Day" }).forEach(function (p) {
        labels.push(AppDates.display(p.Day, "Date", "MMM d", ""));
        bars.open.push(p.Open === null ? p.Close : p.Open);
        bars.high.push(p.High === null ? p.Close : p.High);
        bars.low.push(p.Low === null ? p.Close : p.Low);
        bars.close.push(p.Close);
        bars.volume.push(p.Volume || 0);
    });
    chart.title = form.record.Symbol + (form.record.Company ? "  " + form.record.Company : "");
    chart.setData(labels, [bars]);
    showStudies();
    statusLabel.text = labels.length === 0 ? "No prices yet: Download Prices gets them from Schwab." : labels.length + " days of prices.";
}
function showStudies() {
    var over = [], under = [];
    if (averageCheck.checked)
        over.push("SMA 20");
    if (bandsCheck.checked)
        over.push("Bollinger 20 2");
    if (volumeCheck.checked)
        under.push("Volume");
    if (rsiCheck.checked)
        under.push("RSI 14");
    chart.overlays = over;
    chart.studies = under;
}

// a year of daily prices from Schwab, into the Prices table
function download() {
    if (form.isNew)
        return;
    var id = form.recordId, symbol = form.record.Symbol;
    statusLabel.text = "Asking Schwab for " + symbol + "'s prices...";
    home.schwabGet("/marketdata/v1/pricehistory?symbol=" + encodeURIComponent(symbol)
                   + "&periodType=year&period=5&frequencyType=daily&frequency=1", function (ok, data) {
        if (!ok) {
            statusLabel.text = data;
            return;
        }
        var have = {};
        Db.select("Prices", { where: { Security: id } }).forEach(function (p) { have[p.Day] = true; });
        var added = 0, candles = data.candles || [];
        candles.forEach(function (c, i) {
            var day = home.iso(new Date(c.datetime));
            // (the days it has already are left alone, but the last one, which may have moved)
            if (have[day] && i < candles.length - 1)
                return;
            home.keepPrice(id, day, c);
            added += 1;
        });
        // (a security with no price yet gets the last day's close)
        if (candles.length > 0 && Db.get("Securities", id).LastPrice === null)
            Db.update("Securities", id, { LastPrice: candles[candles.length - 1].close, PriceTime: home.rightNow() });
        home.revalue();
        if (form.recordId === id) {
            form.loadRecord(id);
            statusLabel.text = added + " days of prices downloaded.";
        }
    });
}

// the live price, every half minute while Live is ticked
Timer {
    id: live
    interval: 30000
    repeat: true
    running: liveCheck.checked && !form.isNew
    onTriggered: {
        var id = form.recordId;
        home.updateQuotes([Db.get("Securities", id)], function (ok, result) {
            if (!ok) {
                statusLabel.text = result;
                return;
            }
            var s = Db.get("Securities", id);
            if (form.recordId !== id || s.LastPrice === null)
                return;
            chart.tick(AppDates.display(home.today(), "Date", "MMM d", ""), s.LastPrice);
            form.loadRecord(id);
            statusLabel.text = s.Symbol + " " + home.fmt(s.LastPrice) + " at " + Qt.formatTime(new Date(), "h:mm:ss AP");
        });
    }
}

//// load
rangeCombo.currentIndex = 3;
if (form.args.security)
    form.loadRecord(form.args.security);
else if (!form.first())
    showChart();

//// current
showChart();

//// saved
// a price typed in by hand is today's price
if (form.record.LastPrice !== null && form.record.LastPrice !== undefined) {
    var p = Number(form.record.LastPrice);
    var rows = Db.select("Prices", { where: { Security: form.recordId, Day: home.today() } });
    if (rows.length === 0 || rows[0].Close !== p)
        home.keepPrice(form.recordId, home.today(), rows.length === 0 ? { open: p, high: p, low: p, close: p }
            : { open: rows[0].Open, high: Math.max(rows[0].High, p), low: Math.min(rows[0].Low, p), close: p, volume: rows[0].Volume || undefined });
    home.revalue();
    showChart();
}

//// rangeCombo.changed
showChart();

//// averageCheck.clicked
showStudies();

//// bandsCheck.clicked
showStudies();

//// volumeCheck.clicked
showStudies();

//// rsiCheck.clicked
showStudies();

//// downloadButton.clicked
download();

//// chart.clicked
if (chart.clickedItem)
    statusLabel.text = chart.clickedLabel + ":  open " + home.fmt(chart.clickedItem.open) + "  high " + home.fmt(chart.clickedItem.high)
        + "  low " + home.fmt(chart.clickedItem.low) + "  close " + home.fmt(chart.clickedItem.close);
