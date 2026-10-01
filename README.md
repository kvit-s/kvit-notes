# kvit-notes

Kvit Notes, a Markdown block editor for the desktop, built on the unison
toolkit and kvit-ui, the Kvit component library.

Kvit Notes opens a vault, a folder of Markdown notes, in a window with a
sidebar, a note list and the editor. Each note is an ordinary `.md` file,
shown rendered, with the Markdown of a span revealed while the caret is in
it; the vault's lock, backups, recovery journal and trash are kept in its
`.kvit` folder. The app follows and completes wiki links, finds and
replaces, searches across notes, and draws code in colour, pictures,
callouts, tables, task boards, collection queries, Mermaid diagrams (which
can also be edited on the drawing) and typeset math, with a command menu for
typing it. It has menus, settings, templates, export and import, a tray icon
and file associations, and packages for Windows, macOS and Linux
(`packaging/README.md` says which of them have been tried).

It builds with cgo off and cross-compiles to Windows, macOS and Linux.
Building needs Go 1.27; with an older Go installed, the `toolchain` line in
`go.mod` makes Go fetch 1.27 by itself. It needs the kvit-ui repository
checked out beside it (`../kvit-ui`, which the `replace` lines in `go.mod`
name), and zig (https://ziglang.org/download/) on the PATH: math is typeset
by MicroTeX, a C++ engine that zig builds as a shared library the program
loads at run time.

```sh
./build.sh --test        # build, check formatting, vet, run the headless tests
./build.sh --win         # start kvit-notes on the Windows desktop (from WSL)
./build.sh --help        # every option
packaging/build-all.sh   # the Windows, macOS and Linux downloads, into dist/
build/kvit-notes folder  # open a folder of notes as a vault
build/kvit-notes note.md # edit one note on its own
```

**Licence:** MPL-2.0.
