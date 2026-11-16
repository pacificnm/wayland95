# wayland95-compositor: Wayland 95's compositor on wlroots (C).
# See docs/architecture.md.
pkg_check_modules(WLROOTS REQUIRED IMPORTED_TARGET wlroots-0.18)
pkg_check_modules(WL_SERVER REQUIRED IMPORTED_TARGET wayland-server)
pkg_check_modules(XKB REQUIRED IMPORTED_TARGET xkbcommon)
pkg_check_modules(PIXMAN REQUIRED IMPORTED_TARGET pixman-1)
pkg_check_modules(XCB REQUIRED IMPORTED_TARGET xcb)
pkg_get_variable(WAYLAND_PROTOCOLS_DIR wayland-protocols pkgdatadir)
find_program(WAYLAND_SCANNER wayland-scanner REQUIRED)

# wlroots' headers include the server headers of the protocols they
# implement; they're generated here from the XML
set(GEN ${CMAKE_CURRENT_BINARY_DIR}/protocols)
file(MAKE_DIRECTORY ${GEN})
set(PROTOCOL_HEADERS)
function(server_protocol xml name)
    add_custom_command(
        OUTPUT ${GEN}/${name}-protocol.h
        COMMAND ${WAYLAND_SCANNER} server-header ${xml} ${GEN}/${name}-protocol.h
        DEPENDS ${xml})
    set(PROTOCOL_HEADERS ${PROTOCOL_HEADERS} ${GEN}/${name}-protocol.h PARENT_SCOPE)
endfunction()
server_protocol(${WAYLAND_PROTOCOLS_DIR}/stable/xdg-shell/xdg-shell.xml xdg-shell)
server_protocol(${WAYLAND_PROTOCOLS_DIR}/staging/cursor-shape/cursor-shape-v1.xml cursor-shape-v1)
server_protocol(${WAYLAND_PROTOCOLS_DIR}/unstable/pointer-constraints/pointer-constraints-unstable-v1.xml pointer-constraints-unstable-v1)
server_protocol(${WAYLAND_PROTOCOLS_DIR}/staging/tearing-control/tearing-control-v1.xml tearing-control-v1)
server_protocol(${WAYLAND_PROTOCOLS_DIR}/staging/content-type/content-type-v1.xml content-type-v1)
server_protocol(${WAYLAND_PROTOCOLS_DIR}/staging/ext-session-lock/ext-session-lock-v1.xml ext-session-lock-v1)
server_protocol(${CMAKE_SOURCE_DIR}/protocols/wlr/wlr-layer-shell-unstable-v1.xml wlr-layer-shell-unstable-v1)
server_protocol(${CMAKE_SOURCE_DIR}/protocols/wlr/wlr-output-power-management-unstable-v1.xml wlr-output-power-management-unstable-v1)

add_executable(wayland95-compositor
    main.c
    output.c
    input.c
    view.c
    xdg.c
    xwayland.c
    layers.c
    protocols.c
    server.h
    ${PROTOCOL_HEADERS}
)
set_target_properties(wayland95-compositor PROPERTIES
    C_STANDARD 17
    C_STANDARD_REQUIRED ON
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})
target_compile_definitions(wayland95-compositor PRIVATE WLR_USE_UNSTABLE)
target_compile_options(wayland95-compositor PRIVATE -Wall -Wextra -Wno-unused-parameter)
target_include_directories(wayland95-compositor PRIVATE ${GEN})
target_link_libraries(wayland95-compositor PRIVATE
    PkgConfig::WLROOTS PkgConfig::WL_SERVER PkgConfig::XKB PkgConfig::PIXMAN PkgConfig::XCB)
install(TARGETS wayland95-compositor RUNTIME DESTINATION bin)

# Deliberately broken builds for tests/selftest.sh, which checks that the
# tests catch each break (never installed, built only on request)
foreach(BREAK FRAMES FOCUS)
    string(TOLOWER ${BREAK} break_name)
    add_executable(wayland95-compositor-broken-${break_name} EXCLUDE_FROM_ALL
        main.c output.c input.c view.c xdg.c xwayland.c layers.c protocols.c server.h ${PROTOCOL_HEADERS})
    set_target_properties(wayland95-compositor-broken-${break_name} PROPERTIES
        C_STANDARD 17 RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/tests)
    target_compile_definitions(wayland95-compositor-broken-${break_name} PRIVATE
        WLR_USE_UNSTABLE WAYLAND95_TEST_BREAK_${BREAK})
    target_include_directories(wayland95-compositor-broken-${break_name} PRIVATE ${GEN})
    target_link_libraries(wayland95-compositor-broken-${break_name} PRIVATE
        PkgConfig::WLROOTS PkgConfig::WL_SERVER PkgConfig::XKB PkgConfig::PIXMAN PkgConfig::XCB)
endforeach()
