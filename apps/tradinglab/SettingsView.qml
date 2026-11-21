import QtQuick
import Win95Ui

// Settings: time zone and clock, Claude's default model and effort, the Anthropic and FMP keys (kept in the desktop
// keyring), Test connection with the table of data feeds, what the FMP plan includes and "Hide features my plan does
// not include", the data cache, the FMP MCP status and the data folder. Upstream's update-check settings are left out:
// Wayland 95 updates its programs itself. Port of views/SettingsView.tsx.
Item {
    id: view
    property var app

    readonly property string marketTz: "America/New_York"
    property var chat: ({ models: [], efforts: [], defaultPrefs: { model: "", effort: "" } })
    property var capsInfo: []          // content fmp-caps.json caps: [{id, label, lost}]
    property var claude: ({ model: "", effort: "" })
    property var anthropic: null       // key status {source, encrypted}
    property var fmp: null
    property bool testing: false
    property var tests: null
    property string testErr: ""
    property var mcp: null
    property var cache: null
    property var caps: null            // {checkedAt, unavailable}
    property real now: Date.now() / 1000
    Timer { interval: 30000; running: true; repeat: true; onTriggered: view.now = Date.now() / 1000 }

    // ---------------------------------------------------------------- Claude defaults (shared/chat.ts)
    function modelInfo(id) { return chat.models.find(function (m) { return m.id === id; }) || chat.models[0] || { reasoning: false }; }
    function normalizePrefs(p) {
        const o = p && typeof p === "object" ? p : {};
        const model = chat.models.some(function (m) { return m.id === o.model; }) ? o.model : chat.defaultPrefs.model;
        const effort = chat.efforts.some(function (e) { return e.id === o.effort; }) ? o.effort
                     : model === chat.defaultPrefs.model ? chat.defaultPrefs.effort : modelInfo(model).defaultEffort;
        return { model: model, effort: effort };
    }
    function saveClaude(p) { claude = p; api.invoke("settings:set", ["chatDefaults", p]); app.chatDefaultsChanged(p); }

    // ---------------------------------------------------------------- keys, cache, plan
    function refreshAnthropic() { api.invoke("chat:key:status", [], function (s) { anthropic = s; }); }
    function refreshFmp() { api.invoke("fmp:key:status", [], function (s) { fmp = s; }); }
    function refreshCache() { api.invoke("fmp:cache:stats", [], function (s) { cache = s; }); }
    function refreshCaps() { api.invoke("fmp:caps:get", [], function (c) { caps = c && c.unavailable ? c : null; }); }
    function keyText(s, envName, none) {
        if (!s) return "";
        if (s.source === "stored") return "A key is saved (stored in the keyring).";
        if (s.source === "env") return "Using the " + envName + " environment variable.";
        return none;
    }
    function saveFmp() {
        const k = fmpField.text.trim();
        if (!k) return;
        api.invoke("fmp:key:set", [k], function () {
            fmpField.text = "";
            tests = null;
            refreshFmp();
            app.refreshLive();
            app.toast("success", "FMP API key saved", { title: "Market data", duration: 5000 });
        });
    }
    function clearFmp() {
        api.invoke("fmp:key:clear", [], function () {
            tests = null;
            refreshFmp();
            app.refreshLive();
            app.toast("info", "FMP API key removed. The app will show sample data.");
        });
    }
    function saveAnthropic() {
        const k = anthropicField.text.trim();
        if (!k) return;
        api.invoke("chat:key:set", [k], function () { anthropicField.text = ""; refreshAnthropic(); app.toast("success", "Anthropic API key saved"); });
    }
    function clearAnthropic() { api.invoke("chat:key:clear", [], function () { refreshAnthropic(); app.toast("info", "Anthropic API key removed"); }); }

    function runTest() {
        testing = true;
        tests = null;
        testErr = "";
        api.invoke("fmp:test", [], function (r) {
            const finish = function () {
                api.invoke("fmp:mcp:status", [], function (m) {
                    mcp = m;
                    testing = false;
                    refreshCaps();
                    refreshCache();
                    // the "settings-test" screenshot shows the table
                    if (testScript === "settings-test") Qt.callLater(function () { page.scrollTo(planBox.mapToItem(page.column, 0, 0).y - 8); });
                });
            };
            if (!r.ok) {
                testErr = r.error;
                app.toast("error", r.error, { title: "Connection failed" });
                finish();
                return;
            }
            // options are not an FMP product; check the separate delayed options feed alongside
            api.invoke("options:chain", ["AAPL", true], function (oc) {
                const optRow = oc && oc.ok
                    ? { name: "Options chain (Cboe, delayed)", path: "cdn.cboe.com options/AAPL.json", ok: true, detail: oc.data.contracts.length + " contracts, " + oc.data.expirations.length + " expirations" }
                    : { name: "Options chain (Cboe, delayed)", path: "cdn.cboe.com options/AAPL.json", ok: false, detail: oc ? oc.error : "no answer" };
                const rowsAll = r.data.concat([optRow]);
                tests = rowsAll;
                const good = rowsAll.filter(function (t) { return t.ok; }).length;
                const notInPlan = rowsAll.filter(function (t) { return t.status === "plan"; }).length;
                if (good === rowsAll.length) app.toast("success", "All " + good + " data feeds responded", { title: "Connection OK" });
                else if (notInPlan > 0) app.toast("info", good + " of " + rowsAll.length + " data feeds responded. " + notInPlan + " " + (notInPlan === 1 ? "is" : "are") + " not in your FMP plan, so the features that need " + (notInPlan === 1 ? "it" : "them") + " are hidden.", { title: "Connected" });
                else app.toast("warning", good + " of " + rowsAll.length + " data feeds responded. Expand the list below for details.", { title: "Partly connected" });
                finish();
            });
        });
    }
    // the finest bars the plan can fill working orders against (shared/fmpCaps.ts fillGrain)
    function fillGrain(unavailable) {
        const grains = [["bars1min", "1-minute"], ["bars5min", "5-minute"], ["bars15min", "15-minute"], ["bars30min", "30-minute"], ["bars1hour", "1-hour"]];
        const g = grains.find(function (x) { return unavailable.indexOf(x[0]) < 0; });
        return g ? { label: g[1] } : null;
    }
    function setHide(v) {
        app.hidePlanGaps = v;
        api.invoke("settings:set", ["hidePlanGaps", v]);
        api.invoke("fmp:caps:get", [], function (c) { app.applyCaps(c); });
    }

    // ---------------------------------------------------------------- display (time zone, clock)
    readonly property string tzMode: !app || !app.display.timeZone ? "local" : app.display.timeZone === marketTz ? "market" : "custom"
    function setDisplay(p) { app.updateDisplay(Object.assign({}, app.display, p)); }
    readonly property string zone: app && app.display.timeZone ? app.display.timeZone : TimeZones.current

    Component.onCompleted: {
        api.invoke("content:get", ["chat.json"], function (c) {
            if (c) chat = c;
            api.invoke("settings:get", ["chatDefaults"], function (p) { claude = normalizePrefs(p); });
        });
        api.invoke("content:get", ["fmp-caps.json"], function (c) { if (c) capsInfo = c.caps; });
        refreshAnthropic();
        refreshFmp();
        refreshCache();
        refreshCaps();
        if (testScript === "settings-test") runTest();
    }
    Connections {
        target: api
        function onEvent(channel, payload) { if (channel === "fmp:caps:changed") view.caps = payload && payload.unavailable ? payload : null; }
    }

    // ---------------------------------------------------------------- layout
    DocTitleBar { id: titleBar; width: parent.width; title: "Settings" }

    DocScroll {
        id: page
        y: titleBar.height + 4
        width: parent.width
        height: parent.height - y
        well: false
        padding: 8
        spacing: 14

        Section {
            title: "Time & display"
            Row {
                spacing: 8
                FormLabel { text: "Show times in" }
                ComboBox95 {
                    width: 330
                    popupMenu: menu
                    model: ["Market time — New York", "My local time — " + TimeZones.current, "Another time zone…"]
                    currentIndex: ["market", "local", "custom"].indexOf(view.tzMode)
                    onActivated: (i) => view.setDisplay({ timeZone: i === 0 ? view.marketTz : i === 1 ? "" : (view.tzMode === "custom" ? view.app.display.timeZone : "UTC") })
                }
            }
            Row {
                visible: view.tzMode === "custom"
                spacing: 8
                FormLabel { text: "Time zone" }
                ComboBox95 {
                    id: regionBox
                    width: 120
                    popupMenu: menu
                    model: TimeZones.regions
                    currentIndex: Math.max(0, TimeZones.regions.indexOf(view.zone === "UTC" ? "UTC" : TimeZones.regionOf(view.zone)))
                    onActivated: (i) => {
                        const zs = TimeZones.zones(TimeZones.regions[i]);
                        view.setDisplay({ timeZone: zs.length ? zs[0].id : "UTC" });
                    }
                }
                ComboBox95 {
                    readonly property var zs: TimeZones.zones(TimeZones.regions[regionBox.currentIndex] || "UTC")
                    width: 300
                    popupMenu: menu
                    model: zs.map(function (z) { return z.label; })
                    currentIndex: Math.max(0, zs.findIndex(function (z) { return z.id === view.zone; }))
                    onActivated: (i) => view.setDisplay({ timeZone: zs[i].id })
                }
            }
            Row {
                spacing: 8
                FormLabel { text: "Clock" }
                ComboBox95 {
                    width: 200
                    popupMenu: menu
                    model: ["24-hour (14:30)", "12-hour (2:30 PM)"]
                    currentIndex: view.app && view.app.display.hour12 !== false ? 1 : 0
                    onActivated: (i) => view.setDisplay({ hour12: i === 1 })
                }
            }
            Muted {
                text: "Now: " + TimeZones.timeIn(view.zone, !(view.app && view.app.display.hour12 !== false)) + " (" + view.zone.replace(/_/g, " ") + ") · US market "
                      + (core.isMarketOpen(view.now) ? "open" : "closed") + " (regular hours 09:30–16:00 New York time; holidays are not tracked)."
                      + (view.zone !== view.marketTz ? "\nIntraday charts show bars in this zone. Daily, weekly and monthly bars are dates and are unaffected." : "")
            }
        }

        Section {
            title: "Claude model and effort"
            Muted { text: "What the chat starts with. You are billed per use, and bigger models and higher effort cost more, so the default is the cheaper Sonnet at Medium. The pickers at the bottom of the chat panel change it for the current session only." }
            Row {
                spacing: 8
                FormLabel { text: "Default model" }
                ComboBox95 {
                    width: 380
                    popupMenu: menu
                    model: view.chat.models.map(function (m) { return m.label + " · " + m.desc; })
                    currentIndex: Math.max(0, view.chat.models.findIndex(function (m) { return m.id === view.claude.model; }))
                    onActivated: (i) => view.saveClaude(view.normalizePrefs({ model: view.chat.models[i].id }))
                }
            }
            Row {
                spacing: 8
                FormLabel { text: "Default effort" }
                ComboBox95 {
                    width: 380
                    enabled: !!view.modelInfo(view.claude.model).reasoning
                    popupMenu: menu
                    model: view.chat.efforts.map(function (x) { return x.label + " · " + x.desc; })
                    currentIndex: Math.max(0, view.chat.efforts.findIndex(function (x) { return x.id === view.claude.effort; }))
                    onActivated: (i) => view.saveClaude({ model: view.claude.model, effort: view.chat.efforts[i].id })
                }
            }
            Muted { visible: view.chat.models.length > 0 && !view.modelInfo(view.claude.model).reasoning; text: "This model has no effort setting." }
        }

        Section {
            title: "Anthropic API key (Claude chat)"
            Muted { text: view.keyText(view.anthropic, "ANTHROPIC_API_KEY", "No key configured.") }
            DocField { id: anthropicField; width: 380; password: true; placeholder: "sk-ant-…"; onAccepted: view.saveAnthropic() }
            Row {
                spacing: 6
                Button95 { isDefault: true; text: "Save key"; enabled: anthropicField.text.trim() !== ""; onClicked: view.saveAnthropic() }
                Button95 { visible: !!view.anthropic && view.anthropic.source === "stored"; text: "Remove saved key"; onClicked: view.clearAnthropic() }
            }
        }

        Section {
            title: "Market data key (Financial Modeling Prep)"
            Muted { text: view.keyText(view.fmp, "FMP_API_KEY", "No key configured. The app shows sample data until you add one.") }
            DocField { id: fmpField; width: 380; password: true; placeholder: "FMP API key"; onAccepted: view.saveFmp() }
            Row {
                spacing: 6
                Button95 { isDefault: true; text: "Save key"; enabled: fmpField.text.trim() !== ""; onClicked: view.saveFmp() }
                Button95 { text: view.testing ? "Testing…" : "Test connection"; enabled: !view.testing && !!view.fmp && view.fmp.source !== "none"; onClicked: view.runTest() }
                Button95 { visible: !!view.fmp && view.fmp.source === "stored"; text: "Remove saved key"; onClicked: view.clearFmp() }
            }
            Muted {
                text: "Data cache: " + (view.cache ? view.cache.entries + " saved response" + (view.cache.entries === 1 ? "" : "s") + " (" + (view.cache.bytes / 1e6).toFixed(1) + " MB)" : "…")
                      + ". Responses are kept between sessions and reused while fresh (seconds for live prices, hours for analyst and company data), so screens don't refetch from FMP each time. If FMP is unreachable, the last saved copy is shown."
            }
            Button95 {
                text: "Clear cache"
                onClicked: api.invoke("fmp:cache:clear", [], function () { view.refreshCache(); view.app.toast("success", "Data cache cleared"); })
            }
            Notice { width: page.availableWidth; kind: "error"; text: Util.plain(view.testErr) }
            Bevel {
                visible: !!view.mcp
                width: page.availableWidth
                height: mcpRow.height + 4
                style: "sunken"
                face: Theme.window
                TestRow {
                    id: mcpRow
                    x: 2
                    y: 2
                    width: parent.width - 4
                    row: view.mcp ? { ok: view.mcp.ok, name: "Claude data tools (MCP)", path: "financialmodelingprep.com/mcp",
                                      detail: view.mcp.ok ? view.mcp.count + " tools via " + view.mcp.transport : view.mcp.error,
                                      fields: view.mcp.ok ? (view.mcp.sample || []).concat((view.mcp.count || 0) > 12 ? ["…"] : []) : undefined } : ({})
                }
            }
            GroupBox95 {
                id: planBox
                visible: !!view.fmp && view.fmp.source !== "none" && !!view.caps
                width: page.availableWidth
                height: planCol.implicitHeight + 30
                title: "What your FMP plan includes"
                Column {
                    id: planCol
                    width: parent.width
                    spacing: 6
                    Muted {
                        width: parent.width
                        text: view.caps ? "Checked " + Util.dateTime(view.caps.checkedAt) + ". It is checked again when you save a key or click Test connection, and automatically if something turns out not to be in your plan." : ""
                    }
                    Grid {
                        columns: 2
                        columnSpacing: 12
                        rowSpacing: 2
                        Repeater {
                            model: view.capsInfo
                            Text {
                                required property var modelData
                                readonly property bool missing: !!view.caps && view.caps.unavailable.indexOf(modelData.id) >= 0
                                width: (planCol.width - 12) / 2
                                elide: Text.ElideRight
                                textFormat: Text.StyledText
                                text: (missing ? "<font color=\"#b00000\">✗</font> " : "<font color=\"#007000\">✓</font> ") + Util.plain(modelData.label)
                                      + (missing ? " <font color=\"#808080\">· " + Util.plain(modelData.lost) + "</font>" : "")
                                font.family: Theme.fontFamily
                                font.pixelSize: Theme.fontSize
                                color: Theme.text
                                ToolTipArea { text: parent.missing ? "Not included: " + parent.modelData.lost : "" }
                            }
                        }
                    }
                    Muted {
                        width: parent.width
                        text: {
                            if (!view.caps) return "";
                            const g = view.fillGrain(view.caps.unavailable);
                            return g ? "Limit and stop orders are filled against " + g.label + " bars." : "There are no intraday bars on this plan, so only plain market orders can be filled.";
                        }
                    }
                    CheckBox95 {
                        text: "Hide features my plan does not include"
                        checked: !!view.app && view.app.hidePlanGaps
                        onToggled: (c) => view.setHide(c)
                    }
                }
            }
            Bevel {
                visible: !!view.tests
                width: page.availableWidth
                height: testCol.implicitHeight + 4
                style: "sunken"
                face: Theme.window
                Column {
                    id: testCol
                    x: 2
                    y: 2
                    width: parent.width - 4
                    Repeater {
                        model: view.tests || []
                        TestRow { required property var modelData; width: testCol.width; row: modelData }
                    }
                }
            }
        }

        Section {
            title: "Data folder"
            Muted { text: "Trading Lab keeps its database (accounts, orders, journal, watchlists, strategies, chats and the data cache) here. File > Back Up Data saves a copy of it." }
            TextField95 { width: Math.min(page.availableWidth, 520); text: api.dataDir(); input.readOnly: true; Component.onCompleted: input.cursorPosition = 0 }
            Muted { text: "Trading Lab is updated together with Wayland 95, so it has no update check of its own." }
        }
    }

    // ---------------------------------------------------------------- pieces
    component Section: Column {
        property string title: ""
        width: parent ? parent.width : 0
        spacing: 6
        Heading95 { level: 2; text: parent.title }
        Rectangle { width: parent.width; height: 1; color: Theme.shadow }
    }
    component FormLabel: Label95 { width: 110; height: 21; verticalAlignment: Text.AlignVCenter }
    component Muted: Label95 { width: parent ? parent.width : 0; wrapMode: Text.Wrap; dim: true; visible: text !== "" }
    // one data feed in the Test connection list: ✓/✗, name, path, and the detail (click it for the fields it returned)
    component TestRow: Item {
        id: tr
        property var row: ({})
        property bool open: false
        height: Math.max(20, fieldsText.visible ? fieldsText.y + fieldsText.implicitHeight + 4 : 20)
        Text {
            x: 6
            y: 3
            text: tr.row.ok ? "✓" : "✗"
            color: tr.row.ok ? "#007000" : "#b00000"
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontSize
            font.bold: true
        }
        Text {
            x: 22
            y: 3
            width: 200
            elide: Text.ElideRight
            textFormat: Text.PlainText
            text: tr.row.name || ""
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontSize
            color: Theme.text
        }
        Text {
            x: 226
            y: 3
            width: 200
            elide: Text.ElideRight
            textFormat: Text.PlainText
            text: tr.row.path || ""
            font.family: Theme.monoFamily
            font.pixelSize: Theme.fontSize - 1
            color: Theme.shadow
        }
        Text {
            id: detailText
            x: 430
            y: 3
            width: tr.width - x - 6
            elide: Text.ElideRight
            textFormat: Text.PlainText
            text: (tr.row.fields ? (tr.open ? "▾ " : "▸ ") : "") + (tr.row.detail || "")
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontSize
            color: tr.row.fields ? "#0000c0" : Theme.shadow
            MouseArea { anchors.fill: parent; enabled: !!tr.row.fields; cursorShape: enabled ? Qt.PointingHandCursor : Qt.ArrowCursor; onClicked: tr.open = !tr.open }
        }
        Text {
            id: fieldsText
            visible: tr.open && !!tr.row.fields
            x: 22
            y: 20
            width: tr.width - x - 6
            wrapMode: Text.Wrap
            textFormat: Text.PlainText
            text: tr.row.fields ? tr.row.fields.join(", ") : ""
            font.family: Theme.monoFamily
            font.pixelSize: Theme.fontSize - 1
            color: Theme.shadow
        }
        Rectangle { y: parent.height - 1; width: parent.width; height: 1; color: Theme.face }
    }

    Menu95 { id: menu }
}
