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

# Every core translation unit: all of src/ except the non-Qt platform backends,
# the sample programs, and the generated NewTX metric tables, which are
# #included by the kvit_newtx_* wrapper defs and never compiled standalone.
#
# Written out rather than globbed. The list used to be a GLOB_RECURSE with
# those three exclusions, and its comment gave the reason: it "reproduces [the
# upstream source list] without a 130-line transcription that would drift from
# upstream". That reason held while upstream was still being followed, and this
# is a vendored copy at a pinned commit that nobody is updating, so the drift
# it guarded against can no longer happen.
#
# Be accurate about what the glob cost, because it is not much. CONFIGURE_DEPENDS
# made the build tool re-check the directories on every build, which measures
# 0.01 seconds. What it bought was that a file appearing or disappearing under
# src/ triggered a re-configure, costing about 15 seconds -- in a copy nobody
# is changing, never. So this is tidying rather than a speed-up.
#
# The list below is exactly what the glob selected, 101 files.
set(MICROTEX_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/src/atom/atom_basic.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/atom/atom_char.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/atom/atom_impl.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/atom/atom_matrix.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/atom/atom_row.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/atom/atom_space.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/atom/colors_def.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/atom/unit_conversion.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/box/box.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/box/box_factory.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/box/box_group.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/box/box_single.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/core/core.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/core/formula.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/core/formula_def.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/core/glue.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/core/localized_num.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/core/macro.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/core/macro_def.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/core/macro_impl.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/core/parser.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/fonts/alphabet.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/fonts/font_basic.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/fonts/font_info.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/fonts/fonts.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/latex.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/render.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/res/builtin/formula_mappings.res.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/res/builtin/symbol_mapping.res.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/res/builtin/tex_param.res.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/res/builtin/tex_symbols.res.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/res/font/bi10.def.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/res/font/bx10.def.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/res/font/cmbsy10.def.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/res/font/cmbx10.def.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/res/font/cmbxti10.def.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/res/font/cmex10.def.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/res/font/cmmi10.def.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/res/font/cmmi10_unchanged.def.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/res/font/cmmib10.def.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/res/font/cmmib10_unchanged.def.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/res/font/cmr10.def.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/res/font/cmss10.def.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/res/font/cmssbx10.def.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/res/font/cmssi10.def.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/res/font/cmsy10.def.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/res/font/cmti10.def.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/res/font/cmti10_unchanged.def.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/res/font/cmtt10.def.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/res/font/dsrom10.def.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/res/font/eufb10.def.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/res/font/eufm10.def.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/res/font/i10.def.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/res/font/kvit_newtx_ntxbexa.def.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/res/font/kvit_newtx_ntxbexx.def.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/res/font/kvit_newtx_ntxbsy.def.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/res/font/kvit_newtx_ntxbsy5.def.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/res/font/kvit_newtx_ntxbsy7.def.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/res/font/kvit_newtx_ntxbsyc.def.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/res/font/kvit_newtx_ntxbsym.def.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/res/font/kvit_newtx_ntxexa.def.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/res/font/kvit_newtx_ntxexx.def.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/res/font/kvit_newtx_ntxsy.def.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/res/font/kvit_newtx_ntxsy5.def.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/res/font/kvit_newtx_ntxsy7.def.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/res/font/kvit_newtx_ntxsyc.def.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/res/font/kvit_newtx_ntxsym.def.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/res/font/kvit_newtx_xcharter_bold_tlf_t1.def.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/res/font/kvit_newtx_xcharter_bolditalic_tlf_t1.def.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/res/font/kvit_newtx_xcharter_italic_tlf_t1.def.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/res/font/kvit_newtx_xcharter_roman_tlf_t1.def.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/res/font/kvit_newtx_zchbmi.def.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/res/font/kvit_newtx_zchbmia.def.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/res/font/kvit_newtx_zchmi.def.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/res/font/kvit_newtx_zchmia.def.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/res/font/moustache.def.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/res/font/msam10.def.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/res/font/msbm10.def.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/res/font/r10.def.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/res/font/r10_unchanged.def.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/res/font/rsfs10.def.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/res/font/sb10.def.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/res/font/sbi10.def.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/res/font/si10.def.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/res/font/special.def.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/res/font/ss10.def.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/res/font/stmary10.def.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/res/font/tt10.def.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/res/parser/font_parser.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/res/parser/formula_parser.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/res/reg/builtin_font_reg.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/res/reg/builtin_syms_reg.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/res/sym/amsfonts.def.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/res/sym/amssymb.def.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/res/sym/base.def.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/res/sym/kvit_newtx.def.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/res/sym/stmaryrd.def.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/res/sym/symspecial.def.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/utils/string_utils.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/utils/utf.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/utils/utils.cpp"
)

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
# LaTeX::init(), and where it is depends on the layout the binary is running
# in. MathRenderer::resourceRoot() answers that, and the dev and test answer is
# the vendored `res` directory beside these sources.
#
# The path used to be a PUBLIC compile definition on this target, which put it
# on the compile command of everything that links microtex: 554 of 651 compile
# edges in this repository, and 586 of 713 and 387 of 540 in the two projects
# that build the editor as a subdirectory. That defeats a compiler cache
# outright. A cache decides whether it has compiled a file before by hashing
# the compiler, the whole argument list and the contents of every file the
# compile reads, and it can rewrite the arguments it recognises as paths --
# -I, -isystem, -include, the input file -- to paths relative to the working
# directory before hashing. It cannot rewrite a path inside a macro value,
# because nothing tells it that one is a path. So the same source compiled in a
# second checkout of the same commit looked like a different compile, and none
# of it could be served from the cache.
#
# Only MathRenderer::resourceRoot() ever reads the macro, so it is now set on
# that one source file, in the top-level CMakeLists.txt beside the target that
# compiles it. Nothing about how the path resolves changed: the same value
# reaches the same function, and the KVIT_MATH_RES environment variable still
# overrides it. What changed is that one compile command holds an absolute path
# rather than five hundred.

# Upstream sources trip -Wall pedantry we do not own; keep our warnings off
# this third-party target without dropping them project-wide.
if (NOT MSVC)
    target_compile_options(microtex PRIVATE -w)
endif ()
