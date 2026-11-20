# The Book Club sample, finished: the forms Add Form for (a table) made,
# with a Menu, a ToolBar, a status line, event code and an About form.
# (screenshots.sh: python3 finish.py GENERATED.w95proj OUT.w95proj)
import json, sys

p = json.load(open(sys.argv[1]))
p["app"]["startForm"] = "BooksForm"
forms = {f["name"]: f for f in p["forms"]}

def dock(form, control):
    """Across the top, the rest moved down (as the Studio does)."""
    h = control["height"]
    for c in form["controls"]:
        c["y"] += h
    form["height"] += h
    control.update({"x": 0, "y": 0, "width": form["width"]})
    form["controls"].insert(0, control)

books = forms["BooksForm"]
books["props"]["title"] = "Book Club"
# a status line under the buttons
bottom = max(c["y"] + c["height"] for c in books["controls"] if c["type"] == "Button")
books["controls"].append({"type": "Label", "name": "statusLabel", "x": 12, "y": bottom + 10, "width": 620, "height": 21,
                          "props": {"text": "", "borderStyle": "Sunken", "backColor": "#ffffe1"}, "events": {}})
books["height"] = max(books["height"], bottom + 10 + 21 + 10)
toolbar = {"type": "ToolBar", "name": "toolBar1", "height": 28, "props": {"buttons": [
    {"text": "New", "name": "newTool", "icon": "new", "toolTip": "A new book", "action": "newRecord"},
    {"text": "Save", "name": "saveTool", "icon": "save", "toolTip": "Save the book", "action": "save"},
    {"text": "Delete", "name": "deleteTool", "icon": "delete", "toolTip": "Delete the book", "action": "deleteRecord"},
    {"separator": True},
    {"text": "", "name": "firstTool", "icon": "first", "toolTip": "The first book", "action": "first"},
    {"text": "", "name": "previousTool", "icon": "previous", "toolTip": "The book before", "action": "previous"},
    {"text": "", "name": "nextTool", "icon": "next", "toolTip": "The next book", "action": "next"},
    {"text": "", "name": "lastTool", "icon": "last", "toolTip": "The last book", "action": "last"},
    {"separator": True},
    {"text": "Members", "name": "membersTool", "icon": "people", "toolTip": "The club's members", "action": "openForm", "form": "MembersForm"}]},
    "events": {}}
menu = {"type": "Menu", "name": "menu1", "height": 20, "props": {"menus": [
    {"text": "&File", "name": "fileMenu", "items": [
        {"text": "&New Book", "name": "fileNew", "shortcut": "Ctrl+N", "action": "newRecord"},
        {"text": "&Save", "name": "fileSave", "shortcut": "Ctrl+S", "action": "save"},
        {"text": "&Delete", "name": "fileDelete", "action": "deleteRecord"},
        {"separator": True},
        {"text": "&Clear the Form", "name": "fileClear"},
        {"separator": True},
        {"text": "E&xit", "name": "fileExit", "action": "quit"}]},
    {"text": "&Go", "name": "goMenu", "items": [
        {"text": "&First", "name": "goFirst", "action": "first"},
        {"text": "&Previous", "name": "goPrevious", "shortcut": "Ctrl+PgUp", "action": "previous"},
        {"text": "&Next", "name": "goNext", "shortcut": "Ctrl+PgDown", "action": "next"},
        {"text": "&Last", "name": "goLast", "action": "last"}]},
    {"text": "&Club", "name": "clubMenu", "items": [
        {"text": "&Members...", "name": "clubMembers", "action": "openForm", "form": "MembersForm"}]},
    {"text": "&Help", "name": "helpMenu", "items": [
        {"text": "&About Book Club", "name": "helpAbout", "action": "openForm", "form": "AboutForm"}]}]},
    "events": {"itemClicked": "if (menu1.item === \"fileClear\")\n    form.reset();"}}
dock(books, toolbar)
dock(books, menu)
books["code"] = {
    "general": "// how many stars a rating is\nfunction stars(n) {\n    return n ? \"★★★★★\".substring(0, n) + \"☆☆☆☆☆\".substring(0, 5 - n) : \"\";\n}",
    "current": "if (form.isNew)\n    statusLabel.text = \"A new book: fill it in and press Save.\";\nelse\n    statusLabel.text = form.record.Title + \"  \" + stars(form.record.Rating);",
    "saved": "statusLabel.text = \"Saved \" + form.record.Title + \".\";",
    "validationFailed": "statusLabel.text = \"Not saved: the fields marked in red say why.\";",
}
for c in books["controls"]:
    if c["name"] == "titleLabel":
        c["props"]["fontBold"] = True

members = forms["MembersForm"]
members["props"]["title"] = "Members"

p["forms"].append({"name": "AboutForm", "width": 300, "height": 130, "props": {"title": "About Book Club", "resizable": False},
                   "code": {}, "controls": [
    {"type": "Label", "name": "aboutLabel", "x": 16, "y": 16, "width": 268, "height": 54,
     "props": {"text": "Book Club 1.0\nMade in Wayland Studio", "alignment": "Center", "fontBold": True, "fontSize": 13}, "events": {}},
    {"type": "Button", "name": "okButton", "x": 110, "y": 88, "width": 80, "height": 24, "props": {"text": "OK", "isDefault": True},
     "events": {"clicked": "form.close();"}}]})
p["forms"] = [forms["BooksForm"], forms["MembersForm"], p["forms"][-1]]
json.dump(p, open(sys.argv[2], "w"), indent=1, ensure_ascii=False)
