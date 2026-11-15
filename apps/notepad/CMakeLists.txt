# win95notepad - the Win95-style Notepad (a standalone Wayland client).
# Start > Programs > Notepad; the file manager opens text files with it.

qt_add_executable(win95notepad
    src/main.cpp
    src/documentio.h
    src/documentio.cpp
)

set_target_properties(win95notepad PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}
)

target_include_directories(win95notepad PRIVATE src)

qt_add_qml_module(win95notepad
    URI Win95Notepad
    VERSION 1.0
    QML_FILES
        Main.qml
    RESOURCE_PREFIX /qt/qml
)

target_link_libraries(win95notepad PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Qml
    Qt6::Quick
    win95uiplugin
)

install(TARGETS win95notepad RUNTIME DESTINATION bin)
