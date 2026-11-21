# win95control - the Wayland 95 Control Panel (a standalone Wayland client).
# Applets write ~/.config/wayland95/settings.ini through Wayland95Settings
# (apps/common); the compositor watches that file and applies changes live.

# AppStream catalogs are gzip-compressed
find_package(ZLIB REQUIRED)
# Printers talks to CUPS over its socket
find_package(Qt6 REQUIRED COMPONENTS Network)
# Fonts reads the font list from fontconfig
find_package(PkgConfig REQUIRED)
pkg_check_modules(FONTCONFIG REQUIRED IMPORTED_TARGET fontconfig)
pkg_check_modules(SQLITE3 REQUIRED IMPORTED_TARGET sqlite3)

qt_add_executable(win95control
    src/main.cpp
    src/systeminfo.h
    src/systeminfo.cpp
    src/networkinfo.h
    src/networkinfo.cpp
    src/hardwareinfo.h
    src/hardwareinfo.cpp
    src/software.h
    src/software.cpp
    src/fontcatalog.h
    src/fontcatalog.cpp
    src/printers.h
    src/printers.cpp
    src/locales.h
    src/locales.cpp
    src/services.h
    src/services.cpp
    src/displays.h
    src/displays.cpp
    src/startup.h
    src/startup.cpp
    src/backup.h
    src/backup.cpp
    src/systemlog.h
    src/systemlog.cpp
    src/firewall.h
    src/firewall.cpp
    src/ollamamodels.h
    src/ollamamodels.cpp
    src/launcher.h
)

# Services runs session/wayland95-services as root through pkexec: the
# installed copy, or the one in the source tree before it's installed
include(GNUInstallDirs)
target_compile_definitions(win95control PRIVATE
    WAYLAND95_SERVICES_HELPER="${CMAKE_INSTALL_FULL_LIBEXECDIR}/wayland95/wayland95-services"
    WAYLAND95_SERVICES_HELPER_SRC="${CMAKE_SOURCE_DIR}/session/wayland95-services"
)

# Services > Wayland 95 Development installs what building Wayland 95 needs:
# debian/control's Build-Depends (versions dropped; debhelper-compat is
# debhelper), read here so the two never drift apart
file(STRINGS ${CMAKE_SOURCE_DIR}/debian/control _control_lines)
set(_in_build_depends OFF)
set(_build_depends "")
foreach(_line IN LISTS _control_lines)
    if(_line MATCHES "^Build-Depends:(.*)$")
        set(_in_build_depends ON)
        set(_line "${CMAKE_MATCH_1}")
    elseif(_in_build_depends AND NOT _line MATCHES "^[ \t]")
        set(_in_build_depends OFF)
    endif()
    if(_in_build_depends)
        string(REGEX REPLACE "\\([^)]*\\)" "" _line "${_line}")
        string(REPLACE "," ";" _items "${_line}")
        foreach(_item IN LISTS _items)
            string(STRIP "${_item}" _item)
            if(_item STREQUAL "debhelper-compat")
                set(_item debhelper)
            endif()
            if(_item)
                list(APPEND _build_depends ${_item})
            endif()
        endforeach()
    endif()
endforeach()
list(JOIN _build_depends "," _build_depends)
target_compile_definitions(win95control PRIVATE WAYLAND95_BUILD_DEPENDS="${_build_depends}")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/debian/control)

set_target_properties(win95control PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}
)

target_include_directories(win95control PRIVATE src ${CMAKE_SOURCE_DIR}/apps/common)

qt_add_qml_module(win95control
    URI Win95Control
    VERSION 1.0
    QML_FILES
        Main.qml
        AppletIcon.qml
        PropertySheet.qml
        GroupBox95.qml
        DisplayApplet.qml
        DateTimeApplet.qml
        KeyboardApplet.qml
        TaskbarApplet.qml
        SystemApplet.qml
        MouseApplet.qml
        SoundsApplet.qml
        NetworkApplet.qml
        BluetoothApplet.qml
        HardwareApplet.qml
        PowerApplet.qml
        SoftwareApplet.qml
        PasswordsApplet.qml
        FontsApplet.qml
        FontViewer.qml
        FontIcon.qml
        PrintersApplet.qml
        AccessibilityApplet.qml
        RegionalApplet.qml
        PrinterIcon.qml
        PrintQueueWindow.qml
        AddPrinterWizard.qml
        PrinterProperties.qml
        AddProgramsWindow.qml
        ProgramList.qml
        SoftwareTasks.qml
        HardwareIcon.qml
        BluetoothDeviceIcon.qml
        PreviewBevel.qml
        PreviewTitle.qml
        ColorButton.qml
        ServicesApplet.qml
        SshSettings.qml
        WireGuardSettings.qml
        PostgresSettings.qml
        PythonSettings.qml
        NodeSettings.qml
        ProblemsApplet.qml
        StartupApplet.qml
        BackupApplet.qml
        UnitPage.qml
        MysqlSettings.qml
        SqliteSettings.qml
        DevSettings.qml
        NginxSettings.qml
        SambaSettings.qml
        OllamaSettings.qml
        SystemLogApplet.qml
        FirewallApplet.qml
    RESOURCE_PREFIX /qt/qml
)

target_link_libraries(win95control PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Qml
    Qt6::Quick
    Qt6::DBus
    Qt6::Network
    ZLIB::ZLIB
    PkgConfig::FONTCONFIG
    PkgConfig::SQLITE3
    win95uiplugin
)

install(TARGETS win95control RUNTIME DESTINATION bin)
