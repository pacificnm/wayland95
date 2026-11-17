wayland95_game(solitaire Win95Solitaire)
qt_add_qml_module(win95solitaire
    URI Win95Solitaire
    VERSION 1.0
    QML_FILES Main.qml Card.qml CardBack.qml
    RESOURCE_PREFIX /qt/qml
)
