# win95repos - Repositories: your GitHub repositories and their clones on
# this computer (a standalone Wayland client). It works through the gh and
# git commands, so it uses gh's sign-in and never keeps a token itself.
# Start > Programs > Programming > Repositories.

qt_add_executable(win95repos
    src/main.cpp
    src/repohub.h
    src/repohub.cpp
)

set_target_properties(win95repos PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}
)

target_include_directories(win95repos PRIVATE src ${CMAKE_SOURCE_DIR}/apps/common)

qt_add_qml_module(win95repos
    URI Win95Repos
    VERSION 1.0
    QML_FILES
        Main.qml
        RepoIcon.qml
        ListHeader.qml
        TextView.qml
    RESOURCE_PREFIX /qt/qml
)

target_link_libraries(win95repos PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Qml
    Qt6::Quick
    win95uiplugin
)

install(TARGETS win95repos RUNTIME DESTINATION bin)
