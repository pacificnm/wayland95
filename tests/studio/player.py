#!/usr/bin/env python3
"""Makes player.w95proj in DIR for tests/run.sh's studioplayer suite: Add
opens App.chooseFiles in MUSIC (the Win95 Open box, clicked through) and
prints what was chosen and each one's tags; a muted MediaPlayer plays the
first, with a Visualizer of it (MediaKeys: the desktop's media keys and
sound menu reach it); a Slider is its volume; a Timer shows where it is
in a Label; an Equalizer is the player's.

    player.py DIR MUSIC
"""
import json, sys
d, music = sys.argv[1], sys.argv[2]
add = '''App.chooseFiles({ title: "Add Songs", folder: %s, filters: ["Music (*.mp3 *.flac)", "All Files (*.*)"] }, function (paths) {
    console.log("CHOSE " + JSON.stringify(paths.map(function (p) { return p.replace(/^.*\\//, ""); })));
    paths.forEach(function (p) { App.readTags(p, function (t) { console.log("TAGS " + t.title + " | " + t.artist); }); });
    if (paths.length) {
        player1.source = paths[0];
        player1.play();
    }
});''' % json.dumps(music)
p = {"format": 1, "app": {"name": "playertest", "title": "Player Test", "version": "1.0", "startForm": "Main"},
     "database": {"driver": "sqlite", "file": "player.db"}, "tables": [],
     "forms": [{"name": "Main", "width": 408, "height": 290, "props": {"title": "Player Test"}, "code": {},
                "controls": [
                    {"type": "Button", "name": "addButton", "x": 8, "y": 8, "width": 80, "height": 24, "props": {"text": "Add...", "icon": "add"},
                     "events": {"clicked": add}},
                    {"type": "Slider", "name": "volumeSlider", "x": 96, "y": 6, "width": 160, "height": 30, "props": {"value": 80},
                     "events": {"changed": 'player1.volume = volumeSlider.value;\nconsole.log("VOLUME " + volumeSlider.value);'}},
                    {"type": "Label", "name": "timeLabel", "x": 264, "y": 12, "width": 88, "height": 17, "props": {"text": "0:00"},
                     # (a click: the form taller, or back; its window has to follow)
                     "events": {"clicked": 'form.height = form.height === 290 ? 330 : 290;\nconsole.log("HEIGHT " + form.height);'}},
                    {"type": "MediaPlayer", "name": "player1", "x": 8, "y": 44, "width": 200, "height": 70, "props": {"muted": True, "mediaKeys": True},
                     "events": {"paused": 'console.log("PAUSED");', "started": 'console.log("STARTED");',
                                "nextTrack": 'console.log("NEXT");', "previousTrack": 'console.log("PREVIOUS");'}},
                    {"type": "Visualizer", "name": "vis1", "x": 216, "y": 44, "width": 136, "height": 70, "props": {"player": "player1", "bars": 12}},
                    {"type": "Equalizer", "name": "eq1", "x": 8, "y": 124, "width": 392, "height": 152, "props": {"player": "player1"},
                     "events": {"changed": 'console.log("EQ " + JSON.stringify(player1.eqGains) + " on " + player1.equalizer);'}},
                    {"type": "Timer", "name": "timer1", "x": 372, "y": 4, "width": 28, "height": 28, "props": {"interval": 250},
                     "events": {"timer": 'timeLabel.text = player1.clock(player1.position);\nif (player1.isPlaying && player1.level > 0.3 && !said) { said = true; console.log("LOUD " + player1.position); }'}},
                ]}]}
p["forms"][0]["code"]["general"] = "property bool said: false"
json.dump(p, open(d + "/player.w95proj", "w"), indent=1)
