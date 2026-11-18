var m = ["Ada Lovelace|ada@example.com", "Grace Hopper|grace@example.com", "Linus Pauling|linus@example.com"].map(function (s) {
    var p = s.split("|");
    return Db.insert("Members", { Name: p[0], Email: p[1], Joined: "today-200" }).id;
});
var books = [
    ["The Left Hand of Darkness", "Ursula K. Le Guin", "Fiction", m[0], 304, "today-40", "today-20", 5],
    ["A Brief History of Time", "Stephen Hawking", "Non-fiction", m[1], 212, "today-30", "today-12", 4],
    ["Leaves of Grass", "Walt Whitman", "Poetry", m[2], 145, "today-14", "", ""],
    ["Middlemarch", "George Eliot", "Fiction", m[0], 880, "today-9", "", ""],
    ["Steve Jobs", "Walter Isaacson", "Biography", m[1], 656, "today-60", "today-35", 3],
];
books.forEach(function (b) {
    var r = Db.insert("Books", { Title: b[0], Author: b[1], Genre: b[2], Reader: b[3], Pages: b[4], Started: b[5],
                                 Finished: b[6] || null, Rating: b[7] || null });
    if (!r.ok) Test.out(JSON.stringify(r.errors));
});
Test.out(Db.count("Members") + " members, " + Db.count("Books") + " books");
