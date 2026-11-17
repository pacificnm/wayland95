cmake_minimum_required(VERSION 3.16)

project(win95wayland
    VERSION 0.1.0
    LANGUAGES C CXX  # C: wayland-scanner's protocol tables
)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Target Qt 6.4 (what Ubuntu 24.04 "noble" ships). Bump this if you build
# against a newer Qt - qt_add_qml_module() gains the nicer TARGET-based
# IMPORTS/DEPENDENCIES syntax (QTP0005) from 6.8 onward.
find_package(Qt6 6.4 REQUIRED COMPONENTS Core Gui Qml Quick DBus)
find_package(PkgConfig REQUIRED)

qt_standard_project_setup(REQUIRES 6.4)

# The version problem reports give: the newest in debian/changelog (what the
# update system publishes), else the project's
set(WAYLAND95_VERSION ${PROJECT_VERSION})
if(EXISTS ${CMAKE_SOURCE_DIR}/debian/changelog)
    file(STRINGS ${CMAKE_SOURCE_DIR}/debian/changelog _changelog LIMIT_COUNT 1)
    if(_changelog MATCHES "^wayland95 \\(([^)]+)\\)")
        set(WAYLAND95_VERSION ${CMAKE_MATCH_1})
    endif()
endif()
add_compile_definitions(
    WAYLAND95_VERSION="${WAYLAND95_VERSION}"
    WAYLAND95_ISSUES_URL="https://github.com/pacificnm/wayland95/issues/new"
)
# debian/changelog decides the version: rerun cmake when it changes
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/debian/changelog)

# --- the old Qt compositor ----------------------------------------------------
# Replaced by wayland95-compositor (wlroots) and win95shell. Built only for
# the tests that compare the two (tests/run.sh's classic and screen
# suites), and never installed; it goes in phase 6 (docs/migration.md).
option(WAYLAND95_QT_COMPOSITOR "build the old Qt compositor (for the comparison tests)" OFF)
if(WAYLAND95_QT_COMPOSITOR)
# QPlatformScreen::setPowerState: turning the display off (screensaver.cpp).
# Qt 6.9+ wants the private module asked for; before that Gui provides it.
find_package(Qt6 QUIET COMPONENTS GuiPrivate)
if(NOT TARGET Qt6::GuiPrivate)
    message(FATAL_ERROR "Qt's private Gui headers are needed (Debian/Ubuntu: qt6-base-private-dev)")
endif()

qt_add_executable(win95compositor
    src/main.cpp
    src/windowmanager.h
    src/windowmanager.cpp
    src/launcher.h
    src/launcher.cpp
    src/programmenu.h
    src/programmenu.cpp
    src/polkitagent.h
    src/polkitagent.cpp
    src/accessibility.h
    src/accessibility.cpp
    src/nmsecretagent.h
    src/nmsecretagent.cpp
    src/xdgactivation.h
    src/xdgactivation.cpp
    src/taskswitcher.h
    src/taskswitcher.cpp
    src/touchpad.h
    src/touchpad.cpp
    src/screengrab.h
    src/screengrab.cpp
    src/updatecheck.h
    src/updatecheck.cpp
    src/inputfixes.h
    src/inputfixes.cpp
    src/pointerframes.h
    src/pointerframes.cpp
    src/trashwatcher.h
    src/trashwatcher.cpp
    src/seatsettings.h
    src/seatsettings.cpp
    src/xwayland.h
    src/xwayland.cpp
    src/statusnotifier.h
    src/statusnotifier.cpp
    src/screensaver.h
    src/screensaver.cpp
    src/notifications.h
    src/notifications.cpp
)

# X11 programs: we run Xwayland and are its window manager (src/xwayland.*)
pkg_check_modules(XWM REQUIRED IMPORTED_TARGET xcb wayland-server)

# Touchpads: src/touchpad.cpp catches libinput_get_event(), which Qt's
# eglfs input code calls; the compositor's own definition must be the one
# found, so its symbols are exported
pkg_check_modules(LIBINPUT REQUIRED IMPORTED_TARGET libinput)
set_target_properties(win95compositor PROPERTIES ENABLE_EXPORTS ON)

# xdg-activation-v1 (Qt doesn't provide it for compositors)
find_package(Qt6 REQUIRED COMPONENTS WaylandCompositor WaylandScannerTools)
qt6_generate_wayland_protocol_server_sources(win95compositor PRIVATE_CODE
    FILES /usr/share/wayland-protocols/staging/xdg-activation/xdg-activation-v1.xml
)

target_include_directories(win95compositor PRIVATE
    src
    apps/common
)

set(win95shell_qml_files
    Main.qml
    WindowFrame.qml
    TitleBar.qml
    Taskbar.qml
    TaskButton.qml
    StartButton.qml
    StartMenu.qml
    Clock.qml
    Bevel.qml
    DesktopIcons.qml
    DesktopBackground.qml
    Theme.qml
    MenuIcon.qml
    Tray.qml
    TrayIcon.qml
    ClockTray.qml
    NotificationPopup.qml
    LevelOsd.qml
    NotificationsTray.qml
    VolumeTray.qml
    NetworkTray.qml
    BluetoothTray.qml
    BatteryTray.qml
    DrivesTray.qml
    XWindowFrame.qml
    XPopup.qml
    RunDialog.qml
    PasswordDialog.qml
    NetworkPasswordDialog.qml
    UnlockDialog.qml
    TaskSwitcher.qml
    ScreenCapture.qml
    ShutDownDialog.qml
    SystemNotices.qml
)

# qt_add_qml_module() has to run in this (the executable's own) directory
# scope - calling it from an add_subdirectory() for this single-target
# layout trips a CMake AUTOMOC finalization cycle. Because of that, the
# physical files live under qml/Win95Shell/ but are listed here with a
# QT_RESOURCE_ALIAS stripped back to the bare filename, so the module's
# resource path comes out as the expected ":/qt/qml/Win95Shell/Main.qml"
# rather than duplicating "Win95Shell" twice.
foreach(qml_file ${win95shell_qml_files})
    set_source_files_properties(qml/Win95Shell/${qml_file} PROPERTIES
        QT_RESOURCE_ALIAS ${qml_file}
    )
endforeach()

set_source_files_properties(qml/Win95Shell/Theme.qml PROPERTIES
    QT_QML_SINGLETON_TYPE TRUE
)

set(win95shell_qml_paths "")
foreach(qml_file ${win95shell_qml_files})
    list(APPEND win95shell_qml_paths "qml/Win95Shell/${qml_file}")
endforeach()

qt_add_qml_module(win95compositor
    URI Win95Shell
    VERSION 1.0
    QML_FILES ${win95shell_qml_paths}
    RESOURCE_PREFIX /qt/qml
)

target_link_libraries(win95compositor PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Qml
    Qt6::Quick
    Qt6::WaylandCompositor
    PkgConfig::XWM
    PkgConfig::LIBINPUT
    ${CMAKE_DL_LIBS}
    Qt6::DBus
    Qt6::GuiPrivate
    win95uiplugin # shared Win95Ui controls (dialogs, buttons) from apps/common
)

endif()

# --- bundled programs --------------------------------------------------------
# Each lives in apps/<name>/ as its own Wayland client executable, built into
# the same output directory as win95shell so its Launcher finds it.

# the compositor (wlroots, C) and the test tools (w95input...)
add_subdirectory(compositor)    # wayland95-compositor
add_subdirectory(tests/tools)   # w95input... (tests/run.sh; not installed)

add_subdirectory(apps/common)   # Win95Ui shared QML controls
add_subdirectory(shell)         # win95shell: the taskbar, desktop and Start menu
add_subdirectory(apps/files)    # win95files
add_subdirectory(apps/notepad)  # win95notepad
add_subdirectory(apps/control)  # win95control
add_subdirectory(apps/volume)   # win95volume
add_subdirectory(apps/sysmon)   # win95sysmon
add_subdirectory(apps/notify)   # win95notify (command line)
add_subdirectory(apps/portal)   # win95portal (file dialogs for other programs)
add_subdirectory(apps/imagewriter) # win95imagewriter (disk images to USB drives)
add_subdirectory(apps/games)    # win95solitaire, win95mines
add_subdirectory(apps/repos)    # win95repos
add_subdirectory(apps/markdown) # win95markdown
add_subdirectory(apps/calc)     # win95calc

pkg_check_modules(VTERM IMPORTED_TARGET vterm)
if(VTERM_FOUND)
    add_subdirectory(apps/terminal)
else()
    message(WARNING "libvterm not found - skipping win95terminal. "
                    "Install it with: sudo apt-get install libvterm-dev")
endif()

# --- the login session -------------------------------------------------------
# "Wayland 95" in the login screen's session list (SDDM and GDM read
# share/wayland-sessions), the script it runs, and the portal choices for it.
include(GNUInstallDirs)
configure_file(session/wayland95.desktop.in ${CMAKE_BINARY_DIR}/wayland95.desktop @ONLY)
install(PROGRAMS session/wayland95-session DESTINATION ${CMAKE_INSTALL_BINDIR})
install(FILES ${CMAKE_BINARY_DIR}/wayland95.desktop DESTINATION ${CMAKE_INSTALL_DATADIR}/wayland-sessions)
install(FILES session/wayland95-portals.conf DESTINATION ${CMAKE_INSTALL_DATADIR}/xdg-desktop-portal)
# win95portal: D-Bus starts it on demand; xdg-desktop-portal only looks for
# backends in its own folder (/usr/share/... whatever our prefix is)
configure_file(session/org.freedesktop.impl.portal.desktop.wayland95.service.in
               ${CMAKE_BINARY_DIR}/org.freedesktop.impl.portal.desktop.wayland95.service @ONLY)
install(FILES ${CMAKE_BINARY_DIR}/org.freedesktop.impl.portal.desktop.wayland95.service
        DESTINATION ${CMAKE_INSTALL_DATADIR}/dbus-1/services)
set(WAYLAND95_PORTAL_DIR "/usr/share/xdg-desktop-portal/portals" CACHE PATH
    "Where xdg-desktop-portal looks for backends")
install(FILES session/wayland95.portal DESTINATION ${WAYLAND95_PORTAL_DIR})

# Control Panel > Services: its root helper, and the polkit action that lets
# pkexec run it (polkit reads actions only from /usr/share, whatever our prefix)
install(PROGRAMS session/wayland95-services DESTINATION ${CMAKE_INSTALL_LIBEXECDIR}/wayland95)
# Control Panel > Backup's engine (cron runs it): next to the programs in a
# build tree too, where the applet and the desktop look first
install(PROGRAMS session/wayland95-backup DESTINATION ${CMAKE_INSTALL_BINDIR})
file(CREATE_LINK ${CMAKE_SOURCE_DIR}/session/wayland95-backup ${CMAKE_BINARY_DIR}/wayland95-backup SYMBOLIC)
set(WAYLAND95_SERVICES_HELPER "${CMAKE_INSTALL_FULL_LIBEXECDIR}/wayland95/wayland95-services")
configure_file(session/org.wayland95.services.policy.in ${CMAKE_BINARY_DIR}/org.wayland95.services.policy @ONLY)
set(WAYLAND95_POLKIT_ACTIONS_DIR "/usr/share/polkit-1/actions" CACHE PATH "Where polkit looks for actions")
install(FILES ${CMAKE_BINARY_DIR}/org.wayland95.services.policy DESTINATION ${WAYLAND95_POLKIT_ACTIONS_DIR})
