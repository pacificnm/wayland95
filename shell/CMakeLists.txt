# win95shell: Wayland 95's desktop as panels (layer-shell) of
# wayland95-compositor. See docs/architecture.md and docs/migration.md
# (phase 4). The QML is shared with the Qt compositor (qml/Win95Shell/).
find_package(Qt6 REQUIRED COMPONENTS WaylandClient WaylandScannerTools)
pkg_get_variable(WAYLAND_PROTOCOLS_DIR wayland-protocols pkgdatadir)
if(NOT TARGET Qt6::WaylandClientPrivate)
    message(FATAL_ERROR "win95shell needs Qt's private Wayland client headers (qt6-wayland-private-dev)")
endif()

qt_add_executable(win95shell
    main.cpp
    layershell.h layershell.cpp
    toplevels.h toplevels.cpp
    session.h session.cpp
    desktopkeys.h desktopkeys.cpp
    idletimer.h idletimer.cpp
    displaypower.h displaypower.cpp
    sessionlock.h sessionlock.cpp
    screensaverservice.h screensaverservice.cpp
    screencopy.h screencopy.cpp
    accessibilityservice.h accessibilityservice.cpp
    # services shared with the Qt compositor
    ${CMAKE_SOURCE_DIR}/src/launcher.h ${CMAKE_SOURCE_DIR}/src/launcher.cpp
    ${CMAKE_SOURCE_DIR}/src/programmenu.h ${CMAKE_SOURCE_DIR}/src/programmenu.cpp
    ${CMAKE_SOURCE_DIR}/src/desktopshortcuts.h ${CMAKE_SOURCE_DIR}/src/desktopshortcuts.cpp
    ${CMAKE_SOURCE_DIR}/src/statusnotifier.h ${CMAKE_SOURCE_DIR}/src/statusnotifier.cpp
    ${CMAKE_SOURCE_DIR}/src/notifications.h ${CMAKE_SOURCE_DIR}/src/notifications.cpp
    ${CMAKE_SOURCE_DIR}/src/trashwatcher.h ${CMAKE_SOURCE_DIR}/src/trashwatcher.cpp
    ${CMAKE_SOURCE_DIR}/src/polkitagent.h ${CMAKE_SOURCE_DIR}/src/polkitagent.cpp
    ${CMAKE_SOURCE_DIR}/src/nmsecretagent.h ${CMAKE_SOURCE_DIR}/src/nmsecretagent.cpp
    ${CMAKE_SOURCE_DIR}/src/keyringprompter.h ${CMAKE_SOURCE_DIR}/src/keyringprompter.cpp
    ${CMAKE_SOURCE_DIR}/src/updatecheck.h ${CMAKE_SOURCE_DIR}/src/updatecheck.cpp
    # other programs' look: GTK's theme and fonts, browsers' title bars
    ${CMAKE_SOURCE_DIR}/src/programlook.h ${CMAKE_SOURCE_DIR}/src/programlook.cpp
)
qt_add_resources(win95shell programlook
    PREFIX /programlook
    BASE ${CMAKE_SOURCE_DIR}/src/programlook
    FILES ${CMAKE_SOURCE_DIR}/src/programlook/gtk.css
)
pkg_check_modules(GIO REQUIRED IMPORTED_TARGET gio-2.0)
# (layer-shell names an argument "namespace", a C++ keyword in the Qt
# wrappers: generated from a copy with it renamed)
file(READ ${CMAKE_SOURCE_DIR}/protocols/wlr/wlr-layer-shell-unstable-v1.xml layer_shell_xml)
string(REPLACE "name=\"namespace\"" "name=\"scope\"" layer_shell_xml "${layer_shell_xml}")
file(WRITE ${CMAKE_CURRENT_BINARY_DIR}/protocols/wlr-layer-shell-unstable-v1.xml "${layer_shell_xml}")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/protocols/wlr/wlr-layer-shell-unstable-v1.xml)
qt6_generate_wayland_protocol_client_sources(win95shell FILES
    ${CMAKE_CURRENT_BINARY_DIR}/protocols/wlr-layer-shell-unstable-v1.xml
    ${CMAKE_SOURCE_DIR}/protocols/wlr/wlr-foreign-toplevel-management-unstable-v1.xml
    ${CMAKE_SOURCE_DIR}/protocols/wayland95/wayland95-shell-v1.xml
    ${CMAKE_SOURCE_DIR}/protocols/wlr/wlr-output-power-management-unstable-v1.xml
    ${CMAKE_SOURCE_DIR}/protocols/wlr/wlr-screencopy-unstable-v1.xml
    ${WAYLAND_PROTOCOLS_DIR}/staging/ext-idle-notify/ext-idle-notify-v1.xml
    ${WAYLAND_PROTOCOLS_DIR}/staging/ext-session-lock/ext-session-lock-v1.xml
)
target_include_directories(win95shell PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/apps/common
)
# Start menu > Open as Admin (Root) runs session/wayland95-run-as-admin
# through pkexec: the installed copy, or the source tree's before it's installed
include(GNUInstallDirs)
target_compile_definitions(win95shell PRIVATE
    WAYLAND95_RUN_AS_ADMIN_HELPER="${CMAKE_INSTALL_FULL_LIBEXECDIR}/wayland95/wayland95-run-as-admin"
    WAYLAND95_RUN_AS_ADMIN_HELPER_SRC="${CMAKE_SOURCE_DIR}/session/wayland95-run-as-admin"
)

# the QML: the Qt compositor's files that make sense in a client (not its
# window frames, Xwayland or compositor root), and the shell's own root
set(shared_qml
    Taskbar.qml TaskButton.qml StartButton.qml StartMenu.qml Clock.qml Bevel.qml
    DesktopIcons.qml DesktopBackground.qml Theme.qml MenuIcon.qml
    Tray.qml TrayIcon.qml ClockTray.qml NotificationsTray.qml VolumeTray.qml
    NetworkTray.qml BluetoothTray.qml BatteryTray.qml DrivesTray.qml
    RunDialog.qml ShutDownDialog.qml
    SystemNotices.qml NotificationPopup.qml LevelOsd.qml PasswordDialog.qml NetworkPasswordDialog.qml
    KeyringPasswordDialog.qml
    UnlockDialog.qml ScreenCapture.qml TaskSwitcher.qml
)
set(qml_paths)
foreach(f ${shared_qml})
    set(path ${CMAKE_SOURCE_DIR}/qml/Win95Shell/${f})
    set_source_files_properties(${path} PROPERTIES QT_RESOURCE_ALIAS ${f})
    list(APPEND qml_paths ${path})
endforeach()
set_source_files_properties(${CMAKE_SOURCE_DIR}/qml/Win95Shell/Theme.qml PROPERTIES QT_QML_SINGLETON_TYPE TRUE)
foreach(f Shell.qml Splash.qml DialogLayer.qml ScreenSaver.qml SaverInput.qml PowerPolicy.qml Accessibility.qml)
    set_source_files_properties(qml/${f} PROPERTIES QT_RESOURCE_ALIAS ${f})
    list(APPEND qml_paths qml/${f})
endforeach()

qt_add_qml_module(win95shell
    URI Win95Shell
    VERSION 1.0
    QML_FILES ${qml_paths}
    RESOURCE_PREFIX /qt/qml
)

target_link_libraries(win95shell PRIVATE
    Qt6::Core Qt6::Gui Qt6::Qml Qt6::Quick Qt6::DBus
    Qt6::WaylandClient Qt6::WaylandClientPrivate
    win95uiplugin
    PkgConfig::GIO
)
set_target_properties(win95shell PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})
install(TARGETS win95shell RUNTIME DESTINATION bin)
