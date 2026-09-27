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
build/kvit-notes note.md # edit a note
```

**Status:** the editor is done as the plan's step 3 defines it: a note's
paragraphs, headings, lists, to-dos, quotes, code and dividers, edited in
place with the Markdown markers shown only around the caret, the / menu, the
block menu, block selection, dragging, and one undo history. The 17
storyboards replayed from Kvit's own screenshot test pass, the editor reports
each block to screen readers as editable text, and it runs in a real window on
Windows. The program around it is one window editing one note; the rest of
the app is step 6.

**Licence:** MPL-2.0, as for the Qt version.
