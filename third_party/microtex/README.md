# MicroTeX

The LaTeX math engine Kvit Notes draws formulas with: MicroTeX, the older
cLaTeXMath line by NanoMichael (MIT, `LICENSE.clatexmath`), pinned at
upstream commit 0e3707f6, with Kvit's own changes (the NewTX/XCharter fonts
under `res/fonts/kvit-newtx`, their metric tables under `src/res/font`, the
unrounded size getters in `render.h`, and fixes marked "Local fix" or "Local
change" in the sources). One fix is this repository's own: tinyxml2 opens
files through their UTF-16 path on Windows (`callfopen` in
`tinyxml2/tinyxml2.cpp`), so a resource folder under a path that is not plain
ASCII still works.

It is copied from the Qt app's repository (`~/kvit-notes`,
`third_party/microtex` as of its commit 918f539) without the parts that
painted through a toolkit: the Qt, Cairo, Skia and GDI+ back ends
(`src/platform`), the sample programs (`src/samples`), and the CMake, Meson
and qmake files that built them.

It is built into the shared library the Go package `mathtex` loads, together
with the recording back end and the C interface in `mathtex/native`, by
`tools/build-mathlib.sh`. The program reads `res` at run time, as a
`math-res` folder beside it.

Licences: the engine's is `LICENSE.clatexmath`; tinyxml2's is
`tinyxml2/LICENSE.txt`; the fonts' are in `res/fonts/licences`,
`res/fonts/kvit-newtx/LICENSES` with `res/fonts/kvit-newtx/NOTICE.md`,
`res/greek/LICENSE` and `res/cyrillic/LICENSE`.

The NewTX metric definitions in `src/res/font/kvit_newtx_*.def.cpp` first look
for `build/generated/newtx-charter-microtex/defs/` at the top of the checkout,
where the Qt repository's font generator writes, and otherwise use the copies
in `src/res/font/newtx-generated`. Nothing in this repository writes that
folder, so the copies are what is built.
