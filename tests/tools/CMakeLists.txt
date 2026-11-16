# Test tools (tests/run.sh): Wayland clients that drive and inspect a
# wlroots compositor through standard protocols.
pkg_check_modules(WL_CLIENT REQUIRED IMPORTED_TARGET wayland-client)
find_program(WAYLAND_SCANNER wayland-scanner REQUIRED)
set(GEN ${CMAKE_CURRENT_BINARY_DIR}/protocols)
file(MAKE_DIRECTORY ${GEN})
set(VP_XML ${CMAKE_SOURCE_DIR}/protocols/wlr/wlr-virtual-pointer-unstable-v1.xml)
add_custom_command(
    OUTPUT ${GEN}/wlr-virtual-pointer-unstable-v1-client-protocol.h ${GEN}/wlr-virtual-pointer-unstable-v1-protocol.c
    COMMAND ${WAYLAND_SCANNER} client-header ${VP_XML} ${GEN}/wlr-virtual-pointer-unstable-v1-client-protocol.h
    COMMAND ${WAYLAND_SCANNER} private-code ${VP_XML} ${GEN}/wlr-virtual-pointer-unstable-v1-protocol.c
    DEPENDS ${VP_XML})

add_executable(w95input w95input.c
    ${GEN}/wlr-virtual-pointer-unstable-v1-client-protocol.h ${GEN}/wlr-virtual-pointer-unstable-v1-protocol.c)
set_target_properties(w95input PROPERTIES C_STANDARD 17 RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/tests)
target_compile_options(w95input PRIVATE -Wall -Wextra -Wno-unused-parameter)
target_include_directories(w95input PRIVATE ${GEN})
target_link_libraries(w95input PRIVATE PkgConfig::WL_CLIENT)
