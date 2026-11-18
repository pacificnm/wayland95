# Wayland Studio: make Wayland 95 apps (Start > Programs > Programming).
#
#   win95apprt   (runtime/) what a made app is built from: the ORM
#                (AppDatabase), the elements (App*.qml), the compiler that
#                turns a project into QML and a program file, and `App`
#   win95apprun  (runner/)  runs a project or, with a compiled app on the end
#                of its file, is that app's program
#   win95studio             the Studio: Data Designer, form designer, code

find_package(Qt6 REQUIRED COMPONENTS Sql)

# --- the runtime: the Win95App QML module, static like Win95Ui ---------------
qt_add_library(win95apprt STATIC)

set(win95app_qml
    AppElement.qml
    AppForm.qml
    AppMessageWindow.qml
    AppLabel.qml
    AppTextBox.qml
    AppButton.qml
    AppCheckBox.qml
    AppOptionButton.qml
    AppComboBox.qml
    AppListBox.qml
    AppFrame.qml
    AppImage.qml
    AppShape.qml
    AppDataGrid.qml
    AppMenuBar.qml
    AppToolBar.qml
    AppDateTimePicker.qml
    AppMediaPlayer.qml
    AppAiChat.qml
    AppGraph.qml
)
set(win95app_qml_paths "")
foreach(f ${win95app_qml})
    set_source_files_properties(runtime/${f} PROPERTIES QT_RESOURCE_ALIAS ${f})
    list(APPEND win95app_qml_paths runtime/${f})
endforeach()
set_source_files_properties(runtime/elements.json PROPERTIES QT_RESOURCE_ALIAS elements.json)

qt_add_qml_module(win95apprt
    URI Win95App
    VERSION 1.0
    RESOURCE_PREFIX /qt/qml
    QML_FILES ${win95app_qml_paths}
    RESOURCES runtime/elements.json
    SOURCES
        runtime/appdatabase.h
        runtime/appdatabase.cpp
        runtime/fieldrules.h
        runtime/fieldrules.cpp
        runtime/datetimes.h
        runtime/datetimes.cpp
        runtime/apiclient.h
        runtime/apiclient.cpp
        runtime/aiclient.h
        runtime/aiclient.cpp
        runtime/appcompiler.h
        runtime/appcompiler.cpp
        runtime/appruntime.h
        runtime/appruntime.cpp
)
find_package(Qt6 REQUIRED COMPONENTS Network)
target_link_libraries(win95apprt PUBLIC Qt6::Core Qt6::Gui Qt6::Qml Qt6::Quick Qt6::Sql Qt6::Network)
target_include_directories(win95apprt PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/runtime)

# --- the runner ---------------------------------------------------------------
qt_add_executable(win95apprun runner/main.cpp)
set_target_properties(win95apprun PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})
target_include_directories(win95apprun PRIVATE runtime)
target_link_libraries(win95apprun PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Qml
    Qt6::Quick
    Qt6::Sql
    win95apprtplugin
    win95uiplugin
)
install(TARGETS win95apprun RUNTIME DESTINATION bin)

# --- the Studio ---------------------------------------------------------------
qt_add_executable(win95studio
    src/main.cpp
    src/studiotools.h
    src/studiotools.cpp
    src/codehighlighter.h
    src/codehighlighter.cpp
)
set_target_properties(win95studio PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})
target_include_directories(win95studio PRIVATE src runtime ${CMAKE_SOURCE_DIR}/apps/common)

qt_add_qml_module(win95studio
    URI Win95Studio
    VERSION 1.0
    QML_FILES
        Main.qml
        FormDesigner.qml
        CodeWindow.qml
        TableDesigner.qml
        PropertyGrid.qml
        StudioDialogs.qml
        Label95.qml
        codeassist.js
    RESOURCE_PREFIX /qt/qml
)

# the manual (Help): installed in share/wayland95/studio-manual; a build
# that isn't installed reads it from the source tree
include(GNUInstallDirs)
target_compile_definitions(win95studio PRIVATE
    WAYLAND95_STUDIO_MANUAL="${CMAKE_INSTALL_FULL_DATADIR}/wayland95/studio-manual"
    WAYLAND95_STUDIO_MANUAL_SOURCE="${CMAKE_SOURCE_DIR}/docs/studio-manual")
install(DIRECTORY ${CMAKE_SOURCE_DIR}/docs/studio-manual/
        DESTINATION ${CMAKE_INSTALL_DATADIR}/wayland95/studio-manual
        FILES_MATCHING PATTERN "*.md" PATTERN "*.png"
                       PATTERN "bookclub.w95proj" PATTERN "bookclub.db")

target_link_libraries(win95studio PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Qml
    Qt6::Quick
    Qt6::Sql
    win95apprtplugin
    win95uiplugin
)

install(TARGETS win95studio RUNTIME DESTINATION bin)
