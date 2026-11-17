# win95web - Web Browser: tabs, favorites, history and downloads around Qt
# WebEngine (Chromium), the page engine, used from QML (only its runtime
# is needed). Start > Programs > Internet > Web Browser.

find_package(Qt6 REQUIRED COMPONENTS Network DBus)

qt_add_executable(win95web
    src/main.cpp
    src/browser.h
    src/browser.cpp
)

set_target_properties(win95web PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}
)

target_include_directories(win95web PRIVATE src ${CMAKE_SOURCE_DIR}/apps/common)

set_source_files_properties(Profile.qml PROPERTIES QT_QML_SINGLETON_TYPE TRUE)

qt_add_qml_module(win95web
    URI Win95Web
    VERSION 1.0
    QML_FILES
        Main.qml
        Profile.qml
        WebTab.qml
        WebIcon.qml
        BrowserDialogs.qml
        DownloadsWindow.qml
    RESOURCE_PREFIX /qt/qml
)

target_link_libraries(win95web PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Qml
    Qt6::Quick
    Qt6::Network
    Qt6::DBus
    win95uiplugin
)

install(TARGETS win95web RUNTIME DESTINATION bin)
# (so it can be the default browser: Internet Options, or xdg-settings)
install(FILES win95web.desktop DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
