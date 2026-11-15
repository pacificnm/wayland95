# win95volume - the Volume Control (a standalone Wayland client): one column
# of volume slider + mute per sound source, like Win95's sndvol32. The
# taskbar's speaker icon opens it on double-click.

qt_add_executable(win95volume
    src/main.cpp
)

set_target_properties(win95volume PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}
)

qt_add_qml_module(win95volume
    URI Win95Volume
    VERSION 1.0
    QML_FILES
        Main.qml
        MixerColumn.qml
    RESOURCE_PREFIX /qt/qml
)

target_link_libraries(win95volume PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Qml
    Qt6::Quick
    win95uiplugin
)

install(TARGETS win95volume RUNTIME DESTINATION bin)

# crashhandler.h
target_include_directories(win95volume PRIVATE ${CMAKE_SOURCE_DIR}/apps/common)
