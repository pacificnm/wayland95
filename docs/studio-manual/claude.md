[Contents](index.md) | Previous: [When Something Isn't Right](troubleshooting.md)

# Building Apps with Claude

Claude can build a Wayland Studio app for you. Tell it what the app is
for, and it makes the project: the tables and their rules, the forms
and what's on them, the code for their events. It checks its own work
as it goes, and looks at each form the way you would, to see that it
lines up. Open the project in the Studio while it works, and you see
every change as it's made.

This works through *Claude Code*, Anthropic's Claude for the command
line, or any other program that speaks the Model Context Protocol (MCP).
Wayland Studio comes with the tools they use: a program called
`win95studio-mcp`.

## Giving Claude the tools

If Claude Code is installed: **Help > Build Apps with Claude**, then
**Add to Claude Code**. That's all; it's done once, and every Claude Code
session after it has the tools.

Or, in a Terminal:

```
claude mcp add --scope user wayland-studio -- win95studio-mcp
```

(**Copy Command** in the same box copies this, with the full path of
`win95studio-mcp` on your computer.) Another MCP client wants a *stdio*
server whose command is `win95studio-mcp`, with no arguments.

## Asking for an app

Start Claude Code in a Terminal and say what you want:

```
Make a Wayland Studio app in ~/Recipes for my recipes: a name, the
ingredients, how long it takes, and a rating out of five. One form to
go through them, with a list of all of them beside it.
```

Claude reads how projects are made first, then builds: tables, then
forms, then code. Before it says it's done, it checks the project for
problems (as **Run > Check for Problems** does), draws each form to
look at it, and runs tests that use the app the way you would: it
types, clicks, picks from the lists, answers the message boxes and the
Open boxes, and checks what happened. Then it can start the app on your desktop for you to try,
or make it a program file (as **Make Program** does).

Ask for changes the same way: "Put the rating on the list too", "Make
the Save button the default", "Add a menu with Exit". Claude can also
work on a project you made yourself; tell it where it is.

## Watching in the Studio

Open the project in the Studio (**File > Open Project**) while Claude
works on it. Each time Claude changes the project's file, the Studio
loads it again, and you see the new table or element where it went.

- With nothing unsaved in the Studio, the change is just taken. **Undo**
  takes back Claude's last change, as it would one of yours (save to keep
  it that way).
- If you've changed something in the Studio and not saved it, the Studio
  asks first: **Yes** loads Claude's version (your unsaved changes are
  lost), **No** keeps yours. Save, and Claude's next change starts from
  your version.

It works the other way too: what you change and save in the Studio is
what Claude finds the next time it reads the project.

## What Claude's tools can do

| Tool | What it does |
|---|---|
| `studio_reference` | How projects are made: the project's parts, every element's properties and events, field types and Validation Rules. |
| `studio_manual` | A page of this manual. |
| `new_project`, `get_project` | Make a project, read one. |
| `set_app` | The app's name, title, version, start form, and its database. |
| `put_table`, `remove_table` | Add or change a table and its fields. |
| `put_form`, `remove_form` | Add or change a form, everything on it included. |
| `put_element`, `remove_element` | Add or change one element on a form. |
| `set_code` | The code for an event, an element's or the form's. |
| `check_project` | Everything that would stop it running: the code compiled, and every table, field and form that something names. |
| `show_form` | A picture of a form as it opens, with its records. |
| `run_test` | The app run out of sight, driven the way you'd use it (clicks, typing, choices, the answers to its message boxes), with checks of what it does: what passed, what didn't, and any mistake in its code. |
| `build_program` | Make Program. |
| `run_app` | Start the app on your desktop. |

Every change is checked first, the way the Studio checks what you type
in the Properties window: a property an element doesn't have, a name
that isn't allowed, a data type that isn't one. A change that's wrong
isn't made, and Claude is told why.

## Things to know

- Claude works on the files you point it at. It doesn't need the Studio
  to be open, and it never changes anything in the Studio's own windows.
- Its changes go to the project's file at once. If you want to keep a
  version from before, copy the project's folder first.
- `run_app` starts the app as **Start** does: on the project's own
  database, beside the project.
- `run_test` works on a copy of the project's database, so a test that
  adds or deletes records leaves yours as they were. It runs out of
  sight and makes no sound. (A project kept in MySQL or PostgreSQL can't
  be copied: Claude has to be told it may test on the database itself.)
- Claude can be wrong. Try the app yourself before you rely on it.

[Contents](index.md) | Previous: [When Something Isn't Right](troubleshooting.md)
