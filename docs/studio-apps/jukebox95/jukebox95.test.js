// Jukebox 95's own test: win95studio-mcp's run_test (or
// win95apprun --test jukebox95.test.js jukebox95.w95proj --data A_COPY).
// It runs on a copy of the playlist: the 17 songs from ~/Music.
var P = "PlayerForm";
var songs = Db.count("Tracks");
Test.check(songs > 3, "a playlist to test on (" + songs + " songs)");
var grid = Test.element(P, "grid"), player = Test.element(P, "player1");

// double-click the 8th (Hallowed Be Thy Name: it has its cover inside)
Test.doubleClick(P, "grid", 7);
Test.wait(1500, function () {
    Test.check(player.isPlaying, "a double-click plays it (muted: a test is never heard)");
    Test.equal(Test.element(P, "statusLabel").text, "▶ PLAYING", "the display says so");
    Test.equal(Test.element(P, "countLabel").text, "Track 8 of " + songs, "and which it is");
    Test.check(Test.element(P, "coverImage").picture.indexOf("file://") === 0, "its cover shows (player1.coverArt)");
    Test.check(Test.element(P, "vis1").analyzer.level > 0, "the Visualizer hears it");
    var first = Test.element(P, "nowLabel").text;
    Test.tick(P, "clock");
    Test.tick(P, "clock");
    Test.check(Test.element(P, "nowLabel").text !== first, "a long title goes round as the Timer ticks");

    // the Next key (MediaKeys: the player's nextTrack event)
    player.nextTrack();
    Test.wait(800, function () {
        Test.equal(Test.element(P, "countLabel").text, "Track 9 of " + songs, "the Next key plays the next song");
        player.previousTrack();
        Test.wait(800, function () {
            Test.equal(Test.element(P, "countLabel").text, "Track 8 of " + songs, "Previous (at its start) the one before");

            // volume, shuffle, search
            Test.pick(P, "volumeSlider", 35);
            Test.equal(player.volume, 35, "the Slider is the volume");
            Test.click(P, "shuffleBox");
            Test.check(Test.element(P, "menu1").itemNamed("playShuffle").checked, "Shuffle ticks Play > Shuffle too");
            Test.click(P, "shuffleBox");
            Test.type(P, "searchBox", "maiden");
            Test.equal(grid.rows.length, 8, "Search: maiden leaves Iron Maiden's eight");
            Test.type(P, "searchBox", "");

            // Remove: No keeps it, Yes takes it off (the file stays)
            Test.click(P, "grid", 0);
            Test.answer("No");
            Test.click(P, "removeButton");
            Test.wait(200, function () {
                Test.equal(Db.count("Tracks"), songs, "Remove, No: still there");
                Test.answer("Yes");
                Test.click(P, "removeButton");
                Test.wait(200, function () {
                    Test.equal(Db.count("Tracks"), songs - 1, "Remove, Yes: off the playlist");

                    // Add Folder: one of ~/Music's, its songs' tags read
                    Test.choose("~/Music/iron-maiden/1982-the-number-of-the-beast");
                    Test.click(P, "folderButton");
                    Test.wait(4000, function () {
                        Test.equal(Db.count("Tracks"), songs, "Add Folder puts back the one taken off (the rest are there already)");
                        Test.check(Test.lastMessage().indexOf("1 song added") === 0, "and says so: " + Test.lastMessage());

                        // the equalizer: View > Equalizer opens it under the playlist
                        var form = Test.form(P), eq = Test.element(P, "eq1");
                        Test.click(P, "menu1", "viewEq");
                        Test.check(eq.visible && form.height === 622, "View > Equalizer: the panel, the window taller");
                        // (as its On and its preset list do: the player's, then changed)
                        player.equalizer = true;
                        player.applyEqPreset("Rock");
                        eq.changed();
                        Test.equal(JSON.parse(Db.select("Settings", { where: { Name: "equalizer" } })[0].Value).gains.join(","),
                                   "5,4,3,1,-1,-1,1,3,4,5", "Rock, kept in Settings");
                        Test.equal(String(Test.element(P, "eqLabel").foreColor), "#00e000", "the display's EQ lit");
                        // the next time it opens: as it was
                        player.equalizer = false;
                        player.applyEqPreset("Flat");
                        form.restoreEq();
                        Test.check(player.equalizer && player.eqPresetNow === "Rock" && eq.visible, "opened again: on, Rock, shown");
                        Test.click(P, "eqLabel");
                        Test.check(!eq.visible && form.height === 462, "the display's EQ hides it again");
                        Test.done();
                    });
                });
            });
        });
    });
});
