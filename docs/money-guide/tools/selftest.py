#!/usr/bin/env python3
"""Runs the finished Wayland Money without a screen and prints what it did.

A copy of sample/money.w95proj gets selftest.js on the end of HomeForm's
(General); mock_schwab.py stands in for Schwab. Each "T ..." line is one
check's result; anything else is a warning from the app's code. The forms'
pictures land in images/ (with --shots).

    tools/selftest.py [--shots] [--keep]      (BUILD=<dir with win95apprun>)
"""
import json
import os
import shutil
import subprocess
import sys
import tempfile
import time

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
RUN = os.path.join(os.environ.get("BUILD", os.path.join(ROOT, "..", "..", "build")), "win95apprun")

work = tempfile.mkdtemp(prefix="money-selftest.")
os.mkdir(os.path.join(work, "shots"))
port_file = os.path.join(work, "port")
mock = subprocess.Popen([sys.executable, os.path.join(HERE, "mock_schwab.py"), port_file, os.path.join(work, "requests.log")])
try:
    for _ in range(50):
        if os.path.exists(port_file) and os.path.getsize(port_file):
            break
        time.sleep(0.1)
    port = open(port_file).read().strip()
    project = json.load(open(os.path.join(ROOT, "sample", "money.w95proj"), encoding="utf-8"))
    home = next(f for f in project["forms"] if f["name"] == "HomeForm")
    test = open(os.path.join(HERE, "selftest.js"), encoding="utf-8").read().replace("MOCKURL", "http://127.0.0.1:" + port)
    home["code"]["general"] += "\n\n" + test
    json.dump(project, open(os.path.join(work, "money.w95proj"), "w", encoding="utf-8"))
    # (its own settings folder too: the pictures get the standard colors, not this desktop's)
    env = dict(os.environ, QT_QPA_PLATFORM="offscreen", XDG_DATA_HOME=os.path.join(work, "data"),
               XDG_CONFIG_HOME=os.path.join(work, "config"))
    env.pop("ANTHROPIC_API_KEY", None)
    try:
        out = subprocess.run([RUN, "money.w95proj"], cwd=work, env=env, capture_output=True, text=True, timeout=100)
        text = out.stderr + out.stdout
    except subprocess.TimeoutExpired as e:
        text = (e.stderr or b"").decode() + (e.stdout or b"").decode() + "\nTIMED OUT: the app didn't end by itself"
    for line in text.split("\n"):
        print(line[5:] if line.startswith("qml: ") else line)
    print("--- what Schwab's stand-in was asked ---")
    print(open(os.path.join(work, "requests.log")).read())
    if "--shots" in sys.argv:
        for name in sorted(os.listdir(os.path.join(work, "shots"))):
            shutil.copy(os.path.join(work, "shots", name), os.path.join(ROOT, "images", name))
            print("picture: images/" + name)
finally:
    mock.kill()
    if "--keep" in sys.argv:
        print("left in " + work)
    else:
        shutil.rmtree(work, ignore_errors=True)
