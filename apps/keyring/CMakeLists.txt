# win95keyring - Wayland 95's keyring: the freedesktop Secret Service
# (org.freedesktop.secrets), so programs (Claude, VS Code, browsers, Email)
# can keep passwords encrypted, unlocked by your login password. D-Bus
# starts it; pam_wayland95_keyring hands it the login password.
# Qt Core + D-Bus and OpenSSL's libcrypto: no display needed.

find_package(Qt6 REQUIRED COMPONENTS DBus)
find_package(OpenSSL 3 REQUIRED COMPONENTS Crypto)
include(GNUInstallDirs)

qt_add_executable(win95keyring
    src/main.cpp
    src/crypto.h src/crypto.cpp
    src/passwordlist.h src/passwordlist.cpp
    src/secretservice.h src/secretservice.cpp
)
set_target_properties(win95keyring PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})
target_link_libraries(win95keyring PRIVATE Qt6::Core Qt6::DBus OpenSSL::Crypto)
install(TARGETS win95keyring RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})

# D-Bus starts it the first time a program asks for org.freedesktop.secrets
configure_file(${CMAKE_SOURCE_DIR}/session/org.freedesktop.secrets.service.in
               ${CMAKE_BINARY_DIR}/org.freedesktop.secrets.service @ONLY)
install(FILES ${CMAKE_BINARY_DIR}/org.freedesktop.secrets.service
        DESTINATION ${CMAKE_INSTALL_DATADIR}/dbus-1/services)

# The PAM module (libpam0g-dev). PAM looks for modules in its own folder,
# whatever our prefix; the pam-auth-update profile names it by full path.
find_path(PAM_INCLUDE_DIR security/pam_modules.h)
find_library(PAM_LIBRARY pam)
if(PAM_INCLUDE_DIR AND PAM_LIBRARY)
    if(CMAKE_LIBRARY_ARCHITECTURE)
        set(pam_dir "/usr/lib/${CMAKE_LIBRARY_ARCHITECTURE}/security")
    else()
        set(pam_dir "/usr/lib/security")
    endif()
    set(WAYLAND95_PAM_MODULE_DIR "${pam_dir}" CACHE PATH "Where PAM modules go")
    add_library(pam_wayland95_keyring MODULE pam/pam_wayland95_keyring.c)
    set_target_properties(pam_wayland95_keyring PROPERTIES PREFIX "" C_VISIBILITY_PRESET default
                          LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})
    target_include_directories(pam_wayland95_keyring PRIVATE ${PAM_INCLUDE_DIR})
    target_compile_definitions(pam_wayland95_keyring PRIVATE
        WIN95KEYRING_PATH="${CMAKE_INSTALL_FULL_BINDIR}/win95keyring")
    target_link_libraries(pam_wayland95_keyring PRIVATE ${PAM_LIBRARY})
    install(TARGETS pam_wayland95_keyring LIBRARY DESTINATION ${WAYLAND95_PAM_MODULE_DIR})

    set(WAYLAND95_PAM_MODULE "${WAYLAND95_PAM_MODULE_DIR}/pam_wayland95_keyring.so")
    configure_file(${CMAKE_SOURCE_DIR}/session/wayland95-keyring.pam-config.in
                   ${CMAKE_BINARY_DIR}/wayland95-keyring.pam-config @ONLY)
    set(WAYLAND95_PAM_CONFIGS_DIR "/usr/share/pam-configs" CACHE PATH "pam-auth-update's profiles")
    install(FILES ${CMAKE_BINARY_DIR}/wayland95-keyring.pam-config
            DESTINATION ${WAYLAND95_PAM_CONFIGS_DIR} RENAME wayland95-keyring)
else()
    message(WARNING "PAM headers not found - skipping pam_wayland95_keyring (the keyring will ask for its "
                    "password instead of opening at login). Install them with: sudo apt-get install libpam0g-dev")
endif()
