# win95terminal - a standalone Wayland client: Win95-styled terminal
# emulator built on libvterm. Launched by the compositor from the Start
# menu / desktop icon, but runs fine on any Wayland (or X11) desktop.

qt_add_executable(win95terminal
    src/main.cpp
    src/terminalitem.h
    src/terminalitem.cpp
)

# Put the binary next to win95compositor so the compositor's Launcher finds
# it without needing an install step.
set_target_properties(win95terminal PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}
)

target_include_directories(win95terminal PRIVATE src ${CMAKE_SOURCE_DIR}/apps/common)

qt_add_qml_module(win95terminal
    URI Win95Terminal
    VERSION 1.0
    QML_FILES
        Main.qml
    RESOURCE_PREFIX /qt/qml
)

target_link_libraries(win95terminal PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Qml
    Qt6::Quick
    PkgConfig::VTERM
    win95uiplugin
    util # forkpty() (a no-op stub on glibc >= 2.34, needed on older ones)
)

install(TARGETS win95terminal RUNTIME DESTINATION bin)
