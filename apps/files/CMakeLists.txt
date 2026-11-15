# win95files - the Win95-style file manager (a standalone Wayland client).
# Opened from Start > Programs, the My Computer icon ("computer:") and the
# Recycle Bin icon ("trash:").

find_package(Qt6 REQUIRED COMPONENTS Concurrent)

qt_add_executable(win95files
    src/main.cpp
    src/directorymodel.h
    src/directorymodel.cpp
)

set_target_properties(win95files PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}
)

target_include_directories(win95files PRIVATE src ${CMAKE_SOURCE_DIR}/apps/common)

qt_add_qml_module(win95files
    URI Win95Files
    VERSION 1.0
    QML_FILES
        Main.qml
        FileIcon.qml
    RESOURCE_PREFIX /qt/qml
)

target_link_libraries(win95files PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Qml
    Qt6::Quick
    Qt6::Concurrent
    win95uiplugin
)

install(TARGETS win95files RUNTIME DESTINATION bin)
