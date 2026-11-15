# win95imagewriter - Image Writer (a standalone Wayland client): writes a
# disk image (a Linux .iso, an .img) to a USB drive and checks it, like
# KDE's ISO Image Writer. The drive is opened through UDisks2, which asks
# for the administrator password; the program never runs as root.

find_package(Qt6 REQUIRED COMPONENTS DBus)

qt_add_executable(win95imagewriter
    src/main.cpp
    src/imagewriter.h
    src/imagewriter.cpp
)

set_target_properties(win95imagewriter PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}
)

target_include_directories(win95imagewriter PRIVATE src ${CMAKE_SOURCE_DIR}/apps/common)

qt_add_qml_module(win95imagewriter
    URI Win95ImageWriter
    VERSION 1.0
    QML_FILES
        Main.qml
        GroupBox95.qml
    RESOURCE_PREFIX /qt/qml
)

target_link_libraries(win95imagewriter PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Qml
    Qt6::Quick
    Qt6::DBus
    win95uiplugin
)

install(TARGETS win95imagewriter RUNTIME DESTINATION bin)
