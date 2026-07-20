find_package(Qt6 REQUIRED COMPONENTS Test QuickTest)

# Block tests
qt_add_executable(test_block test_block.cpp)
target_link_libraries(test_block PRIVATE kvit-core Qt6::Test)
add_test(NAME BlockTests COMMAND test_block)

# BlockAttributes tests (per-block presentation tag parse/serialize)
qt_add_executable(test_blockattributes test_blockattributes.cpp)
target_link_libraries(test_blockattributes PRIVATE kvit-core Qt6::Test)
add_test(NAME BlockAttributesTests COMMAND test_blockattributes)

# ShortcutMap tests (features.md §13 shortcut catalog audit)
# Shortcut catalog audit. Qt resolves StandardKey through the platform theme,
# which only exists once a QGuiApplication does: without Qt6::Gui this binary
# would assert against Qt's built-in fallback table instead of the bindings the
# running app gets, and the two genuinely differ (Redo answers to Ctrl+Y in the
# app and not in the fallback). Qt6::Gui makes QTEST_MAIN build a
# QGuiApplication, so the audit sees what the app sees.
qt_add_executable(test_shortcutmap test_shortcutmap.cpp)
target_link_libraries(test_shortcutmap PRIVATE kvit-core Qt6::Test Qt6::Gui)
add_test(NAME ShortcutMapTests COMMAND test_shortcutmap)
# A GUI application needs a platform plugin; offscreen loads a real platform
# theme without needing a display.
set_tests_properties(ShortcutMapTests
    PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

# Accessibility announcer tests (live-region seam). Needs a GUI
# context for the QAccessible announcement path, so Qt6::Gui gives QGuiApplication.
qt_add_executable(test_accessibility test_accessibility.cpp)
target_link_libraries(test_accessibility PRIVATE kvit-core Qt6::Test Qt6::Gui)
add_test(NAME AccessibilityTests COMMAND test_accessibility)

# System integration tests (tray + hotkey seams + quick capture).
# QSystemTrayIcon needs a QApplication (Widgets), so QTEST_MAIN links Widgets.
qt_add_executable(test_systemintegration test_systemintegration.cpp)
target_link_libraries(test_systemintegration PRIVATE kvit-core Qt6::Test Qt6::Widgets)
add_test(NAME SystemIntegrationTests COMMAND test_systemintegration)

# FileWatcher tests (external-change watch + own-write guard).
qt_add_executable(test_filewatcher test_filewatcher.cpp)
target_link_libraries(test_filewatcher PRIVATE kvit-core Qt6::Test)
add_test(NAME FileWatcherTests COMMAND test_filewatcher)

# Update checker tests: version ordering, payload parsing, opt-out +
# once-per-day gating through a fake fetcher.
qt_add_executable(test_updatechecker test_updatechecker.cpp)
target_link_libraries(test_updatechecker PRIVATE kvit-core Qt6::Test)
add_test(NAME UpdateCheckerTests COMMAND test_updatechecker)

# Network egress policy: every remote access the app makes is gated here, and
# the tests drive a loopback HTTP server rather than the real internet.
qt_add_executable(test_egresspolicy test_egresspolicy.cpp)
target_link_libraries(test_egresspolicy PRIVATE kvit-core Qt6::Test Qt6::Network Qt6::Qml)
add_test(NAME EgressPolicyTests COMMAND test_egresspolicy)

# Single-file mode tests: a file argument opens with no vault, typed blocks
# parse in-file, and the vault upgrade keeps the file.
qt_add_executable(test_singlefilemode test_singlefilemode.cpp)
target_link_libraries(test_singlefilemode PRIVATE kvit-core Qt6::Test Qt6::Widgets)
add_test(NAME SingleFileModeTests COMMAND test_singlefilemode)

# War-and-Peace performance gate (features.md §21.7): the 561,693-word
# load/serialize/bulk-delete budgets. Needs a GUI context for kvit-core, so Gui.
qt_add_executable(test_performance_warandpeace test_performance_warandpeace.cpp)
target_link_libraries(test_performance_warandpeace PRIVATE kvit-core Qt6::Test Qt6::Gui)
add_test(NAME WarAndPeacePerformanceTests COMMAND test_performance_warandpeace)

# Size-cap validation corpora (oversized-file guard):
# 10/25/50 MB prose- and one-line-shaped documents, measuring load, full
# serialize, and journal write. Prints numbers; asserts loose sanity bounds.
qt_add_executable(test_performance_bigfiles test_performance_bigfiles.cpp)
target_link_libraries(test_performance_bigfiles PRIVATE kvit-core Qt6::Test Qt6::Gui)
add_test(NAME BigFilePerformanceTests COMMAND test_performance_bigfiles)
set_tests_properties(BigFilePerformanceTests PROPERTIES TIMEOUT 600)

# Performance instrumentation: PerfLog unit tests and deterministic
# generated corpus self-checks.
qt_add_executable(test_perflog test_perflog.cpp)
target_link_libraries(test_perflog PRIVATE kvit-core Qt6::Test)
add_test(NAME PerfLogTests COMMAND test_perflog)

qt_add_executable(test_perf_corpus
    test_perf_corpus.cpp
    perf/corpus.cpp
)
target_include_directories(test_perf_corpus PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(test_perf_corpus PRIVATE kvit-core Qt6::Test)
add_test(NAME PerfCorpusTests COMMAND test_perf_corpus)

# Primary performance gates: headless operation timings reported
# through PerfLog. These establish the baselines the budgets are set against.
qt_add_executable(test_performance_ops
    test_performance_ops.cpp
    perf/corpus.cpp
)
target_include_directories(test_performance_ops PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(test_performance_ops PRIVATE kvit-core Qt6::Test Qt6::Gui)
add_test(NAME PerformanceOpsTests COMMAND test_performance_ops)

# Secondary GUI harness. Advisory under offscreen/WSLg variability; the
# test skips when no Qt Quick window can be exposed.
qt_add_executable(test_performance_app
    test_performance_app.cpp
    testsetup.h
    perf/corpus.cpp
    integration_tests.qrc
)
target_include_directories(test_performance_app PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(test_performance_app PRIVATE
    kvit-core
    Qt6::Quick
    Qt6::QuickControls2
    Qt6::QuickTest
    Qt6::Multimedia
    Qt6::Widgets
    Qt6::Test
)
target_compile_definitions(test_performance_app PRIVATE
    KVIT_TEST_FIXTURES="${CMAKE_CURRENT_SOURCE_DIR}/fixtures")
add_test(NAME PerformanceAppTests COMMAND test_performance_app)
set_tests_properties(PerformanceAppTests PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
    TIMEOUT 120)

# UndoStack tests
qt_add_executable(test_undostack test_undostack.cpp)
target_link_libraries(test_undostack PRIVATE kvit-core Qt6::Test)
add_test(NAME UndoStackTests COMMAND test_undostack)

# Commands tests
qt_add_executable(test_commands test_commands.cpp)
target_link_libraries(test_commands PRIVATE kvit-core Qt6::Test)
add_test(NAME CommandsTests COMMAND test_commands)

# BlockModel tests
qt_add_executable(test_blockmodel test_blockmodel.cpp)
target_link_libraries(test_blockmodel PRIVATE kvit-core Qt6::Test)
add_test(NAME BlockModelTests COMMAND test_blockmodel)

# MarkdownFormatter tests
qt_add_executable(test_markdownformatter test_markdownformatter.cpp)
target_link_libraries(test_markdownformatter PRIVATE kvit-core Qt6::Test)
add_test(NAME MarkdownFormatterTests COMMAND test_markdownformatter)

# CodeLanguages tests (code syntax-highlight rule tables)
qt_add_executable(test_codelanguages test_codelanguages.cpp)
target_link_libraries(test_codelanguages PRIVATE kvit-core Qt6::Test)
add_test(NAME CodeLanguagesTests COMMAND test_codelanguages)

# ImageAssets tests (image/media parse-build-classify-resolve)
qt_add_executable(test_imageassets test_imageassets.cpp)
target_link_libraries(test_imageassets PRIVATE kvit-core Qt6::Test)
add_test(NAME ImageAssetsTests COMMAND test_imageassets)

# TableData tests (pipe-table parse/serialize/mutate)
qt_add_executable(test_tabledata test_tabledata.cpp)
target_link_libraries(test_tabledata PRIVATE kvit-core Qt6::Test)
add_test(NAME TableDataTests COMMAND test_tabledata)

# TodoMeta tests (due date / priority tail)
qt_add_executable(test_todometa test_todometa.cpp)
target_link_libraries(test_todometa PRIVATE kvit-core Qt6::Test)
add_test(NAME TodoMetaTests COMMAND test_todometa)

# KanbanData tests (board parse/serialize/mutate)
qt_add_executable(test_kanbandata test_kanbandata.cpp)
target_link_libraries(test_kanbandata PRIVATE kvit-core Qt6::Test)
add_test(NAME KanbanDataTests COMMAND test_kanbandata)

# MathRenderer tests (MicroTeX seam render corpus).
# Needs a GUI context (font loading) — QTEST_MAIN gives it a QGuiApplication.
qt_add_executable(test_mathrenderer test_mathrenderer.cpp)
target_link_libraries(test_mathrenderer PRIVATE kvit-core Qt6::Test Qt6::Gui)
target_compile_definitions(test_mathrenderer PRIVATE
    KVIT_SOURCE_ROOT="${CMAKE_SOURCE_DIR}")
add_test(NAME MathRendererTests COMMAND test_mathrenderer)

# DiagramClassifier tests (character-diagram detection corpus). Reads the
# checked-in llm-diagram.md fixture, so it needs the source root.
qt_add_executable(test_diagramclassifier test_diagramclassifier.cpp)
target_link_libraries(test_diagramclassifier PRIVATE kvit-core Qt6::Test)
target_compile_definitions(test_diagramclassifier PRIVATE
    KVIT_SOURCE_ROOT="${CMAKE_SOURCE_DIR}")
add_test(NAME DiagramClassifierTests COMMAND test_diagramclassifier)

# DiagramRepair tests (ingest straightening of LLM diagram flaws). Reads the
# checked-in llm-diagram.md flawed corpus.
qt_add_executable(test_diagramrepair test_diagramrepair.cpp)
target_link_libraries(test_diagramrepair PRIVATE kvit-core Qt6::Test)
target_compile_definitions(test_diagramrepair PRIVATE
    KVIT_SOURCE_ROOT="${CMAKE_SOURCE_DIR}")
add_test(NAME DiagramRepairTests COMMAND test_diagramrepair)

# MermaidParser tests (lexer offsets + flowchart AST + diagnostics, §8.2/§9).
qt_add_executable(test_mermaidparser test_mermaidparser.cpp)
target_link_libraries(test_mermaidparser PRIVATE kvit-core Qt6::Test Qt6::Gui)
add_test(NAME MermaidParserTests COMMAND test_mermaidparser)

# MermaidSequence tests (sequence family parser + lifeline layout, §9
# grammar-first against the pinned sequenceDiagram.jison).
qt_add_executable(test_mermaidsequence test_mermaidsequence.cpp)
target_link_libraries(test_mermaidsequence PRIVATE kvit-core Qt6::Test Qt6::Gui)
add_test(NAME MermaidSequenceTests COMMAND test_mermaidsequence)

# MermaidClass tests (class family parser + compartment layout, §9
# grammar-first against the pinned classDiagram.jison).
qt_add_executable(test_mermaidclass test_mermaidclass.cpp)
target_link_libraries(test_mermaidclass PRIVATE kvit-core Qt6::Test Qt6::Gui)
add_test(NAME MermaidClassTests COMMAND test_mermaidclass)

# MermaidState tests (state family parser + compound layout, §9 grammar-first
# against the pinned stateDiagram.jison).
qt_add_executable(test_mermaidstate test_mermaidstate.cpp)
target_link_libraries(test_mermaidstate PRIVATE kvit-core Qt6::Test Qt6::Gui)
add_test(NAME MermaidStateTests COMMAND test_mermaidstate)

# MermaidEr tests (ER family parser + entity-table layout, §9 grammar-first
# against the pinned erDiagram.jison).
qt_add_executable(test_mermaider test_mermaider.cpp)
target_link_libraries(test_mermaider PRIVATE kvit-core Qt6::Test Qt6::Gui)
add_test(NAME MermaidErTests COMMAND test_mermaider)

# MermaidEdits tests (on-diagram editing: pos-line arrangement, arranged
# layout, byte-preserving write-back, gestures).
qt_add_executable(test_mermaidedits test_mermaidedits.cpp)
target_link_libraries(test_mermaidedits PRIVATE kvit-core Qt6::Test Qt6::Gui)
add_test(NAME MermaidEditsTests COMMAND test_mermaidedits)

# DiagramLayout tests (deterministic layered layout + scene + renderer cache,
# §8.4/§8.5). QFontMetricsF needs a GUI context — Qt6::Gui gives it.
qt_add_executable(test_diagramlayout test_diagramlayout.cpp)
target_link_libraries(test_diagramlayout PRIVATE kvit-core Qt6::Test Qt6::Gui)
add_test(NAME DiagramLayoutTests COMMAND test_diagramlayout)

# DocumentSerializer tests
qt_add_executable(test_documentserializer test_documentserializer.cpp)
target_link_libraries(test_documentserializer PRIVATE kvit-core Qt6::Test)
add_test(NAME DocumentSerializerTests COMMAND test_documentserializer)

# LlmNormalizer tests (LLM-markdown repairs): rewrite and idempotence per
# fixture, canonical-corpus fixed point, lookahead bound.
qt_add_executable(test_llmnormalizer test_llmnormalizer.cpp)
target_link_libraries(test_llmnormalizer PRIVATE kvit-core Qt6::Test)
add_test(NAME LlmNormalizerTests COMMAND test_llmnormalizer)

# DocumentManager tests
qt_add_executable(test_documentmanager test_documentmanager.cpp)
target_link_libraries(test_documentmanager PRIVATE kvit-core Qt6::Test)
add_test(NAME DocumentManagerTests COMMAND test_documentmanager)

# BlockMenuModel tests (slash-command catalog)
qt_add_executable(test_blockmenumodel test_blockmenumodel.cpp)
target_link_libraries(test_blockmenumodel PRIVATE kvit-core Qt6::Test)
add_test(NAME BlockMenuModelTests COMMAND test_blockmenumodel)

# MathCommandModel tests (backslash-menu catalog + matcher).
# The catalog-integrity slots render through MicroTeX, so a GUI context
# (font loading) is required — QTEST_MAIN gives it a QGuiApplication.
qt_add_executable(test_mathcommandmodel test_mathcommandmodel.cpp)
target_link_libraries(test_mathcommandmodel PRIVATE kvit-core Qt6::Test Qt6::Gui)
add_test(NAME MathCommandModelTests COMMAND test_mathcommandmodel)

# DocumentSelection tests (selection state object)
qt_add_executable(test_documentselection test_documentselection.cpp)
target_link_libraries(test_documentselection PRIVATE kvit-core Qt6::Test)
add_test(NAME DocumentSelectionTests COMMAND test_documentselection)

# DocumentSearch tests (search state object)
qt_add_executable(test_documentsearch test_documentsearch.cpp)
target_link_libraries(test_documentsearch PRIVATE kvit-core Qt6::Test)
add_test(NAME DocumentSearchTests COMMAND test_documentsearch)

# DocumentOutline tests (heading-tree projection + slugs)
qt_add_executable(test_documentoutline test_documentoutline.cpp)
target_link_libraries(test_documentoutline PRIVATE kvit-core Qt6::Test)
add_test(NAME DocumentOutlineTests COMMAND test_documentoutline)

# DocumentStats tests (word/char/reading counts)
qt_add_executable(test_documentstats test_documentstats.cpp)
target_link_libraries(test_documentstats PRIVATE kvit-core Qt6::Test)
add_test(NAME DocumentStatsTests COMMAND test_documentstats)

# NoteTemplates tests (template CRUD + variable expansion)
qt_add_executable(test_notetemplates test_notetemplates.cpp)
target_link_libraries(test_notetemplates PRIVATE kvit-core Qt6::Test)
add_test(NAME NoteTemplatesTests COMMAND test_notetemplates)

# DocumentExporter tests (HTML/PDF/text export). Needs a GUI
# context (font loading for math rendering + the PDF print seam), so QTEST_MAIN
# gets a QGuiApplication via Qt6::Gui.
qt_add_executable(test_documentexporter test_documentexporter.cpp)
target_link_libraries(test_documentexporter PRIVATE kvit-core Qt6::Test Qt6::Gui)
add_test(NAME DocumentExporterTests COMMAND test_documentexporter)

# DocumentImporter tests (import files/folders into the collection)
qt_add_executable(test_documentimporter test_documentimporter.cpp)
target_link_libraries(test_documentimporter PRIVATE kvit-core Qt6::Test)
add_test(NAME DocumentImporterTests COMMAND test_documentimporter)

# EmbedMetadata tests (OpenGraph parse + fetch seam + cache)
qt_add_executable(test_embedmetadata test_embedmetadata.cpp)
target_link_libraries(test_embedmetadata PRIVATE kvit-core Qt6::Test)
add_test(NAME EmbedMetadataTests COMMAND test_embedmetadata)

# ClipboardHelper tests (multi-format copy + the paste format matrix, §5.1/§5.3)
qt_add_executable(test_clipboardhelper test_clipboardhelper.cpp)
target_link_libraries(test_clipboardhelper PRIVATE kvit-core Qt6::Test)
add_test(NAME ClipboardHelperTests COMMAND test_clipboardhelper)

# HtmlToMarkdown tests (the HTML arm of the smart-paste matrix, features.md §5.3)
qt_add_executable(test_htmltomarkdown test_htmltomarkdown.cpp)
target_link_libraries(test_htmltomarkdown PRIVATE kvit-core Qt6::Test)
add_test(NAME HtmlToMarkdownTests COMMAND test_htmltomarkdown)

# NoteFrontMatter tests (note metadata block)
qt_add_executable(test_notefrontmatter test_notefrontmatter.cpp)
target_link_libraries(test_notefrontmatter PRIVATE kvit-core Qt6::Test)
add_test(NAME NoteFrontMatterTests COMMAND test_notefrontmatter)

# NoteCollection tests (collection object)
qt_add_executable(test_notecollection test_notecollection.cpp)
target_link_libraries(test_notecollection PRIVATE kvit-core Qt6::Test)
add_test(NAME NoteCollectionTests COMMAND test_notecollection)

# Cross-process vault safety. Launches itself as a second process, so this is
# the only suite that can observe two Kvit sessions on one vault.
qt_add_executable(test_vaultlock test_vaultlock.cpp)
target_link_libraries(test_vaultlock PRIVATE kvit-core Qt6::Test)
add_test(NAME VaultLockTests COMMAND test_vaultlock)

# Wiki-link navigation history
qt_add_executable(test_navigationhistory test_navigationhistory.cpp)
target_link_libraries(test_navigationhistory PRIVATE kvit-core Qt6::Test)
add_test(NAME NavigationHistoryTests COMMAND test_navigationhistory)

# Quick switcher filter
qt_add_executable(test_quickswitchermodel test_quickswitchermodel.cpp)
target_link_libraries(test_quickswitchermodel PRIVATE kvit-core Qt6::Test)
add_test(NAME QuickSwitcherModelTests COMMAND test_quickswitchermodel)

# Collection query block core
qt_add_executable(test_querydata test_querydata.cpp)
target_link_libraries(test_querydata PRIVATE kvit-core Qt6::Test)
add_test(NAME QueryDataTests COMMAND test_querydata)

# Mermaid → text export
qt_add_executable(test_textcanvas test_textcanvas.cpp)
target_link_libraries(test_textcanvas PRIVATE kvit-core Qt6::Test)
add_test(NAME TextCanvasTests COMMAND test_textcanvas)

qt_add_executable(test_textdiagram test_textdiagram.cpp)
target_link_libraries(test_textdiagram PRIVATE kvit-core Qt6::Test)
add_test(NAME TextDiagramTests COMMAND test_textdiagram)
# Offscreen only off-Windows: the Windows offscreen platform ships no
# fonts at all, which zeroes the text metrics the box layout depends on.
if(NOT WIN32)
    set_tests_properties(TextDiagramTests PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
endif()

# Deferred app startup controller (collection scan bootstrap).
qt_add_executable(test_startupcontroller test_startupcontroller.cpp)
target_link_libraries(test_startupcontroller PRIVATE kvit-core Qt6::Test)
add_test(NAME StartupControllerTests COMMAND test_startupcontroller)

# NoteListModel tests (note-list projection)
qt_add_executable(test_notelistmodel test_notelistmodel.cpp)
target_link_libraries(test_notelistmodel PRIVATE kvit-core Qt6::Test)
add_test(NAME NoteListModelTests COMMAND test_notelistmodel)

# FolderTreeModel tests (folder projection and cached recursive counts)
qt_add_executable(test_foldertreemodel test_foldertreemodel.cpp)
target_link_libraries(test_foldertreemodel PRIVATE kvit-core Qt6::Test)
add_test(NAME FolderTreeModelTests COMMAND test_foldertreemodel)

# CollectionSearch tests (global search)
qt_add_executable(test_collectionsearch test_collectionsearch.cpp)
target_link_libraries(test_collectionsearch PRIVATE kvit-core Qt6::Test)
add_test(NAME CollectionSearchTests COMMAND test_collectionsearch)

# SearchIndexDb tests (SQLite FTS5 engine + differential oracle)
qt_add_executable(test_searchindexdb test_searchindexdb.cpp)
target_link_libraries(test_searchindexdb PRIVATE kvit-core Qt6::Test)
add_test(NAME SearchIndexDbTests COMMAND test_searchindexdb)

# SettingsStore tests (per-user settings)
qt_add_executable(test_settingsstore test_settingsstore.cpp)
target_link_libraries(test_settingsstore PRIVATE kvit-core Qt6::Test)
add_test(NAME SettingsStoreTests COMMAND test_settingsstore)

# Theme tests (token tables, overrides, engine coupling)
qt_add_executable(test_theme test_theme.cpp)
target_link_libraries(test_theme PRIVATE kvit-core Qt6::Test)
add_test(NAME ThemeTests COMMAND test_theme)

# Typography tests (type scale, clamps, engine plumbing)
qt_add_executable(test_typography test_typography.cpp)
target_link_libraries(test_typography PRIVATE kvit-core Qt6::Test)
add_test(NAME TypographyTests COMMAND test_typography)

# BlockEditorEngine tests (hybrid editing engine)
qt_add_executable(test_blockeditorengine test_blockeditorengine.cpp)
target_link_libraries(test_blockeditorengine PRIVATE kvit-core Qt6::Test)
add_test(NAME BlockEditorEngineTests COMMAND test_blockeditorengine)

# Integration tests (Qt Quick Test)
qt_add_executable(test_integration
    test_integration.cpp
    testsetup.h
    integration_tests.qrc
)

target_link_libraries(test_integration PRIVATE
    kvit-core
    Qt6::Quick
    Qt6::QuickControls2
    Qt6::QuickTest
    Qt6::Multimedia
)

# The committed audio/video fixtures the media-block tests copy and play.
target_compile_definitions(test_integration PRIVATE
    KVIT_TEST_FIXTURES="${CMAKE_CURRENT_SOURCE_DIR}/fixtures")

# The normal developer test keeps one process and one persistent window for
# the whole suite. Do not replace this with per-function process isolation:
# doing so maps hundreds of compositor-positioned windows and repeatedly
# disrupts the user's desktop.
add_test(NAME IntegrationTests COMMAND test_integration -input ${CMAKE_CURRENT_SOURCE_DIR}/tst_integration.qml)
set_tests_properties(IntegrationTests PROPERTIES
    ENVIRONMENT "QT_QUICK_BACKEND=software")

# Per-function isolation is retained only as an explicit CI/diagnostic tool.
# It defaults offscreen in its runner and is disabled in normal CTest runs.
add_test(NAME IntegrationTestsIsolated COMMAND
    bash ${CMAKE_SOURCE_DIR}/tools/run-integration-gate.sh
    $<TARGET_FILE:test_integration> ${CMAKE_CURRENT_SOURCE_DIR}/tst_integration.qml)
set_tests_properties(IntegrationTestsIsolated PROPERTIES
    DISABLED TRUE
    ENVIRONMENT "QT_QUICK_BACKEND=software"
    TIMEOUT 1800)

# Visual storyboard tests (Qt Quick Test) — screenshot walkthroughs.
qt_add_executable(test_visual
    test_visual.cpp
    testsetup.h
    integration_tests.qrc
)

target_link_libraries(test_visual PRIVATE
    kvit-core
    Qt6::Quick
    Qt6::QuickControls2
    Qt6::QuickTest
    Qt6::Multimedia
)

target_compile_definitions(test_visual PRIVATE
    KVIT_TEST_FIXTURES="${CMAKE_CURRENT_SOURCE_DIR}/fixtures")

add_test(NAME VisualTests COMMAND test_visual -input ${CMAKE_CURRENT_SOURCE_DIR}/tst_visual.qml)

# A QML load error leaves the Qt Quick Test harness waiting forever on
# its `when:` condition (observed 2026-07-07: a type missing from the
# test qrc hung the gate for hours). Timeouts turn any future hang into
# a failure the gate reports instead of an infinite wait.
set_tests_properties(IntegrationTests PROPERTIES TIMEOUT 600)
set_tests_properties(VisualTests PROPERTIES TIMEOUT 300)

# The two extension seams of the open-core split: the fence-language
# block-kind registry and the module registry main() installs into. Both
# need a GUI context because kvit-core links Quick.
qt_add_executable(test_blockkindregistry test_blockkindregistry.cpp)
target_link_libraries(test_blockkindregistry PRIVATE kvit-core Qt6::Test Qt6::Gui)
add_test(NAME BlockKindRegistryTests COMMAND test_blockkindregistry)

qt_add_executable(test_extensionregistry test_extensionregistry.cpp)
target_link_libraries(test_extensionregistry PRIVATE kvit-core Qt6::Test Qt6::Gui)
add_test(NAME ExtensionRegistryTests COMMAND test_extensionregistry)

# The composed shell (the app/library split): AppContext plus the shipped
# qml/main.qml resource — the top-level file itself, not the integration
# suite's mirror of it.
qt_add_executable(test_shell
    test_shell.cpp
    ${CMAKE_SOURCE_DIR}/resources.qrc
)
target_link_libraries(test_shell PRIVATE
    kvit-core Qt6::Test Qt6::Quick Qt6::QuickControls2 Qt6::Multimedia)
add_test(NAME ShellTests COMMAND test_shell)
set_tests_properties(ShellTests PROPERTIES TIMEOUT 300)

# The premium module's tests, built only when the module is (KVIT_AGENT=ON).
# They live under tests/agent/ so the public export drops them with the rest
# of the module.
if(KVIT_AGENT)
    qt_add_executable(test_agentmodule agent/test_agentmodule.cpp)
    target_link_libraries(test_agentmodule PRIVATE kvit-agent Qt6::Test Qt6::Gui)
    add_test(NAME AgentModuleTests COMMAND test_agentmodule)

    # Loads the shipped shell — the top-level resources.qrc itself, not a
    # mirror of it — with the module installed, and checks the bar lands in
    # its slot. The one case that proves the seam end to end.
    qt_add_executable(test_agentshell
        agent/test_agentshell.cpp
        ${CMAKE_SOURCE_DIR}/resources.qrc
    )
    target_link_libraries(test_agentshell PRIVATE
        kvit-agent Qt6::Test Qt6::Quick Qt6::QuickControls2 Qt6::Multimedia)
    add_test(NAME AgentShellTests COMMAND test_agentshell)
    set_tests_properties(AgentShellTests PROPERTIES TIMEOUT 300)
endif()

# Wall-clock budgets sit inside unit suites whose other cases are pure
# correctness, so they cannot simply be relabelled without dropping that
# coverage from the merge gate. Instead the budget assertions skip on shared
# CI runners (tests/timingbudget.h explains why), and these entries re-run
# the same functions with enforcement forced on under the performance label,
# which CI runs as a non-blocking trend job. A real regression still shows.
add_test(NAME QueryDataTimingBudget
         COMMAND test_querydata testEvaluate1000NoteBudget)
add_test(NAME SearchIndexTimingBudget
         COMMAND test_searchindexdb testQueryPerformanceGate)
set_tests_properties(QueryDataTimingBudget SearchIndexTimingBudget
    PROPERTIES ENVIRONMENT "KVIT_ENFORCE_TIMING_BUDGETS=1")

# tools/win-sync.sh runs rsync --delete against a directory the caller names,
# so its refusals are the safety property worth gating. A shell test drives
# the script itself; it builds a throwaway source tree and mirrors under a
# temporary directory, and never touches a real mount.
if(UNIX AND NOT APPLE)
    add_test(NAME WinSyncGuards
             COMMAND bash ${CMAKE_CURRENT_SOURCE_DIR}/test_win_sync.sh)
endif()

# ── CTest labels. The suite is heterogeneous, so every entry gets exactly
#    one label and CI filters with `ctest -L <label>`:
#      unit        deterministic, headless-safe — the merge gate on all OSes
#      performance timing-sensitive — Linux CI non-blocking trend job
#      shell       loads the production shell; deterministic offscreen, and a
#                  blocking merge gate on all OSes alongside `unit`
#      visual      need a real, focused display — local gate; xvfb runs are
#                  informational only
#
# `shell` is separate from `unit` only because it constructs a QQuickWindow
# and so needs a QPA platform, not because it is any less deterministic: the
# suites below take about 0.6s and pass repeatedly under QT_QPA_PLATFORM=
# offscreen. They were previously labelled `gui-smoke` and run in CI under
# continue-on-error, which meant the one test that loads the shipped
# resources.qrc could not fail a merge — a QML syntax error, a resource
# missing from the qrc, an import that does not resolve, or a context
# property renamed out from under main.qml all merged green. They gate now.
set(KVIT_PERFORMANCE_TESTS
    WarAndPeacePerformanceTests
    BigFilePerformanceTests
    PerformanceOpsTests
    PerformanceAppTests
    QueryDataTimingBudget
    SearchIndexTimingBudget)
set(KVIT_SHELL_TESTS
    ShellTests
    AgentShellTests)
set(KVIT_VISUAL_TESTS
    IntegrationTests
    IntegrationTestsIsolated
    VisualTests)
get_property(_kvit_tests DIRECTORY PROPERTY TESTS)
foreach(_t IN LISTS _kvit_tests)
    if(_t IN_LIST KVIT_PERFORMANCE_TESTS)
        set_property(TEST ${_t} APPEND PROPERTY LABELS performance)
    elseif(_t IN_LIST KVIT_SHELL_TESTS)
        set_property(TEST ${_t} APPEND PROPERTY LABELS shell)
    elseif(_t IN_LIST KVIT_VISUAL_TESTS)
        set_property(TEST ${_t} APPEND PROPERTY LABELS visual)
    else()
        set_property(TEST ${_t} APPEND PROPERTY LABELS unit)
    endif()
endforeach()
