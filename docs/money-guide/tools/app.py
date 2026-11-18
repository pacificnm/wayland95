"""Wayland Money, described once: its tables, its forms and their code.

build.py makes two things from this: sample/money.w95proj (the finished
project, which the tests run) and the guide's chapters (the tables of
fields and elements, and the code, are written out from here, so the
guide and the project that was tested can't drift apart).
"""
import os
import re

HERE = os.path.dirname(os.path.abspath(__file__))

# --------------------------------------------------------------------------
# tables
# --------------------------------------------------------------------------

def F(name, type, label=None, **rules):
    f = {"name": name, "type": type}
    if label:
        f["label"] = label
    f.update(rules)
    return f

FREQUENCIES = ["Once", "Weekly", "Every two weeks", "Twice a month", "Monthly",
               "Every three months", "Every six months", "Yearly"]
ACCOUNT_TYPES = ["Checking", "Savings", "Credit Card", "Cash", "Investment", "Retirement", "Loan", "Asset", "Liability"]
ACTIONS = ["Buy", "Sell", "Dividend", "Interest", "Reinvest", "Add Shares", "Remove Shares", "Fee"]

TABLES = [
    {"name": "Accounts", "label": "Accounts", "fields": [
        F("Name", "text", required=True, unique=True, maxLength=60),
        F("Type", "choice", choices=ACCOUNT_TYPES, default="Checking", required=True),
        F("Institution", "text", maxLength=60),
        F("AccountNumber", "text", "Account Number", maxLength=40),
        F("OpeningBalance", "currency", "Opening Balance", default="0"),
        F("OpeningDate", "date", "Opening Date", default="today"),
        F("CreditLimit", "currency", "Credit Limit", validator="not_negative"),
        F("InterestRate", "decimal", "Interest Rate", decimals=3, validator="percentage"),
        F("Closed", "boolean"),
        F("SchwabHash", "text", "Schwab Link"),
        F("LastSync", "datetime", "Last Download"),
        F("Notes", "memo"),
    ]},
    {"name": "Categories", "label": "Categories", "fields": [
        F("Name", "text", required=True, unique=True, maxLength=60),
        F("Kind", "choice", choices=["Expense", "Income"], default="Expense", required=True),
        F("TaxRelated", "boolean", "Tax-related"),
        F("Notes", "text"),
    ]},
    {"name": "Payees", "label": "Payees", "fields": [
        F("Name", "text", required=True, unique=True, maxLength=80),
        F("Category", "lookup", "Usual Category", lookupTable="Categories"),
        F("Phone", "phone"),
        F("Email", "email", "E-mail"),
        F("Website", "url"),
        F("Address", "memo"),
        F("Notes", "memo"),
    ]},
    {"name": "Securities", "label": "Securities", "fields": [
        F("Symbol", "text", required=True, unique=True, maxLength=32, pattern="[A-Z0-9 ./$-]+",
          message="A symbol is capitals and digits: AAPL, BRK/B."),
        F("Company", "text", maxLength=120),
        F("Type", "choice", choices=["Stock", "ETF", "Mutual Fund", "Bond", "Option", "Money Market", "Other"],
          default="Stock", required=True),
        F("LastPrice", "decimal", "Last Price", decimals=4, validator="not_negative"),
        F("Change", "decimal", decimals=4),
        F("ChangePercent", "decimal", "Change %", decimals=2),
        F("PriceTime", "datetime", "Price As Of"),
        F("Watch", "boolean", "On Watch List"),
    ]},
    {"name": "Transactions", "label": "Transactions", "fields": [
        F("Account", "lookup", lookupTable="Accounts", required=True),
        F("Date", "date", required=True, default="today"),
        F("Number", "text", "Num", maxLength=12),
        F("Payee", "lookup", lookupTable="Payees"),
        F("Category", "lookup", lookupTable="Categories"),
        F("Memo", "text", maxLength=120),
        F("Amount", "currency", required=True, validator="expression", rule="value !== 0 || record.Action",
          message="Amount can't be 0."),
        F("Cleared", "choice", "Status", choices=["Unreconciled", "Cleared", "Reconciled"], default="Unreconciled"),
        F("TransferAccount", "lookup", "Transfer Account", lookupTable="Accounts",
          validator="different_from", validatorArg="Account", message="A transfer goes to another account."),
        F("TransferId", "integer", "Transfer's Other Half"),
        F("IsSplit", "boolean", "Split"),
        F("Action", "choice", choices=ACTIONS),
        F("Security", "lookup", lookupTable="Securities", validator="only_if", validatorArg="Action"),
        F("Quantity", "decimal", "Shares", decimals=4, validator="not_negative"),
        F("Price", "decimal", decimals=4, validator="not_negative"),
        F("Commission", "currency", validator="not_negative"),
        F("Source", "choice", choices=["Manual", "Schwab", "Bill"], default="Manual"),
        F("SchwabId", "text", "Schwab Id", unique=True),
    ]},
    {"name": "Splits", "label": "Splits", "fields": [
        F("Transaction", "lookup", lookupTable="Transactions", required=True),
        F("Category", "lookup", lookupTable="Categories", required=True),
        F("Memo", "text", maxLength=120),
        F("Amount", "currency", required=True, validator="not_zero"),
    ]},
    {"name": "Bills", "label": "Bills and Deposits", "fields": [
        F("Name", "text", required=True, maxLength=80),
        F("Kind", "choice", choices=["Bill", "Deposit", "Transfer"], default="Bill", required=True),
        F("Account", "lookup", lookupTable="Accounts", required=True),
        F("ToAccount", "lookup", "To Account", lookupTable="Accounts", validator="different_from", validatorArg="Account",
          message="A transfer goes to another account."),
        F("Payee", "lookup", lookupTable="Payees"),
        F("Category", "lookup", lookupTable="Categories"),
        F("Amount", "currency", required=True, validator="positive"),
        F("Frequency", "choice", choices=FREQUENCIES, default="Monthly", required=True),
        F("NextDue", "date", "Next Due", required=True, default="today"),
        F("EndDate", "date", "Ends", validator="on_or_after_field", validatorArg="NextDue"),
        F("RemindDays", "integer", "Remind Days", min=0, max=60, default="7"),
        F("AutoEnter", "boolean", "Enter By Itself"),
        F("Active", "boolean", default="yes"),
        F("Memo", "text", maxLength=120),
    ]},
    {"name": "Budgets", "label": "Budgets", "fields": [
        F("Category", "lookup", lookupTable="Categories", required=True, unique=True),
        F("Amount", "currency", "Monthly Amount", required=True, validator="positive"),
        F("Notes", "text"),
    ]},
    {"name": "Holdings", "label": "Holdings", "fields": [
        F("Account", "lookup", lookupTable="Accounts", required=True),
        F("Security", "lookup", lookupTable="Securities", required=True),
        F("Quantity", "decimal", "Shares", decimals=4),
        F("CostBasis", "currency", "Cost Basis"),
        F("MarketValue", "currency", "Market Value"),
        F("AsOf", "datetime", "As Of"),
    ]},
    {"name": "Prices", "label": "Prices", "fields": [
        F("Security", "lookup", lookupTable="Securities", required=True),
        F("Day", "date", required=True),
        F("Open", "decimal", decimals=4),
        F("High", "decimal", decimals=4, validator="at_least_field", validatorArg="Low"),
        F("Low", "decimal", decimals=4),
        F("Close", "decimal", decimals=4, required=True),
        F("Volume", "integer", min=0),
    ]},
    {"name": "WorthHistory", "label": "Net Worth History", "fields": [
        F("Day", "date", required=True, unique=True),
        F("Assets", "currency"),
        F("Liabilities", "currency"),
        F("Worth", "currency", "Net Worth"),
    ]},
    {"name": "Settings", "label": "Settings", "fields": [
        F("Name", "text", required=True, unique=True, maxLength=40),
        F("Value", "memo"),
    ]},
    {"name": "Listing", "label": "Listing (column headings)", "fields":
        [F(n, "text") for n in ["Date", "Num", "Payee", "Category", "Memo", "C", "Account", "Type", "Item", "Symbol", "Name"]]
        + [F(n, "currency") for n in ["Payment", "Deposit", "Balance", "Amount", "Income", "Expenses", "Budget", "Spent",
                                      "Left", "Shares", "Price", "Value", "Cost", "Gain"]]
        + [F("Percent", "currency", "Gain %"), F("Principal", "currency"), F("Interest", "currency")]},
]

def table(name):
    return next(t for t in TABLES if t["name"] == name)

# --------------------------------------------------------------------------
# code: tools/code/<Form>.js, in sections headed "//// name"
# --------------------------------------------------------------------------

def read_code(form):
    path = os.path.join(HERE, "code", form + ".js")
    if not os.path.exists(path):
        return {}
    sections, name, lines = {}, None, []
    for line in open(path, encoding="utf-8").read().split("\n"):
        if line.startswith("//// "):
            if name is not None:
                sections[name] = "\n".join(lines).strip("\n")
            name, lines = line[5:].strip(), []
        else:
            lines.append(line)
    if name is not None:
        sections[name] = "\n".join(lines).strip("\n")
    return sections

# the parts of HomeForm's (General) the finished app has, in order (the
# "-no..." parts are stand-ins a chapter uses until a later one replaces them)
HOME_GENERAL = ["core", "register", "bills", "investing", "schwab", "schwab-start"]

def form_code(form):
    """{ "general": ..., "load": ..., ... } and { element: { event: code } } for the finished app"""
    sections = read_code(form)
    code, events = {}, {}
    if form == "HomeForm":
        code["general"] = "\n\n".join(sections["(General) " + part] for part in HOME_GENERAL)
        sections["load"] = sections["load bills"]
    for name, text in sections.items():
        if name.startswith("(General)"):
            if form != "HomeForm":
                code["general"] = text
        elif " " in name:
            continue                      # (a chapter's earlier version of an event)
        elif "." in name:
            element, event = name.split(".")
            events.setdefault(element, {})[event] = text
        else:
            code[name] = text
    return code, events

# --------------------------------------------------------------------------
# forms
# --------------------------------------------------------------------------

def C(type, name, x, y, w, h, **props):
    return {"type": type, "name": name, "x": x, "y": y, "width": w, "height": h, "props": props, "events": {}}

def L(name, x, y, w, text, **props):
    return C("Label", name, x, y, w, 17, text=text, **props)

def lower_first(s):
    return s[0].lower() + s[1:]

def add_form_for(table_name):
    """what Project > Add Form for <table> draws (apps/studio/Main.qml, addFormForTable)"""
    t = table(table_name)
    controls, y, first = [], 12, ""
    for f in t["fields"]:
        caption, ty = f.get("label") or f["name"], f["type"]
        h = 60 if ty == "memo" else 21
        datey = ty in ("date", "time", "datetime")
        kind = ("CheckBox" if ty == "boolean" else "ComboBox" if ty in ("choice", "lookup")
                else "DateTimePicker" if datey else "TextBox")
        suffix = {"CheckBox": "Check", "ComboBox": "Combo", "DateTimePicker": "Picker", "TextBox": "Box"}[kind]
        name = lower_first(f["name"]) + suffix
        if kind != "CheckBox":
            controls.append(C("Label", lower_first(f["name"]) + "Label", 12, y + 2, 92, 17, text=caption + ":"))
        props = {"field": f["name"]}
        if kind == "CheckBox":
            props["text"] = caption
        if ty == "memo":
            props["multiLine"] = True
        if datey:
            props["mode"] = {"date": "Date", "time": "Time", "datetime": "Date and Time"}[ty]
        controls.append(C(kind, name, 108, y, 180, 17 if kind == "CheckBox" else h, **props))
        if not first and kind == "TextBox":
            first = name
        y += h + 7
    buttons_y = y + 5
    grid_height = max(160, buttons_y + 24 - 12)
    delete = ("if (form.isNew)\n    return;\n"
              "form.msgBox(\"Delete this record?\", \"Delete\", \"question\", [\"Yes\", \"No\"], function (answer) {\n"
              "    if (answer === 0)\n        form.deleteRecord();\n});")
    for i, (name, text, code) in enumerate([
            ("newButton", "New", "form.newRecord();" + ("\n" + first + ".forceActiveFocus();" if first else "")),
            ("saveButton", "Save", "form.save();"),
            ("deleteButton", "Delete", delete)]):
        b = C("Button", name, 12 + i * 92, buttons_y, 84, 24, text=text)
        if name == "saveButton":
            b["props"]["isDefault"] = True
        b["events"]["clicked"] = code
        controls.append(b)
    controls.append(C("DataGrid", "grid", 304, 12, 328, grid_height))
    return {"name": table_name + "Form", "width": 644, "height": 12 + grid_height + 12,
            "props": {"title": t.get("label") or table_name, "table": table_name}, "code": {}, "controls": controls,
            "generated": True}

def control(form, name):
    return next(c for c in form["controls"] if c["name"] == name)

REPORTS = ["Spending by Category", "Income by Category", "Income and Expenses by Month", "Spending by Payee",
           "Net Worth over Time", "Account Balances", "Tax-related Totals", "Portfolio by Kind"]
PERIODS = ["This month", "Last month", "This year", "Last year", "Last 12 months", "All dates"]

HOME_MENU = [
    {"text": "&File", "name": "fileMenu", "items": [
        {"text": "&Find Transactions...", "name": "fileFind", "shortcut": "Ctrl+F", "action": "openForm", "form": "FindForm"},
        {"separator": True},
        {"text": "E&xit", "name": "fileExit", "action": "quit"}]},
    {"text": "&Accounts", "name": "accountsMenu", "items": [
        {"text": "&Account List...", "name": "accountsList", "shortcut": "Ctrl+Shift+A", "action": "openForm", "form": "AccountsForm"},
        {"text": "Open &Register", "name": "accountsRegister", "shortcut": "Ctrl+R"},
        {"text": "&Balance an Account...", "name": "accountsBalance"},
        {"separator": True},
        {"text": "&Payees...", "name": "accountsPayees", "action": "openForm", "form": "PayeesForm"},
        {"text": "&Categories...", "name": "accountsCategories", "action": "openForm", "form": "CategoriesForm"}]},
    {"text": "&Bills", "name": "billsMenu", "items": [
        {"text": "&Bills && Deposits...", "name": "billsList", "shortcut": "Ctrl+B", "action": "openForm", "form": "BillsForm"},
        {"text": "B&udget...", "name": "billsBudget", "action": "openForm", "form": "BudgetForm"}]},
    {"text": "&Investing", "name": "investMenu", "items": [
        {"text": "&Portfolio...", "name": "investPortfolio", "shortcut": "Ctrl+P", "action": "openForm", "form": "PortfolioForm"},
        {"text": "Record a &Trade...", "name": "investTrade"},
        {"text": "&Securities && Prices...", "name": "investSecurities", "action": "openForm", "form": "SecuritiesForm"}]},
    {"text": "&Reports", "name": "reportsMenu", "items": [
        {"text": "&Reports && Charts...", "name": "reportsList", "action": "openForm", "form": "ReportsForm"},
        {"text": "&Loan Planner...", "name": "reportsPlanner", "action": "openForm", "form": "PlannerForm"}]},
    {"text": "&Online", "name": "onlineMenu", "items": [
        {"text": "&Download from Schwab", "name": "onlineSync", "shortcut": "F9"},
        {"text": "Schwab &Setup...", "name": "onlineSetup", "action": "openForm", "form": "SchwabForm"}]},
    {"text": "&Help", "name": "helpMenu", "items": [
        {"text": "Ask the &Advisor...", "name": "helpAdvisor", "shortcut": "F2", "action": "openForm", "form": "AdvisorForm"},
        {"separator": True},
        {"text": "&About Wayland Money", "name": "helpAbout", "action": "openForm", "form": "AboutForm"}]},
]

HOME_TOOLBAR = [
    {"text": "Accounts", "name": "accountsTool", "glyph": "▤", "toolTip": "The list of accounts", "action": "openForm", "form": "AccountsForm"},
    {"text": "Register", "name": "registerTool", "glyph": "✎", "toolTip": "The selected account's register"},
    {"text": "Find", "name": "findTool", "glyph": "⌕", "toolTip": "Find transactions", "action": "openForm", "form": "FindForm"},
    {"separator": True},
    {"text": "Bills", "name": "billsTool", "glyph": "✉", "toolTip": "Bills and deposits", "action": "openForm", "form": "BillsForm"},
    {"text": "Budget", "name": "budgetTool", "glyph": "◔", "toolTip": "The budget", "action": "openForm", "form": "BudgetForm"},
    {"separator": True},
    {"text": "Portfolio", "name": "portfolioTool", "glyph": "▲", "toolTip": "What the investment accounts hold", "action": "openForm", "form": "PortfolioForm"},
    {"text": "Reports", "name": "reportsTool", "glyph": "▥", "toolTip": "Reports and charts", "action": "openForm", "form": "ReportsForm"},
    {"separator": True},
    {"text": "Download", "name": "syncTool", "glyph": "⇩", "toolTip": "Download from Schwab (F9)"},
    {"text": "Advisor", "name": "advisorTool", "glyph": "✦", "toolTip": "Ask the Advisor", "action": "openForm", "form": "AdvisorForm"},
]

ADVISOR_INSTRUCTIONS = (
    "You are the Advisor in Wayland Money, a personal finance program. You help the person understand "
    "their own money: what they spend, what they owe, what's due, how their investments are doing.\n"
    "Use the tools for figures instead of adding up records yourself: spending_by_category, "
    "account_balances and bills_due. Amounts are US dollars; in the Transactions table a negative "
    "Amount is money going out.\n"
    "Keep answers short and plain. Give figures, not investment recommendations: you are not a "
    "licensed adviser, and say so if asked what to buy or sell.")

def forms():
    out = []

    # ---- Home -----------------------------------------------------------
    home = {"name": "HomeForm", "width": 760, "height": 560, "props": {"title": "Wayland Money"}, "controls": [
        C("Menu", "menu1", 0, 0, 760, 20, menus=HOME_MENU),
        C("ToolBar", "toolBar1", 0, 20, 760, 28, buttons=HOME_TOOLBAR),
        C("Frame", "accountsFrame", 8, 56, 372, 270, text="Accounts"),
        C("Frame", "billsFrame", 388, 56, 364, 124, text="Bills coming up"),
        C("DataGrid", "accountGrid", 16, 76, 356, 188, table="Listing", columns=["Account", "Type", "Balance"]),
        L("assetsCaption", 16, 272, 48, "Assets:"),
        L("assetsLabel", 64, 272, 96, "", alignment="Right"),
        L("owedCaption", 176, 272, 40, "Owed:"),
        L("owedLabel", 216, 272, 96, "", alignment="Right", foreColor="#c00000"),
        L("worthCaption", 16, 298, 72, "Net worth:", fontBold=True),
        L("worthLabel", 88, 298, 120, "", fontBold=True),
        C("ListBox", "billList", 396, 76, 348, 96),
        C("Graph", "spendingGraph", 388, 188, 364, 240,
          graph={"type": "Pie", "source": "code", "title": "Spending this month", "legend": "Right", "prefix": "$"}),
        C("Button", "syncButton", 388, 436, 150, 24, text="Download from Schwab"),
        L("syncLabel", 546, 440, 206, ""),
        C("Graph", "worthGraph", 8, 334, 372, 188,
          graph={"type": "Line", "table": "WorthHistory", "labelField": "Day", "groupDates": "day",
                 "series": [{"field": "Worth", "aggregate": "sum", "name": "Net worth"}],
                 "title": "Net worth", "legend": "None", "prefix": "$"}),
        L("statusLabel", 8, 530, 744, "", borderStyle="Sunken"),
    ]}
    control(home, "statusLabel")["height"] = 21
    out.append(home)

    # ---- Accounts, Payees, Categories -----------------------------------
    accounts = add_form_for("Accounts")
    accounts["height"] = 456
    control(accounts, "schwabHashBox")["props"]["enabled"] = False
    control(accounts, "lastSyncPicker")["props"]["enabled"] = False
    control(accounts, "grid")["props"]["columns"] = ["Name", "Type", "Institution"]
    accounts["controls"] += [
        C("Button", "registerButton", 12, 420, 120, 24, text="Open Register"),
        L("balanceLabel", 144, 424, 488, "", fontBold=True),
    ]
    out.append(accounts)
    out.append(add_form_for("Payees"))
    out.append(add_form_for("Categories"))

    # ---- Register ---------------------------------------------------------
    out.append({"name": "RegisterForm", "width": 780, "height": 540,
                "props": {"title": "Register", "table": "Transactions"}, "controls": [
        C("Frame", "entryFrame", 8, 344, 764, 188, text="Transaction"),
        L("accountLabel", 8, 12, 56, "Account:"),
        C("ComboBox", "accountCombo", 64, 8, 200, 21),
        L("findLabel", 280, 12, 32, "Find:"),
        C("TextBox", "findBox", 312, 8, 160, 21, toolTip="Only the transactions with this text in them"),
        L("balanceLabel", 480, 12, 292, "", alignment="Right", fontBold=True),
        C("DataGrid", "grid", 8, 36, 764, 300, table="Listing",
          columns=["Date", "Num", "Payee", "Category", "Memo", "C", "Payment", "Deposit", "Balance"]),
        C("OptionButton", "optWithdrawal", 20, 364, 96, 17, text="Withdrawal", group="kind", checked=True),
        C("OptionButton", "optDeposit", 124, 364, 76, 17, text="Deposit", group="kind"),
        C("OptionButton", "optTransfer", 208, 364, 76, 17, text="Transfer", group="kind"),
        L("dateLabel", 20, 394, 60, "Date:"),
        C("DateTimePicker", "dateBox", 84, 392, 120, 21, field="Date"),
        L("numLabel", 220, 394, 52, "Number:"),
        C("TextBox", "numBox", 276, 392, 80, 21, field="Number"),
        L("statusLabel", 372, 394, 48, "Status:"),
        C("ComboBox", "statusCombo", 424, 392, 120, 21, field="Cleared"),
        L("payeeLabel", 20, 422, 60, "Payee:"),
        C("ComboBox", "payeeCombo", 84, 420, 240, 21, field="Payee"),
        C("Button", "payeesButton", 328, 420, 24, 21, text="...", toolTip="The list of payees: add a new one there"),
        L("amountLabel", 372, 422, 48, "Amount:"),
        C("TextBox", "amountBox", 424, 420, 120, 21, alignment="Right"),
        L("categoryLabel", 20, 450, 60, "Category:"),
        C("ComboBox", "categoryCombo", 84, 448, 240, 21, field="Category"),
        L("toLabel", 372, 450, 64, "To account:"),
        C("ComboBox", "toCombo", 440, 448, 208, 21, field="TransferAccount"),
        L("memoLabel", 20, 478, 60, "Memo:"),
        C("TextBox", "memoBox", 84, 476, 460, 21, field="Memo"),
        C("Button", "newButton", 664, 364, 96, 24, text="New"),
        C("Button", "enterButton", 664, 392, 96, 24, text="Enter", isDefault=True),
        C("Button", "splitButton", 664, 420, 96, 24, text="Split..."),
        C("Button", "deleteButton", 664, 448, 96, 24, text="Delete"),
    ]})

    out.append({"name": "SplitForm", "width": 440, "height": 310,
                "props": {"title": "Split a Transaction", "table": "Splits", "resizable": False}, "controls": [
        L("totalLabel", 8, 8, 424, "", fontBold=True),
        C("DataGrid", "grid", 8, 32, 424, 150, table="Listing", columns=["Category", "Memo", "Amount"]),
        L("leftLabel", 8, 188, 424, ""),
        L("categoryLabel", 8, 216, 60, "Category:"),
        C("ComboBox", "categoryCombo", 72, 214, 200, 21, field="Category"),
        L("amountLabel", 284, 216, 48, "Amount:"),
        C("TextBox", "amountBox", 336, 214, 96, 21, alignment="Right"),
        L("memoLabel", 8, 244, 60, "Memo:"),
        C("TextBox", "memoBox", 72, 242, 360, 21, field="Memo"),
        C("Button", "addButton", 8, 276, 90, 24, text="Save Part", isDefault=True),
        C("Button", "deleteButton", 104, 276, 90, 24, text="Delete Part"),
        C("Button", "doneButton", 342, 276, 90, 24, text="Done"),
    ]})

    out.append({"name": "ReconcileForm", "width": 560, "height": 418,
                "props": {"title": "Balance an Account"}, "controls": [
        L("statementLabel", 8, 12, 90, "Statement date:"),
        C("DateTimePicker", "statementPicker", 100, 8, 120, 21, value="today", allowBlank=False),
        L("endingLabel", 236, 12, 90, "Ending balance:"),
        C("TextBox", "endingBox", 328, 8, 110, 21, alignment="Right"),
        C("DataGrid", "grid", 8, 40, 544, 280, table="Listing", columns=["Date", "Num", "Payee", "C", "Payment", "Deposit"]),
        L("clearedCaption", 8, 330, 96, "Cleared balance:"),
        L("clearedLabel", 108, 330, 110, "", alignment="Right", fontBold=True),
        L("differenceCaption", 236, 330, 70, "Difference:"),
        L("differenceLabel", 310, 330, 242, "", fontBold=True),
        C("Button", "toggleButton", 8, 360, 110, 24, text="Mark Cleared"),
        C("Button", "finishButton", 350, 360, 96, 24, text="Finish", enabled=False),
        C("Button", "cancelButton", 456, 360, 96, 24, text="Close"),
        L("helpLabel", 8, 394, 544, "Double-click each transaction that's on the statement, until the difference is 0.00."),
    ]})

    out.append({"name": "FindForm", "width": 720, "height": 440,
                "props": {"title": "Find Transactions"}, "controls": [
        L("findLabel", 8, 12, 36, "Text:"),
        C("TextBox", "findBox", 48, 8, 160, 21),
        L("accountLabel", 220, 12, 52, "Account:"),
        C("ComboBox", "accountCombo", 276, 8, 180, 21),
        L("fromLabel", 8, 40, 36, "From:"),
        C("DateTimePicker", "fromPicker", 48, 36, 110, 21),
        L("toLabel", 166, 40, 24, "To:"),
        C("DateTimePicker", "toPicker", 192, 36, 110, 21),
        L("leastLabel", 316, 40, 80, "Amount from:"),
        C("TextBox", "leastBox", 398, 36, 70, 21, alignment="Right"),
        L("mostLabel", 474, 40, 20, "to:"),
        C("TextBox", "mostBox", 496, 36, 70, 21, alignment="Right"),
        C("Button", "findButton", 624, 34, 88, 24, text="Find", isDefault=True),
        C("DataGrid", "grid", 8, 68, 704, 340, table="Listing",
          columns=["Date", "Account", "Num", "Payee", "Category", "Memo", "Amount"]),
        L("countLabel", 8, 416, 704, ""),
    ]})

    # ---- Bills, Budget ------------------------------------------------------
    bills = add_form_for("Bills")
    bills["height"] = 473
    g = control(bills, "grid")
    g["props"]["columns"] = ["Name", "NextDue", "Amount", "Frequency"]
    g["props"]["orderBy"] = "NextDue"
    bills["controls"] += [
        C("Button", "enterButton", 12, 437, 130, 24, text="Enter in Register"),
        C("Button", "skipButton", 150, 437, 110, 24, text="Skip This One"),
        L("dueLabel", 272, 441, 360, "", fontBold=True),
    ]
    out.append(bills)

    out.append({"name": "BudgetForm", "width": 700, "height": 440,
                "props": {"title": "Budget", "table": "Budgets"}, "controls": [
        C("Frame", "editFrame", 8, 316, 684, 116, text="A category's budget"),
        L("monthLabel", 8, 12, 44, "Month:"),
        C("DateTimePicker", "monthPicker", 56, 8, 130, 21, value="today", dateFormat="Custom",
          customDateFormat="MMMM yyyy", allowBlank=False),
        C("DataGrid", "grid", 8, 36, 340, 250, table="Listing", columns=["Category", "Budget", "Spent", "Left"]),
        C("Graph", "graph1", 356, 8, 336, 278,
          graph={"type": "Column", "source": "code", "legend": "Bottom", "prefix": "$"}),
        L("totalLabel", 8, 292, 684, "", fontBold=True),
        L("categoryLabel", 20, 340, 60, "Category:"),
        C("ComboBox", "categoryCombo", 84, 338, 220, 21, field="Category"),
        L("amountLabel", 320, 340, 56, "A month:"),
        C("TextBox", "amountBox", 380, 338, 100, 21, field="Amount", alignment="Right"),
        L("notesLabel", 20, 368, 60, "Notes:"),
        C("TextBox", "notesBox", 84, 366, 396, 21, field="Notes"),
        C("Button", "newButton", 500, 336, 84, 24, text="New"),
        C("Button", "saveButton", 592, 336, 84, 24, text="Save", isDefault=True),
        C("Button", "deleteButton", 592, 366, 84, 24, text="Delete"),
    ]})

    # ---- Investing ----------------------------------------------------------
    securities = add_form_for("Securities")
    securities["height"] = 612
    control(securities, "grid")["props"]["columns"] = ["Symbol", "Company", "LastPrice"]
    control(securities, "priceTimePicker")["props"]["enabled"] = False
    securities["controls"] += [
        L("rangeLabel", 12, 276, 40, "Show:"),
        C("ComboBox", "rangeCombo", 56, 272, 100, 21, items=["1 month", "3 months", "6 months", "1 year", "5 years"]),
        C("CheckBox", "averageCheck", 172, 274, 108, 17, text="20-day average"),
        C("CheckBox", "bandsCheck", 284, 274, 108, 17, text="Bollinger bands"),
        C("CheckBox", "volumeCheck", 396, 274, 70, 17, text="Volume", checked=True),
        C("CheckBox", "rsiCheck", 470, 274, 50, 17, text="RSI"),
        C("CheckBox", "liveCheck", 528, 274, 100, 17, text="Live price", toolTip="Ask Schwab for the price every half minute"),
        C("Graph", "chart", 12, 300, 620, 270,
          graph={"type": "Candlestick", "source": "code", "legend": "None", "candleColors": "Green and red",
                 "studies": ["Volume"], "decimals": 2}),
        C("Button", "downloadButton", 12, 578, 120, 24, text="Download Prices"),
        L("statusLabel", 140, 580, 492, "", borderStyle="Sunken"),
    ]
    control(securities, "statusLabel")["height"] = 21
    out.append(securities)

    out.append({"name": "PortfolioForm", "width": 760, "height": 482,
                "props": {"title": "Portfolio"}, "controls": [
        L("accountLabel", 8, 12, 56, "Account:"),
        C("ComboBox", "accountCombo", 68, 8, 240, 21),
        C("Button", "pricesButton", 424, 6, 104, 24, text="Update Prices"),
        C("Button", "tradeButton", 536, 6, 112, 24, text="Record a Trade..."),
        C("Button", "securityButton", 656, 6, 96, 24, text="Security..."),
        C("DataGrid", "grid", 8, 36, 744, 220, table="Listing",
          columns=["Symbol", "Name", "Shares", "Price", "Value", "Cost", "Gain", "Percent"]),
        L("totalLabel", 8, 262, 744, "", fontBold=True),
        C("Graph", "graph1", 8, 284, 744, 164,
          graph={"type": "Pie", "source": "code", "legend": "Right", "prefix": "$", "title": "What it's in"}),
        L("statusLabel", 8, 454, 744, "", borderStyle="Sunken"),
    ]})
    control(out[-1], "statusLabel")["height"] = 21

    out.append({"name": "TradeForm", "width": 380, "height": 328,
                "props": {"title": "Record a Trade", "table": "Transactions", "resizable": False}, "controls": [
        L("accountLabel", 12, 14, 84, "Account:"),
        C("ComboBox", "accountCombo", 100, 12, 264, 21, field="Account"),
        L("actionLabel", 12, 42, 84, "What happened:"),
        C("ComboBox", "actionCombo", 100, 40, 140, 21, field="Action"),
        L("dateLabel", 12, 70, 84, "Date:"),
        C("DateTimePicker", "datePicker", 100, 68, 140, 21, field="Date"),
        L("securityLabel", 12, 98, 84, "Security:"),
        C("ComboBox", "securityCombo", 100, 96, 140, 21, field="Security"),
        L("quantityLabel", 12, 126, 84, "Shares:"),
        C("TextBox", "quantityBox", 100, 124, 100, 21, field="Quantity", alignment="Right"),
        L("priceLabel", 12, 154, 84, "Price each:"),
        C("TextBox", "priceBox", 100, 152, 100, 21, field="Price", alignment="Right"),
        L("commissionLabel", 12, 182, 84, "Commission:"),
        C("TextBox", "commissionBox", 100, 180, 100, 21, field="Commission", alignment="Right"),
        L("amountLabel", 12, 210, 84, "Amount:"),
        C("TextBox", "amountBox", 100, 208, 100, 21, alignment="Right"),
        L("memoLabel", 12, 238, 84, "Memo:"),
        C("TextBox", "memoBox", 100, 236, 264, 21, field="Memo"),
        L("totalLabel", 12, 266, 352, "", fontBold=True),
        C("Button", "enterButton", 184, 292, 84, 24, text="Enter", isDefault=True),
        C("Button", "closeButton", 280, 292, 84, 24, text="Close"),
    ]})

    # ---- Schwab ---------------------------------------------------------------
    out.append({"name": "SchwabForm", "width": 560, "height": 500,
                "props": {"title": "Schwab Setup", "resizable": False}, "controls": [
        C("Frame", "setupFrame", 8, 8, 544, 130, text="Your app at developer.schwab.com"),
        C("Frame", "signFrame", 8, 146, 544, 126, text="Sign in (Schwab asks again every 7 days)"),
        L("keyLabel", 20, 32, 90, "App Key:"),
        C("TextBox", "keyBox", 112, 30, 428, 21),
        L("secretLabel", 20, 60, 90, "Secret:"),
        C("TextBox", "secretBox", 112, 58, 428, 21, password=True),
        L("callbackLabel", 20, 88, 90, "Callback URL:"),
        C("TextBox", "callbackBox", 112, 86, 428, 21),
        C("CheckBox", "autoCheck", 112, 114, 300, 17, text="Download when Wayland Money starts"),
        C("Button", "connectButton", 20, 168, 170, 24, text="1. Sign In at Schwab..."),
        L("stateLabel", 200, 172, 340, "", fontBold=True),
        L("pasteLabel", 20, 200, 400, "2. Paste the address the browser ended on:"),
        C("TextBox", "pasteBox", 20, 220, 400, 21),
        C("Button", "finishButton", 428, 218, 112, 24, text="Finish Sign-In"),
        L("noteLabel", 20, 248, 520, "Be quick: the address only works for about half a minute."),
        C("Button", "syncButton", 8, 280, 120, 24, text="Download Now"),
        C("Button", "signOutButton", 136, 280, 90, 24, text="Sign Out"),
        C("Button", "closeButton", 462, 280, 90, 24, text="Close"),
        C("ListBox", "logList", 8, 312, 544, 180),
    ]})

    # ---- Reports, Planner -------------------------------------------------------
    out.append({"name": "ReportsForm", "width": 780, "height": 500,
                "props": {"title": "Reports and Charts"}, "controls": [
        C("ListBox", "reportList", 8, 8, 300, 116, items=REPORTS),
        L("periodLabel", 8, 136, 48, "Period:"),
        C("ComboBox", "periodCombo", 60, 132, 248, 21, items=PERIODS),
        L("fromLabel", 8, 164, 48, "From:"),
        C("DateTimePicker", "fromPicker", 60, 160, 120, 21),
        L("toLabel", 192, 164, 20, "To:"),
        C("DateTimePicker", "toPicker", 216, 160, 92, 21),
        C("DataGrid", "grid", 8, 192, 300, 276, table="Listing", columns=["Item", "Amount"]),
        L("totalLabel", 8, 476, 300, "", fontBold=True),
        C("Graph", "graph1", 316, 8, 456, 484,
          graph={"type": "Pie", "source": "code", "legend": "Bottom", "prefix": "$", "showValues": True}),
    ]})

    out.append({"name": "PlannerForm", "width": 720, "height": 460,
                "props": {"title": "Loan Planner"}, "controls": [
        L("accountLabel", 8, 12, 90, "A debt of yours:"),
        C("ComboBox", "accountCombo", 100, 8, 180, 21),
        L("amountLabel", 8, 40, 90, "Amount owed:"),
        C("TextBox", "amountBox", 100, 36, 100, 21, alignment="Right"),
        L("rateLabel", 8, 68, 90, "Interest rate %:"),
        C("TextBox", "rateBox", 100, 64, 60, 21, alignment="Right"),
        L("yearsLabel", 8, 96, 90, "Years:"),
        C("TextBox", "yearsBox", 100, 92, 60, 21, text="30", alignment="Right"),
        L("extraLabel", 8, 124, 90, "Extra a month:"),
        C("TextBox", "extraBox", 100, 120, 100, 21, alignment="Right"),
        C("Label", "resultLabel", 8, 150, 280, 60, text="", wordWrap=True, borderStyle="Sunken"),
        C("Graph", "graph1", 296, 8, 416, 202,
          graph={"type": "Line", "source": "code", "legend": "None", "title": "What's still owed", "prefix": "$", "markers": False}),
        C("DataGrid", "grid", 8, 218, 704, 234, table="Listing", columns=["Num", "Payment", "Principal", "Interest", "Balance"]),
    ]})

    # ---- Advisor, About -----------------------------------------------------------
    out.append({"name": "AdvisorForm", "width": 480, "height": 460,
                "props": {"title": "Advisor"}, "controls": [
        C("AIChat", "chat1", 8, 8, 464, 412, instructions=ADVISOR_INSTRUCTIONS,
          greeting="Hello! Ask me about your spending, your balances or what's due.",
          formAccess=False),
        C("Button", "newButton", 8, 428, 130, 24, text="New Conversation"),
    ]})

    out.append({"name": "AboutForm", "width": 300, "height": 172,
                "props": {"title": "About Wayland Money", "resizable": False}, "controls": [
        C("Shape", "banner", 0, 0, 300, 48, backColor="#000080", borderColor="#000080"),
        L("titleLabel", 16, 12, 268, "Wayland Money", foreColor="#ffffff", fontBold=True, fontSize=18),
        L("versionLabel", 16, 60, 268, ""),
        L("madeLabel", 16, 82, 268, "Made in Wayland Studio."),
        C("Label", "noteLabel", 16, 102, 268, 30, text="Not made by or with Charles Schwab; it uses their Trader API with your own account.", wordWrap=True),
        C("Button", "okButton", 110, 140, 80, 24, text="OK", isDefault=True),
    ]})
    control(out[-1], "titleLabel")["height"] = 24

    # the code, from tools/code
    for f in out:
        code, events = form_code(f["name"])
        f["code"] = code
        names = [c["name"] for c in f["controls"]]
        for element, handlers in events.items():
            if element not in names:
                raise SystemExit("%s: code for %s, which isn't on the form" % (f["name"], element))
            control(f, element)["events"].update(handlers)
    return out

def project():
    fs = forms()
    for f in fs:
        f.pop("generated", None)
    return {
        "format": 1,
        "app": {"name": "money", "title": "Wayland Money", "version": "1.0", "startForm": "HomeForm"},
        "database": {"driver": "sqlite", "file": "money.db"},
        "tables": TABLES,
        "forms": fs,
    }
