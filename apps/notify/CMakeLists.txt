# win95notify - send a desktop notification from a script or terminal
# (like notify-send). Talks to org.freedesktop.Notifications, so it works
# with Wayland 95's notifications or any other desktop's. Qt Core + D-Bus only.

find_package(Qt6 REQUIRED COMPONENTS DBus)

qt_add_executable(win95notify main.cpp)

set_target_properties(win95notify PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}
)

target_link_libraries(win95notify PRIVATE Qt6::Core Qt6::DBus)

install(TARGETS win95notify RUNTIME DESTINATION bin)
