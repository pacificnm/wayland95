#!/usr/bin/env python3
"""Makes newelements.w95proj in DIR: one form with what came in with the
music player (Jukebox 95): App's file boxes (answered by code), listFiles,
fileInfo and readTags; a Timer; Sliders (across, up and down, bound to a
field); a MediaPlayer's tags, loaded and positionChanged events; a
Visualizer of it; a DataGrid with column widths. Its load and timer code
print `T ...` lines (tests/studio/run.sh compares them, sorted) and quit.
The player is muted: nothing reaches the speakers.

    newelements.py DIR TONE.mp3
"""
import json, os, sys

d, tone = os.path.abspath(sys.argv[1]), os.path.abspath(sys.argv[2])
songs = d + "/songs"
for f in ("a/2.mp3", "a/10.mp3", "a/x.txt", "b/1.flac"):
    os.makedirs(os.path.dirname(songs + "/" + f), exist_ok=True)
    open(songs + "/" + f, "w").close()

general = r'''
property int ticks: 0
property bool posSeen: false
function log(s) { console.log("T " + s); }
function rel(p) { return String(p).replace("SONGS/", ""); }
'''
load = r'''
log("files " + JSON.stringify(App.listFiles("SONGS", "*.mp3 *.flac", true).map(rel)));
log("files flat " + App.listFiles("SONGS/a", ["*.mp3"]).length + " none " + App.listFiles("SONGS/nope", "*").length);
var fi = App.fileInfo("SONGS/a/10.mp3");
log("info " + fi.exists + " " + fi.isFile + " " + fi.name + " " + fi.baseName + " " + fi.suffix + " home " + (App.fileInfo("~").isDir));
App.readTags("TONE", function (t) { log("tags " + t.ok + " " + t.title + "|" + t.artist + "|" + t.album + "|" + t.trackNumber + "|" + t.genre + "|" + Math.round(t.duration / 1000)); });
App.readTags("SONGS/missing.mp3", function (t) { log("tags missing " + t.ok + " " + t.error); });
App.readTags("SONGS/a/x.txt", function (t) { log("tags text " + t.ok + " " + (t.error !== "")); });
var box = App.chooseFiles({ title: "Add Songs", folder: "SONGS/a", filters: ["Music (*.mp3 *.flac)", "All Files (*.*)"] },
                          function (paths) { log("chose " + JSON.stringify(paths.map(rel))); });
log("box " + box.title + " | " + box.mode + " multiple " + box.multiple + " filters " + JSON.stringify(box.filters));
box.choose(["SONGS/a/2.mp3", "SONGS/a/10.mp3"]);
App.chooseFolder({}, function (p) { log("folder [" + p + "]"); }).cancel();
App.chooseSaveFile({ name: "list.m3u", suffix: "m3u" }, function (p) { log("save " + rel(p)); }).choose("SONGS/list.m3u");
App.chooseFile({}, function (p) { log("one " + rel(p)); }).choose("SONGS/b/1.flac");
log("slider " + slider1.value + " vslider " + slider2.value);
log("grid " + JSON.stringify(grid.columnWidths));
player1.source = "TONE";
player1.play();
'''
clock = r'''
ticks += 1;
if (ticks === 8)
    log("vis " + vis1.analyzer.levels.length + " moving " + (Math.max.apply(null, player1.levels) > 0.3 && player1.level > 0.3)
        + " wave " + player1.waveform.length + " playing " + player1.isPlaying);
if (ticks === 9) {
    form.setField("Volume", 70);
    log("bound slider " + slider3.value);
}
if (ticks === 10) {
    log("player tags " + player1.artist + "|" + player1.album + "|" + player1.trackNumber + "|" + player1.genre);
    player1.stop();
}
if (ticks === 16) {
    log("fallen " + (player1.level === 0));
    App.quit();
}
'''
timer = 'if (timer1.count === 3) log("timer 3 running " + timer1.running);\nif (timer1.count === 5) { timer1.stop(); log("stopped " + timer1.running); }'
p = {"format": 1, "app": {"name": "newelements", "title": "New Elements", "version": "1.0", "startForm": "Main"},
     "database": {"driver": "sqlite", "file": "newelements.db"},
     "tables": [{"name": "Items", "fields": [{"name": "Name", "type": "text"}, {"name": "Volume", "type": "integer"},
                                             {"name": "Note", "type": "text"}]}],
     "forms": [{"name": "Main", "width": 400, "height": 300, "props": {"title": "New", "table": "Items"},
                "code": {"general": general, "load": load},
                "controls": [
                    {"type": "Timer", "name": "timer1", "x": 360, "y": 8, "width": 28, "height": 28, "props": {"interval": 200},
                     "events": {"timer": timer}},
                    {"type": "Timer", "name": "clock", "x": 360, "y": 40, "width": 28, "height": 28, "props": {"interval": 250},
                     "events": {"timer": clock}},
                    {"type": "Slider", "name": "slider1", "x": 8, "y": 8, "width": 160, "height": 30, "props": {"value": 40, "max": 50}},
                    {"type": "Slider", "name": "slider2", "x": 8, "y": 44, "width": 30, "height": 100,
                     "props": {"orientation": "Vertical", "value": 7, "min": 5, "max": 10}},
                    {"type": "Slider", "name": "slider3", "x": 50, "y": 44, "width": 160, "height": 30, "props": {"field": "Volume"}},
                    {"type": "MediaPlayer", "name": "player1", "x": 8, "y": 150, "width": 200, "height": 60, "props": {"muted": True},
                     "events": {"loaded": 'log("loaded " + player1.title + " " + Math.round(player1.duration / 1000));',
                                "positionChanged": 'if (!posSeen && player1.position > 0) { posSeen = true; log("position moves"); }'}},
                    {"type": "Visualizer", "name": "vis1", "x": 220, "y": 150, "width": 160, "height": 60,
                     "props": {"player": "player1", "bars": 16}},
                    {"type": "DataGrid", "name": "grid", "x": 8, "y": 220, "width": 380, "height": 70,
                     "props": {"columns": ["Name:200", "Volume", "Note:50"]}},
                ]}]}
text = json.dumps(p, indent=1).replace("SONGS", songs).replace("TONE", tone)
open(d + "/newelements.w95proj", "w").write(text)
