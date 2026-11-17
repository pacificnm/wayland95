# The games: win95solitaire (Klondike patience) and win95mines (clear a
# minefield). Start > Programs > Games. Each is a standalone Wayland client
# built from the same src/main.cpp; the games themselves are QML.

function(wayland95_game game uri)
    qt_add_executable(win95${game}
        ${CMAKE_CURRENT_SOURCE_DIR}/../src/main.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/../src/gamestore.h
        ${CMAKE_CURRENT_SOURCE_DIR}/../src/gamestore.cpp
    )
    set_target_properties(win95${game} PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})
    target_include_directories(win95${game} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../src ${CMAKE_SOURCE_DIR}/apps/common)
    target_compile_definitions(win95${game} PRIVATE
        GAME_NAME="win95${game}" GAME_QML="qrc:/qt/qml/${uri}/Main.qml")
    target_link_libraries(win95${game} PRIVATE Qt6::Core Qt6::Gui Qt6::Qml Qt6::Quick win95uiplugin)
    install(TARGETS win95${game} RUNTIME DESTINATION bin)
endfunction()

add_subdirectory(solitaire)
add_subdirectory(mines)
