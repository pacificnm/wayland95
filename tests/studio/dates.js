// tests/studio/run.sh: date and time defaults, through win95apprun --eval
// (any project: this sets its own table). Prints what's wrong, then "ok N".
var problems = 0, checks = 0;
function expect(what, got, want) {
    checks++;
    if (JSON.stringify(got) !== JSON.stringify(want)) {
        problems++;
        Test.out("FAIL " + what + ": " + JSON.stringify(got) + ", not " + JSON.stringify(want));
    }
}
function pad(n) { return (n < 10 ? "0" : "") + n; }
function day(offset) {
    var d = new Date();
    d.setDate(d.getDate() + offset);
    return d.getFullYear() + "-" + pad(d.getMonth() + 1) + "-" + pad(d.getDate());
}
function secondsFromNow(text) { return Math.round((new Date(text).getTime() - Date.now()) / 1000); }
function near(what, seconds, want) {
    checks++;
    if (Math.abs(seconds - want) > 5) {
        problems++;
        Test.out("FAIL " + what + ": " + seconds + "s from now, not " + want);
    }
}

Db.setSchema([{ name: "Stamps", fields: [
    { name: "Title", type: "text", required: true },
    { name: "Made", type: "datetime", auto: "created", required: true },
    { name: "Changed", type: "datetime", auto: "modified", required: true },
    { name: "MadeOn", type: "date", auto: "created" },
    { name: "Due", type: "date", default: "today+7" },
    { name: "Remind", type: "datetime", default: "now+2h" },
    { name: "Back", type: "date", default: "today - 1w" },
] }]);
Db.dropTable("Stamps");
Db.syncSchema();

var d = Db.defaults("Stamps");
expect("a new record: Due is today + 7 days", d.Due, day(7));
expect("Back is today - 1 week", d.Back, day(-7));
near("Remind is now + 2 hours", secondsFromNow(d.Remind), 7200);
near("Made shows now", secondsFromNow(d.Made), 0);
expect("MadeOn shows today", d.MadeOn, day(0));

var r = Db.insert("Stamps", { Title: "one", Made: "2000-01-01 00:00", Changed: "2000-01-01 00:00" });
expect("the stamped ones aren't required of what's written", [r.ok, r.messages], [true, []]);
var row = Db.get("Stamps", r.id);
near("Made: when it was made (what was given doesn't count)", secondsFromNow(row.Made), 0);
near("Changed: then too", secondsFromNow(row.Changed), 0);
expect("MadeOn: today", row.MadeOn, day(0));
expect("Due: its default", row.Due, day(7));
expect("a default is only a start: one given is kept", Db.get("Stamps", Db.insert("Stamps", { Title: "two", Due: "2030-05-01" }).id).Due, "2030-05-01");

var made = row.Made;
var u = Db.update("Stamps", r.id, { Title: "one, again", Made: "2001-01-01 00:00", Changed: "2001-01-01 00:00" });
expect("an update that names them is fine", [u.ok, u.messages], [true, []]);
row = Db.get("Stamps", r.id);
expect("Made stays as it was", row.Made, made);
near("Changed: now", secondsFromNow(row.Changed), 0);
expect("an update of nothing stamped still stamps Changed", Db.update("Stamps", r.id, { Title: "three" }).values.Changed !== undefined, true);
expect("and leaves Made out", Db.update("Stamps", r.id, { Title: "four" }).values.Made, undefined);

expect("today-1 as a value", Db.validate("Stamps", { Title: "x", Due: "today-1" }).values.Due, day(-1));
near("now+90m as a value", secondsFromNow(Db.validate("Stamps", { Title: "x", Remind: "now+90m" }).values.Remind), 5400);
expect("not a relative date", Db.validate("Stamps", { Title: "x", Due: "today+" }).errors.Due !== undefined, true);
Db.dropTable("Stamps");

Test.out(problems ? problems + " wrong of " + checks : "ok " + checks);
