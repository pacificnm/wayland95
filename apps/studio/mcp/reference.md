# Building apps in Wayland Studio

Wayland Studio (win95studio) makes Windows 95-style desktop apps for
Wayland 95, the way Visual Basic did: **tables** (an ORM over SQLite,
MySQL or PostgreSQL, each field with its own validation) and **forms**
(windows laid out in pixels, with JavaScript event code). A project is
one JSON file, `NAME.w95proj`. These tools edit that file; a Studio
window that has it open reloads it, so the user sees each change.

## How to work

1. `new_project` (or `get_project` for an existing one).
2. `put_table` for each table the app keeps.
3. `put_form` for each window (or `put_element` one element at a time),
   then `set_code` for what the elements and the form do.
4. `check_project`: compiles everything, the code too. Fix what it says.
5. `show_form`: look at each form as it opens. Fix the layout until it
   looks right: aligned, nothing cut off, nothing overlapping.
6. `run_test`: drive it as a person would (click, type, pick, answer the
   message boxes) and check what it does, on a copy of its database. Run
   one for what each form's code is for; fix what it finds. For example:

   ```
   Test.type("BooksForm", "titleBox", "Dune");
   Test.click("BooksForm", "menu1", "fileSave");
   Test.equal(Db.count("Books"), 1, "Save adds the book");
   Test.answer("Yes");                      // the next message box
   Test.click("BooksForm", "deleteButton");
   Test.wait(200, function () { Test.equal(Db.count("Books"), 0, "Delete, Yes: it's gone"); });
   ```
7. `run_app` starts it on the user's desktop for them to try;
   `build_program` makes a program file (no Studio needed to run it).

Every write is checked first and refused, with why, if it's wrong (an
unknown property, a bad name, a value of the wrong kind). After a write
the result lists what the compiler found wrong in the whole project.

Read the other topics before writing a lot: `studio_reference` with
topic `elements` (each element's properties and events; with `element`
for one), `fields` (data types, field settings, Validation Rules).
`studio_manual` has the user's manual: `code.md` is the full API for
event code, `forms.md` and `data-designer.md` say what everything does.

## The project

```json
{
  "format": 1,
  "app": { "name": "bookclub", "title": "Book Club", "version": "1.0", "startForm": "BooksForm" },
  "database": { "driver": "sqlite", "file": "bookclub.db" },
  "tables": [ ... ],
  "forms": [ ... ]
}
```

- `app.name`: the program's name (its data folder, its window class):
  small letters, no spaces. `app.title`: what people read.
  `app.startForm`: the form that opens first.
- `database`: SQLite keeps a file beside the project (`file`). For
  MySQL/MariaDB or PostgreSQL: `{ "driver": "mysql" | "postgresql",
  "host", "port", "name", "user", "password" }`. The tables are made
  (and new fields added) when the app runs.
- A project may also have `apis` (web APIs used as tables) and `ai`
  (the key for AIChat elements). Leave them as they are unless asked.

## A table

```json
{
  "name": "Books",
  "label": "Books",
  "fields": [
    { "name": "Title", "type": "text", "required": true, "maxLength": 80 },
    { "name": "Genre", "type": "choice", "choices": ["Fiction", "Poetry"], "default": "Fiction" },
    { "name": "Reader", "type": "lookup", "lookupTable": "Members" },
    { "name": "Pages", "type": "integer", "min": 1 },
    { "name": "Added", "type": "datetime", "auto": "created" }
  ]
}
```

- Names start with a letter: letters, digits and `_`. Code reads a field
  as `form.record.Title`.
- Every table gets a key `id`, numbered by the database (`record.id`).
  Don't add an `id` field.
- A `lookup` field holds the id of a record of `lookupTable`; a ComboBox
  bound to it lists that table's records by their first text field.
- `required`, `unique`, lengths, ranges, `pattern` and a Validation Rule
  (`validator`, `validatorArg`, `message`) are checked on every save, in
  the app and in code (`Db.insert`). Topic `fields` lists them all.

## A form

```json
{
  "name": "BooksForm",
  "width": 644,
  "height": 382,
  "props": { "title": "Book Club", "table": "Books" },
  "code": {
    "general": "function stars(n) { return \"*\".repeat(n); }",
    "current": "statusLabel.text = form.isNew ? \"A new book\" : form.record.Title;"
  },
  "controls": [ ... elements ... ]
}
```

- `width` and `height`: the inside of the window in pixels (the title
  bar and borders are extra).
- `props.table` makes it a data form: it shows one record of that table
  at a time, and elements whose `props.field` names a field show and
  edit it. Menu/ToolBar actions and code move through the records and
  save them; validation errors mark the elements of the fields that
  failed.
- `code`: the form's events (`load`, `current`, `saved`,
  `validationFailed`, `unload`) and `general`: functions and variables
  that the form's other code can use.

## An element

```json
{ "type": "TextBox", "name": "titleBox", "x": 108, "y": 64, "width": 180, "height": 21,
  "props": { "field": "Title" },
  "events": { "changed": "statusLabel.text = \"Edited\";" } }
```

- `name`: starts with a small letter (letters, digits, `_`), unique on
  its form. Code uses it: `titleBox.text`.
- `props`: only the ones that differ from the default. Each type's are
  in topic `elements`.
- Menu and ToolBar sit across the top of the form: their x, y and width
  are set for them (a Menu first, 20 high; ToolBars below it, 28 high).
  Start the other elements below them.
- Frame and Shape are drawn behind the other elements; put the elements
  that are "in" a Frame inside its rectangle (there is no nesting).
- A Timer is only seen in the Studio (28x28; put it in a corner): its
  `timer` event runs every `interval` ms while it's `enabled`.
- A Visualizer's and an Equalizer's `player` is the Name of a MediaPlayer
  on the same form. The equalizer is the MediaPlayer's (`equalizer`,
  `eqPreset`, `eqPreamp`; code: `eqGains`, `setEqGain`, `applyEqPreset`);
  an Equalizer (392x152) shows and changes it.
- A DataGrid's `columns` can give widths: `["Title:180", "Artist",
  "Length:48"]` (the ones without share what's left).

### Laying out a form the Windows 95 way

- Margins of 8 to 12 pixels; elements on an 8-pixel grid where it fits.
- Labels 17 high, left of their box, the label's top 2 pixels lower than
  the box's; text boxes and combo boxes 21 high; buttons 75 to 80 wide
  and 23 to 24 high, in a row at the bottom or down the right side.
- Line labels up in one column and their boxes in the next.
- The form only as big as what's on it, plus the margin.
- `show_form` after every few changes: what looks wrong usually is.

## Menus and toolbars

```json
{ "type": "Menu", "name": "menu1", "height": 20, "props": { "menus": [
  { "text": "&File", "name": "fileMenu", "items": [
    { "text": "&New", "name": "fileNew", "shortcut": "Ctrl+N", "action": "newRecord" },
    { "text": "&Save", "name": "fileSave", "shortcut": "Ctrl+S", "action": "save" },
    { "separator": true },
    { "text": "E&xit", "name": "fileExit", "action": "quit" } ] } ] } }
```

```json
{ "type": "ToolBar", "name": "toolBar1", "height": 28, "props": { "buttons": [
  { "text": "Save", "name": "saveTool", "icon": "save", "toolTip": "Save the book", "action": "save" },
  { "text": "", "name": "nextTool", "icon": "next", "toolTip": "The next book", "action": "next" },
  { "separator": true } ] } }
```

- A tool button shows an `icon`: one of Wayland 95's own (`new`, `save`,
  `delete`, `first`, `previous`, `next`, `last`, `print`, `find`,
  `people`...; studio_reference topic `elements`, element `Icons`, lists
  them all). Use them rather than a `glyph` (a character, the old way) or
  a `picture` file. With `showCaptions` (true by default) the text shows
  beside the icon; a button with an empty text shows just its icon.
- A Button can have an `icon` too (`props.icon`), left of its text.

- `&` marks the access key (only in menu items: a Label or Button shows
  it as it is). Item names are unique on the form.
- `action`: `openForm` (with `"form": "OtherForm"`), `closeForm`, `quit`,
  `newRecord`, `save`, `deleteRecord` (asks first), `first`,
  `previous`, `next`, `last`.
- An item with no action runs the element's `itemClicked` event, where
  `menu1.item` is the item's name.

## Code

Event code is JavaScript, run with the form's elements by name and:

- `form`: `form.record`, `form.save()`, `form.newRecord()`,
  `form.loadRecord(id)`, `form.msgBox(text, title, icon, buttons, fn)`...
- `App`: `App.openForm("Name", args)`, `App.msgBox(...)`, `App.quit()`.
- `Db`: `Db.select("Books", { where: { Genre: "Fiction" }, orderBy: "Title" })`,
  `Db.get`, `Db.insert`, `Db.update`, `Db.remove`, `Db.count`.
- `AppDates`: dates as they're kept (`2026-11-17`, `14:30:00`).
- Files: `App.chooseFile`, `chooseFiles`, `chooseFolder`, `chooseSaveFile`
  (the Win95 boxes; `(options, function (answer) { })`, options `title`,
  `folder`, `filters: ["Music (*.mp3)"]`, `name`, `suffix`),
  `App.listFiles(folder, "*.mp3 *.flac", recursive)`, `App.fileInfo(path)`,
  `App.readTags(path, function (tags) { })` (a media file's title,
  artist, album... without playing it).

`studio_manual` page `code.md` has all of it, with examples. A message
box doesn't wait: what to do with the answer goes in its function.
