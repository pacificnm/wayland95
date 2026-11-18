// tests/studio/run.sh: API tables (mockapi.py, and api.w95proj made for its
// port by run.sh): each kind of sign-in, an endpoint read as a table, and
// its records read, added, changed and deleted as any table's. Prints
// what's wrong, then "ok N".
var problems = 0, checks = 0;
function expect(what, got, want) {
    checks++;
    if (JSON.stringify(got) !== JSON.stringify(want)) {
        problems++;
        Test.out("FAIL " + what + ": " + JSON.stringify(got) + ", not " + JSON.stringify(want));
    }
}
function wait(ms) { var end = Date.now() + ms; while (Date.now() < end) {} }
function pick(o, keys) { var out = {}; keys.forEach(function (k) { if (o && o[k] !== undefined) out[k] = o[k]; }); return out; }

// --- an endpoint, read as a table (Add API Table)
var r = Db.readEndpoint("key", "/key/products");
expect("read", [r.ok, r.recordsPath, r.key, r.count], [true, "data", "id", 2]);
expect("its fields, in the API's order", (r.fields || []).map(function (f) { return f.name + " " + f.type + (f.path ? " " + f.path : ""); }),
       ["name text", "price float", "inStock boolean", "released date", "vendor_name text vendor.name", "vendor_email email vendor.email", "updated datetime"]);
expect("a sample", r.sample.map(function (s) { return [s.id, s.name, s.vendor_name]; }), [[1, "Anvil", "Acme"], [2, "Bell", "Acme"]]);

// --- the table, as the project has it (its fields: a copy to change; r.fields is made anew each time)
var products = { name: "Products", source: "api", api: "key", endpoint: "/key/products", recordsPath: "data", itemPath: "data",
                 key: "id", keyAuto: true, stamps: false, fields: JSON.parse(JSON.stringify(r.fields)) };
products.fields.forEach(function (f) { if (f.name === "name") { f.required = true; f.unique = true; } });
var people = { name: "People", source: "api", api: "open", endpoint: "/open/people", key: "uuid", keyAuto: true, stamps: false,
               fields: Db.readEndpoint("open", "/open/people").fields };
var orders = { name: "Orders", fields: [{ name: "Who", type: "lookup", lookupTable: "People", required: true }, { name: "Qty", type: "integer" }] };
Db.setSchema([products, people, orders]);
expect("API tables: nothing for the database to make", Db.schemaPlan().actions, ["Create the table Orders"]);
Db.syncSchema();

var rows = Db.select("Products");
expect("select", rows.map(function (x) { return [x.id, x.name, x.price, x.inStock, x.released, x.vendor_name]; }),
       [[1, "Anvil", 99.5, true, "2024-01-02", "Acme"], [2, "Bell", 5, false, "2024-02-03", "Acme"]]);
var utc = new Date(Date.UTC(2024, 4, 6, 10, 30, 0));
function pad(n) { return (n < 10 ? "0" : "") + n; }
var local = utc.getFullYear() + "-" + pad(utc.getMonth() + 1) + "-" + pad(utc.getDate()) + "T" + pad(utc.getHours()) + ":" + pad(utc.getMinutes()) + ":00";
expect("a UTC time, in this computer's", rows[0].updated, local);
expect("where, search, order, limit", [Db.select("Products", { where: { price: { op: "<", value: 10 } } }).length,
                                        Db.select("Products", { search: "ANV" }).map(function (x) { return x.name; }),
                                        Db.select("Products", { orderBy: "price" }).map(function (x) { return x.name; }),
                                        Db.select("Products", { orderBy: "name", descending: true, limit: 1 }).map(function (x) { return x.name; }),
                                        Db.count("Products", { inStock: true })],
       [1, ["Anvil"], ["Bell", "Anvil"], ["Bell"], 1]);
expect("get", Db.get("Products", 2).name, "Bell");
expect("the API's own time: read-only", (r.fields || []).filter(function (f) { return f.readOnly; }).map(function (f) { return f.name; }), ["updated"]);
var made = Db.insert("Products", { name: "Cup", price: 2.5, inStock: true, released: "2024-03-04", vendor_name: "Bolt", vendor_email: "b@bolt.example" });
expect("insert: POSTed, its id the API's", [made.ok, made.id, Db.get("Products", made.id).vendor_name, Db.count("Products")], [true, 3, "Bolt", 3]);
expect("the table's rules first", Db.insert("Products", { price: 1 }).errors, { name: "name is required." });
expect("No Duplicates, in the API's list", Db.insert("Products", { name: "Anvil" }).errors, { name: "There is already a record with this name." });
var refused = Db.insert("Products", { name: "Bad", price: -1 });
expect("what the API refuses, it says", [refused.ok, refused.error.indexOf("422") > 0, refused.error.indexOf("price can't be negative") > 0], [false, true, true]);
expect("update: PATCHed", [Db.update("Products", made.id, { price: 3 }).ok, Db.get("Products", made.id).price, Db.get("Products", made.id).name], [true, 3, "Cup"]);
products.updateMethod = "PUT";
Db.setSchema([products, people, orders]);
expect("update: PUT, the whole record", [Db.update("Products", made.id, { name: "Mug" }).ok, Db.get("Products", made.id).name, Db.get("Products", made.id).price],
       [true, "Mug", 3]);
expect("remove: DELETEd", [Db.remove("Products", made.id).ok, Db.get("Products", made.id).id, Db.count("Products")], [true, undefined, 2]);
products.canDelete = false;
Db.setSchema([products, people, orders]);
expect("can't delete: said", Db.remove("Products", 1).error, "Products's records can't be deleted here.");

// --- text keys, a plain list; a database table looking one up
expect("text keys", [Db.get("People", "p-2").name, Db.select("People").map(function (x) { return x.id; })], ["Grace", ["p-1", "p-2"]]);
var pmade = Db.insert("People", { name: "Linus", email: "linus@example.com" });
expect("its id: text", [pmade.ok, pmade.id], [true, "p-3"]);
var o = Db.insert("Orders", { Who: "p-1", Qty: 2 });
expect("a Lookup of an API's table", [o.ok, Db.get("Orders", o.id).Who, Db.lookupLabel("People", "p-1")], [true, "p-1", "Ada"]);
expect("and what isn't there", Db.insert("Orders", { Who: "p-9" }).errors, { Who: "Who: that record isn't in People." });
expect("its list", Db.lookupChoices("People").map(function (x) { return x.id + "=" + x.label; }), ["p-1=Ada", "p-2=Grace", "p-3=Linus"]);

// --- the sign-ins
function okFor(api, path) { var a = Db.callApi(api, "GET", path); return [a.ok, a.status]; }
expect("API key in the query", okFor("qkey", "/qkey/products"), [true, 200]);
expect("Basic", okFor("basic", "/basic/products"), [true, 200]);
expect("Bearer", okFor("bearer", "/bearer/products"), [true, 200]);
expect("a key from the environment (${env:...})", okFor("envkey", "/key/products"), [true, 200]);
var wrong = Db.callApi("wrong", "GET", "/key/products");
expect("the wrong key: refused, and said", [wrong.ok, wrong.status, wrong.error.indexOf("401") > 0 && wrong.error.indexOf("sign in first") > 0], [false, 401, true]);
expect("OAuth, client credentials", okFor("cc", "/oauth/products"), [true, 200]);
expect("again: the same token", okFor("cc", "/oauth/products"), [true, 200]);
wait(2200);
expect("run out: a new one", okFor("cc", "/oauth/products"), [true, 200]);
expect("OAuth, password", okFor("pw", "/oauth/products"), [true, 200]);
wait(2200);
expect("run out: refreshed", okFor("pw", "/oauth/products"), [true, 200]);
expect("OAuth, authorization code (PKCE, the browser)", okFor("code", "/oauth/products"), [true, 200]);
var bad = Db.callApi("badoauth", "GET", "/oauth/products");
expect("OAuth refused: said", [bad.ok, bad.error.indexOf("Signing in to badoauth didn't work") === 0], [false, true]);
var handed = Db.callApi("open", "GET", "/stats").data;
expect("what the server handed out", Object.keys(handed).sort().map(function (k) { return k + " " + handed[k]; }),
       ["authorization_code 1", "client_credentials 2", "password 1", "refresh_token 1"]);
expect("the code's own call", Db.callApi("key", "POST", "/key/products", { name: "Via code", price: 1 }).data.data.name, "Via code");

Test.out(problems ? problems + " wrong of " + checks : "ok " + checks);
