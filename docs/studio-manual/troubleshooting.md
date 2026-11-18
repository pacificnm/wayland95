[Contents](index.md) | Previous: [Running and Making Programs](running.md)

# When Something Isn't Right

**A label shows nothing when the app runs.** It has a **DataField**: a
label with one shows that field's value instead of its Caption (on the
canvas it says `[field]`), and a new record's field is empty. For a
caption beside a box, clear the label's DataField and give the box one.

**Save is refused, and I don't see why.** The fields that were refused
have a red frame; rest the pointer on one for its message, or turn on the
form's **ShowErrors** for a message box that lists them. Often it's a
field that's **Required** with no **Default Value** that isn't on the form
at all: put it on the form, give it a default, or untick Required.

**New Record in the Data view is refused.** The same: the message is in
red under the fields. A field the database fills in itself (a Default
Value of *When the record is made* or *When the record is saved*) is never
the reason.

**A date field won't take a "created" or "last changed" time.** Set its
**Default Value** to *When the record is made* or *When the record is
saved*: the database fills it in. A plain default (Now) is only filled in
once, when a new record starts.

**The grid is empty.** It shows its table's records, and the table may
have none yet: add some in the Data view, or with the form. The canvas
never shows records, only the columns; press F5 to see them.

**The Data view says the database isn't up to the project.** The project
has a table or a field the database doesn't have yet. **Yes** adds them;
nothing that's there is changed.

**I pointed the project at a database, and its tables aren't there.**
**Project > Import Tables...** brings them in (the records stay in the
database). Starting the app doesn't: it only makes the project's own
tables.

**An imported table's records can't be changed.** The table has no key
in the database (or one of several columns): records can be read and
added, but there's nothing to say which one to change. Give the table a
primary key in the database, then remove it from the project and import
it again.

**An API's table says "... said 401".** The API didn't take its sign-in:
**Project > APIs...**, check the key or the password, and **Test**. For
OAuth, **Sign Out** then **Sign In** gets new tokens. A value written
`${env:NAME}` is empty unless that variable is set where the program
runs.

**An API's table is empty, or says it has no list of records.** Its
**Records at** says where the list is in the API's answer (`data`,
`data.items`); **Add API Table**'s **Read** shows what it found.

**An API's table has only some of its records.** The API gives its list a
page at a time: the table's **Query** can ask for a bigger page
(`per_page=100`, `limit=500`: the API's own words for it).

**A Validation Rule says it "has no ... set".** The rule needs a value
of its own (the row under it in the field's properties): the text to start
with, a number of days, the other field.

**A rule names a field that isn't in the table.** The other field of an
*another field* rule was deleted: pick another, or another rule.

**Start says an item opens a form that isn't in the project.** A menu
item or a tool button has the Action *Open a form*, and that form was
deleted (renaming one is followed). Open the Menu Editor and pick one.

**My code doesn't run.** Look in the Output window: a mistake is named by
form, event and line. Names are as they're written: `titleBox`, not
`TitleBox`. And a message box doesn't wait: what to do with its answer
goes in its function.

**The program I made doesn't have my records.** A program keeps its own
SQLite file, in `~/.local/share/<its name>`, and starts with empty tables.
The records you typed in the Studio are the project's. To share data,
keep it on a MySQL or PostgreSQL server (**Project > Database...**).

**A graph says "No data".** Its table has no records, none pass its
**Only where**, or its Values field is empty in all of them. The Graph
Editor's preview shows the records the Studio's database has.

**A graph shows made-up values on the form.** That's the canvas: it
always does. Press **F5** to see the records.

**The AI chat says the app has no Anthropic API key.** The project has
none (**Project > Properties...**, **API key**), and `ANTHROPIC_API_KEY`
isn't set where the app runs; or the key is `${env:NAME}` and that
variable isn't set there. Give it one, and **Test** it.

**The AI chat says "The API key wasn't accepted."** The key is wrong, or
was revoked: put a new one in **Project > Properties...** and **Test** it.

**Claude says it can't change records, or can't see the form.** Its
**DataAccess** is **Read** (the default) or **None**, or **FormAccess** is
off: change them in its properties.

**Claude stopped "after 20 steps".** It used **MaxSteps** tools for one
message. Ask it to go on, or give it more steps.

**The manual doesn't open.** It opens in the Markdown Reader
(`win95markdown --reader`, the Markdown Editor's reading mode); both come
with Wayland 95. The manual's files are in
`share/wayland95/studio-manual`.

[Contents](index.md) | Previous: [Running and Making Programs](running.md)
