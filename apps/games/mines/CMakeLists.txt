wayland95_game(mines Win95Mines)
qt_add_qml_module(win95mines
    URI Win95Mines
    VERSION 1.0
    QML_FILES Main.qml Counter.qml Glyph.qml
    RESOURCE_PREFIX /qt/qml
)
