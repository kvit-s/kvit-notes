cmake_minimum_required(VERSION 3.21)
# SemVer: the product launches as 1.0.0 (0.x would
# undersell a feature-complete editor). PROJECT_VERSION carries the three
# numeric components only, because CMake rejects a prerelease suffix here.
project(kvit-notes VERSION 1.0.0 LANGUAGES CXX)

# The full SemVer string the binary reports, prerelease suffix included.
#
# A tag build passes the version derived from the tag, so v1.0.0-rc1 produces
# a binary that calls itself 1.0.0-rc1. Without this an RC reported plain
# 1.0.0 and UpdateChecker::compareVersions — which does implement SemVer
# prerelease ordering — saw the installed RC as equal to the stable release
# and never offered the upgrade off it.
#
# The value must start with PROJECT_VERSION; anything after that must be a
# SemVer prerelease or build-metadata suffix. packaging/linux/build-appimage.sh
# derives it from the tag and CI passes the same string to every packaging
# job, so the reported version, the artifact filename and the tag cannot
# disagree. Left unset it is exactly PROJECT_VERSION, which is what a
# developer build and a final release both want.
set(KVIT_VERSION_FULL "${PROJECT_VERSION}" CACHE STRING
    "Full SemVer version the binary reports (e.g. 1.0.0-rc1)")

if(NOT KVIT_VERSION_FULL MATCHES "^${PROJECT_VERSION}([-+].+)?$")
    message(FATAL_ERROR
        "KVIT_VERSION_FULL ('${KVIT_VERSION_FULL}') must be PROJECT_VERSION "
        "('${PROJECT_VERSION}') optionally followed by a '-prerelease' or "
        "'+build' suffix. Bump project(VERSION) to release a different "
        "base version.")
endif()

# C++20 is the build's real requirement, not a choice made here: vendored
# MicroTeX propagates cxx_std_20 PUBLIC to every target that links it, so the
# whole tree has always compiled as C++20. Stating it at the root makes the
# toolchain baseline explicit (MSVC 2022 / AppleClang 15 / GCC 12+).
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

# QtSvg is deliberately absent from the build: the SVG math path was removed.
# The deployed Qt runtime must still ship the QtSvg module — user-inserted
# .svg image blocks (imageassets.cpp extension list)
# load through the `qsvg` imageformats plugin, which links libQt6Svg
# internally even though the app does not.
find_package(Qt6 REQUIRED COMPONENTS Core Gui Quick QuickControls2 Test QuickTest Widgets Multimedia Network Concurrent Sql)

# The premium agent module. OFF is the open editor: nothing under
# src/agent/ or qml/agent/ is compiled, no KVIT_AGENT define is set, and the
# two guarded fragments in src/main.cpp vanish with the preprocessor. Exporting
# the public repo is then the deletion of those paths, not an exclusion list.
option(KVIT_AGENT "Build the premium agent module (src/agent, qml/agent)" OFF)

# The premium module is a separate product, not an optional part of this one.
# It lives in the private kvit-notes-pro repository, which links this
# repository in as a submodule and configures the build from there; this
# checkout deliberately contains none of its sources. Turning the option on
# here otherwise fails deep inside qt_add_executable with "Cannot find source
# file" for each missing path, which reads like a broken checkout rather than
# a build that was asked for something this repository does not have.
if(KVIT_AGENT)
    set(_kvit_agent_expected
        src/agent/agentmodule.cpp
        agent_resources.qrc
        tests/agent/test_agentmodule.cpp
        tests/agent/test_agentshell.cpp)
    set(_kvit_agent_missing "")
    foreach(_file IN LISTS _kvit_agent_expected)
        if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/${_file}")
            list(APPEND _kvit_agent_missing "${_file}")
        endif()
    endforeach()
    if(_kvit_agent_missing)
        string(REPLACE ";" "\n    " _kvit_agent_missing_lines "${_kvit_agent_missing}")
        message(FATAL_ERROR
            "KVIT_AGENT=ON, but the premium agent module is not present in this "
            "source tree.\n"
            "Missing:\n    ${_kvit_agent_missing_lines}\n\n"
            "This repository is the open core of Kvit Notes and ships without the "
            "premium module by design. The module lives in the private "
            "kvit-notes-pro repository, which includes this one as a submodule and "
            "configures the build from there.\n\n"
            "To build the open editor, configure with -DKVIT_AGENT=OFF (the "
            "default). To build the premium editor, configure kvit-notes-pro "
            "rather than this directory.")
    endif()
    unset(_kvit_agent_expected)
    unset(_kvit_agent_missing)
endif()

# AddressSanitizer + UndefinedBehaviorSanitizer. A separate build rather than
# a default, because the instrumentation costs roughly 2x runtime and changes
# the allocator, so it is a job of its own rather than something every build
# carries. Deterministic suites only: the sanitizers report on what actually
# executes, and the Qt Quick suites do not execute the same way twice here.
#
#   cmake -S . -B build-asan -DKVIT_SANITIZE=ON && ctest --test-dir build-asan -L unit
#
# GCC and Clang share these flags; MSVC has ASan but not UBSan and needs a
# different spelling, so the option is a no-op there rather than a broken
# build.
option(KVIT_SANITIZE "Build with AddressSanitizer and UndefinedBehaviorSanitizer" OFF)
if(KVIT_SANITIZE)
    if(MSVC)
        message(WARNING "KVIT_SANITIZE is not wired for MSVC; building without sanitizers")
    else()
        # -fno-omit-frame-pointer keeps the reports' stacks readable;
        # -fno-sanitize-recover makes UBSan fail the run rather than log and
        # continue, so a violation cannot pass as a green test.
        add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer
                            -fno-sanitize-recover=undefined)
        add_link_options(-fsanitize=address,undefined)
    endif()
endif()

# Vendored MicroTeX: the math typesetting engine
# behind the MathRenderer seam. Built as a static `microtex` target.
add_subdirectory(third_party/microtex)

# ── The module boundary (code-plan.md §6.1)
#
# Seven static libraries, one directory under src/ each, linked in one
# direction only:
#
#   kvit-content      pure format transforms: markdown value parsers, the
#                     inline-markdown transition tables, diagrams, math,
#                     HTML→markdown. Depends on no other module.
#         ↑
#   kvit-domain       the block document itself: model, serializer, undo
#                     stack and commands, selection, in-document search,
#                     outline, statistics.
#         ↑
#   kvit-search  kvit-platform
#                     the rebuildable note index (SQLite FTS5), and the
#                     machine the app runs on: settings, file watching,
#                     network policy, tray, hotkeys, appearance tokens.
#         ↑
#   kvit-repository   the vault: containment, note filesystem access,
#                     atomic persistence, trash, backups, recovery,
#                     templates, import.
#         ↑
#   kvit-application  use-case orchestration: the document session, startup,
#                     the list/tree/switcher models, queries, export.
#         ↑
#   kvit-qml          the presentation surface — the editor engine that
#                     drives a QML TextArea, the `Kvit` module's type and
#                     singleton registrations — and the composition root
#                     (AppContext, KvitApplication) that wires the whole
#                     object graph and hands it to a QQmlEngine. A
#                     composition root depends on everything by definition,
#                     which is why it sits at the top rather than in
#                     kvit-application.
#
# The direction is enforced, not documented: each target publishes only its
# own directory as an include path, and inherits the directories of the
# modules it links. A domain source that writes #include "notecollection.h"
# does not find the header and does not compile. tools/check-layering.py
# checks the same rule over the sources, plus the two ownership rules the
# include graph alone cannot express — Qt Network only inside kvit-platform,
# filesystem mutation only where a module is meant to perform it.
#
# kvit-core remains as an interface target that pulls in all seven, so the
# test suite and an external consumer that links "the editor" are unchanged.

# ── kvit-content — format transforms, and nothing else.
#
# Every file here is a pure function of its input: markdown in, markdown or
# a parsed value out. Nothing reads a setting, opens a file or knows a block
# model exists, which is what lets the layers above all share it.
add_library(kvit-content STATIC
    src/content/blockattributes.cpp
    src/content/codelanguages.cpp
    src/content/htmltomarkdown.cpp
    src/content/imageassets.cpp
    src/content/inlinemarkdown.cpp
    src/content/kanbandata.cpp
    src/content/llmnormalizer.cpp
    src/content/markdownformatter.cpp
    src/content/mathcommandmodel.cpp
    src/content/mathrenderer.cpp
    src/content/notefrontmatter.cpp
    src/content/tabledata.cpp
    src/content/todometa.cpp
    src/content/wikilinkscanner.cpp
    src/content/fuzzymatch.h
    src/content/diagrams/classlayout.cpp
    src/content/diagrams/diagramcanvas.cpp
    src/content/diagrams/diagramclassifier.cpp
    src/content/diagrams/diagramlayout.cpp
    src/content/diagrams/diagrampainter.cpp
    src/content/diagrams/diagramrepair.cpp
    src/content/diagrams/erlayout.cpp
    src/content/diagrams/mermaidclass.cpp
    src/content/diagrams/mermaidedits.cpp
    src/content/diagrams/mermaider.cpp
    src/content/diagrams/mermaidlexer.cpp
    src/content/diagrams/mermaidparser.cpp
    src/content/diagrams/mermaidrenderer.cpp
    src/content/diagrams/mermaidsequence.cpp
    src/content/diagrams/mermaidstate.cpp
    src/content/diagrams/sequencelayout.cpp
    src/content/diagrams/statelayout.cpp
    src/content/diagrams/textcanvas.cpp
    src/content/diagrams/textdiagram.cpp
)
# Quick because DiagramCanvas is a QQuickPaintedItem; microtex because the
# math renderer's headers use it.
target_link_libraries(kvit-content PUBLIC
    Qt6::Core
    Qt6::Gui
    Qt6::Quick
    microtex
)
target_link_libraries(kvit-content PRIVATE Qt6::Concurrent)
target_include_directories(kvit-content PUBLIC src/content)

# ── kvit-domain — the block document.
add_library(kvit-domain STATIC
    src/domain/block.cpp
    src/domain/blockkindregistry.cpp
    src/domain/blockmodel.cpp
    src/domain/changeindentcommand.cpp
    src/domain/changetypecommand.cpp
    src/domain/compoundcommand.cpp
    src/domain/convertblockcommand.cpp
    src/domain/documentoutline.cpp
    src/domain/documentsearch.cpp
    src/domain/documentselection.cpp
    src/domain/documentserializer.cpp
    src/domain/documentstats.cpp
    src/domain/insertblockcommand.cpp
    src/domain/mergeblockscommand.cpp
    src/domain/moveblockcommand.cpp
    src/domain/moveblockscommand.cpp
    src/domain/perflog.cpp
    src/domain/persistencepool.cpp
    src/domain/removeblockcommand.cpp
    src/domain/setblockattributescommand.cpp
    src/domain/setcheckedcommand.cpp
    src/domain/splitblockcommand.cpp
    src/domain/textchangecommand.cpp
    src/domain/undocommand.cpp
    src/domain/undostack.cpp
    src/domain/cancellationtoken.h
    src/domain/noteentry.h
)
target_link_libraries(kvit-domain PUBLIC kvit-content)
target_link_libraries(kvit-domain PRIVATE Qt6::Concurrent)
target_include_directories(kvit-domain PUBLIC src/domain)

# ── kvit-search — the rebuildable note index.
#
# Sql is PRIVATE and named nowhere else: the FTS5 database is this module's
# implementation detail, and a caller asks it questions rather than issuing
# queries of its own.
add_library(kvit-search STATIC
    src/search/collectionsearchindex.cpp
    src/search/searchindexdb.cpp
    src/search/searchindexfeed.cpp
)
target_link_libraries(kvit-search PUBLIC kvit-domain)
target_link_libraries(kvit-search PRIVATE Qt6::Sql Qt6::Concurrent)
target_include_directories(kvit-search PUBLIC src/search)

# ── kvit-platform — the machine the app runs on.
#
# Qt6::Network is linked here and by no other module. That is the network
# ownership rule in build form: every remote access the app makes goes
# through EgressFetcher, which applies EgressPolicy, and a module above this
# one cannot open a connection because it has nothing to open one with.
# PUBLIC only because egressfetcher.h holds a QNetworkAccessManager member
# and names QHostAddress in its resolver seam.
add_library(kvit-platform STATIC
    src/platform/accessibilityannouncer.cpp
    src/platform/clipboardhelper.cpp
    src/platform/egressfetcher.cpp
    src/platform/egresspolicy.cpp
    src/platform/filewatcher.cpp
    src/platform/globalhotkey.cpp
    src/platform/remoteimageprovider.cpp
    src/platform/remotemediacache.cpp
    src/platform/settingsstore.cpp
    src/platform/shortcutcatalog.cpp
    src/platform/systemtray.cpp
    src/platform/theme.cpp
    src/platform/typography.cpp
    src/platform/updatechecker.cpp
    src/platform/embedfetcher.h
)
target_link_libraries(kvit-platform PUBLIC kvit-domain Qt6::Network)
target_link_libraries(kvit-platform PRIVATE Qt6::Widgets)
target_include_directories(kvit-platform PUBLIC src/platform)

# ── kvit-repository — the vault.
#
# The only module that reads and writes notes. The one exception is the note
# currently open, whose in-memory body the document session owns; the
# repository reaches that session through the OpenDocumentSession interface
# and never through the class that implements it.
add_library(kvit-repository STATIC
    src/repository/assetstore.cpp
    src/repository/collectionstatestore.cpp
    src/repository/documentimporter.cpp
    src/repository/notebackupstore.cpp
    src/repository/notecollection.cpp
    src/repository/notefileio.cpp
    src/repository/noteindexfile.cpp
    src/repository/notetemplates.cpp
    src/repository/notetrashstore.cpp
    src/repository/recoveryjournalstore.cpp
    src/repository/vaultlock.cpp
    src/repository/vaultpaths.cpp
    src/repository/wikilinkindex.cpp
    src/repository/opendocumentsession.h
)
target_link_libraries(kvit-repository PUBLIC kvit-domain kvit-search)
target_link_libraries(kvit-repository PRIVATE Qt6::Concurrent)
target_include_directories(kvit-repository PUBLIC src/repository)

# ── kvit-application — use-case orchestration.
add_library(kvit-application STATIC
    src/application/blockmenumodel.cpp
    src/application/collectionsearch.cpp
    src/application/documentexporter.cpp
    src/application/documentmanager.cpp
    src/application/embedmetadata.cpp
    src/application/extensionregistry.cpp
    src/application/foldertreemodel.cpp
    src/application/navigationhistory.cpp
    src/application/notelistmodel.cpp
    src/application/querydata.cpp
    src/application/querytools.cpp
    src/application/quickswitchermodel.cpp
    src/application/startupcontroller.cpp
    src/application/appactions.h
)
target_link_libraries(kvit-application PUBLIC kvit-repository kvit-platform)
target_link_libraries(kvit-application PRIVATE Qt6::Concurrent Qt6::Widgets)
target_include_directories(kvit-application PUBLIC src/application)

# ── kvit-qml — the presentation surface and the composition root.
add_library(kvit-qml STATIC
    src/qml/appcontext.cpp
    src/qml/blockeditorengine.cpp
    src/qml/kvitapplication.cpp
    src/qml/qmlservices.cpp
    src/qml/qmlsingletons.h
)
target_link_libraries(kvit-qml PUBLIC kvit-application)
# QuickControls2 for the shell's style, Multimedia for the media block's QML
# imports, Widgets because KvitApplication is a QApplication.
target_link_libraries(kvit-qml PRIVATE
    Qt6::QuickControls2
    Qt6::Widgets
    Qt6::Multimedia
)
target_include_directories(kvit-qml PUBLIC src/qml)

# qmltyperegistrar writes `#include <diagramcanvas.h>` — the basename, with no
# directory — for every type carrying a QML registration macro, and guards each
# one with __has_include. The rest of the tree reaches that header as
# "diagrams/diagramcanvas.h", so without the shorter spelling on the path the
# guard silently skips the include and the generated file then fails to
# compile on the undeclared type.
target_include_directories(kvit-qml PRIVATE src/content/diagrams)

# The version the running app reports (About, update check comparison).
target_compile_definitions(kvit-qml PRIVATE
    KVIT_VERSION="${KVIT_VERSION_FULL}")

# The `Kvit` QML module. Its types are declared by the registration macros in
# src/qml/qmlsingletons.h — as QML_FOREIGN wrappers, so a class in any of the
# seven modules can reach QML while only this one links the registration
# machinery — and qmltyperegistrar writes the .qmltypes description of them at
# build time.
#
# The point of generating that file is qmllint. It previously read a
# hand-written stub (tools/qmllint/Kvit) that named the types but described
# none of their properties, kept in step with the code by a checker script.
# The generated file carries the real surface — every property, method and
# signal — so qmllint can check uses, not just imports, and cannot fall out of
# date with the C++.
#
# NO_PLUGIN: kvit-qml is linked directly into every binary that uses it, so
# there is nothing for a plugin to load. See AppContext::registerQmlTypes()
# for the consequence that has, which is not obvious.
#
# The QML files themselves still ship in resources.qrc rather than through
# QML_FILES here. Moving them is a larger change and is not needed for the
# type checking this enables.
# DEPENDENCIES is stated rather than derived. Qt writes the qmldir's
# `depends` line from the QML modules the target links DIRECTLY, and kvit-qml
# reaches Qt6::Quick transitively through kvit-application now. Without the
# line qmllint cannot resolve a value type like QColor in a Kvit type's
# property and reports several hundred unresolved-type findings on files that
# are in fact correct.
qt_add_qml_module(kvit-qml
    URI Kvit
    VERSION 1.0
    NO_PLUGIN
    DEPENDENCIES QtQuick
    OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/qmlmodules/Kvit
)

# The `Kvit` module registers types that are compiled into the modules below
# this one — Block's enum, the fence-kind numbering, the code-language
# singleton, the diagram canvas, the settings store. qmltyperegistrar resolves
# a QML_FOREIGN wrapper against the metatype description of the class it names,
# and it reads those descriptions from the metatypes.json files that reach
# kvit-qml through its link interface. Extracting them is not automatic for a
# plain add_library, so each module asks for it here. Without this the
# registrar cannot tell that BlockKinds is a namespace rather than a type and
# emits a QMetaType::fromType<BlockKinds>() that does not compile.
foreach(_module content domain search platform repository application)
    qt_extract_metatypes(kvit-${_module})
endforeach()

# ── kvit-core — "the editor", as one name.
#
# The stock executable below is a thin main() linking it; a superset build
# that adds a premium module links the same thing and supplies its own main();
# the test suite links it rather than naming the module a given test happens
# to exercise. It is an interface target, so it contributes no objects of its
# own — it is the seven modules under one label.
add_library(kvit-core INTERFACE)
target_link_libraries(kvit-core INTERFACE kvit-qml)

# The premium module, built as its own library so the open build never
# compiles a line of it. It links the core and is linked BY the executable —
# the core knows nothing about it, and it reaches the app only through
# ExtensionRegistry.
if(KVIT_AGENT)
    add_library(kvit-agent STATIC
        src/agent/agentmodule.cpp
        agent_resources.qrc
    )
    target_link_libraries(kvit-agent PUBLIC kvit-core)
    target_compile_definitions(kvit-agent PUBLIC KVIT_AGENT)
endif()

# Main executable: main() plus the shell's QML. The resource file stays with
# the executable rather than the library so a superset binary picks which
# shell it ships; it adds the open repo's resources.qrc to its own target.
qt_add_executable(kvit-notes
    src/main.cpp
    resources.qrc
)

target_link_libraries(kvit-notes PRIVATE
    kvit-core
    # main.cpp constructs the QApplication itself, so the executable needs
    # Widgets directly rather than inheriting it from kvit-core.
    Qt6::Widgets
)

if(KVIT_AGENT)
    target_link_libraries(kvit-notes PRIVATE kvit-agent)
endif()

# ── Platform packaging: icon, bundle metadata, install layout.
#    QML ships inside resources.qrc, so the only runtime payload beyond
#    the executable is the MicroTeX resource tree; MathRenderer::resourceRoot()
#    resolves it relative to the executable in each installed layout below,
#    and `kvit-notes --math-selftest` verifies the result headlessly.
if(WIN32)
    target_sources(kvit-notes PRIVATE packaging/windows/kvit.rc)
    set_target_properties(kvit-notes PROPERTIES WIN32_EXECUTABLE TRUE)
elseif(APPLE)
    target_sources(kvit-notes PRIVATE packaging/icons/kvit.icns)
    set_source_files_properties(packaging/icons/kvit.icns PROPERTIES
        MACOSX_PACKAGE_LOCATION Resources)
    set_target_properties(kvit-notes PROPERTIES
        MACOSX_BUNDLE TRUE
        MACOSX_BUNDLE_BUNDLE_NAME "Kvit Notes"
        MACOSX_BUNDLE_GUI_IDENTIFIER "org.kvit.Notes"
        MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
        MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}"
        MACOSX_BUNDLE_ICON_FILE "kvit.icns")
endif()

include(GNUInstallDirs)

# ── The license payload every binary artifact must carry.
#
# These four files are a distribution obligation, not documentation: MPL-2.0
# §3.1 for the project's own code, MIT for MicroTeX, and Zlib for tinyxml2 all
# require the license text to travel with the binary. They used to be absent
# from every install layout — only the AppImage script copied the notices
# summary, and that summary refers to source-tree paths which no binary
# artifact contains — so an install or a package satisfied none of them.
#
# The font licenses are not listed here because the whole of
# third_party/microtex/res/ is installed as math-res below, which already
# carries res/greek/LICENSE, res/cyrillic/LICENSE, res/fonts/licences/ and
# res/fonts/kvit-newtx/{LICENSES,NOTICE.md} in place beside the fonts they
# cover. Qt's LGPL texts come from the Qt kit at deploy time and are added by
# the platform packaging script.
#
# tools/check-license-payload.sh verifies the result against an installed
# tree or an AppDir, and packaging fails when a file is missing.
set(KVIT_LICENSE_FILES
    "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE"
    "${CMAKE_CURRENT_SOURCE_DIR}/THIRD-PARTY-NOTICES.md"
    "${CMAKE_CURRENT_SOURCE_DIR}/third_party/microtex/LICENSE.clatexmath"
    "${CMAKE_CURRENT_SOURCE_DIR}/third_party/microtex/tinyxml2/LICENSE.txt")

# A missing source file here is a configure-time error rather than a package
# that quietly ships without its notices.
foreach(_lic IN LISTS KVIT_LICENSE_FILES)
    if(NOT EXISTS "${_lic}")
        message(FATAL_ERROR
            "Required license file is missing: ${_lic}. Every binary artifact "
            "must ship it; see KVIT_LICENSE_FILES in CMakeLists.txt.")
    endif()
endforeach()

# tinyxml2's text is LICENSE.txt in the vendored tree; renamed on install so
# four files from four projects do not collide in one directory.
set(KVIT_LICENSE_RENAMES
    LICENSE
    THIRD-PARTY-NOTICES.md
    LICENSE.microtex
    LICENSE.tinyxml2)

function(kvit_install_licenses destination)
    list(LENGTH KVIT_LICENSE_FILES _count)
    math(EXPR _last "${_count} - 1")
    foreach(_i RANGE ${_last})
        list(GET KVIT_LICENSE_FILES ${_i} _lic)
        list(GET KVIT_LICENSE_RENAMES ${_i} _name)
        install(FILES "${_lic}" DESTINATION "${destination}" RENAME "${_name}")
    endforeach()
endfunction()

if(WIN32)
    # Portable layout: the executable at the prefix root, math-res beside it.
    # windeployqt fills in the Qt runtime next to them.
    install(TARGETS kvit-notes RUNTIME DESTINATION .)
    install(DIRECTORY third_party/microtex/res/ DESTINATION math-res)
    kvit_install_licenses(licenses)
elseif(APPLE)
    # Self-contained bundle; macdeployqt operates on the installed .app.
    install(TARGETS kvit-notes BUNDLE DESTINATION .)
    install(DIRECTORY third_party/microtex/res/
        DESTINATION "kvit-notes.app/Contents/Resources/math-res")
    kvit_install_licenses("kvit-notes.app/Contents/Resources/licenses")
else()
    # Linux FHS layout, consumed directly and by linuxdeploy for the AppImage.
    install(TARGETS kvit-notes RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
    install(DIRECTORY third_party/microtex/res/
        DESTINATION ${CMAKE_INSTALL_DATADIR}/kvit-notes/math-res)
    install(FILES packaging/linux/kvit-notes.desktop
        DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
    foreach(size 16 24 32 48 64 128 256 512)
        install(FILES packaging/icons/hicolor/${size}x${size}/apps/kvit-notes.png
            DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/${size}x${size}/apps)
    endforeach()
    install(FILES packaging/icons/kvit.svg
        DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/scalable/apps
        RENAME kvit-notes.svg)
    kvit_install_licenses(${CMAKE_INSTALL_DATADIR}/licenses/kvit-notes)
endif()

# Scriptable launcher for the real shell (tools/uidriver.cpp): runs the same
# composition as kvit-notes and saves frames of what it renders, for observing
# the shipped UI. Off by default; it is a development tool, not a test.
option(KVIT_UI_DRIVER "Build the scriptable UI driver (tools/uidriver.cpp)" OFF)
if(KVIT_UI_DRIVER)
    find_package(Qt6 REQUIRED COMPONENTS Test)
    qt_add_executable(kvit-uidriver
        tools/uidriver.cpp
        resources.qrc
    )
    # Widgets for the same reason kvit-notes needs it: this tool constructs
    # the QApplication itself. kvit-core stopped exporting Widgets when its
    # public link surface was narrowed, and nothing in a default build
    # notices, because this target is off unless it is asked for.
    target_link_libraries(kvit-uidriver PRIVATE kvit-core Qt6::Test Qt6::Widgets)
endif()

# Enable testing. Only when this project is the top-level build: a consumer that adds
# this repo as a subdirectory to link kvit-core (the kvit-hub portfolio dashboard) brings
# its own suite, and the test targets here resolve their paths against CMAKE_SOURCE_DIR,
# which under a subdirectory build points at the consumer's root rather than this one.
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    enable_testing()
    add_subdirectory(tests)
endif()
