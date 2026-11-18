// tests/studio/run.sh: the ORM through `win95apprun --eval` against
// contacts.w95proj. Each line is "what: result"; run.sh compares the lot
// with orm.expected.
function pad(n) { return (n < 10 ? "0" : "") + n; }
var now = new Date();
var today = now.getFullYear() + "-" + pad(now.getMonth() + 1) + "-" + pad(now.getDate());
function show(what, r) { Test.out(what + ": " + JSON.stringify(r).split(today).join("TODAY")); }
function names(rows) { return rows.map(function (r) { return r.Name; }); }
function plain(row) { delete row.created_on; delete row.modified_on; return row; }

show("tables", Db.tables.map(function (t) { return t.name; }));
show("driver", Db.driver);

// every rule refusing at once, by field
var bad = Db.insert("Contacts", { Name: "", Email: "nope", Phone: "hello", Age: "abc", Born: "2999-01-01", Kind: "Enemy",
                                  Company: 7, Code: "x", Discount: 80 });
show("refused", bad.ok);
show("refused.errors", bad.errors);
show("refused.messages", bad.messages.length);
show("nothing written", Db.count("Contacts"));

// validate() alone writes nothing
var v = Db.validate("Contacts", { Name: "A", Age: -1, Wakes: "25:00", Seen: "soon", Score: "1.2.3", Active: "maybe" });
show("validate", v.errors);
show("validate wrote", Db.count("Contacts"));
show("length", Db.validate("Contacts", { Name: "An unreasonably long name for anybody" }).errors);

var co = Db.insert("Companies", { Name: "Acme", Website: "https://acme.example" });
show("company", [co.ok, co.id]);
show("company again", Db.insert("Companies", { Name: "Acme" }).errors);
show("company url", Db.insert("Companies", { Name: "Bolt", Website: "bolt" }).errors);

// a good record: values come back in their types, trimmed and rounded
var ada = Db.insert("Contacts", { Name: " Ada ", Email: "ada@example.com", Phone: "+1 (555) 010-9999 x12", Age: "36",
                                  Balance: "12.34567", Score: 9.87, Ratio: 0.1, Born: "1985-12-10", Wakes: "7:05",
                                  Seen: "2024-03-05 08:30", Kind: "Family", Company: co.id, Code: "AB-123", Discount: 80,
                                  Notes: "line one\nline two" });
show("ada", [ada.ok, ada.id, ada.messages]);
show("ada row", plain(Db.get("Contacts", ada.id)));
show("ada stamped", typeof Db.get("Contacts", ada.id).created_on);

// defaults fill what isn't given
var bob = Db.insert("Contacts", { Name: "Bob", Age: 20 });
show("bob row", plain(Db.get("Contacts", bob.id)));
Db.insert("Contacts", { Name: "Cy", Age: 50, Active: false, Kind: "Work" });

// No Duplicates, and a record's own value isn't one
show("same e-mail", Db.insert("Contacts", { Name: "Eve", Email: "ada@example.com" }).errors);
show("update keeps e-mail", Db.update("Contacts", ada.id, { Email: "ada@example.com", Age: 37 }).ok);
show("update refused", Db.update("Contacts", ada.id, { Age: 200 }).errors);
show("update unknown field", Db.update("Contacts", ada.id, { Nope: 1 }).errors);
show("update gone", Db.update("Contacts", 999, { Age: 1 }).error);
show("age", Db.get("Contacts", ada.id).Age);
// a rule that reads the whole record
show("rule", Db.insert("Contacts", { Name: "Dee", Discount: 60 }).errors);
show("rule passes", Db.insert("Contacts", { Name: "Dee", Discount: 60, Kind: "Family" }).ok);

show("order", names(Db.select("Contacts", { orderBy: "Name", descending: true })));
show("where >=", names(Db.select("Contacts", { where: { Age: { op: ">=", value: 37 } } })));
show("where yes/no", names(Db.select("Contacts", { where: { Active: false } })));
show("where null", names(Db.select("Contacts", { where: { Email: null }, orderBy: "Name" })));
show("where like", names(Db.select("Contacts", { where: { Name: { op: "like", value: "%y" } } })));
show("search", names(Db.select("Contacts", { search: "EXAMPLE" })));
show("limit", names(Db.select("Contacts", { limit: 2, offset: 1 })));
show("count", [Db.count("Contacts"), Db.count("Contacts", { Kind: "Family" })]);
show("bad field", [Db.select("Contacts", { where: { "x; DROP TABLE Contacts": 1 } }).length, Db.lastError]);
show("bad table", Db.insert("Nope", {}).error);

show("lookup list", Db.lookupChoices("Companies"));
show("lookup label", Db.lookupLabel("Companies", co.id));
show("remove in use", Db.remove("Companies", co.id).error);
show("remove", Db.remove("Contacts", ada.id).ok);
show("remove free", Db.remove("Companies", co.id).ok);
show("defaults", Db.defaults("Contacts"));
show("names", [Db.nameProblem("Total"), Db.nameProblem("2nd"), Db.nameProblem("id"), Db.nameProblem("")]);
show("field info", Db.fieldInfo("Contacts", "Kind").choices);
