# kvit-notes-go

Kvit Notes, the Markdown block editor, built on the unison toolkit and
kvit-ui-go.

It builds with cgo off and cross-compiles to Windows, macOS and Linux.
Building needs Go 1.27; with an older Go installed, the `toolchain` line in
`go.mod` makes Go fetch 1.27 by itself. It needs `~/kvit-ui-go` beside it,
and zig (https://ziglang.org/download/) on the PATH: math is typeset by
MicroTeX, which stays C++ and is built by zig as a shared library that the
program loads at run time.

```sh
./build.sh --test        # build, check formatting, vet, run the headless tests
./build.sh --win         # start kvit-notes on the Windows desktop (from WSL)
./build.sh --help        # every option
packaging/build-all.sh   # the Windows, macOS and Linux downloads, into dist/
build/kvit-notes folder  # open a folder of notes as a vault
build/kvit-notes note.md # edit one note on its own
```

**Status:** the app opens a vault, a folder of Markdown notes, in Kvit's
window: the sidebar, the note list and the editor, with the app's formats
on disk, its lock, backups, recovery journal and trash. It follows and
completes links, finds and replaces, searches across notes, and draws code
in colour, pictures, callouts, tables, task boards, collection queries,
Mermaid diagrams (edited on the drawing too) and typeset math, with the
command menu for typing it. It has the app's menus, settings, templates,
export and import, tray icon and file associations, and packages for
Windows, macOS and Linux (`packaging/README.md` says which of them have
been tried).

**Licence:** MPL-2.0.
