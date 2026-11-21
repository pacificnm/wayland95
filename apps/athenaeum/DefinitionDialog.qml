import QtQuick
import Win95Ui

// A word's meaning, pronunciation and examples from an installed Wiktionary
// ("molecules" leads to "molecule"). Without a dictionary, it says which to get.
Dialog95 {
    id: dialog
    property var result: ({})
    signal findDictionaries()
    title: "Define"
    dialogWidth: 460
    dialogHeight: result.found === true ? 320 : 130

    function open(word) {
        result = LibraryBrowser.define(word);
        title = "Define “" + Util.plain(result.word || word) + "”";
        show();
    }
    function describe(r) {
        if (!r.found)
            return "";
        var esc = function (s) { return String(s || "").replace(/&/g, "&amp;").replace(/</g, "&lt;"); };
        var h = "<p><b style='font-size:15px'>" + esc(r.word) + "</b>";
        if (r.pronunciation)
            h += "&nbsp;&nbsp;" + esc(r.pronunciation);
        h += "</p>";
        if (r.formOf)
            h += "<p><i>" + esc(r.asked) + "</i>: " + esc(r.formOf) + "</p>";
        for (var i = 0; i < r.parts.length; ++i) {
            var p = r.parts[i];
            h += "<p><i>" + esc(p.name) + "</i></p><ol>";
            for (var k = 0; k < p.senses.length; ++k) {
                var s = p.senses[k];
                h += "<li>" + esc(s.definition) + (s.example ? "<br><span style='color:#606060'>“" + esc(s.example) + "”</span>" : "") + "</li>";
            }
            h += "</ol>";
        }
        return h;
    }
    Keys.onEscapePressed: close()

    TextView95 {
        x: 10
        y: 10
        width: parent.width - 20
        height: parent.height - 56
        visible: dialog.result.found === true
        textFormat: Text.RichText // built above, every outside string escaped
        text: dialog.describe(dialog.result)
    }
    Label95 {
        x: 16
        y: 16
        width: parent.width - 32
        visible: dialog.result.found !== true
        wrapMode: Text.Wrap
        text: dialog.result.hasDictionary
              ? "“" + (dialog.result.word || "") + "” is not in your dictionaries."
              : "Definitions come from a Wiktionary library. Get one in the Library Manager: the Simple English Wiktionary is 31 MB."
    }
    Label95 {
        x: 10
        anchors.bottom: parent.bottom
        anchors.bottomMargin: 14
        visible: dialog.result.found === true
        small: true
        dim: true
        text: "From " + (dialog.result.library || "")
    }
    Row {
        anchors.right: parent.right
        anchors.rightMargin: 10
        anchors.bottom: parent.bottom
        anchors.bottomMargin: 10
        spacing: 6
        Button95 {
            width: 130
            visible: dialog.result.hasDictionary !== true
            text: "Get a Dictionary..."
            onClicked: { dialog.close(); dialog.findDictionaries(); }
        }
        Button95 { text: "Close"; isDefault: true; onClicked: dialog.close() }
    }
}
