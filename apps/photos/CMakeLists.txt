# win95photos - Photo Gallery: browses folders of photos as thumbnails,
# shows them full-window (zoom, slide show) and does basic editing (rotate,
# crop, resize, light and color, effects) with undo (a standalone Wayland
# client). Start > Programs > Accessories > Photo Gallery; the file
# manager opens pictures with it.

find_package(Qt6 REQUIRED COMPONENTS Concurrent)

qt_add_executable(win95photos
    src/main.cpp
    src/exif.h
    src/exif.cpp
    src/photofiles.h
    src/photofiles.cpp
    src/photofolder.h
    src/photofolder.cpp
    src/photoeditor.h
    src/photoeditor.cpp
    src/photoservices.h
    src/photoservices.cpp
)

set_target_properties(win95photos PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}
)

target_include_directories(win95photos PRIVATE src ${CMAKE_SOURCE_DIR}/apps/common)

qt_add_qml_module(win95photos
    URI Win95Photos
    VERSION 1.0
    QML_FILES
        Main.qml
        PhotoView.qml
        EditPanel.qml
        PhotoIcon.qml
        PhotoButton.qml
        SlideShow.qml
    RESOURCE_PREFIX /qt/qml
)

target_link_libraries(win95photos PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Qml
    Qt6::Quick
    Qt6::Concurrent
    win95uiplugin
)

install(TARGETS win95photos RUNTIME DESTINATION bin)
