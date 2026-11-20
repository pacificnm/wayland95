# win95archive - Archive Manager: opens zip, 7z, tar(.gz/.bz2/.xz/.zst),
# rar, iso, cab and the single-file compressors, lists what's in them,
# extracts, views, tests, and makes and changes zip, 7z and tar archives
# (a standalone Wayland client). libarchive does the work; 7-Zip and unar,
# if installed, do what it can't (7z with a password, some RAR archives).
# Start > Programs > Accessories > Archive Manager; the file manager opens
# archives with it and runs it for Extract Here / Add to archive.

find_package(Qt6 REQUIRED COMPONENTS Concurrent)

qt_add_executable(win95archive
    src/main.cpp
    src/archiveengine.h
    src/archiveengine.cpp
    src/archivedocument.h
    src/archivedocument.cpp
)

set_target_properties(win95archive PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}
)

target_include_directories(win95archive PRIVATE src ${CMAKE_SOURCE_DIR}/apps/common)

qt_add_qml_module(win95archive
    URI Win95Archive
    VERSION 1.0
    QML_FILES
        Main.qml
        ArchiveIcon.qml
        ArchiveButton.qml
        AddDialog.qml
        ExtractDialog.qml
        ProgressDialog.qml
        PasswordDialog.qml
    RESOURCE_PREFIX /qt/qml
)

target_link_libraries(win95archive PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Qml
    Qt6::Quick
    Qt6::Concurrent
    LibArchive::LibArchive
    win95uiplugin
)

install(TARGETS win95archive RUNTIME DESTINATION bin)
