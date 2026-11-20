// The equalizer (tests/studio/run.sh, through win95apprun --test): a 1 kHz tone, its level
// with the equalizer off, on and flat, the 1K band up and down, a preset that leaves 1 kHz
// alone, the preamp, and off again; and the Equalizer element following the player.

var p = Test.element("Main", "player1"), eq = Test.element("Main", "eq1");
Test.equal(eq.target === p, true, "the Equalizer finds its MediaPlayer");
Test.equal(p.eqFrequencies.join(" "), "31 62 125 250 500 1000 2000 4000 8000 16000", "ten bands");
p.play();
Test.wait(1500, function () {
    var flatOff = p.level;
    Test.check(flatOff > 0.4 && flatOff < 0.62, "the tone's level, the equalizer off: " + flatOff.toFixed(2));
    p.equalizer = true;
    Test.check(!p.analyzer.streaming, "a test run: no stream of its own (never heard)");
    Test.wait(800, function () {
        Test.check(Math.abs(p.level - flatOff) < 0.03, "on, flat: the same: " + p.level.toFixed(2));
        p.setEqGain(5, 12);
        Test.equal(eq.presetNow, "Custom", "a band moved: Custom");
        Test.wait(800, function () {
            Test.check(p.level > flatOff + 0.2, "1K +12 dB: louder (" + p.level.toFixed(2) + ", 12 dB is +0.25)");
            p.setEqGain(5, -12);
            Test.wait(800, function () {
                Test.check(p.level < flatOff - 0.2, "1K -12 dB: quieter (" + p.level.toFixed(2) + ")");
                p.applyEqPreset("Bass Boost");
                Test.equal(eq.presetNow, "Bass Boost", "a preset");
                Test.wait(800, function () {
                    Test.check(Math.abs(p.level - flatOff) < 0.05, "Bass Boost leaves a 1 kHz tone as it was: " + p.level.toFixed(2));
                    p.eqPreamp = -6;
                    Test.wait(800, function () {
                        Test.check(p.level < flatOff - 0.08, "preamp -6 dB: quieter (" + p.level.toFixed(2) + ", 6 dB is -0.125)");
                        p.equalizer = false;
                        Test.wait(800, function () {
                            Test.check(Math.abs(p.level - flatOff) < 0.03, "off: as it was: " + p.level.toFixed(2));
                            Test.done();
                        });
                    });
                });
            });
        });
    });
});
