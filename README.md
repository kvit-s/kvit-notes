# kvit-notes-go

The Go version of Kvit Notes, the Markdown block editor, built on the unison
toolkit and kvit-ui-go. It replaces the Qt/QML app in `~/kvit-notes`, and
takes over that repository's name when it does everything that one does
(`PARITY.md`).

It builds with cgo off and cross-compiles to Windows, macOS and Linux.
Building needs Go 1.27; with an older Go installed, the `toolchain` line in
`go.mod` makes Go fetch 1.27 by itself. It needs `~/kvit-ui-go` beside it.

```sh
./build.sh --test        # build, check formatting, vet, run the headless tests
./build.sh --win         # start kvit-notes on the Windows desktop (from WSL)
./build.sh --help        # every option
build/kvit-notes folder  # open a folder of notes as a vault
build/kvit-notes note.md # edit one note on its own
```

**Status:** the app opens a vault, a folder of Markdown notes, in Kvit's
window: the sidebar, the note list and the editor, with the Qt app's formats
on disk, its lock, backups, recovery journal and trash, so both apps can be
used on the same notes in turn. It follows and completes links, finds and
replaces, searches across notes, draws code in colour, pictures, callouts,
tables, task boards and collection queries, and has the Qt app's menus,
settings, templates, export and import. `PARITY.md` lists every feature of
the Qt app with what exists and the test behind it; `PLAN.md` says what is
left, chiefly math rendering, Mermaid diagrams and the tray icon.

**Licence:** MPL-2.0, as for the Qt version.
