# win95email - Email: IMAP and SMTP mail, with folders, a message list and a
# preview pane (a standalone Wayland client). Start > Programs > Internet >
# Email. Passwords are kept in the desktop's keyring (the Secret Service).

find_package(Qt6 REQUIRED COMPONENTS Network DBus)

qt_add_executable(win95email
    src/main.cpp
    src/mime.h
    src/mime.cpp
    src/imap.h
    src/imap.cpp
    src/smtp.h
    src/smtp.cpp
    src/secrets.h
    src/secrets.cpp
    src/mailstore.h
    src/mailstore.cpp
)

set_target_properties(win95email PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}
)

target_include_directories(win95email PRIVATE src ${CMAKE_SOURCE_DIR}/apps/common)

qt_add_qml_module(win95email
    URI Win95Email
    VERSION 1.0
    QML_FILES
        Main.qml
        Compose.qml
        AccountDialogs.qml
        MailIcon.qml
    RESOURCE_PREFIX /qt/qml
)

target_link_libraries(win95email PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Qml
    Qt6::Quick
    Qt6::Network
    Qt6::DBus
    win95uiplugin
)

install(TARGETS win95email RUNTIME DESTINATION bin)
