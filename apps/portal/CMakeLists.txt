# win95portal - Wayland 95's xdg-desktop-portal backend: programs asking
# the portal for an Open or Save dialog (VS Code, browsers, Flatpak apps)
# get the Win95 one.

qt_add_executable(win95portal
    src/main.cpp
    src/filechooser.h
    src/filechooser.cpp
)

set_target_properties(win95portal PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}
)

target_include_directories(win95portal PRIVATE src)

qt_add_qml_module(win95portal
    URI Win95Portal
    VERSION 1.0
    QML_FILES
        FileChooserWindow.qml
    RESOURCE_PREFIX /qt/qml
)

target_link_libraries(win95portal PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Qml
    Qt6::Quick
    Qt6::DBus
    win95uiplugin
)

install(TARGETS win95portal RUNTIME DESTINATION bin)
