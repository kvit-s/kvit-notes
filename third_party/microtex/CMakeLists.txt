# Vendored MicroTeX (NanoMichael/cLaTeXMath, pinned commit 0e3707f6, MIT).
# The only vendored third-party code in the editor, proven to build and
# link on this toolchain before it was adopted. Built as a
# static library `microtex` with the QPainter backend and linked into
# kvit-core behind the MathRenderer seam. Qt has no math typesetting engine;
# this turns a TeX string into a laid-out render we paint to a QImage.
#
# The upstream CMake builds sample executables and every platform backend; we
# compile only the core sources plus the Qt (QPainter) backend, and vendor
# tinyxml2 (used by the resource parsers) alongside.

cmake_minimum_required(VERSION 3.16)

find_package(Qt6 REQUIRED COMPONENTS Gui)

# Every core translation unit: all of src/ except the non-Qt platform
# backends and the sample programs. The upstream target_sources list enumerates
# exactly this set (atom, box, core, fonts, utils, res/*, latex.cpp,
# render.cpp); globbing with those two exclusions reproduces it without a
# 130-line transcription that would drift from upstream.
file(GLOB_RECURSE MICROTEX_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp")
list(FILTER MICROTEX_SOURCES EXCLUDE REGEX "/src/platform/")
list(FILTER MICROTEX_SOURCES EXCLUDE REGEX "/src/samples/")
# The vendored generated NewTX metric tables are #included by the
# kvit_newtx_* wrapper defs, never compiled standalone.
list(FILTER MICROTEX_SOURCES EXCLUDE REGEX "/src/res/font/newtx-generated/")

add_library(microtex STATIC
    ${MICROTEX_SOURCES}
    "${CMAKE_CURRENT_SOURCE_DIR}/src/platform/qt/graphic_qt.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/tinyxml2/tinyxml2.cpp"
)

# GCC's debug-info pass exceeds its variable-tracking limit on this generated
# symbol table and emits an unavoidable "retrying without" note (even with
# -w). Disable only the pass GCC was going to discard for this source anyway.
set_property(SOURCE
    "${CMAKE_CURRENT_SOURCE_DIR}/src/res/builtin/tex_symbols.res.cpp"
    APPEND PROPERTY COMPILE_OPTIONS
    "$<$<CXX_COMPILER_ID:GNU>:-fno-var-tracking-assignments>")

# Upstream uses C++20 on GNU (std::filesystem in latex.cpp, plus C++20 idioms).
target_compile_features(microtex PUBLIC cxx_std_20)

# BUILD_QT selects the QPainter backend headers; HAVE_LOG / GRAPHICS_DEBUG are
# left off (spike a built with both OFF — no stderr chatter, no debug overlays).
target_compile_definitions(microtex PUBLIC BUILD_QT)

# SYSTEM: the `-w` below silences these sources while microtex itself is
# compiled, but its headers are also compiled as part of every one of our
# translation units that includes one, and there they are subject to our
# warning flags. Upstream's constructor initialiser order and unused
# parameters are not ours to fix, and a warning nobody can act on is what
# makes a build's output stop being read.
target_include_directories(microtex SYSTEM PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
    "${CMAKE_CURRENT_SOURCE_DIR}/tinyxml2"
)

target_link_libraries(microtex PUBLIC Qt6::Gui)

# The resource root (fonts + formula/symbol XML) is read at runtime by
# LaTeX::init(). Expose the vendored source path so the MathRenderer seam can
# resolve it in dev and test; deployment can override via the KVIT_MATH_RES
# environment variable (handled in MathRenderer).
target_compile_definitions(microtex PUBLIC
    KVIT_MATH_RES_ROOT="${CMAKE_CURRENT_SOURCE_DIR}/res")

# Upstream sources trip -Wall pedantry we do not own; keep our warnings off
# this third-party target without dropping them project-wide.
if (NOT MSVC)
    target_compile_options(microtex PRIVATE -w)
endif ()
