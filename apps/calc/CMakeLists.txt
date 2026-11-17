# win95calc - Calculator: Standard and Scientific views (a standalone
# Wayland client). Start > Programs > Accessories > Calculator.

qt_add_executable(win95calc
    src/main.cpp
    src/calcengine.h
    src/calcengine.cpp
)

set_target_properties(win95calc PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}
)

target_include_directories(win95calc PRIVATE src ${CMAKE_SOURCE_DIR}/apps/common)

qt_add_qml_module(win95calc
    URI Win95Calc
    VERSION 1.0
    QML_FILES
        Main.qml
        CalcButton.qml
    RESOURCE_PREFIX /qt/qml
)

target_link_libraries(win95calc PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Qml
    Qt6::Quick
    win95uiplugin
)

install(TARGETS win95calc RUNTIME DESTINATION bin)
