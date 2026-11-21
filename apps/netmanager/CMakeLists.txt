# win95netmanager - Network Manager: logs in to the web pages of the
# network's devices (routers, switches, access points, printers, storage,
# cameras) with Chromium (Qt WebEngine), from devices you save: their
# address and user name, the password in the desktop's keyring, and the
# certificate you trusted for one that signs its own. Control Panel >
# Network Manager opens it. Built only when Qt WebEngine's headers are
# there (it reads a certificate's fingerprint in C++).

find_package(Qt6 QUIET COMPONENTS WebEngineQuick WebEngineCore Network DBus)
if(NOT TARGET Qt6::WebEngineQuick OR NOT TARGET Qt6::WebEngineCore)
    message(WARNING "Qt WebEngine not found - skipping win95netmanager. "
                    "Install it with: sudo apt-get install qt6-webengine-dev")
    return()
endif()

qt_add_executable(win95netmanager
    src/main.cpp
    src/devices.h
    src/devices.cpp
)

set_target_properties(win95netmanager PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}
)

target_include_directories(win95netmanager PRIVATE src ${CMAKE_SOURCE_DIR}/apps/common)
# Qt's QML targets list Debian's separate private-header folders
target_compile_options(win95netmanager PRIVATE -Wno-missing-include-dirs)

qt_add_qml_module(win95netmanager
    URI Win95NetManager
    VERSION 1.0
    QML_FILES
        Main.qml
        DeviceDialog.qml
        DeviceIcon.qml
        GroupBox95.qml
    RESOURCES
        login.js
    RESOURCE_PREFIX /qt/qml
)

target_link_libraries(win95netmanager PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Qml
    Qt6::Quick
    Qt6::Network
    Qt6::DBus
    Qt6::WebEngineCore
    Qt6::WebEngineQuick
    win95uiplugin
)

install(TARGETS win95netmanager RUNTIME DESTINATION bin)
