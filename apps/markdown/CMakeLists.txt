# win95markdown - the Markdown Editor: reads and writes Markdown files with
# a WordPad-style page (bold, headings, lists, links, tables...) and a
# Markdown view of the same text (a standalone Wayland client).
# Start > Programs > Accessories > Markdown Editor; the file manager opens
# .md files with it.

qt_add_executable(win95markdown
    src/main.cpp
    src/markdowndocument.h
    src/markdowndocument.cpp
)

set_target_properties(win95markdown PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}
)

target_include_directories(win95markdown PRIVATE src ${CMAKE_SOURCE_DIR}/apps/common)

qt_add_qml_module(win95markdown
    URI Win95Markdown
    VERSION 1.0
    QML_FILES
        Main.qml
        FormatButton.qml
    RESOURCE_PREFIX /qt/qml
)

target_link_libraries(win95markdown PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Qml
    Qt6::Quick
    win95uiplugin
)

install(TARGETS win95markdown RUNTIME DESTINATION bin)
