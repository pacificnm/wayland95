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
)

# Services runs session/wayland95-services as root through pkexec: the
# installed copy, or the one in the source tree before it's installed
include(GNUInstallDirs)
target_compile_definitions(win95control PRIVATE
    WAYLAND95_SERVICES_HELPER="${CMAKE_INSTALL_FULL_LIBEXECDIR}/wayland95/wayland95-services"
    WAYLAND95_SERVICES_HELPER_SRC="${CMAKE_SOURCE_DIR}/session/wayland95-services"
)

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
        NginxSettings.qml
        SambaSettings.qml
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
