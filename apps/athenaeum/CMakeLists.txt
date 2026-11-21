# win95athenaeum - Athenaeum: KAthenaeum (github.com/pacificnm/KAthenaeum),
# the offline Wikipedia library with a local AI research assistant, as a
# Wayland 95 program. Kiwix ZIM files are read with libzim; answers come
# from Ollama on this computer, or from Claude (Anthropic's API, key in the
# keyring); chats, projects and notes are kept in
# SQLite. The library and assistant are KAthenaeum's own C++ (src/core);
# KDE Frameworks are replaced by plain Qt (src/compat stands in for KI18n)
# and the pages are drawn with Win95Ui. Start > Programs > Accessories >
# Athenaeum. Built only when libzim and Qt WebEngine's headers are there.

find_package(Qt6 QUIET COMPONENTS Sql Network Concurrent WebEngineQuick DBus)
pkg_check_modules(ZIM QUIET IMPORTED_TARGET libzim>=9.0)
pkg_check_modules(LIBXML2 QUIET IMPORTED_TARGET libxml-2.0)
if(NOT TARGET Qt6::WebEngineQuick OR NOT ZIM_FOUND OR NOT LIBXML2_FOUND)
    message(WARNING "libzim, libxml2 or Qt WebEngine not found - skipping win95athenaeum. "
                    "Install them with: sudo apt-get install libzim-dev libxml2-dev qt6-webengine-dev")
    return()
endif()
find_package(Threads REQUIRED)

# Everything that is not interface: KAthenaeum's src/core, unchanged
add_library(athenaeum_core STATIC
    src/compat/KLocalizedString
    src/compat/klocalizedstring.cpp
    src/core/answerer.cpp src/core/answerer.h
    src/core/articlerenderer.cpp src/core/articlerenderer.h
    src/core/catalog.cpp src/core/catalog.h
    src/core/chats.cpp src/core/chats.h
    src/core/citations.cpp src/core/citations.h
    src/core/classroom.cpp src/core/classroom.h
    src/core/database.cpp src/core/database.h
    src/core/dictionary.cpp src/core/dictionary.h
    src/core/downloads.cpp src/core/downloads.h
    src/core/drafts.cpp src/core/drafts.h
    src/core/embedding.cpp src/core/embedding.h
    src/core/extract.cpp src/core/extract.h
    src/core/xmlutil_p.h
    src/core/highlights.cpp src/core/highlights.h
    src/core/history.cpp src/core/history.h
    src/core/library.cpp src/core/library.h
    src/core/network.cpp src/core/network.h
    src/core/ollama.cpp src/core/ollama.h
    src/core/claude.cpp src/core/claude.h
    src/core/projects.cpp src/core/projects.h
    src/core/rag.cpp src/core/rag.h
    src/core/settings.cpp src/core/settings.h
    src/core/teacher.cpp src/core/teacher.h
    src/core/timeline.cpp src/core/timeline.h
    src/core/zimarchive.cpp src/core/zimarchive.h
    src/core/zimarchives_p.h
    src/core/zimreader.cpp src/core/zimreader.h
    src/core/zimsearch.cpp src/core/zimsearch.h
)
set_target_properties(athenaeum_core PROPERTIES CXX_STANDARD 20 AUTOMOC ON)
target_include_directories(athenaeum_core PUBLIC src src/compat)
target_link_libraries(athenaeum_core
    PUBLIC Qt6::Core Qt6::Sql Qt6::Network
    PRIVATE Qt6::Gui Qt6::Concurrent PkgConfig::ZIM PkgConfig::LIBXML2 Threads::Threads)

qt_add_executable(win95athenaeum
    src/main.cpp
    src/i18ncontext.cpp src/i18ncontext.h
    src/windowgrabber.h
    src/controlpanel.h
    src/appstate.cpp src/appstate.h
    src/articlestore.cpp src/articlestore.h
    src/chatlist.cpp src/chatlist.h
    src/classroomcontroller.cpp src/classroomcontroller.h
    src/connectivity.cpp src/connectivity.h
    src/conversation.cpp src/conversation.h
    src/documentmodel.cpp src/documentmodel.h
    src/draftcontroller.cpp src/draftcontroller.h
    src/librarybrowser.cpp src/librarybrowser.h
    src/librarymanager.cpp src/librarymanager.h
    src/librarymodels.cpp src/librarymodels.h
    src/modelsettings.cpp src/modelsettings.h
    src/notes.cpp src/notes.h
    src/projectlist.cpp src/projectlist.h
    src/projectmodel.cpp src/projectmodel.h
    src/searchcontroller.cpp src/searchcontroller.h
    src/timelinemodel.cpp src/timelinemodel.h
    src/zimschemehandler.cpp src/zimschemehandler.h
)
set_target_properties(win95athenaeum PROPERTIES
    CXX_STANDARD 20
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}
)
target_include_directories(win95athenaeum PRIVATE src ${CMAKE_SOURCE_DIR}/apps/common)
# Qt's QML targets list Debian's separate private-header folders
target_compile_options(win95athenaeum PRIVATE -Wno-missing-include-dirs)

set_source_files_properties(Util.qml PROPERTIES QT_QML_SINGLETON_TYPE TRUE)

qt_add_qml_module(win95athenaeum
    URI Win95Athenaeum
    VERSION 1.0
    QML_FILES
        ArticlePage.qml
        AskPage.qml
        AthenaeumIcon.qml
        ChatsPage.qml
        ChatView.qml
        ChipFlow.qml
        CitationDialog.qml
        ClassroomPage.qml
        CoursePage.qml
        DatabaseError.qml
        DefinitionDialog.qml
        DocumentWindow.qml
        DocumentWindows.qml
        GroupBox95.qml
        Heading95.qml
        HighlightCard.qml
        IconButton.qml
        InputDialog.qml
        Label95.qml
        LessonPage.qml
        LibrariesPage.qml
        LibraryManagerPage.qml
        LibraryPage.qml
        ListWell.qml
        Main.qml
        NotesPage.qml
        Notice.qml
        Page95.qml
        ProjectPage.qml
        ProjectPicker.qml
        ProjectsPage.qml
        QuizView.qml
        SearchPage.qml
        SettingsPage.qml
        TextArea95.qml
        TextView95.qml
        TimelinePage.qml
        TitlesPage.qml
        UnitTestPage.qml
        Util.qml
        TestDriver.qml
    RESOURCE_PREFIX /qt/qml
)

target_link_libraries(win95athenaeum PRIVATE
    athenaeum_core
    Qt6::Core
    Qt6::Gui
    Qt6::Qml
    Qt6::Quick
    Qt6::Concurrent
    Qt6::WebEngineQuick
    Qt6::DBus # the keyring (secrets.h), for the Claude API key
    win95uiplugin
)

install(TARGETS win95athenaeum RUNTIME DESTINATION bin)
