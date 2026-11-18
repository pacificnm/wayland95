"""The guide's chapters: tools/chapters/*.md with their {{...}} lines filled
in from app.py (the tables of fields and elements, the menus, the code).

    {{fields Accounts}}                      a table's fields
    {{elements RegisterForm a,b,c}}          those elements (in that order); * for all of them
    {{form RegisterForm}}                    the form's own properties
    {{code RegisterForm (General)}}          one piece of code (a section of tools/code/RegisterForm.js)
    {{events RegisterForm a,b}}              those elements' events' code; * for all (and the form's own)
    {{menu HomeForm fileFind,fileExit}}      those menu items; * for the whole menu
    {{toolbar HomeForm accountsTool}}        those tool buttons; * for all
    {{graph HomeForm worthGraph}}            a Graph's settings, as the Graph Editor has them
"""
import json
import os
import re

import app

ROOT = os.path.dirname(app.HERE)
STUDIO = os.path.join(ROOT, "..", "..", "apps", "studio")

CHAPTERS = [
    ("index", "Contents"),
    ("01-plan", "The Plan"),
    ("02-tables", "The Tables"),
    ("03-home", "The Home Page"),
    ("04-accounts", "Accounts, Payees and Categories"),
    ("05-register", "The Account Register"),
    ("06-reconcile", "Balancing an Account"),
    ("07-bills", "Bills and Deposits"),
    ("08-budget", "The Budget"),
    ("09-investing", "Investments"),
    ("10-schwab", "Charles Schwab"),
    ("11-reports", "Reports, Charts and the Loan Planner"),
    ("12-advisor", "The Advisor"),
    ("13-finish", "Finishing: the Menu, the Program, the Test"),
    ("14-findings", "What This Build Showed"),
]

TYPES = {"text": "Text", "memo": "Long Text", "integer": "Whole Number", "decimal": "Decimal", "float": "Floating Point",
         "currency": "Currency", "boolean": "Yes/No", "date": "Date", "time": "Time", "datetime": "Date and Time",
         "choice": "Choice", "lookup": "Lookup", "email": "E-mail", "url": "URL", "phone": "Phone"}

def registry():
    return json.load(open(os.path.join(STUDIO, "runtime", "elements.json"), encoding="utf-8"))

def rules():
    """the Validation Rules: id -> (its name in the list, the name of its value's row)"""
    out = {}
    text = open(os.path.join(STUDIO, "runtime", "fieldrules.cpp"), encoding="utf-8").read()
    for m in re.finditer(r'\{ "([a-z_0-9]+)", "([^"]+)", [A-Z]+, Arg::\w+, "([^"]*)"', text):
        out[m.group(1)] = (m.group(2), m.group(3))
    return out

REG = registry()
RULES = rules()

def code_block(text):
    return "```\n" + text + "\n```"

# ---- tables ---------------------------------------------------------------

def field_properties(f):
    out = []
    if f.get("label"):
        out.append("Caption: " + f["label"])
    if f.get("required"):
        out.append("Required")
    if f.get("unique"):
        out.append("No Duplicates")
    if "choices" in f:
        out.append("Choices: " + ", ".join(f["choices"]))
    if "lookupTable" in f:
        out.append("Lookup Table: " + f["lookupTable"])
    if "default" in f:
        d = f["default"]
        d = {"today": "Today", "yes": "Yes"}.get(d, d)
        out.append("Default Value: " + d)
    if "maxLength" in f:
        out.append("Max Length: %d" % f["maxLength"])
    if "decimals" in f:
        out.append("Decimal Places: %d" % f["decimals"])
    if "min" in f:
        out.append("Minimum: %s" % f["min"])
    if "max" in f:
        out.append("Maximum: %s" % f["max"])
    if "pattern" in f:
        out.append("Pattern: `%s`" % f["pattern"])
    if "validator" in f:
        name, arg = RULES[f["validator"]]
        rule = "Validation Rule: *" + name.replace("…", "") + "*"
        if f["validator"] == "expression":
            rule = "Validation Rule: *Custom expression* (the Expression is under this table)"
        elif "validatorArg" in f:
            rule += ", %s: %s" % (arg, f["validatorArg"])
        out.append(rule)
    if "message" in f:
        out.append("Validation Text: " + f["message"])
    return "; ".join(out)

def fields(name):
    t = app.table(name)
    lines = ["| Field Name | Data Type | Properties |", "|---|---|---|"]
    for f in t["fields"]:
        lines.append("| %s | %s | %s |" % (f["name"], TYPES[f["type"]], field_properties(f)))
    return "\n".join(lines)

# ---- forms ------------------------------------------------------------------

FORMS = {f["name"]: f for f in app.forms()}

def element_info(type):
    return next(e for e in REG["elements"] if e["type"] == type)

def prop_labels(type):
    info = element_info(type)
    props = list(info["props"])
    for s in info.get("sets", []):
        props += REG[s]
    props += REG["common"]
    return {p["name"]: p for p in props}

def show_value(prop, value):
    t = prop.get("type")
    if t == "bool":
        return "True" if value else "False"
    if t == "list":
        return ", ".join(value)
    if t in ("menu", "toolbar"):
        return "(see below)"
    if t == "graph":
        return "(the Graph Editor: see below)"
    if value == "":
        return "(blank)"
    return str(value).replace("\n", " ").replace("|", "\\|")

def element_row(c):
    labels = prop_labels(c["type"])
    props = []
    for name, value in c["props"].items():
        p = labels[name]
        if p.get("type") in ("menu", "toolbar", "graph") or (name == "text" and value == "" and c["type"] == "Label"):
            if name == "text":
                props.append("Caption: (blank)")
            continue
        props.append("%s: %s" % (p["label"], show_value(p, value)))
    info = element_info(c["type"])
    kind = info.get("label") and "%s (%s)" % (c["type"], info["label"]) or c["type"]
    return "| %s | `%s` | %d, %d | %d × %d | %s |" % (kind, c["name"], c["x"], c["y"], c["width"], c["height"], "; ".join(props))

def elements(form, names):
    f = FORMS[form]
    chosen = f["controls"] if names == "*" else [app.control(f, n) for n in names.split(",")]
    lines = ["| Element | (Name) | Left, Top | Width × Height | Properties |", "|---|---|---|---|---|"]
    for c in chosen:
        if c["type"] in ("Menu", "ToolBar"):
            continue
        lines.append(element_row(c))
    return "\n".join(lines)

def form_props(form):
    f = FORMS[form]
    labels = {p["name"]: p for p in REG["form"]["props"]}
    lines = ["| Property | Value |", "|---|---|", "| (Name) | `%s` |" % form]
    for name, value in f["props"].items():
        lines.append("| %s | %s |" % (labels[name]["label"], show_value(labels[name], value)))
    lines.append("| Width, Height | %d, %d |" % (f["width"], f["height"]))
    return "\n".join(lines)

def code(form, section):
    sections = app.read_code(form)
    if section not in sections:
        raise SystemExit("%s has no code section %r" % (form, section))
    return code_block(sections[section])

def events(form, names):
    f = FORMS[form]
    sections = app.read_code(form)
    out = []
    for name, text in sections.items():
        if name.startswith("(General)") or " " in name:
            continue
        element = name.split(".")[0] if "." in name else ""
        if names != "*" and (element or "form") not in names.split(","):
            continue
        if "." in name:
            out.append("`%s`, **%s**:\n\n%s" % (element, name.split(".")[1], code_block(text)))
        else:
            out.append("The form's **%s**:\n\n%s" % (name, code_block(text)))
    if not out:
        raise SystemExit("%s: no events for %s" % (form, names))
    return "\n\n".join(out)

ACTIONS = {"": "(none: its code only)", "openForm": "Open a form", "closeForm": "Close this form", "newRecord": "New record",
           "save": "Save the record", "deleteRecord": "Delete the record", "quit": "Quit the program"}

def menu(form, names):
    m = app.control(FORMS[form], "menu1")["props"]["menus"]
    lines = ["| Menu | Caption | Name | Shortcut | Action |", "|---|---|---|---|---|"]
    for top in m:
        first = True
        for item in top["items"]:
            if names != "*" and item.get("name") not in names.split(","):
                continue
            if item.get("separator"):
                if names == "*":
                    lines.append("| | *(separator)* | | | |")
                continue
            action = ACTIONS[item.get("action", "")]
            if item.get("action") == "openForm":
                action += ": " + item["form"]
            lines.append("| %s | `%s` | `%s` | %s | %s |" % ("**" + top["text"] + "**" if first or names != "*" else "",
                                                             item["text"], item["name"], item.get("shortcut", ""), action))
            first = False
    return "\n".join(lines)

def toolbar(form, names):
    buttons = app.control(FORMS[form], "toolBar1")["props"]["buttons"]
    lines = ["| Caption | Name | Symbol | ToolTip | Action |", "|---|---|---|---|---|"]
    for b in buttons:
        if b.get("separator"):
            if names == "*":
                lines.append("| *(separator)* | | | | |")
            continue
        if names != "*" and b["name"] not in names.split(","):
            continue
        action = ACTIONS[b.get("action", "")]
        if b.get("action") == "openForm":
            action += ": " + b["form"]
        lines.append("| %s | `%s` | %s | %s | %s |" % (b["text"], b["name"], b["glyph"], b["toolTip"], action))
    return "\n".join(lines)

GRAPH_WORDS = [("type", "Type"), ("source", "Data"), ("table", "Table"), ("labelField", "Labels"), ("groupDates", "Dates by"),
               ("series", "Values"), ("title", "Title"), ("legend", "Legend"), ("prefix", "Before"), ("decimals", "Decimals"),
               ("showValues", "Values on the graph"), ("markers", "Markers"), ("candleColors", "Candles"), ("studies", "Under them")]

def graph(form, name):
    g = app.control(FORMS[form], name)["props"]["graph"]
    lines = ["| Graph Editor | Setting |", "|---|---|"]
    for key, word in GRAPH_WORDS:
        if key not in g:
            continue
        v = g[key]
        if key == "source":
            v = "What code gives it"
        elif key == "series":
            v = ", ".join("%s (%s)" % (s["field"], s["aggregate"].capitalize()) for s in v)
        elif key == "groupDates":
            v = "A " + v + " at a time"
        elif key == "studies":
            v = ", ".join(v)
        elif isinstance(v, bool):
            v = "On" if v else "Off"
        elif key == "prefix":
            v = "`%s`" % v
        lines.append("| %s | %s |" % (word, v))
    return "\n".join(lines)

DIRECTIVES = {"fields": fields, "elements": elements, "form": form_props, "code": code, "events": events,
              "menu": menu, "toolbar": toolbar, "graph": graph}

def expand(text):
    def one(m):
        words = m.group(1).split(None, 2)
        fn = DIRECTIVES[words[0]]
        return fn(*words[1:])
    return re.sub(r"^\{\{(.+?)\}\}$", one, text, flags=re.M)

def nav(i):
    parts = ["[Contents](index.md)"]
    if i > 1:
        parts.append("Previous: [%s](%s.md)" % (CHAPTERS[i - 1][1], CHAPTERS[i - 1][0]))
    if i + 1 < len(CHAPTERS):
        parts.append("Next: [%s](%s.md)" % (CHAPTERS[i + 1][1], CHAPTERS[i + 1][0]))
    return " | ".join(parts)

def write_chapters():
    for i, (name, title) in enumerate(CHAPTERS):
        source = os.path.join(app.HERE, "chapters", name + ".md")
        if not os.path.exists(source):
            print("(no chapter yet: %s)" % name)
            continue
        body = expand(open(source, encoding="utf-8").read()).strip("\n")
        if i > 0:
            body = nav(i) + "\n\n" + body + "\n\n" + nav(i)
        with open(os.path.join(ROOT, name + ".md"), "w", encoding="utf-8") as f:
            f.write(body + "\n")
        print(name + ".md")
