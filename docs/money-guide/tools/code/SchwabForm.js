//// (General)
readonly property var home: App.form("HomeForm")
property bool busy: false
// (each account's cash, as Schwab has it, while a download is under way)
property var cashNow: ({})
readonly property var kinds: ({ EQUITY: "Stock", COLLECTIVE_INVESTMENT: "ETF", MUTUAL_FUND: "Mutual Fund",
                                FIXED_INCOME: "Bond", OPTION: "Option", CASH_EQUIVALENT: "Money Market" })
readonly property var allTypes: ["TRADE", "RECEIVE_AND_DELIVER", "DIVIDEND_OR_INTEREST", "ACH_RECEIPT",
    "ACH_DISBURSEMENT", "CASH_RECEIPT", "CASH_DISBURSEMENT", "ELECTRONIC_FUND", "WIRE_OUT", "WIRE_IN",
    "JOURNAL", "MEMORANDUM", "MARGIN_CALL", "MONEY_MARKET", "SMA_ADJUSTMENT"]

function say(text) {
    var lines = logList.items.slice();
    lines.push(Qt.formatTime(new Date(), "hh:mm:ss") + "  " + text);
    logList.items = lines;
    logList.currentIndex = lines.length - 1;
}
function round2(n) { return Math.round(n * 100) / 100; }

function showState() {
    var signedIn = home.setting("SchwabRefresh") !== "";
    if (!signedIn) {
        stateLabel.text = "Not signed in.";
        stateLabel.foreColor = "#c00000";
    } else {
        // Schwab's sign-in lasts seven days
        var until = new Date(Number(home.setting("SchwabSignedIn", Date.now())) + 7 * 86400000);
        stateLabel.text = "Signed in until " + Qt.formatDateTime(until, "ddd MMM d, h:mm AP") + ".";
        stateLabel.foreColor = "#008000";
    }
    syncButton.enabled = signedIn && !busy;
    signOutButton.enabled = signedIn;
}

// ---- signing in -----------------------------------------------------------

function keepSetup() {
    home.setSetting("SchwabKey", keyBox.text.trim());
    home.setSetting("SchwabSecret", secretBox.text.trim());
    home.setSetting("SchwabCallback", callbackBox.text.trim());
    home.setSetting("SchwabAuto", autoCheck.checked ? "yes" : "no");
}

// step 1: Schwab's own sign-in page, in the browser
function openSignIn() {
    if (keyBox.text.trim() === "" || secretBox.text.trim() === "" || callbackBox.text.trim() === "") {
        form.msgBox("Type the App Key, the Secret and the Callback URL of your app at developer.schwab.com.", "Schwab", "warning");
        return;
    }
    keepSetup();
    Qt.openUrlExternally(home.schwabBase() + "/v1/oauth/authorize?client_id=" + encodeURIComponent(keyBox.text.trim())
                         + "&redirect_uri=" + encodeURIComponent(callbackBox.text.trim()));
    say("The browser is opening Schwab. Sign in there, then copy the address it ends on and paste it below.");
    pasteBox.text = "";
    pasteBox.forceActiveFocus();
}

// step 2: the address the browser ended on has the code; swap it for tokens
function finishSignIn() {
    var pasted = pasteBox.text.trim(), found = /[?&]code=([^&#]+)/.exec(pasted);
    var code = found ? decodeURIComponent(found[1]) : decodeURIComponent(pasted);
    if (code === "") {
        form.msgBox("Paste the whole address the browser ended on: it starts with your Callback URL and has code= in it.",
                    "Schwab", "warning");
        return;
    }
    keepSetup();
    say("Finishing the sign-in...");
    home.schwabTokens("grant_type=authorization_code&code=" + encodeURIComponent(code)
                      + "&redirect_uri=" + encodeURIComponent(callbackBox.text.trim()), function (ok, why) {
        if (!ok) {
            say("The sign-in didn't work: " + why);
            say("(The code only lasts about 30 seconds: sign in again and paste it straight away.)");
            return;
        }
        home.setSetting("SchwabSignedIn", Date.now());
        pasteBox.text = "";
        say("Signed in.");
        showState();
        syncAll();
    });
}

// ---- downloading ------------------------------------------------------------

function finish(problem) {
    busy = false;
    if (problem)
        say(problem);
    else
        home.setSetting("SchwabLastSync", home.rightNow());
    home.refreshHome();
    showState();
}

// everything: the accounts, what they hold, their transactions, then prices
function syncAll() {
    if (busy)
        return;
    busy = true;
    showState();
    say("Downloading from Schwab...");
    home.schwabGet("/trader/v1/accounts/accountNumbers", function (ok, numbers) {
        if (!ok) {
            finish(numbers);
            return;
        }
        linkAccounts(numbers);
        home.schwabGet("/trader/v1/accounts?fields=positions", function (ok2, accounts) {
            if (!ok2) {
                finish(accounts);
                return;
            }
            var ids = [];
            cashNow = ({});
            accounts.forEach(function (entry) {
                var id = takeAccount(entry);
                if (id !== null)
                    ids.push(id);
            });
            takeTransactions(ids, 0, function () {
                home.updateQuotes(Db.select("Securities"), function (ok3, result) {
                    say(ok3 ? result + " prices updated." : "Prices: " + result);
                    finish("");
                    say("Done.");
                });
            });
        });
    });
}

// Schwab's accounts, matched to ours by account number (made, if they're new)
function linkAccounts(numbers) {
    numbers.forEach(function (n) {
        var number = String(n.accountNumber);
        var rows = Db.select("Accounts", { where: { AccountNumber: number } });
        if (rows.length > 0) {
            if (rows[0].SchwabHash !== n.hashValue)
                Db.update("Accounts", rows[0].id, { SchwabHash: n.hashValue });
            return;
        }
        var r = Db.insert("Accounts", { Name: "Schwab ..." + number.slice(-4), Type: "Investment", Institution: "Charles Schwab",
                                        AccountNumber: number, SchwabHash: n.hashValue, OpeningBalance: 0 });
        say(r.ok ? "New account: Schwab ..." + number.slice(-4) + " (rename it in Accounts > Account List)."
                 : "Account ..." + number.slice(-4) + " wasn't added: " + r.messages.join(" "));
    });
}

// one account of Schwab's answer: its positions become our Holdings
function takeAccount(entry) {
    var sa = entry.securitiesAccount;
    if (!sa)
        return null;
    var rows = Db.select("Accounts", { where: { AccountNumber: String(sa.accountNumber) } });
    if (rows.length === 0)
        return null;
    var account = rows[0], stamp = home.rightNow(), held = 0;
    Db.select("Holdings", { where: { Account: account.id } }).forEach(function (h) { Db.remove("Holdings", h.id); });
    (sa.positions || []).forEach(function (p) {
        var ins = p.instrument || {}, shares = (p.longQuantity || 0) - (p.shortQuantity || 0);
        if (!ins.symbol || shares === 0)
            return;
        var type = kinds[ins.assetType] || "Other", each = type === "Option" ? 100 : 1;
        var security = home.findSecurity(ins.symbol, ins.description, type);
        if (security === null) {
            say("Skipped " + ins.symbol + ": Securities won't take that symbol.");
            return;
        }
        Db.insert("Holdings", { Account: account.id, Security: security, Quantity: shares,
                                CostBasis: round2((p.averagePrice || 0) * shares * each),
                                MarketValue: round2(p.marketValue || 0), AsOf: stamp });
        if (p.marketValue)
            Db.update("Securities", security, { LastPrice: Math.round(p.marketValue / shares / each * 10000) / 10000, PriceTime: stamp });
        held += p.marketValue || 0;
    });
    // its cash: what the account is worth, less what it holds
    var now = sa.currentBalances || {}, start = sa.initialBalances || {}, all = entry.aggregatedBalance || {};
    var worth = all.liquidationValue !== undefined ? all.liquidationValue
              : now.liquidationValue !== undefined ? now.liquidationValue : start.liquidationValue;
    var cash = worth !== undefined ? worth - held
             : now.cashBalance !== undefined ? now.cashBalance
             : now.totalCash !== undefined ? now.totalCash : (start.cashBalance || 0);
    var kept = cashNow;
    kept[account.id] = round2(cash);
    cashNow = kept;
    return account.id;
}

// each account's transactions, one account after another
function takeTransactions(ids, i, done) {
    if (i >= ids.length) {
        done();
        return;
    }
    var account = Db.get("Accounts", ids[i]);
    // from a week before the last download (the first time: as far back as Schwab goes, a year)
    var from = account.LastSync ? home.addDays(account.LastSync, -7) : home.addDays(home.today(), -364);
    var path = "/trader/v1/accounts/" + account.SchwabHash + "/transactions?startDate="
             + encodeURIComponent(home.toDate(from).toISOString()) + "&endDate=" + encodeURIComponent(new Date().toISOString())
             + "&types=" + allTypes.join(",");
    home.schwabGet(path, function (ok, list) {
        if (!ok) {
            say(account.Name + ": " + list);
        } else {
            var added = 0, sum = 0;
            list.forEach(function (t) {
                if (takeTransaction(account, t))
                    added += 1;
            });
            // the opening balance is whatever makes the cash come out as Schwab has it
            Db.select("Transactions", { where: { Account: account.id } }).forEach(function (t) { sum += t.Amount; });
            Db.update("Accounts", account.id, { OpeningBalance: round2(cashNow[account.id] - sum), LastSync: home.rightNow() });
            say(account.Name + ": " + added + " new " + (added === 1 ? "transaction" : "transactions")
                + ", cash " + home.fmt(cashNow[account.id]) + ".");
        }
        takeTransactions(ids, i + 1, done);
    });
}

// one of Schwab's transactions into our table (false: it's there already, or isn't one of ours)
function takeTransaction(account, t) {
    if (t.status && t.status !== "VALID")
        return false;
    var key = String(t.activityId);
    if (Db.count("Transactions", { SchwabId: key }) > 0)
        return false;
    var amount = round2(t.netAmount || 0), item = null, fees = 0;
    (t.transferItems || []).forEach(function (it) {
        var ins = it.instrument || {};
        if (it.feeType)
            fees += Math.abs(it.cost || it.amount || 0);
        else if (ins.symbol && ins.assetType !== "CURRENCY" && item === null)
            item = it;
    });
    var record = { Account: account.id, Date: String(t.tradeDate || t.time || home.today()).substring(0, 10),
                   Memo: String(t.description || t.type || "").substring(0, 120), Amount: amount,
                   Cleared: "Cleared", Source: "Schwab", SchwabId: key };
    var action = "";
    if (t.type === "TRADE" && item)
        action = (amount !== 0 ? amount < 0 : item.amount > 0) ? "Buy" : "Sell";
    else if (t.type === "RECEIVE_AND_DELIVER" && item)
        action = item.amount >= 0 ? "Add Shares" : "Remove Shares";
    else if (t.type === "DIVIDEND_OR_INTEREST")
        action = /interest|\bint\b/i.test(t.description || "") ? "Interest" : "Dividend";
    if (action === "" && amount === 0)
        return false;
    if (action !== "") {
        record.Action = action;
        if (item) {
            var ins = item.instrument;
            record.Security = home.findSecurity(ins.symbol, ins.description, kinds[ins.assetType] || "Other");
            if (action !== "Dividend" && action !== "Interest") {
                record.Quantity = Math.abs(item.amount || 0);
                record.Price = item.price || 0;
            }
        }
        if (fees > 0)
            record.Commission = round2(fees);
        var wanted = action === "Dividend" ? "Investment Income : Dividends" : action === "Interest" ? "Investment Income : Interest" : "";
        var found = wanted === "" ? [] : Db.select("Categories", { where: { Name: wanted } });
        if (found.length > 0)
            record.Category = found[0].id;
    }
    var r = Db.insert("Transactions", record);
    if (!r.ok)
        say("Not taken: " + record.Memo + " (" + r.messages.join(" ") + ")");
    return r.ok;
}

//// load
keyBox.text = home.setting("SchwabKey");
secretBox.text = home.setting("SchwabSecret");
callbackBox.text = home.setting("SchwabCallback", "https://127.0.0.1");
autoCheck.checked = home.setting("SchwabAuto") === "yes";
showState();

//// connectButton.clicked
openSignIn();

//// finishButton.clicked
finishSignIn();

//// pasteBox.accepted
finishSignIn();

//// autoCheck.clicked
home.setSetting("SchwabAuto", autoCheck.checked ? "yes" : "no");

//// syncButton.clicked
syncAll();

//// signOutButton.clicked
home.setSetting("SchwabAccess", "");
home.setSetting("SchwabRefresh", "");
home.setSetting("SchwabAccessUntil", 0);
say("Signed out: the tokens are forgotten.");
showState();

//// closeButton.clicked
form.close();
