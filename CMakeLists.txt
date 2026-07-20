cmake_minimum_required(VERSION 3.16)
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

# The core library: the whole editor except its entry point.
# The stock executable below is a thin main() linking it, and a superset build
# that adds a premium module links the same library and supplies its own main().
# `kvit-core` is the CMake target name; `kvit_core` is the same library under
# the naming the split document uses.
add_library(kvit-core STATIC
    src/appcontext.cpp
    src/kvitapplication.cpp
    src/blockkindregistry.cpp
    src/extensionregistry.cpp
    src/block.cpp
    src/perflog.cpp
    src/persistencepool.cpp
    src/blockattributes.cpp
    src/shortcutcatalog.cpp
    src/accessibilityannouncer.cpp
    src/globalhotkey.cpp
    src/systemtray.cpp
    src/filewatcher.cpp
    src/blockmodel.cpp
    src/markdownformatter.cpp
    src/undocommand.cpp
    src/undostack.cpp
    src/compoundcommand.cpp
    src/textchangecommand.cpp
    src/insertblockcommand.cpp
    src/removeblockcommand.cpp
    src/moveblockcommand.cpp
    src/moveblockscommand.cpp
    src/changetypecommand.cpp
    src/splitblockcommand.cpp
    src/mergeblockscommand.cpp
    src/setcheckedcommand.cpp
    src/setblockattributescommand.cpp
    src/changeindentcommand.cpp
    src/convertblockcommand.cpp
    src/documentserializer.cpp
    src/llmnormalizer.cpp
    src/diagrams/diagramclassifier.cpp
    src/diagrams/diagramrepair.cpp
    src/diagrams/mermaidlexer.cpp
    src/diagrams/mermaidparser.cpp
    src/diagrams/mermaidsequence.cpp
    src/diagrams/mermaidclass.cpp
    src/diagrams/mermaidstate.cpp
    src/diagrams/mermaider.cpp
    src/diagrams/mermaidedits.cpp
    src/diagrams/diagramlayout.cpp
    src/diagrams/sequencelayout.cpp
    src/diagrams/classlayout.cpp
    src/diagrams/statelayout.cpp
    src/diagrams/erlayout.cpp
    src/diagrams/diagrampainter.cpp
    src/diagrams/textcanvas.cpp
    src/diagrams/textdiagram.cpp
    src/diagrams/mermaidrenderer.cpp
    src/diagrams/diagramcanvas.cpp
    src/documentmanager.cpp
    src/blockmenumodel.cpp
    src/mathcommandmodel.cpp
    src/codelanguages.cpp
    src/imageassets.cpp
    src/tabledata.cpp
    src/todometa.cpp
    src/kanbandata.cpp
    src/mathrenderer.cpp
    src/clipboardhelper.cpp
    src/htmltomarkdown.cpp
    src/blockeditorengine.cpp
    src/documentselection.cpp
    src/documentsearch.cpp
    src/documentoutline.cpp
    src/documentstats.cpp
    src/documentexporter.cpp
    src/documentimporter.cpp
    src/egressfetcher.cpp
    src/egresspolicy.cpp
    src/embedmetadata.cpp
    src/remoteimageprovider.cpp
    src/notetemplates.cpp
    src/notefrontmatter.cpp
    src/wikilinkscanner.cpp
    src/notecollection.cpp
    src/noteindexfile.cpp
    src/wikilinkindex.cpp
    src/searchindexfeed.cpp
    src/recoveryjournalstore.cpp
    src/notefileio.cpp
    src/notebackupstore.cpp
    src/notetrashstore.cpp
    src/navigationhistory.cpp
    src/quickswitchermodel.cpp
    src/querydata.cpp
    src/querytools.cpp
    src/foldertreemodel.cpp
    src/notelistmodel.cpp
    src/searchindexdb.cpp
    src/collectionsearchindex.cpp
    src/collectionsearch.cpp
    src/settingsstore.cpp
    src/startupcontroller.cpp
    src/theme.cpp
    src/typography.cpp
    src/updatechecker.cpp
    src/vaultlock.cpp
)

# The version the running app reports (About, update check comparison).
target_compile_definitions(kvit-core PRIVATE
    KVIT_VERSION="${KVIT_VERSION_FULL}")

# Every module the composed application needs is linked into the library, not
# into the executable, so any binary linking kvit-core gets a complete editor:
# QuickControls2 for the shell's style, Network for the embed fetcher, and
# Multimedia for the media block's QML imports.
# PUBLIC only where kvit-core's own headers expose the module, so a consumer
# that includes them compiles. Everything else is an implementation detail and
# is PRIVATE: it still links into the final binary, but it no longer becomes
# part of the contract every consumer inherits.
#
#   Gui, Quick   - QImage/QColor/QTextDocument and QQuickItem/QQmlEngine appear
#                  in headers.
#   Network      - the egress fetcher and update checker expose types.
#   Concurrent   - QFuture appears in the async persistence and collection
#                  headers.
#   microtex     - the math renderer's headers use it.
#
# Widgets is only forward-declared (kvitapplication.h) or unused in headers;
# Sql appears in one comment; QuickControls2 and Multimedia are used by a
# single .cpp and by QML imports at runtime, not by any header.
target_link_libraries(kvit-core PUBLIC
    Qt6::Core
    Qt6::Gui
    Qt6::Quick
    Qt6::Network
    Qt6::Concurrent
    microtex
)
target_link_libraries(kvit-core PRIVATE
    Qt6::QuickControls2
    Qt6::Widgets
    Qt6::Multimedia
    Qt6::Sql
)

target_include_directories(kvit-core PUBLIC src)

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
