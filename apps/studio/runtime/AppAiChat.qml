import QtQuick
import Win95Ui

// AIChat: a chat with Claude (Anthropic's AI) inside the app. The person
// using the app types; Claude answers (in Markdown, as it comes) and, with
// the tools this chat gives it, works the app for them:
//
//   DataAccess Read             list_tables, find_records, get_record
//              Read and Change  ... add_record, update_record, delete_record
//   FormAccess                  list_forms, read_form, set_field, set_element,
//                               click_element, form_action, open_form
//   ApiAccess                   call_api (the project's APIs)
//   and the app's own:          chat1.addTool(name, description, inputs, function (input) { ... })
//
// With ConfirmChanges the person is asked before each change (a record
// added, changed or deleted, a form's record saved or deleted, an API
// called with anything but GET).
//
// The key is the project's (Project > Properties in the Studio; blank:
// ANTHROPIC_API_KEY): the app never asks for one.
//
//   chat1.send("text")  clear()  cancel()  addTool()  removeTool(name)  hasKey
//   chat1.busy  lastReply  lastTool { name, input, result, isError }  errorString
//   chat1.messages (the conversation, as the API has it)  usage { input, output }
AppElement {
    id: chat

    property string model: "claude-opus-5-5"
    property string instructions: ""
    property string greeting: "Hello! What can I do for you?"
    property string effort: "Default"
    property int maxTokens: 16000
    property int maxSteps: 20
    property string dataAccess: "Read"
    property bool formAccess: true
    property bool apiAccess: false
    property bool confirmChanges: true
    property bool showToolCalls: true
    signal replied()
    signal toolUsed()
    signal failed()

    readonly property bool busy: running
    property var messages: []
    property string lastReply: ""
    property var lastTool: ({})
    property string errorString: ""
    property var usage: ({ input: 0, output: 0 })
    // whether the app has an Anthropic API key (the project's, or ANTHROPIC_API_KEY)
    readonly property bool hasKey: client.hasKey
    readonly property string noKey: "This app has no Anthropic API key: it's set in Wayland Studio's Project > Properties (or ANTHROPIC_API_KEY where it runs)."

    defaultBack: Theme.face
    activeFocusOnTab: false

    // --- the conversation ---------------------------------------------------

    property bool running: false
    property string status: ""
    property var startMessages: []
    property int turnFirstRow: 0
    property int steps: 0
    property string lastSent: ""
    property int streamRow: -1
    property int streamBlock: -1
    property var customTools: ({})
    // (the results of this turn's tool calls so far: a Stop part way keeps them)
    property var pendingResults: []

    AiClient {
        id: client
        onTextDelta: (index, text) => chat.streamText(index, text)
        onBlockStarted: (index, block) => {
            if (block.type === "tool_use")
                chat.status = "Claude is using " + block.name + "…";
            else if (block.type === "thinking" || block.type === "redacted_thinking")
                chat.status = "Claude is thinking…";
        }
        onFinished: (message) => chat.answered(message)
        onFailed: (message, status, type) => {
            // (nothing came of it: as if it wasn't sent, and the text back to send again)
            if (chat.steps === 0 && chat.running) {
                chat.messages = chat.startMessages;
                if (input.text === "")
                    input.text = chat.lastSent;
            }
            chat.stopWith(message, true);
        }
    }

    function send(text) {
        text = text === undefined || text === null ? "" : String(text).trim();
        if (text === "" || running || designMode)
            return false;
        if (!client.hasKey) {
            stopWith(noKey, true);
            return false;
        }
        errorString = "";
        startMessages = messages;
        var m = messages.slice(), last = m.length ? m[m.length - 1] : null;
        if (last && last.role === "user") {
            // (after Stop, or an answer cut short: the tools' results, then this)
            var content = typeof last.content === "string" ? [{ type: "text", text: last.content }] : last.content.slice();
            content.push({ type: "text", text: text });
            m[m.length - 1] = { role: "user", content: content };
        } else {
            m.push({ role: "user", content: text });
        }
        messages = m;
        show("user", text);
        lastSent = text;
        turnFirstRow = shown.count;
        steps = 0;
        running = true;
        request();
        return true;
    }

    function clear() {
        cancel();
        messages = [];
        errorString = "";
        lastReply = "";
        shown.clear();
        if (greeting !== "")
            show("assistant", greeting);
    }

    function cancel() {
        if (!running)
            return;
        client.cancel();
        closeOpenTools("The person stopped Claude, so this wasn't done.");
        running = false;
        status = "";
        show("note", "(Stopped.)");
    }

    // the app's own tool: `inputs` is { name: { type: "string", description: "..." } }
    // (or a whole JSON Schema); `run(input)` returns what Claude is told
    // (throw to tell it what went wrong)
    function addTool(name, description, inputs, run) {
        var schema = inputs && inputs.type === "object" ? inputs : { type: "object", properties: inputs || {} };
        var t = Object.assign({}, customTools);
        t[String(name)] = { name: String(name), description: String(description || ""), input_schema: schema,
                            run: function (input, reply) { reply(run(input), false); } };
        customTools = t;
    }
    function removeTool(name) {
        var t = Object.assign({}, customTools);
        delete t[String(name)];
        customTools = t;
    }

    function request() {
        pendingResults = [];
        streamRow = -1;
        streamBlock = -1;
        status = "Claude is thinking…";
        var body = {
            model: model,
            max_tokens: maxTokens > 0 ? maxTokens : 16000,
            system: systemPrompt(),
            messages: messages,
            // (the conversation so far is cached: each step re-reads less)
            cache_control: { type: "ephemeral" }
        };
        var defs = tools().map(function (t) {
            var d = { name: t.name, description: t.description, input_schema: t.input_schema };
            if (!client.customServer())
                d.eager_input_streaming = true;
            return d;
        });
        if (defs.length > 0)
            body.tools = defs;
        if (effort !== "" && effort !== "Default")
            body.output_config = { effort: effort.toLowerCase() };
        // a request a model's safety checks decline goes to the one
        // Anthropic recommends (Haiku has none)
        if (model.indexOf("haiku") < 0)
            body.fallbacks = "default";
        if (!client.send(body))
            stopWith("Claude is still answering.", true);
    }

    function answered(message) {
        if (!running)
            return;
        var u = message.usage || {};
        usage = { input: usage.input + (u.input_tokens || 0) + (u.cache_read_input_tokens || 0) + (u.cache_creation_input_tokens || 0),
                  output: usage.output + (u.output_tokens || 0) };
        var stop = message.stop_reason;
        if (stop === "refusal") {
            // (what came before the refusal isn't kept)
            while (shown.count > turnFirstRow)
                shown.remove(shown.count - 1);
            messages = startMessages;
            stopWith("Claude can't help with that request.", true);
            return;
        }
        var content = echoed(message.content || []);
        messages = messages.concat([{ role: "assistant", content: content }]);
        var uses = content.filter(function (b) { return b.type === "tool_use"; });
        if (stop === "tool_use" && uses.length > 0) {
            if (++steps > Math.max(1, maxSteps)) {
                closeOpenTools("Not run: Claude had taken MaxSteps steps.");
                stopWith("(Claude stopped after " + maxSteps + " steps: ask it to go on.)", false);
                return;
            }
            pendingResults = [];
            runTools(uses, message.invalidInputs || {}, 0, pendingResults, function (results) {
                if (!running)
                    return;
                messages = messages.concat([{ role: "user", content: results }]);
                request();
            });
            return;
        }
        if (uses.length > 0)
            closeOpenTools("Not run: the answer was cut short.");
        if (stop === "pause_turn" && ++steps <= maxSteps) {
            request();
            return;
        }
        running = false;
        status = "";
        if (stop === "max_tokens")
            show("note", "(The answer was cut short: it reached MaxTokens.)");
        lastReply = content.filter(function (b) { return b.type === "text"; })
                           .map(function (b) { return b.text; }).join("\n\n");
        if (lastReply !== "")
            replied();
    }

    function stopWith(message, isError) {
        running = false;
        status = "";
        show(isError ? "error" : "note", message);
        if (isError) {
            errorString = message;
            failed();
        }
    }

    // an answer's blocks as they go back to the API: after a fallback part
    // way through, the first model's thinking and tool calls before it are
    // left out (the fallback block itself stays where it was)
    function echoed(content) {
        var boundary = -1;
        for (var i = 0; i < content.length; ++i) {
            if (content[i].type === "fallback")
                boundary = i;
        }
        if (boundary < 0)
            return content;
        var internal = ["thinking", "redacted_thinking", "tool_use", "server_tool_use"];
        return content.filter(function (b, i) { return i >= boundary || internal.indexOf(b.type) < 0; });
    }

    // the last answer's tool calls get their results: those that ran, theirs;
    // the others, why they didn't
    function closeOpenTools(why) {
        var last = messages.length ? messages[messages.length - 1] : null;
        if (!last || last.role !== "assistant" || typeof last.content === "string")
            return;
        var open = last.content.filter(function (b) { return b.type === "tool_use"; });
        if (open.length === 0)
            return;
        var done = pendingResults;
        messages = messages.concat([{ role: "user", content: open.map(function (b) {
            var r = done.filter(function (x) { return x.tool_use_id === b.id; })[0];
            return r ? r : { type: "tool_result", tool_use_id: b.id, is_error: true, content: why };
        }) }]);
        pendingResults = [];
    }

    // --- running Claude's tool calls -------------------------------------------

    function runTools(uses, invalid, i, results, done) {
        if (!running)
            return;
        if (i >= uses.length) {
            done(results);
            return;
        }
        var use = uses[i], got = false;
        status = "Claude is using " + use.name + "…";
        runTool(use, invalid[use.id], function (result, isError) {
            if (got || !running)
                return;
            got = true;
            var r = { type: "tool_result", tool_use_id: use.id, content: resultText(result) };
            if (isError)
                r.is_error = true;
            results.push(r);
            lastTool = { name: use.name, input: use.input, result: result, isError: !!isError };
            if (showToolCalls)
                show(isError ? "toolError" : "tool", toolLabel(use.name, use.input || {}, !isError) + (isError ? ": " + errorText(result) : ""));
            toolUsed();
            runTools(uses, invalid, i + 1, results, done);
        });
    }

    function runTool(use, invalidInput, reply) {
        if (invalidInput !== undefined) {
            reply({ INVALID_JSON: invalidInput }, true);
            return;
        }
        var t = tools().filter(function (x) { return x.name === use.name; })[0];
        if (!t) {
            reply("There's no tool called " + use.name + ".", true);
            return;
        }
        var problem = checkInput(t.input_schema, use.input);
        if (problem !== "") {
            reply("The input isn't right: " + problem, true);
            return;
        }
        try {
            t.run(use.input, reply);
        } catch (e) {
            reply(e && e.message ? e.message : String(e), true);
        }
    }

    // what went wrong, for the person (a refused record: its messages)
    function errorText(result) {
        if (result && typeof result === "object" && result.messages && result.messages.length)
            return result.messages.join(" ");
        if (result && typeof result === "object" && result.error)
            return String(result.error);
        if (result && typeof result === "object" && result.INVALID_JSON !== undefined)
            return "its input wasn't JSON";
        var s = resultText(result);
        return s.length > 200 ? s.substring(0, 200) + "…" : s;
    }
    function resultText(result) {
        var s = typeof result === "string" ? result : JSON.stringify(result === undefined ? null : result);
        if (s.length > 30000)
            s = s.substring(0, 30000) + "… (" + (s.length - 30000) + " more characters left out: ask for less)";
        return s;
    }

    // the input against its tool's schema (the stream doesn't check it)
    function checkInput(schema, input) {
        if (input === null || typeof input !== "object" || Array.isArray(input))
            return "it isn't an object";
        var props = schema.properties || {}, required = schema.required || [];
        for (var r = 0; r < required.length; ++r) {
            if (input[required[r]] === undefined || input[required[r]] === null)
                return "\"" + required[r] + "\" is missing";
        }
        for (var name in input) {
            var p = props[name];
            if (!p)
                continue;
            var v = input[name];
            if (p.enum && p.enum.indexOf(v) < 0)
                return "\"" + name + "\" must be one of " + p.enum.join(", ");
            var types = p.type === undefined ? [] : [].concat(p.type);
            if (types.length > 0 && !types.some(function (t) { return isType(v, t); }))
                return "\"" + name + "\" should be " + types.join(" or ");
        }
        return "";
    }
    function isType(v, t) {
        switch (t) {
        case "string": return typeof v === "string";
        case "number": return typeof v === "number" && isFinite(v);
        case "integer": return typeof v === "number" && Math.floor(v) === v;
        case "boolean": return typeof v === "boolean";
        case "array": return Array.isArray(v);
        case "object": return v !== null && typeof v === "object" && !Array.isArray(v);
        case "null": return v === null;
        }
        return true;
    }

    // with ConfirmChanges, ask the person first
    function confirm(what, yes, reply) {
        if (!confirmChanges) {
            yes();
            return;
        }
        status = "Waiting for you to say yes or no…";
        form.msgBox("Claude wants to " + what + "\n\nLet it?", "Claude", "question", ["Yes", "No"], function (answer) {
            if (!running)
                return;
            if (answer === 0) {
                try {
                    yes();
                } catch (e) {
                    reply(e && e.message ? e.message : String(e), true);
                }
            } else {
                reply("The person using the app said no, so this wasn't done.", true);
            }
        });
    }

    function db() { return form.db; }
    function app() { return form.app; }
    function tableNamed(name) {
        var info = db().tableInfo(String(name));
        if (!info || !info.name)
            throw new Error("There's no table called \"" + name + "\" (list_tables lists them).");
        return info.name;
    }
    function formNamed(name) {
        if (name === undefined || name === null || name === "" || name === form.formName)
            return form;
        var f = app().form(String(name));
        if (!f) {
            if (app().formNames.indexOf(String(name)) < 0)
                throw new Error("There's no form called \"" + name + "\" (list_forms lists them).");
            throw new Error("The form \"" + name + "\" isn't open (open_form opens it).");
        }
        return f;
    }
    function elementNamed(f, name) {
        var e = f.element(String(name));
        if (!e)
            throw new Error("The form " + f.formName + " has no element called \"" + name + "\" (read_form lists them).");
        return e;
    }
    function typeOf(e) {
        var m = /^App([A-Za-z]+?)(_QML|\()/.exec(String(e));
        var t = m ? m[1] : "";
        return t === "MenuBar" ? "Menu" : t === "AiChat" ? "AIChat" : t;
    }
    function describeElement(e) {
        var d = { name: e.objectName, type: typeOf(e) };
        if (e.field)
            d.field = e.field;
        if (!e.visible)
            d.visible = false;
        if (!e.enabled)
            d.enabled = false;
        ["text", "shown", "checked", "value", "mode", "group", "readOnly", "isPlaying"].forEach(function (p) {
            var v = e[p];
            if (v !== undefined && typeof v !== "function" && !(p === "shown" && v === e.text))
                d[p] = v;
        });
        if (e.list !== undefined && Array.isArray(e.list))
            d.choices = e.list.slice(0, 100);
        if (d.type === "DataGrid") {
            d.table = e.source;
            d.rows = e.rows.length;
            d.currentId = e.currentId;
        }
        return d;
    }
    function describeValues(values) {
        var lines = [];
        for (var k in values)
            lines.push("    " + k + ": " + (values[k] === null ? "(nothing)" : typeof values[k] === "object" ? JSON.stringify(values[k]) : values[k]));
        return lines.join("\n");
    }
    function changed(r, more) {
        if (!r.ok)
            return { ok: false, error: r.error || "", errors: r.errors || {}, messages: r.messages || [] };
        return Object.assign({ ok: true }, more || {});
    }

    function tool(name, description, properties, required, run) {
        return { name: name, description: description,
                 input_schema: { type: "object", properties: properties, required: required }, run: run };
    }

    // what Claude can use here (the chat's settings decide)
    function tools() {
        var t = [];
        var formArg = { type: "string", description: "The form's name; leave it out for the form this chat is on." };
        if (dataAccess !== "None") {
            t.push(tool("list_tables",
                "List the app's tables and their fields. Call this first whenever you need to know what data the app keeps or a field's exact name.",
                {}, [], function (input, reply) { reply(tablesSummary(true), false); }));
            t.push(tool("find_records",
                "Find records in one of the app's tables. Use it to answer any question about the app's data, and to find a record's id before changing it. Gives at most `limit` records and how many match in all.",
                { table: { type: "string", description: "The table's name, as list_tables gives it." },
                  where: { type: "object", description: "Fields that must equal these values: {\"City\": \"Paris\"}." },
                  search: { type: "string", description: "Text to look for in the table's text fields." },
                  orderBy: { type: "string", description: "The field to sort by." },
                  descending: { type: "boolean", description: "Sort from the highest." },
                  limit: { type: "integer", description: "How many records at most: 1 to 200 (25 if left out)." },
                  offset: { type: "integer", description: "How many matching records to skip (the next page)." } },
                ["table"], function (input, reply) {
                    var table = tableNamed(input.table), options = {};
                    ["where", "search", "orderBy", "descending"].forEach(function (k) {
                        if (input[k] !== undefined && input[k] !== null && input[k] !== "")
                            options[k] = input[k];
                    });
                    var all = Object.assign({}, options);
                    options.limit = Math.max(1, Math.min(200, input.limit || 25));
                    options.offset = Math.max(0, input.offset || 0);
                    var rows = db().select(table, options);
                    var total = options.search ? db().select(table, all).length : db().count(table, options.where || {});
                    reply({ table: table, total: total, records: rows }, false);
                }));
            t.push(tool("get_record", "One record of a table, by its id.",
                { table: { type: "string" }, id: { description: "The record's id." } }, ["table", "id"],
                function (input, reply) {
                    var r = db().get(tableNamed(input.table), input.id);
                    if (r.id === undefined)
                        reply("There's no record " + input.id + " in " + input.table + ".", true);
                    else
                        reply(r, false);
                }));
        }
        if (dataAccess === "Read and Change") {
            t.push(tool("add_record",
                "Add a record to a table. Fields left out get their defaults; the table's validation rules apply, and what they refuse comes back in `errors`.",
                { table: { type: "string" }, values: { type: "object", description: "Field name -> value." } }, ["table", "values"],
                function (input, reply) {
                    var table = tableNamed(input.table);
                    confirm("add a record to " + table + ":\n" + describeValues(input.values), function () {
                        var r = db().insert(table, input.values);
                        reply(changed(r, { id: r.id }), !r.ok);
                    }, reply);
                }));
            t.push(tool("update_record",
                "Change some fields of a record (find its id with find_records first). The table's validation rules apply.",
                { table: { type: "string" }, id: { description: "The record's id." },
                  values: { type: "object", description: "Field name -> new value, only the fields to change." } },
                ["table", "id", "values"], function (input, reply) {
                    var table = tableNamed(input.table);
                    if (db().get(table, input.id).id === undefined)
                        throw new Error("There's no record " + input.id + " in " + table + ".");
                    confirm("change record " + input.id + " of " + table + ":\n" + describeValues(input.values), function () {
                        var r = db().update(table, input.id, input.values);
                        reply(changed(r, { id: input.id }), !r.ok);
                    }, reply);
                }));
            t.push(tool("delete_record", "Delete a record, by its id. Only when the person asks for it.",
                { table: { type: "string" }, id: { description: "The record's id." } }, ["table", "id"],
                function (input, reply) {
                    var table = tableNamed(input.table);
                    var r0 = db().get(table, input.id);
                    if (r0.id === undefined)
                        throw new Error("There's no record " + input.id + " in " + table + ".");
                    confirm("delete record " + input.id + " of " + table + ":\n" + describeValues(r0), function () {
                        var r = db().remove(table, input.id);
                        reply(changed(r, { deleted: input.id }), !r.ok);
                    }, reply);
                }));
        }
        if (formAccess) {
            t.push(tool("list_forms", "The app's forms: which are open, and the table each open one shows.",
                {}, [], function (input, reply) {
                    var names = app().formNames, out = [];
                    for (var i = 0; i < names.length; ++i) {
                        var f = app().form(names[i]);
                        out.push(f ? { name: names[i], open: true, title: f.title, table: f.table } : { name: names[i], open: false });
                    }
                    reply({ thisForm: form.formName, forms: out }, false);
                }));
            t.push(tool("read_form",
                "What an open form shows: its record (with a DataTable), whether it's saved, what validation refused, and its elements (text boxes, check boxes, lists, buttons...) with their names and values. Call it before filling in or working a form.",
                { form: formArg }, [], function (input, reply) {
                    var f = formNamed(input.form);
                    reply({ form: f.formName, title: f.title, table: f.table, isNew: f.isNew, recordId: f.recordId,
                            record: f.record, unsaved: f.dirty, errors: f.errors,
                            elements: f.elements().filter(function (e) { return e !== chat; }).map(describeElement) }, false);
                }));
            t.push(tool("set_field",
                "Set a field of the record an open form shows, as typing it in would (it isn't saved until form_action save).",
                { form: formArg, field: { type: "string" }, value: { description: "The field's new value." } }, ["field"],
                function (input, reply) {
                    var f = formNamed(input.form);
                    if (f.table === "")
                        throw new Error("The form " + f.formName + " has no DataTable: set_element sets its elements.");
                    if (!f.fieldInfo(input.field).name)
                        throw new Error(f.table + " has no field called \"" + input.field + "\".");
                    f.setField(input.field, input.value === undefined ? null : input.value);
                    reply({ record: f.record }, false);
                }));
            t.push(tool("set_element",
                "Put a value in an element of an open form, as the person would: a TextBox's text, a CheckBox ticked (true) or not, an OptionButton chosen (true), a ComboBox's or ListBox's item (its text), a DateTimePicker's date (\"2030-01-31\", \"14:30\", \"today+7\").",
                { form: formArg, element: { type: "string", description: "The element's name, from read_form." },
                  value: { description: "What to put in it." } }, ["element"],
                function (input, reply) {
                    var e = elementNamed(formNamed(input.form), input.element), type = typeOf(e), v = input.value;
                    if (!e.enabled || !e.visible)
                        throw new Error(input.element + " is " + (!e.visible ? "hidden" : "disabled") + " just now.");
                    if (type === "TextBox") {
                        if (e.readOnly)
                            throw new Error(input.element + " is locked.");
                        e.text = v === null || v === undefined ? "" : String(v);
                    } else if (type === "Label") {
                        e.text = v === null || v === undefined ? "" : String(v);
                    } else if (type === "CheckBox") {
                        if (e.checked !== !!v)
                            e.toggle();
                    } else if (type === "OptionButton") {
                        if (v)
                            e.choose();
                    } else if (type === "ComboBox" || type === "ListBox") {
                        var at = e.list.indexOf(String(v));
                        if (at < 0 && e.lookups) {
                            for (var i = 0; i < e.lookups.length; ++i) {
                                if (String(e.lookups[i].id) === String(v))
                                    at = i;
                            }
                        }
                        if (at < 0)
                            throw new Error("\"" + v + "\" isn't one of " + input.element + "'s items: " + e.list.join(", "));
                        e.choose(at);
                    } else if (type === "DateTimePicker") {
                        e.setValue(v === null || v === undefined ? "" : String(v));
                    } else {
                        throw new Error(input.element + " is a " + type + ": it doesn't take a value.");
                    }
                    reply(describeElement(e), false);
                }));
            t.push(tool("click_element",
                "Click a Button (or a Label, CheckBox or OptionButton) on an open form: whatever the app does for that click happens.",
                { form: formArg, element: { type: "string", description: "The element's name, from read_form." } }, ["element"],
                function (input, reply) {
                    var e = elementNamed(formNamed(input.form), input.element), type = typeOf(e);
                    if (!e.enabled || !e.visible)
                        throw new Error(input.element + " is " + (!e.visible ? "hidden" : "disabled") + " just now.");
                    if (type === "Button" || type === "Label")
                        e.clicked();
                    else if (type === "CheckBox")
                        e.toggle();
                    else if (type === "OptionButton")
                        e.choose();
                    else
                        throw new Error(input.element + " is a " + type + ": it can't be clicked.");
                    reply({ clicked: input.element }, false);
                }));
            t.push(tool("form_action",
                "Do something to an open form's record: save it, start a new one, show another (showRecord, with its id), go to the first, previous, next or last, delete it, reset the form, or close the form.",
                { form: formArg,
                  action: { type: "string", enum: ["save", "newRecord", "showRecord", "first", "previous", "next", "last", "deleteRecord", "reset", "close"] },
                  id: { description: "The record to show (showRecord)." } }, ["action"],
                function (input, reply) {
                    var f = formNamed(input.form), a = input.action;
                    function state() { return { form: f.formName, isNew: f.isNew, recordId: f.recordId, record: f.record, errors: f.errors }; }
                    if (f.table === "" && ["save", "newRecord", "showRecord", "first", "previous", "next", "last", "deleteRecord"].indexOf(a) >= 0)
                        throw new Error("The form " + f.formName + " has no DataTable.");
                    if (a === "save") {
                        confirm("save the record on " + f.formName + ":\n" + describeValues(f.record), function () {
                            var ok = f.save();
                            reply(Object.assign({ ok: ok }, state()), !ok);
                        }, reply);
                    } else if (a === "deleteRecord") {
                        if (f.isNew)
                            throw new Error("The form's record isn't saved: there's nothing to delete.");
                        confirm("delete record " + f.recordId + " of " + f.table + " (on " + f.formName + ")", function () {
                            var ok = f.deleteRecord();
                            reply(Object.assign({ ok: ok }, state()), !ok);
                        }, reply);
                    } else if (a === "showRecord") {
                        if (!f.loadRecord(input.id))
                            throw new Error("There's no record " + input.id + " in " + f.table + ".");
                        reply(state(), false);
                    } else if (a === "close") {
                        if (f === form)
                            throw new Error("This chat is on that form: the person closes it.");
                        f.close();
                        reply({ closed: f.formName }, false);
                    } else {
                        f[a]();
                        reply(state(), false);
                    }
                }));
            t.push(tool("open_form", "Open one of the app's forms (or bring it to the front if it's open).",
                { form: { type: "string", description: "The form's name, from list_forms." },
                  args: { type: "object", description: "What the form is given (its code reads form.args)." } }, ["form"],
                function (input, reply) {
                    if (app().formNames.indexOf(input.form) < 0)
                        throw new Error("There's no form called \"" + input.form + "\" (list_forms lists them).");
                    app().showForm(input.form, input.args || {});
                    reply({ opened: input.form }, false);
                }));
        }
        if (apiAccess) {
            t.push(tool("call_api",
                "Make a request of one of the web APIs the app is connected to (its sign-in is added). Use it for what the API tables don't cover.",
                { api: { type: "string", description: "The API connection's name." },
                  method: { type: "string", enum: ["GET", "POST", "PUT", "PATCH", "DELETE"] },
                  path: { type: "string", description: "The path after the API's base address: \"/orders/7\"." },
                  query: { type: "object", description: "Query string values." },
                  body: { type: "object", description: "The JSON to send (POST, PUT, PATCH)." } }, ["api", "method", "path"],
                function (input, reply) {
                    var call = function () {
                        var r = db().callApi(input.api, input.method, input.path, input.body === undefined ? null : input.body, input.query || {});
                        reply(r, !r.ok);
                    };
                    if (input.method === "GET")
                        call();
                    else
                        confirm("call " + input.api + ": " + input.method + " " + input.path
                                + (input.body ? "\n" + JSON.stringify(input.body, null, 2) : ""), call, reply);
                }));
        }
        for (var name in customTools)
            t.push(customTools[name]);
        return t;
    }

    // the tables, for the system prompt (short) and list_tables (whole)
    function tablesSummary(whole) {
        var tables = db() ? db().tables : [], out = [];
        for (var i = 0; i < tables.length && (whole || i < 40); ++i) {
            var tb = tables[i], fields = [];
            var list = tb.fields || [];
            for (var j = 0; j < list.length; ++j) {
                var f = list[j];
                if (whole) {
                    var d = { name: f.name, type: f.type };
                    if (f.required) d.required = true;
                    if (f.type === "lookup") d.lookupTable = f.lookupTable;
                    if (f.choices && f.choices.length) d.choices = f.choices;
                    if (f.readOnly || f.auto) d.readOnly = true;
                    if (f.rule) d.rule = db().ruleSummary(tb, f);
                    fields.push(d);
                } else if (j < 30) {
                    fields.push(f.name + " (" + f.type + (f.type === "lookup" ? " of " + f.lookupTable : "") + (f.required ? ", required" : "") + ")");
                }
            }
            if (whole)
                out.push({ name: tb.name, api: tb.api || undefined, fields: fields });
            else
                out.push("- " + tb.name + (tb.api ? " (from the API " + tb.api + ")" : "") + ": id, " + fields.join(", "));
        }
        return whole ? { tables: out } : out.join("\n");
    }

    function systemPrompt() {
        var a = app(), lines = [];
        lines.push("You are Claude, the assistant built into \"" + (a.title || a.name) + "\", a desktop app made with Wayland Studio."
                   + " The person using the app is talking to you in a chat on its \"" + (form.title || form.formName) + "\" form (" + form.formName + ").");
        lines.push("Use your tools to look things up and to do things in the app rather than guessing, and say plainly what you did."
                   + " The chat is small and shows Markdown: keep answers short.");
        if (dataAccess === "Read")
            lines.push("You can read the app's records but not change them.");
        else if (dataAccess === "None")
            lines.push("You can't see the app's records.");
        if (confirmChanges && (dataAccess === "Read and Change" || formAccess || apiAccess))
            lines.push("The person is asked before each change you make; if they say no, don't try it again unless they ask.");
        lines.push("Today is " + Qt.formatDate(new Date(), "dddd d MMMM yyyy") + ".");
        if (dataAccess !== "None" && db() && db().tables.length > 0)
            lines.push("\nThe app's tables (every record also has an id):\n" + tablesSummary(false));
        if (formAccess)
            lines.push("\nThe app's forms: " + a.formNames.join(", ") + "." + (form.table !== "" ? " This form shows a record of " + form.table + "." : ""));
        if (instructions.trim() !== "")
            lines.push("\n" + instructions.trim());
        return lines.join("\n");
    }

    // a line for a tool call: what was done, or (it wasn't) what Claude tried
    function toolLabel(name, input, done) {
        var f = input.form ? " on " + input.form : "";
        function say(did, tried) { return done ? did : "Couldn't " + tried; }
        switch (name) {
        case "list_tables": return say("Looked at the tables", "look at the tables");
        case "find_records": return say("Looked in " + input.table, "look in " + input.table) + (input.search ? " for \"" + input.search + "\"" : "");
        case "get_record": return say("Read ", "read ") + input.table + " " + input.id;
        case "add_record": return say("Added", "add") + " a record to " + input.table;
        case "update_record": return say("Changed ", "change ") + input.table + " " + input.id;
        case "delete_record": return say("Deleted ", "delete ") + input.table + " " + input.id;
        case "list_forms": return say("Looked at the forms", "look at the forms");
        case "read_form": return say("Read the form", "read the form") + (input.form ? " " + input.form : "");
        case "set_field": return say("Set ", "set ") + input.field + f;
        case "set_element": return say("Set ", "set ") + input.element + f;
        case "click_element": return say("Clicked ", "click ") + input.element + f;
        case "form_action": return say("Form: ", "do ") + input.action + f;
        case "open_form": return say("Opened ", "open ") + input.form;
        case "call_api": return (done ? "" : "Couldn't call ") + input.method + " " + input.api + " " + input.path;
        }
        return say("Used ", "use ") + name;
    }

    // --- what shows ----------------------------------------------------------

    ListModel { id: shown }

    function show(kind, text) {
        var stick = view.atYEnd || view.contentHeight <= view.height;
        shown.append({ kind: kind, text: String(text) });
        if (stick)
            Qt.callLater(view.positionViewAtEnd);
    }
    function streamText(index, text) {
        if (!running)
            return;
        if (streamRow < 0 || streamRow >= shown.count) {
            show("assistant", "");
            streamRow = shown.count - 1;
            streamBlock = index;
        } else if (index !== streamBlock) {
            streamBlock = index;
            text = "\n\n" + text;
        }
        var stick = view.atYEnd || view.contentHeight <= view.height;
        shown.setProperty(streamRow, "text", shown.get(streamRow).text + text);
        status = "";
        if (stick)
            Qt.callLater(view.positionViewAtEnd);
    }

    Component.onCompleted: {
        if (greeting !== "")
            show("assistant", greeting);
        // (said at once, for whoever made the app to see on Start)
        if (!designMode && !client.hasKey)
            show("error", noKey);
    }
    onGreetingChanged: {
        if (designMode) {
            shown.clear();
            if (greeting !== "")
                show("assistant", greeting);
        }
    }

    // the conversation
    Item {
        id: transcript
        x: 0
        y: 0
        width: parent.width
        height: parent.height - bottom.height - 4
        Bevel {
            anchors.fill: parent
            style: "sunken"
            face: chat.backColor !== "" ? chat.backColor : Theme.window
        }
        ListView {
            id: view
            x: 4
            y: 3
            // (the scroll bar's room is kept even when it isn't needed: text
            // narrowed by it would be taller, and need it)
            width: parent.width - 10 - bar.width
            height: parent.height - 6
            clip: true
            spacing: 6
            boundsBehavior: Flickable.StopAtBounds
            model: shown
            delegate: Column {
                id: entry
                required property string kind
                required property string text
                width: ListView.view.width
                spacing: 1
                Text {
                    visible: entry.kind === "user" || entry.kind === "assistant"
                    text: entry.kind === "user" ? "You" : "Claude"
                    color: entry.kind === "user" ? "#000080" : "#800000"
                    font.family: chat.family
                    font.pixelSize: chat.pixelSize
                    font.bold: true
                }
                TextEdit {
                    width: parent.width
                    readOnly: true
                    selectByMouse: !chat.designMode
                    wrapMode: TextEdit.Wrap
                    textFormat: entry.kind === "assistant" ? TextEdit.MarkdownText : TextEdit.PlainText
                    text: entry.kind === "tool" || entry.kind === "toolError" ? "⚙ " + entry.text : entry.text
                    color: entry.kind === "error" || entry.kind === "toolError" ? "#c00000"
                         : entry.kind === "tool" || entry.kind === "note" ? Theme.disabledText : chat.textColor
                    font.family: chat.family
                    font.pixelSize: entry.kind === "tool" || entry.kind === "toolError" ? Math.max(9, chat.pixelSize - 1) : chat.pixelSize
                    font.italic: entry.kind === "note" || chat.fontItalic
                    font.bold: chat.fontBold && entry.kind !== "tool"
                    selectionColor: Theme.selection
                    selectedTextColor: Theme.selectionText
                }
            }
            footer: Item {
                width: view.width
                height: chat.status !== "" ? statusText.implicitHeight + 6 : 0
                Text {
                    id: statusText
                    y: 6
                    width: parent.width
                    visible: chat.status !== ""
                    text: chat.status
                    elide: Text.ElideRight
                    color: Theme.disabledText
                    font.family: chat.family
                    font.pixelSize: chat.pixelSize
                    font.italic: true
                }
            }
        }
        ScrollBar95 {
            id: bar
            flickable: view
            anchors.right: parent.right
            anchors.rightMargin: 2
            y: 2
            height: parent.height - 4
        }
    }
    onStatusChanged: {
        if (view.atYEnd || view.contentHeight <= view.height + 30)
            Qt.callLater(view.positionViewAtEnd);
    }

    // what the person types
    Item {
        id: bottom
        y: parent.height - height
        width: parent.width
        height: 23
        TextField95 {
            id: input
            x: 0
            width: parent.width - sendButton.width - 4
            height: parent.height
            enabled: chat.enabled && !chat.designMode
            activeFocusOnTab: !chat.designMode
            input.font.family: chat.family
            input.font.pixelSize: chat.pixelSize
            onAccepted: sendButton.clicked()
            onEscaped: {
                if (chat.running)
                    chat.cancel();
            }
        }
        Button95 {
            id: sendButton
            anchors.right: parent.right
            width: 64
            height: parent.height
            text: chat.running ? "Stop" : "Send"
            enabled: chat.enabled && !chat.designMode && (chat.running || input.text.trim() !== "")
            onClicked: {
                if (chat.running) {
                    chat.cancel();
                } else if (chat.send(input.text)) {
                    input.text = "";
                }
                input.forceActiveFocus();
            }
        }
    }
}
