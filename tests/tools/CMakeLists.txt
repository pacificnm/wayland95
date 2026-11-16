# Test tools (tests/run.sh): Wayland clients that drive and inspect a
# wlroots compositor through standard protocols.
pkg_check_modules(WL_CLIENT REQUIRED IMPORTED_TARGET wayland-client)
find_program(WAYLAND_SCANNER wayland-scanner REQUIRED)
set(GEN ${CMAKE_CURRENT_BINARY_DIR}/protocols)
file(MAKE_DIRECTORY ${GEN})
function(client_protocol name)
    set(xml ${CMAKE_SOURCE_DIR}/protocols/wlr/${name}.xml)
    if(NOT EXISTS ${xml})
        set(xml ${CMAKE_SOURCE_DIR}/protocols/${name}.xml)
    endif()
    add_custom_command(
        OUTPUT ${GEN}/${name}-client-protocol.h ${GEN}/${name}-protocol.c
        COMMAND ${WAYLAND_SCANNER} client-header ${xml} ${GEN}/${name}-client-protocol.h
        COMMAND ${WAYLAND_SCANNER} private-code ${xml} ${GEN}/${name}-protocol.c
        DEPENDS ${xml})
endfunction()
client_protocol(wlr-virtual-pointer-unstable-v1)
client_protocol(wlr-foreign-toplevel-management-unstable-v1)
client_protocol(virtual-keyboard-unstable-v1)
pkg_check_modules(XKBC REQUIRED IMPORTED_TARGET xkbcommon)

add_executable(w95input w95input.c
    ${GEN}/wlr-virtual-pointer-unstable-v1-client-protocol.h ${GEN}/wlr-virtual-pointer-unstable-v1-protocol.c
    ${GEN}/virtual-keyboard-unstable-v1-client-protocol.h ${GEN}/virtual-keyboard-unstable-v1-protocol.c)
set_target_properties(w95input PROPERTIES C_STANDARD 17 RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/tests)
target_compile_options(w95input PRIVATE -Wall -Wextra -Wno-unused-parameter)
target_include_directories(w95input PRIVATE ${GEN})
target_link_libraries(w95input PRIVATE PkgConfig::WL_CLIENT PkgConfig::XKBC)

add_executable(w95query w95query.c
    ${GEN}/wlr-foreign-toplevel-management-unstable-v1-client-protocol.h
    ${GEN}/wlr-foreign-toplevel-management-unstable-v1-protocol.c)
set_target_properties(w95query PROPERTIES C_STANDARD 17 RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/tests)
target_compile_options(w95query PRIVATE -Wall -Wextra -Wno-unused-parameter)
target_include_directories(w95query PRIVATE ${GEN})
target_link_libraries(w95query PRIVATE PkgConfig::WL_CLIENT)

# a GTK 3 window that reports what it receives (Wayland, or X11 with
# GDK_BACKEND=x11)
pkg_check_modules(GTK3 IMPORTED_TARGET gtk+-3.0)
if(GTK3_FOUND)
    add_executable(w95probe-gtk w95probe-gtk.c)
    set_target_properties(w95probe-gtk PROPERTIES C_STANDARD 17 RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/tests)
    target_compile_options(w95probe-gtk PRIVATE -Wall -Wno-unused-parameter -Wno-deprecated-declarations)
    target_link_libraries(w95probe-gtk PRIVATE PkgConfig::GTK3)
endif()

# the same for Qt (what Wayland 95's own apps use)
find_package(Qt6 QUIET COMPONENTS Widgets)
if(TARGET Qt6::Widgets)
    add_executable(w95probe-qt w95probe-qt.cpp)
    set_target_properties(w95probe-qt PROPERTIES AUTOMOC OFF RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/tests)
    target_link_libraries(w95probe-qt PRIVATE Qt6::Widgets)
endif()
