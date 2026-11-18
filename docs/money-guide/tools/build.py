#!/usr/bin/env python3
"""Makes sample/money.w95proj and the guide's chapters from app.py, code/ and chapters/.

    tools/build.py            the project and the chapters
    tools/build.py --project  only the project
    tools/build.py --stages   Check for Problems, on the project as each chapter leaves it
"""
import json
import os
import sys

import app

ROOT = os.path.dirname(app.HERE)

def write_project():
    path = os.path.join(ROOT, "sample", "money.w95proj")
    with open(path, "w", encoding="utf-8") as f:
        json.dump(app.project(), f, indent=1, ensure_ascii=False)
        f.write("\n")
    return path

# the forms each chapter adds, and the parts of HomeForm's (General) it leaves the Home page with
STAGES = [
    ("03-home", ["HomeForm"], ["core", "core-nobills"], "load"),
    ("04-accounts", ["AccountsForm", "PayeesForm", "CategoriesForm"], ["core", "core-nobills"], "load"),
    ("05-register", ["RegisterForm", "SplitForm", "FindForm"], ["core", "core-nobills", "register", "register-noholdings"], "load"),
    ("06-reconcile", ["ReconcileForm"], ["core", "core-nobills", "register", "register-noholdings"], "load"),
    ("07-bills", ["BillsForm"], ["core", "register", "register-noholdings", "bills"], "load bills"),
    ("08-budget", ["BudgetForm"], ["core", "register", "register-noholdings", "bills"], "load bills"),
    ("09-investing", ["SecuritiesForm", "TradeForm", "PortfolioForm"], ["core", "register", "bills", "investing"], "load bills"),
    ("10-schwab", ["SchwabForm"], ["core", "register", "bills", "investing", "schwab", "schwab-start"], "load bills"),
    ("11-reports", ["ReportsForm", "PlannerForm"], None, None),
    ("12-advisor", ["AdvisorForm"], None, None),
    ("13-finish", ["AboutForm"], None, None),
]

def check_stages():
    """the project as it stands at the end of each chapter: does it pass Check for Problems?"""
    import subprocess
    import tempfile
    run = os.path.join(os.environ.get("BUILD", os.path.join(ROOT, "..", "..", "build")), "win95apprun")
    sections = app.read_code("HomeForm")
    have, general, load, bad = [], None, None, 0
    for chapter, forms, parts, load_name in STAGES:
        have += forms
        if parts:
            general, load = "\n\n".join(sections["(General) " + p] for p in parts), sections[load_name]
        p = app.project()
        p["forms"] = [f for f in p["forms"] if f["name"] in have]
        home = p["forms"][0]
        home["code"]["general"], home["code"]["load"] = general, load
        for c in home["controls"]:
            if c["type"] == "Menu":
                for m in c["props"]["menus"]:
                    m["items"] = [i for i in m["items"] if i.get("action") != "openForm" or i["form"] in have]
                c["props"]["menus"] = [m for m in c["props"]["menus"] if any(not i.get("separator") for i in m["items"])]
            if c["type"] == "ToolBar":
                c["props"]["buttons"] = [b for b in c["props"]["buttons"] if b.get("action") != "openForm" or b["form"] in have]
        with tempfile.TemporaryDirectory() as d:
            path = os.path.join(d, "money.w95proj")
            json.dump(p, open(path, "w", encoding="utf-8"))
            out = subprocess.run([run, "--check", path], env=dict(os.environ, QT_QPA_PLATFORM="offscreen"),
                                 capture_output=True, text=True)
        ok = out.returncode == 0
        bad += 0 if ok else 1
        print("%s  after %s (%d forms)%s" % ("ok  " if ok else "FAIL", chapter, len(have), "" if ok else "\n" + out.stdout))
    return bad

if __name__ == "__main__":
    if "--stages" in sys.argv:
        sys.exit(1 if check_stages() else 0)
    print(write_project())
    if "--project" not in sys.argv:
        import guide
        guide.write_chapters()
