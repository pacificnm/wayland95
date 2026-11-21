# win95tradinglab - Trading Lab (github.com/pacificnm/trading-labs.app), a desktop app for learning to trade with
# paper money: live-data charts with studies and drawings, simulated paper accounts with a full order ticket, research
# screens (Financial Modeling Prep data, Cboe delayed options), a journal, a strategy library and a Claude assistant.
# Upstream is Electron + React + TypeScript; its main process is ported to C++ here (src/core: same SQLite schema,
# same behaviour and messages) and its screens are redrawn with Win95Ui. Lessons, candle-pattern text, the Claude
# tool list and the Help pages were converted once into content/. API keys live in the desktop keyring.
# Start > Programs > Accessories > Trading Lab. Needs only Qt (Sql, Network, Concurrent, DBus).

find_package(Qt6 QUIET COMPONENTS Sql Network Concurrent DBus)
if(NOT TARGET Qt6::Sql OR NOT TARGET Qt6::Network OR NOT TARGET Qt6::Concurrent OR NOT TARGET Qt6::DBus)
    message(WARNING "Qt Sql, Network, Concurrent or DBus not found - skipping win95tradinglab.")
    return()
endif()

# The ported main process and shared logic: plain Qt, no interface
add_library(tradinglab_core STATIC
    src/core/accounts.cpp src/core/accounts.h
    src/core/accountsinfo.cpp src/core/accountsinfo.h
    src/core/backup.cpp src/core/backup.h
    src/core/calcstate.cpp src/core/calcstate.h
    src/core/candlepatterns.cpp src/core/candlepatterns.h
    src/core/chartsettings.cpp src/core/chartsettings.h
    src/core/chartsetup.cpp src/core/chartsetup.h
    src/core/chat.cpp src/core/chat.h
    src/core/congresslogic.cpp src/core/congresslogic.h
    src/core/db.cpp src/core/db.h
    src/core/engine.cpp src/core/engine.h
    src/core/fmp.cpp src/core/fmp.h
    src/core/fmpcache.cpp src/core/fmpcache.h
    src/core/fmpcaps.cpp src/core/fmpcaps.h
    src/core/fmphelpers.cpp src/core/fmphelpers.h
    src/core/fmpmcp.cpp src/core/fmpmcp.h
    src/core/http.cpp src/core/http.h
    src/core/indicators.cpp src/core/indicators.h
    src/core/jsnum.cpp src/core/jsnum.h
    src/core/marketlogic.cpp src/core/marketlogic.h
    src/core/normalize.cpp src/core/normalize.h
    src/core/nytime.cpp src/core/nytime.h
    src/core/optionsfeed.cpp src/core/optionsfeed.h
    src/core/optionsmath.cpp src/core/optionsmath.h
    src/core/orderdraft.cpp src/core/orderdraft.h
    src/core/portfolio.cpp src/core/portfolio.h
    src/core/portfolioorders.cpp src/core/portfolioorders.h
    src/core/portfoliostore.cpp src/core/portfoliostore.h
    src/core/position.cpp src/core/position.h
    src/core/sampledata.cpp src/core/sampledata.h
    src/core/schema.cpp src/core/schema.h
    src/core/screenerlogic.cpp src/core/screenerlogic.h
    src/core/stores.cpp src/core/stores.h
    src/core/studies.cpp src/core/studies.h
    src/core/studycolors.cpp src/core/studycolors.h
    src/core/timeframe.cpp src/core/timeframe.h
    src/core/trade.cpp src/core/trade.h
    src/core/util.cpp src/core/util.h
    src/core/bar.h
    src/core/barsvariant.h
    src/core/version.h
)
set_target_properties(tradinglab_core PROPERTIES CXX_STANDARD 20 AUTOMOC ON)
target_include_directories(tradinglab_core PUBLIC src)
target_link_libraries(tradinglab_core PUBLIC Qt6::Core Qt6::Sql Qt6::Network PRIVATE Qt6::Concurrent)

qt_add_executable(win95tradinglab
    src/main.cpp
    src/backend.cpp src/backend.h
    src/core.cpp src/core_more.cpp src/core.h
    src/keystore.cpp src/keystore.h
    src/windowgrabber.h
    src/chartview.cpp src/chartview.h
)
set_target_properties(win95tradinglab PROPERTIES
    CXX_STANDARD 20
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}
)
target_include_directories(win95tradinglab PRIVATE src ${CMAKE_SOURCE_DIR}/apps/common)
target_compile_options(win95tradinglab PRIVATE -Wno-missing-include-dirs)

set_source_files_properties(Util.qml PROPERTIES QT_QML_SINGLETON_TYPE TRUE)

qt_add_qml_module(win95tradinglab
    URI Win95TradingLab
    VERSION 1.0
    QML_FILES
        AboutDialog.qml
        AccountView.qml
        AnalystView.qml
        CalculatorView.qml
        ChartColorButton.qml
        ChartData.qml
        ChartDiagram.qml
        ChartSettingsView.qml
        ChartStudyCard.qml
        ChartView.qml
        ChatPanel.qml
        CongressView.qml
        DatabaseError.qml
        DocBadge.qml
        DocCandlePatterns.qml
        DocChip.qml
        DocField.qml
        DocMarkdown.qml
        DocPatternChart.qml
        DocRows.qml
        DocScroll.qml
        DocTitleBar.qml
        FundamentalsView.qml
        GroupBox95.qml
        Heading95.qml
        HelpView.qml
        IconButton.qml
        IndexTicker.qml
        InputDialog.qml
        JournalView.qml
        Label95.qml
        ListWell.qml
        Main.qml
        MarketNewsView.qml
        MarketView.qml
        NewsView.qml
        Notice.qml
        OptionsView.qml
        PortfolioView.qml
        PromptDialog.qml
        PxButton.qml
        PxCard.qml
        PxHint.qml
        PxNum.qml
        PxPage.qml
        PxSeg.qml
        PxSymbolSearch.qml
        PxTable.qml
        PxTitle.qml
        QuoteView.qml
        ScreenerView.qml
        SettingsView.qml
        StrategiesView.qml
        SvCard.qml
        SvKV.qml
        SvNewsList.qml
        SvNote.qml
        SvPage.qml
        SvRangeBar.qml
        SvSample.qml
        SvTable.qml
        SvTitleBar.qml
        SymbolHeader.qml
        TextArea95.qml
        TextView95.qml
        Toasts.qml
        ToolRunner.qml
        ToolTipArea.qml
        TradeTicket.qml
        TradesView.qml
        Util.qml
        WatchlistView.qml
    RESOURCE_PREFIX /qt/qml
)

# content/: converted once from upstream (see content/README.md)
file(GLOB_RECURSE TRADINGLAB_CONTENT RELATIVE ${CMAKE_CURRENT_SOURCE_DIR} CONFIGURE_DEPENDS content/*.json content/*.md)
qt_add_resources(win95tradinglab tradinglab_content
    PREFIX /tradinglab
    FILES ${TRADINGLAB_CONTENT}
)

target_link_libraries(win95tradinglab PRIVATE
    tradinglab_core
    Qt6::Core
    Qt6::Gui
    Qt6::Qml
    Qt6::Quick
    Qt6::Concurrent
    Qt6::DBus # the keyring (secrets.h), for the FMP and Anthropic keys
    win95uiplugin
)

install(TARGETS win95tradinglab RUNTIME DESTINATION bin)
