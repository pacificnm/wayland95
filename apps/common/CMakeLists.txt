# Win95Ui - shared Win95-styled QML controls for the bundled apps (bevels,
# buttons, menus, scrollbars, message boxes). Built as a static QML module;
# an app uses it by linking `win95uiplugin` and adding
# Q_IMPORT_QML_PLUGIN(Win95UiPlugin) to its main.cpp.

# Bluetooth and tray backends talk to system services over D-Bus
find_package(Qt6 REQUIRED COMPONENTS DBus)

qt_add_library(win95ui STATIC)

set_source_files_properties(Theme.qml PROPERTIES
    QT_QML_SINGLETON_TYPE TRUE
)

qt_add_qml_module(win95ui
    URI Win95Ui
    VERSION 1.0
    RESOURCE_PREFIX /qt/qml
    QML_FILES
        Theme.qml
        Bevel.qml
        EtchedText.qml
        ToolButton95.qml
        Button95.qml
        TextField95.qml
        ScrollBar95.qml
        Menu95.qml
        MenuBar95.qml
        MessageBox95.qml
        FocusRect95.qml
        Dialog95.qml
        DialogWindow95.qml
        RadioButton95.qml
        CheckBox95.qml
        ComboBox95.qml
        FileDialog95.qml
        TabWidget95.qml
        Slider95.qml
        VSlider95.qml
        SpinBox95.qml
        ProgressBar95.qml
        SizeGrip95.qml
        ScreenSaverView.qml
        SaverStarfield.qml
        SaverPolygons.qml
        SaverCurves.qml
        SaverMarquee.qml
        SaverMatrix.qml
    SOURCES
        win95fs.h
        win95fs.cpp
        wayland95settings.h
        wayland95settings.cpp
        audiomixer.h
        audiomixer.cpp
        bluetoothinfo.h
        bluetoothinfo.cpp
        networkstatus.h
        networkstatus.cpp
        batteryinfo.h
        batteryinfo.cpp
        powermanager.h
        powermanager.cpp
        timezones.h
        timezones.cpp
        desktopentry.h
        desktopentry.cpp
        accountpassword.h
        accountpassword.cpp
        crashhandler.h
        crashhandler.cpp
        problemreports.h
        problemreports.cpp
        drives.h
        drives.cpp
        secrets.h
        secrets.cpp
)

target_link_libraries(win95ui PRIVATE Qt6::Core Qt6::Gui Qt6::Qml Qt6::Quick)
# public: the D-Bus symbols end up in every program linking this static library
target_link_libraries(win95ui PUBLIC Qt6::DBus util) # util: forkpty() before glibc 2.34
target_include_directories(win95ui PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
