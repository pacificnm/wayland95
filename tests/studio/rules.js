// tests/studio/run.sh: every Validation Rule on the list, through
// `win95apprun --eval` (any project: this sets its own schema). Each rule
// gets values it has to take and values it has to refuse; what's wrong is
// printed, and "ok N" at the end.
var problems = [];
function bad(what) { problems.push(what); Test.out("FAIL " + what); }

function pad(n) { return (n < 10 ? "0" : "") + n; }
function day(offset) {
    var d = new Date();
    d.setDate(d.getDate() + offset);
    return d.getFullYear() + "-" + pad(d.getMonth() + 1) + "-" + pad(d.getDate());
}
function yearsAgo(years, days) {
    var d = new Date();
    d.setFullYear(d.getFullYear() - years);
    d.setDate(d.getDate() + (days || 0));
    return d.getFullYear() + "-" + pad(d.getMonth() + 1) + "-" + pad(d.getDate());
}
function weekdayFrom(offset, weekend) {
    for (var i = offset; ; ++i) {
        var d = new Date();
        d.setDate(d.getDate() + i);
        var w = d.getDay() === 0 || d.getDay() === 6;
        if (w === weekend)
            return day(i);
    }
}

// [rule, type, arg, [taken], [refused]]
var cases = [
    ["letters", "text", null, ["Ada", "Zoë", "Ñandú"], ["Ada1", "Ada Lovelace", "a-b"]],
    ["letters_spaces", "text", null, ["Ada Lovelace", "José María"], ["Ada2", "O'Neil"]],
    ["person_name", "text", null, ["Mary-Jane O'Neil", "J. R. R. Tolkien", "Zoë"], ["R2D2", "-Ada", "a_b"]],
    ["digits", "text", null, ["0123", "9"], ["12a", "1 2", "-1"]],
    ["alphanumeric", "text", null, ["abc123", "Zoë9"], ["abc 123", "a-b"]],
    ["alphanumeric_spaces", "text", null, ["abc 123"], ["abc_123", "a.b"]],
    ["no_spaces", "text", null, ["one-word"], ["two words", "tab\there"]],
    ["ascii", "text", null, ["Plain text! ~"], ["Zoë", "→"]],
    ["uppercase", "text", null, ["ABC 12", "ÉCOLE"], ["Abc"]],
    ["lowercase", "email", null, ["ada@example.com"], ["Ada@example.com"]],
    ["capitalized", "text", null, ["Hello there", "Émile"], ["hello", "1st"]],
    ["single_line", "memo", null, ["one line"], ["two\nlines"]],
    ["no_html", "text", null, ["1 < 2 and 3 > 2", "a <3 b"], ["<b>bold</b>", "x <script src=y>", "<!-- c -->"]],
    ["starts_with", "text", "AB-", ["AB-1"], ["ab-1", "XAB-"]],
    ["ends_with", "email", "@example.com", ["ada@example.com"], ["ada@example.org"]],
    ["contains", "text", "needle", ["hayneedlehay"], ["hayNeedlehay"]],
    ["not_contains", "memo", "spam", ["fine text"], ["buy SPAM now"]],
    ["one_of", "text", ["Red", "Green"], ["Red", "Green"], ["red", "Blue"]],
    ["not_one_of", "text", ["admin", "root"], ["ada"], ["Admin", "root"]],
    ["exact_length", "text", 5, ["12345"], ["1234", "123456"]],
    ["min_words", "memo", 3, ["one two three", "a b c d"], ["one two"]],
    ["max_words", "text", 2, ["one two"], ["one two three"]],
    ["email", "text", null, ["a.b@example.co.uk"], ["a@b", "a b@example.com", "@example.com"]],
    ["web_url", "url", null, ["http://example.com", "https://example.com:8080/a?b#c"], ["ftp://example.com", "https://"]],
    ["https_url", "text", null, ["https://example.com/x"], ["http://example.com"]],
    ["domain", "text", null, ["example.com", "a-b.example.co.uk"], ["example", "-a.com", "a..com", "exa mple.com"]],
    ["ipv4", "text", null, ["192.168.1.10", "0.0.0.0", "255.255.255.255"], ["256.1.1.1", "1.2.3", "1.2.3.4.5", "a.b.c.d"]],
    ["ipv6", "text", null, ["2001:db8::1", "::1", "::", "fe80::1:2:3:4", "1:2:3:4:5:6:7:8", "::ffff:192.168.1.1"],
     ["1:2:3:4:5:6:7:8:9", "2001:db8:::1", "1::2::3", "12345::", "192.168.1.1", "1:2:3:4:5:6:7::8:9"]],
    ["ip", "text", null, ["10.0.0.1", "::1"], ["10.0.0", "x::y"]],
    ["mac", "text", null, ["00:1A:2B:3C:4D:5E", "00-1a-2b-3c-4d-5e"], ["00:1A:2B:3C:4D", "00:1A-2B:3C:4D:5E", "00:1A:2B:3C:4D:5G"]],
    ["username", "text", null, ["ada_l", "a.b-c"], ["ab", "_ada", "ada lovelace", "a234567890123456789012345678901234"]],
    ["strong_password", "text", null, ["Abcdef1!", "Zoë-1234"], ["abcdef1!", "ABCDEF1!", "Abcdefg!", "Abcdefg1", "Ab1!"]],
    ["slug", "text", null, ["my-first-post", "post2"], ["My-post", "my--post", "-post", "my_post"]],
    ["identifier", "text", null, ["_name", "name2"], ["2name", "my-name"]],
    ["uuid", "text", null, ["123e4567-e89b-12d3-a456-426614174000", "{123E4567-E89B-12D3-A456-426614174000}"],
     ["123e4567e89b12d3a456426614174000", "{123e4567-e89b-12d3-a456-426614174000", "123e4567-e89b-12d3-a456-42661417400g"]],
    ["hex", "text", null, ["DEADbeef", "0x1F"], ["0x", "12G"]],
    ["hex_color", "text", null, ["#1A2B3C", "#abc"], ["1A2B3C", "#1A2B3", "#GGG"]],
    ["json", "memo", null, ["{\"a\": [1, 2]}", "[]"], ["{a: 1}", "[1, 2"]],
    ["phone_nanp", "phone", null, ["(212) 555-0123", "212.555.0123", "+1 212 555 0123", "2125550123 x12"],
     ["(012) 555-0123", "212 155 0123", "555-0123", "+44 20 7946 0958"]],
    ["phone_e164", "text", null, ["+44 20 7946 0958", "+12125550123"], ["020 7946 0958", "+0 123 456 789", "+12"]],
    ["zip_us", "text", null, ["90210", "90210-1234"], ["9021", "90210-12", "ABCDE"]],
    ["postal_ca", "text", null, ["K1A 0B1", "k1a0b1"], ["D1A 0B1", "K1A 0B", "12345"]],
    ["postcode_uk", "text", null, ["SW1A 1AA", "EC1A 1BB", "M1 1AE", "B33 8TH", "CR2 6XH", "DN55 1PT", "W1A 0AX", "gir 0aa"],
     ["SW1A 1A", "QA1 1AA", "12345"]],
    ["postcode_5", "text", null, ["75001"], ["7500", "750011"]],
    ["postcode_4", "text", null, ["2000"], ["200", "20000"]],
    ["country_code", "text", null, ["US", "GB", "DE"], ["us", "XX", "USA"]],
    ["language_code", "text", null, ["en", "fr", "haw"], ["EN", "qq", "english"]],
    ["currency_code", "text", null, ["USD", "EUR", "JPY"], ["usd", "ABC", "US"]],
    ["credit_card", "text", null, ["4111 1111 1111 1111", "5500-0000-0000-0004", "378282246310005"],
     ["4111 1111 1111 1112", "1234", "4111x1111x1111x1111"]],
    ["iban", "text", null, ["GB82 WEST 1234 5698 7654 32", "DE89370400440532013000"], ["GB82 WEST 1234 5698 7654 33", "GB82"]],
    ["bic", "text", null, ["DEUTDEFF", "NEDSZAJJXXX"], ["DEUTDEF", "deutdeff", "DEUTDEFF1"]],
    ["routing_us", "text", null, ["011000015", "021000021"], ["011000016", "12345678"]],
    ["ssn_us", "text", null, ["123-45-6789", "123456789"], ["000-12-3456", "666-12-3456", "912-12-3456", "123-00-4567", "123-45-0000"]],
    ["ein_us", "text", null, ["12-3456789"], ["123-456789", "12-345678"]],
    ["isbn", "text", null, ["978-0-306-40615-7", "0-306-40615-2", "080442957X"], ["978-0-306-40615-8", "0-306-40615-3", "12345"]],
    ["barcode", "text", null, ["4006381333931", "036000291452", "96385074", "10614141000415"], ["4006381333932", "12345"]],
    ["vin", "text", null, ["1HGCM82633A004352"], ["1HGCM82633A00435", "1HGCM82633A00435I"]],

    ["positive", "integer", null, [1, "5"], [0, -1]],
    ["not_negative", "decimal", null, [0, 2.5], [-0.01]],
    ["negative", "float", null, [-0.5], [0, 1]],
    ["not_zero", "integer", null, [-1, 1], [0]],
    ["greater_than", "currency", 10, [10.01], [10, 9]],
    ["less_than", "integer", 10, [9], [10, 11]],
    ["not_equal", "integer", 13, [12, 14], [13]],
    ["multiple_of", "decimal", 0.25, [1.75, 0, -0.5], [1.7, 0.1]],
    ["multiple_of", "integer", 5, [15, -10], [12]],
    ["whole", "float", null, [3, -2], [3.5]],
    ["max_decimals", "currency", 2, [1.25, 3, 0.1], [1.255, 0.001]],
    ["even", "integer", null, [2, 0, -4], [3, -1]],
    ["odd", "integer", null, [3, -1], [2, 0]],
    ["percentage", "decimal", null, [0, 100, 55.5], [-1, 100.5]],
    ["fraction", "float", null, [0, 1, 0.5], [1.01, -0.1]],
    ["latitude", "float", null, [-90, 51.5], [90.1, -91]],
    ["longitude", "float", null, [-180, 179.9], [180.5]],
    ["year", "integer", null, [1999, 2026], [999, 10000]],
    ["month", "integer", null, [1, 12], [0, 13]],
    ["day_of_month", "integer", null, [1, 31], [0, 32]],
    ["port", "integer", null, [1, 8080, 65535], [0, 65536]],

    ["past", "date", null, [day(-1)], [day(0), day(1)]],
    ["not_future", "date", null, [day(0), day(-10)], [day(1)]],
    ["future", "date", null, [day(1)], [day(0)]],
    ["not_past", "date", null, [day(0), day(5)], [day(-1)]],
    ["past", "datetime", null, [day(-1) + " 10:00"], [day(1) + " 10:00"]],
    ["future", "datetime", null, [day(2) + " 00:00"], [day(-2) + " 23:59"]],
    ["today", "date", null, [day(0)], [day(-1), day(1)]],
    ["within_past_days", "date", 7, [day(0), day(-7)], [day(-8), day(1)]],
    ["within_next_days", "date", 30, [day(0), day(30)], [day(31), day(-1)]],
    ["min_age", "date", 18, [yearsAgo(18), yearsAgo(40)], [yearsAgo(18, 1), day(0)]],
    ["max_age", "date", 65, [yearsAgo(65), day(0)], [yearsAgo(66), yearsAgo(65, -400)]],
    ["weekday", "date", null, [weekdayFrom(0, false)], [weekdayFrom(0, true)]],
    ["weekend", "datetime", null, [weekdayFrom(0, true) + " 09:00"], [weekdayFrom(0, false) + " 09:00"]],
    ["first_of_month", "date", null, ["2024-02-01"], ["2024-02-02"]],
    ["last_of_month", "date", null, ["2024-02-29", "2023-02-28", "2024-04-30"], ["2024-02-28", "2024-04-29"]],
    ["whole_hour", "time", null, ["14:00", "00:00"], ["14:01", "14:00:30"]],
    ["minute_step", "time", 15, ["9:15", "10:45", "0:00"], ["9:10", "9:15:01"]],
    ["minute_step", "datetime", 30, ["2024-01-01 09:30"], ["2024-01-01 09:20"]],
    ["working_hours", "time", null, ["9:00", "17:00", "12:30"], ["8:59", "17:01"]],
];

// one field with one rule, as the Data Designer keeps it
function schema(rule, type, arg, extra) {
    var v = { name: "V", label: "The Value", type: type, validator: rule };
    if (arg !== null && arg !== undefined)
        v.validatorArg = arg;
    for (var k in extra || {})
        v[k] = extra[k];
    return [{ name: "T", fields: [v, { name: "O", label: "Other", type: arguments[4] || type }] }];
}
var known = {};
["text", "memo", "email", "url", "phone", "integer", "decimal", "float", "currency", "boolean", "date", "time", "datetime", "choice", "lookup"]
    .forEach(function (t) { Db.validators(t).forEach(function (v) { known[v.id] = v; }); });

var checked = {}, count = 0;
cases.forEach(function (c) {
    var rule = c[0], type = c[1], arg = c[2];
    checked[rule] = true;
    if (!Db.validators(type).some(function (v) { return v.id === rule; }))
        bad(rule + " isn't offered for " + type);
    Db.setSchema(schema(rule, type, arg));
    c[3].forEach(function (value) {
        var r = Db.validate("T", { V: value }, null, true);
        count++;
        if (!r.ok)
            bad(rule + " (" + type + ") refused " + JSON.stringify(value) + ": " + r.messages.join(" "));
    });
    c[4].forEach(function (value) {
        var r = Db.validate("T", { V: value }, null, true);
        count++;
        if (r.ok)
            bad(rule + " (" + type + ") took " + JSON.stringify(value));
        else if (r.errors.V !== Db.ruleMessage(Db.tableInfo("T"), Db.fieldInfo("T", "V")) && r.errors.V.indexOf("The Value must be") !== 0)
            bad(rule + " (" + type + ") said " + JSON.stringify(r.errors.V) + " for " + JSON.stringify(value));
    });
});

// another field: [rule, type, otherType, [[value, other] taken], [[value, other] refused]]
var pairs = [
    ["same_as", "text", "text", [["pw", "pw"], [null, null]], [["pw", "PW"], ["pw", null], [null, "pw"]]],
    ["different_from", "email", "email", [["a@x.com", "b@x.com"], ["a@x.com", null]], [["a@x.com", "a@x.com"]]],
    ["more_than_field", "integer", "decimal", [[5, 4.5], [5, null]], [[4, 4], [3, 4.5]]],
    ["at_least_field", "currency", "integer", [[5, 5], [6, 5]], [[4.99, 5]]],
    ["less_than_field", "float", "float", [[1, 2]], [[2, 2]]],
    ["at_most_field", "integer", "integer", [[2, 2], [1, 2]], [[3, 2]]],
    ["after_field", "date", "date", [["2024-01-02", "2024-01-01"], ["2024-01-02", null]], [["2024-01-01", "2024-01-01"], ["1/1/2024", "2024-01-02"]]],
    ["on_or_after_field", "datetime", "date", [["2024-01-01 08:00", "2024-01-01"]], [["2023-12-31 23:00", "2024-01-01"]]],
    ["before_field", "time", "time", [["9:00", "17:00"]], [["17:00", "9:00"], ["9:00", "9:00"]]],
    ["on_or_before_field", "date", "datetime", [["2024-01-01", "2024-01-01 10:00"]], [["2024-01-02", "2024-01-01 10:00"]]],
    ["required_if", "text", "text", [["x", "y"], [null, null], ["x", null]], [[null, "y"]]],
    ["required_if", "text", "boolean", [[null, false], ["x", true]], [[null, true]]],
    ["required_if", "boolean", "text", [[true, "y"], [false, null]], [[false, "y"]]],
    ["required_unless", "phone", "email", [["555 0100", null], [null, "a@x.com"]], [[null, null]]],
    ["only_if", "text", "choice", [["why", "Other"], [null, null]], [["why", null]]],
    ["same_as", "lookup", "lookup", [[null, null]], [[null, 3]]],
];
pairs.forEach(function (c) {
    var rule = c[0], type = c[1], otherType = c[2];
    checked[rule] = true;
    var info = Db.validators(type).filter(function (v) { return v.id === rule; })[0];
    if (!info)
        bad(rule + " isn't offered for " + type);
    else if (info.argTypes.indexOf(otherType) < 0)
        bad(rule + " on " + type + " can't name a " + otherType);
    var s = schema(rule, type, "O", {}, otherType);
    if (otherType === "choice")
        s[0].fields[1].choices = ["Other", "None"];
    Db.setSchema(s);
    c[3].forEach(function (p) {
        var r = Db.validate("T", { V: p[0], O: p[1] }, null, true);
        count++;
        if (!r.ok)
            bad(rule + " (" + type + "/" + otherType + ") refused " + JSON.stringify(p) + ": " + r.messages.join(" "));
    });
    c[4].forEach(function (p) {
        var r = Db.validate("T", { V: p[0], O: p[1] }, null, true);
        count++;
        if (r.ok)
            bad(rule + " (" + type + "/" + otherType + ") took " + JSON.stringify(p));
    });
});

// every rule on the list is tried
for (var id in known) {
    if (!checked[id] && id !== "expression")
        bad("no test for " + id);
}

// the messages
function said(rule, type, arg, value, extra) {
    Db.setSchema(schema(rule, type, arg, extra));
    var r = Db.validate("T", { V: value }, null, true);
    return r.ok ? "(taken)" : r.errors.V;
}
function expect(what, got, want) { count++; if (got !== want) bad(what + ": " + JSON.stringify(got) + ", not " + JSON.stringify(want)); }
expect("a rule's own message", said("starts_with", "text", "AB", "x"), "The Value has to start with \"AB\".");
expect("a list in the message", said("one_of", "text", ["S", "M", "L"], "XL"), "The Value has to be one of: S, M, L.");
expect("the other field's caption", (function () {
    Db.setSchema(schema("more_than_field", "integer", "O"));
    return Db.validate("T", { V: 1, O: 2 }, null, true).errors.V;
})(), "The Value has to be more than Other.");
expect("a Yes/No other field", (function () {
    Db.setSchema(schema("required_if", "text", "O", {}, "boolean"));
    return Db.validate("T", { V: "", O: true }, null, true).errors.V;
})(), "The Value is required when Other is Yes.");
expect("the field's own Validation Text first", said("digits", "text", null, "x", { message: "Numbers, please." }), "Numbers, please.");
expect("a rule without its value", said("starts_with", "text", null, "x"), "The Value's validation rule (Starts with…) has no starts with set.");
expect("a rule about a field that's gone", said("same_as", "text", "Gone", "x"),
       "The Value's validation rule is about a field that isn't in the table (Gone).");
expect("a rule this doesn't know", said("nope", "text", null, "x"), "The Value's validation rule isn't one this knows (nope).");
expect("empty values aren't checked", said("digits", "text", null, ""), "(taken)");
expect("required still comes first", said("digits", "text", null, "", { required: true }), "The Value is required.");
expect("the expression, as before", said("expression", "integer", null, 60, { rule: "value <= 50" }), "The Value isn't valid.");
expect("a rule alone is an expression", said("", "integer", null, 60, { rule: "value <= 50", message: "Too much." }), "Too much.");
expect("the default message, from outside", Db.ruleMessage({ fields: [] }, { name: "Zip", type: "text", validator: "zip_us" }),
       "Zip has to be a ZIP code, like 90210 or 90210-1234.");
expect("a pattern's default message", Db.ruleMessage({ fields: [] }, { name: "Code", type: "text", pattern: "[A-Z]+" }),
       "Code isn't in the form it has to be.");
expect("the summary", Db.ruleSummary({ fields: [] }, { name: "N", type: "integer", validator: "greater_than", validatorArg: 5 }),
       "has to be more than 5");
expect("every rule's message starts with the field",
       Object.keys(known).filter(function (id) { return known[id].message.indexOf("%1 ") !== 0; }).join(","), "");

Test.out(problems.length ? problems.length + " wrong of " + count : "ok " + Object.keys(known).length + " rules, " + count + " checks");
