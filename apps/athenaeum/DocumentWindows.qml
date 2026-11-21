import QtQuick

// Opens project documents in windows of their own, one window per
// document: opening one that is already open brings it forward. They close
// with the program (each saves on closing).
QtObject {
    id: windows
    property var mainWindow
    property var open_: ({})

    function open(documentId) {
        var w = open_[documentId];
        if (w) {
            w.raise();
            w.requestActivate();
            return;
        }
        w = component.createObject(null, { documentId: documentId, mainWindow: mainWindow });
        if (!w)
            return;
        var map = open_;
        map[documentId] = w;
        open_ = map;
        w.closing.connect(function () {
            var m = windows.open_;
            delete m[documentId];
            windows.open_ = m;
            w.destroy(1000);
        });
        w.show();
    }
    function closeAll() {
        for (var id in open_)
            open_[id].close();
    }

    property Component component: Component { DocumentWindow {} }
}
