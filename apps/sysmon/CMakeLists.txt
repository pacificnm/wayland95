# win95sysmon - the System Monitor (a standalone Wayland client): live
# charts of processor, memory, network and disk use, and the running
# programs with End Process. Reads /proc directly; needs only Qt.

qt_add_executable(win95sysmon
    src/main.cpp
    src/systemmonitor.h
    src/systemmonitor.cpp
)

set_target_properties(win95sysmon PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}
)

target_include_directories(win95sysmon PRIVATE src)

qt_add_qml_module(win95sysmon
    URI Win95SysMon
    VERSION 1.0
    QML_FILES
        Main.qml
        Chart95.qml
    RESOURCE_PREFIX /qt/qml
)

target_link_libraries(win95sysmon PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Qml
    Qt6::Quick
    win95uiplugin
)

install(TARGETS win95sysmon RUNTIME DESTINATION bin)
