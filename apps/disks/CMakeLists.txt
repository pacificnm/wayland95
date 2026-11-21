# win95disks - Disk Manager (a standalone Wayland client): the computer's
# disks and their partitions, like Windows' Disk Management: mount and
# unmount, format, make and delete partitions, new partition tables, labels,
# check and repair, and each disk's S.M.A.R.T. health. The disks are changed
# through UDisks2, which asks for the administrator password; S.M.A.R.T.
# comes from smartctl (smartmontools), run through wayland95-services
# (pkexec). The program never runs as root.

find_package(Qt6 REQUIRED COMPONENTS DBus)

qt_add_executable(win95disks
    src/main.cpp
    src/diskmanager.h
    src/diskmanager.cpp
    src/smartinfo.h
    src/smartinfo.cpp
)

set_target_properties(win95disks PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}
)

target_include_directories(win95disks PRIVATE src ${CMAKE_SOURCE_DIR}/apps/common)

# smartctl runs as root through Control Panel's helper (and its polkit
# action); in a build tree that isn't installed yet, the one in the sources
include(GNUInstallDirs)
target_compile_definitions(win95disks PRIVATE
    WAYLAND95_SERVICES_HELPER="${CMAKE_INSTALL_FULL_LIBEXECDIR}/wayland95/wayland95-services"
    WAYLAND95_SERVICES_HELPER_SRC="${CMAKE_SOURCE_DIR}/session/wayland95-services"
)

qt_add_qml_module(win95disks
    URI Win95Disks
    VERSION 1.0
    QML_FILES
        Main.qml
        DiskMap.qml
        DriveIcon.qml
        GroupBox95.qml
        PartitionDialog.qml
        SmartWindow.qml
    RESOURCE_PREFIX /qt/qml
)

target_link_libraries(win95disks PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Qml
    Qt6::Quick
    Qt6::DBus
    win95uiplugin
)

install(TARGETS win95disks RUNTIME DESTINATION bin)
