// tests/studio/run.sh: a database the Studio didn't make (made here, in
// whichever database the project names: SQLite, MySQL/MariaDB, PostgreSQL),
// read into project tables (Db.readSchema(): Project > Import Tables), then
// read and written through them. Prints what's wrong, then "ok N".
var problems = 0, checks = 0;
function expect(what, got, want) {
    checks++;
    if (JSON.stringify(got) !== JSON.stringify(want)) {
        problems++;
        Test.out("FAIL " + what + ": " + JSON.stringify(got) + ", not " + JSON.stringify(want));
    }
}
function sql(s) { var e = Test.sql(s); if (e) { problems++; Test.out("FAIL sql: " + e + "\n  " + s); } }
var kind = Db.driver, lite = kind === "sqlite", my = kind === "mysql", pg = kind === "postgresql";
var q = function (n) { return my ? "`" + n + "`" : "\"" + n + "\""; };

// the database, as someone else made it
["orders", "products", "customers", "log", "Order Lines"].forEach(function (t) { Test.sql("DROP TABLE IF EXISTS " + q(t)); });
var autoKey = lite ? "INTEGER PRIMARY KEY" : my ? "INTEGER NOT NULL AUTO_INCREMENT PRIMARY KEY" : "SERIAL PRIMARY KEY";
var myTail = my ? " ENGINE=InnoDB" : "";
sql("CREATE TABLE customers (customer_id " + autoKey + ", " + q("Full Name") + " VARCHAR(80) NOT NULL, email VARCHAR(120) UNIQUE, "
    + "active BOOLEAN NOT NULL DEFAULT " + (pg ? "TRUE" : "1") + ", joined DATE" + (my ? "" : " DEFAULT CURRENT_DATE")
    + ", balance DECIMAL(10,2), photo " + (pg ? "BYTEA" : "BLOB") + ")" + myTail);
sql("CREATE TABLE products (sku VARCHAR(20) NOT NULL PRIMARY KEY, title VARCHAR(100) NOT NULL, price NUMERIC(8,2) DEFAULT 0, kind TEXT)" + myTail);
sql("CREATE TABLE orders (id " + (lite ? "INTEGER PRIMARY KEY AUTOINCREMENT" : autoKey) + ", customer_id INTEGER NOT NULL REFERENCES customers(customer_id), "
    + "sku VARCHAR(20) REFERENCES products(sku), qty INTEGER NOT NULL, ordered " + (pg ? "TIMESTAMP" : "DATETIME") + " DEFAULT CURRENT_TIMESTAMP, notes TEXT"
    + (my ? ", FOREIGN KEY (customer_id) REFERENCES customers(customer_id), FOREIGN KEY (sku) REFERENCES products(sku)" : "") + ")" + myTail);
sql("CREATE TABLE log (msg TEXT, at VARCHAR(30))" + myTail);
sql("CREATE TABLE " + q("Order Lines") + " (line INTEGER, note TEXT)" + myTail);
sql("INSERT INTO customers (customer_id, " + q("Full Name") + ", email, active, joined, balance) VALUES "
    + "(7, 'Ada Lovelace', 'ada@example.com', " + (pg ? "TRUE" : "1") + ", '2020-01-02', 12.5), (9, 'Grace Hopper', 'grace@example.com', " + (pg ? "FALSE" : "0") + ", '2021-03-04', 0)");
if (pg)
    sql("SELECT setval(pg_get_serial_sequence('customers', 'customer_id'), 9)");
sql("INSERT INTO products VALUES ('A-1', 'Anvil', 99.95, 'tool'), ('B-2', 'Bell', 5, 'toy')");
sql("INSERT INTO orders (customer_id, sku, qty, ordered, notes) VALUES (7, 'A-1', 2, '2024-05-06 10:30:00', 'first')");
sql("INSERT INTO log VALUES ('started', '2024-01-01')");
sql("INSERT INTO " + q("Order Lines") + " VALUES (1, 'one'), (2, 'two')");

// read
var r = Db.readSchema();
expect("read", r.ok, true);
var tables = {};
(r.tables || []).forEach(function (t) { tables[t.name] = t; });
function field(t, n) { return (tables[t].fields || []).filter(function (f) { return f.name === n; })[0] || null; }
function pick(o, keys) { var out = {}; keys.forEach(function (k) { if (o && o[k] !== undefined) out[k] = o[k]; }); return out; }
expect("customers", pick(tables.customers, ["key", "keyAuto", "stamps", "records"]), { key: "customer_id", keyAuto: true, stamps: false, records: 2 });
expect("its key isn't a field", field("customers", "customer_id"), null);
expect("a name with a space", pick(field("customers", "Full_Name"), ["column", "label", "type", "maxLength", "required"]),
       { column: "Full Name", label: "Full Name", type: "text", maxLength: 80, required: true });
expect("unique", pick(field("customers", "email"), ["type", "unique"]), { type: "text", unique: true });
expect("a Yes/No", pick(field("customers", "active"), ["type", "default"]), { type: "boolean", default: "yes" });
expect("a date", field("customers", "joined").type, "date");
if (!my)
    expect("a date's default", field("customers", "joined").default, "today");
expect("a decimal", pick(field("customers", "balance"), ["type", "decimals"]), { type: "decimal", decimals: 2 });
expect("binary: left out", [field("customers", "photo"), r.notes.some(function (n) { return n.indexOf("customers.photo") === 0; })], [null, true]);
expect("products: a typed-in key", pick(tables.products, ["key", "keyAuto"]), { key: "sku", keyAuto: false });
expect("it's a field", pick(field("products", "sku"), ["type", "required", "unique"]), { type: "text", required: true, unique: true });
expect("foreign keys: Lookups", [pick(field("orders", "customer_id"), ["type", "lookupTable", "required"]), pick(field("orders", "sku"), ["type", "lookupTable"])],
       [{ type: "lookup", lookupTable: "customers", required: true }, { type: "lookup", lookupTable: "products" }]);
expect("a timestamp", pick(field("orders", "ordered"), ["type", "default"]), { type: "datetime", default: "now" });
expect("a table's name with a space", pick(tables.Order_Lines, ["dbName"]), { dbName: "Order Lines" });
expect("no key", tables.log.key, lite ? "rowid" : "");
if (!lite)
    expect("no key: said", r.notes.some(function (n) { return n.indexOf("log has no key") === 0; }), true);

// the imported tables, as the project's
var mine = r.tables.filter(function (t) { return ["customers", "products", "orders", "log", "Order_Lines"].indexOf(t.name) >= 0; });
Db.setSchema(mine);
expect("nothing to make", Db.schemaPlan().actions, []);
expect("records", Db.select("customers", { orderBy: "Full_Name" }).map(function (x) { return [x.id, x.Full_Name, x.active, x.joined, x.balance]; }),
       [[7, "Ada Lovelace", true, "2020-01-02", 12.5], [9, "Grace Hopper", false, "2021-03-04", 0]]);
expect("get by its key", Db.get("customers", 9).email, "grace@example.com");
var n = Db.insert("customers", { Full_Name: "Linus", email: "linus@example.com" });
expect("insert: the database's number", [n.ok, typeof n.id, n.id > 9, Db.get("customers", n.id).Full_Name], [true, "number", true, "Linus"]);
expect("unique", Db.insert("customers", { Full_Name: "Again", email: "ada@example.com" }).errors, { email: "There is already a record with this email." });
expect("required, by the database's name", Db.insert("customers", {}).errors, { Full_Name: "Full Name is required." });
expect("update", [Db.update("customers", n.id, { balance: 3.25 }).ok, Db.get("customers", n.id).balance], [true, 3.25]);
var p = Db.insert("products", { sku: "C-3", title: "Cup" });
expect("a typed-in key", [p.ok, p.id, Db.get("products", "C-3").title], [true, "C-3", "Cup"]);
expect("the same key", Db.insert("products", { sku: "A-1", title: "Again" }).errors, { sku: "There is already a record with this sku." });
expect("a key changed", [Db.update("products", "C-3", { sku: "C-4" }).id, Db.get("products", "C-4").title, Db.get("products", "C-3").title],
       ["C-4", "Cup", undefined]);
var o = Db.insert("orders", { customer_id: 9, sku: "B-2", qty: 1 });
expect("Lookups both ways", [o.ok, Db.get("orders", o.id).sku, Db.get("orders", o.id).customer_id, Db.lookupLabel("products", "B-2"), Db.lookupLabel("customers", 9)],
       [true, "B-2", 9, "Bell", "Grace Hopper"]);
expect("a Lookup's list: the title, not the key", Db.lookupChoices("products").map(function (x) { return x.id + "=" + x.label; }), ["A-1=Anvil", "B-2=Bell", "C-4=Cup"]);
expect("Lookups refuse what isn't there", Object.keys(Db.insert("orders", { customer_id: 999, sku: "Z-9", qty: 1 }).errors).sort(), ["customer_id", "sku"]);
expect("in use", Db.remove("customers", 7).error, "This can't be deleted: a record in orders still uses it.");
expect("remove", [Db.remove("orders", o.id).ok, Db.remove("customers", 9).ok, Db.count("customers")], [true, true, 2]);
expect("a name with a space, read", Db.select("Order_Lines", { orderBy: "line", descending: true }).map(function (x) { return x.note; }), ["two", "one"]);
var l = Db.insert("log", { msg: "again", at: "2024-02-02" });
if (lite) {
    expect("SQLite's rowid as the key", [l.ok, l.id, Db.update("log", l.id, { msg: "changed" }).ok, Db.get("log", l.id).msg], [true, 2, true, "changed"]);
} else {
    expect("no key: added, not changed", [l.ok, Db.count("log"), Db.update("log", 1, { msg: "x" }).ok], [true, 2, false]);
}
expect("search", Db.select("customers", { search: "ADA" }).map(function (x) { return x.email; }), ["ada@example.com"]);
expect("where on its key", Db.select("customers", { where: { id: { op: ">", value: 7 } } }).length, 1);

["orders", "products", "customers", "log", "Order Lines"].forEach(function (t) { Test.sql("DROP TABLE IF EXISTS " + q(t)); });
Test.out(problems ? problems + " wrong of " + checks : "ok " + checks);
