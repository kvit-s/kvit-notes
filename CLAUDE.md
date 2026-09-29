# kvit-notes-go

This repository is the Go version of Kvit Notes, the Markdown block editor
app. It is built on the unison toolkit (`github.com/richardwilkes/unison`)
and on kvit-ui-go, the Kvit component library (`~/kvit-ui-go`). The Qt/QML
version it replaces is in `~/kvit-notes` and is the specification:
`features.md` there says what the app does, and `PARITY.md` here lists each
of its sections and marks what exists in Go.

This repository is one step of moving all the Kvit desktop apps from Qt to Go.
The plan is `~/kvit-shirei/go-ui-plan.md`; read its sections 3 to 6 before
changing how this repository is laid out or built. The editor package is
the plan's step 3; the full app is step 6.

## Recording the migration

The migration is recorded in `~/kvit-shirei/migration-log.md` for a later
blog post. Append a dated entry there, newest last, when you:
- finish a step of the plan;
- make a decision the plan does not cover;
- find something that works differently from what the plan expects;
- measure something.

Give the command behind every number, and save screenshots under
`~/kvit-shirei/migration-log/<date>/`.

## What is where

| Package | What it holds |
|---|---|
| `editor` | The block editor as a unison widget, public so kvit-works-go and kvit-hub-go can embed it. `model.go` (blocks, Markdown in and out), `inline.go` (inline spans and which markers show), `doc.go` (every operation, and undo) know nothing of the toolkit. `editor.go` (the panel, row geometry), `layout.go` (one block's text through kvit-ui's `text` package), `draw.go`, `keys.go`, `pointer.go`, `slashmenu.go` (the / menu), `blockmenu.go`, `access.go` (what screen readers are told) and `probe.go` (positions for tests and tools). The block kinds drawn as something other than text each have a file: `image.go` (pictures and media), `embed.go` (web page cards), `callout.go` (callouts and toggles), `table.go`, `toc.go` (table of contents), `board.go` (task boards) with `cardedit.go` (a card's line and description, edited in place), `queryblock.go` (collection queries), `dropcap.go`. A Mermaid block is `diagram.go` (its drawing, controls, zoom, preview and source), `diagramdraw.go` (painting a scene), `diagramedit.go` (the gestures on the drawing), `diagramaccess.go` (what a screen reader is told of it) and `mathdiagram.go` (its `$$…$$` labels). Math is `mathblock.go` (display equations), `mathinline.go` (inline formulas, typeset in prose, cells and cards), `mathassist.go` (the `$` pair, the backslash that opens the command menu, Tab through a template's slots) and `mathmenu.go` (the command menu). `print.go` cuts the note into pages for PDF. `commands.go` is what a toolbar asks of the editor, `formatbar.go` the bar over a selection, `wikicomplete.go` the `[[` list, `links.go` following and editing links, `marks.go` the find bar's matches, `stats.go` the counts |
| `vault` | A vault on disk, in the Qt app's formats: the scan (`scan.go`), front matter (`frontmatter.go`), `.kvit/collection.json` (`collection.go`), the lock shared with the Qt app (`lock*.go`), saving with backups and the one-time `.md.bak`, notes and folders created, renamed, moved, captured and trashed (`vault.go`), the recovery journal, backups and trash (`safety.go`), other programs' changes (`watch.go`), templates (`templates.go`), the picture folders in `.kvit/settings.json` (`vaultsettings.go`), and where a vault and the settings are found (`places*.go`) |
| `highlight`, `links`, `search`, `export`, `kanban`, `query`, `ignore` | Ports of Qt logic without any interface, each with the Qt tests ported beside it: code colouring by language; wiki-link scanning, resolution, backlinks and the redirects that keep links working after a rename; finding in a note, replacing, and the search index across notes; export to Markdown, HTML and text, import, and HTML to Markdown; the task board's Markdown; the collection query's spec and evaluation; the `.gitignore` rules a vault's scan follows |
| `mermaid`, `diagram`, `textdiagram` | Diagrams, ports of the Qt app's `src/content/diagrams` with its tests: `mermaid` reads a fence into a syntax tree (flowchart, sequence, class, state and ER, each by mermaid@11.16.0's grammar) and makes the source edits behind the gestures on a drawing; `diagram` lays a tree out into a `Scene` of shapes, paths and text in logical pixels, off the interface thread, with a cache and the Qt app's limits; `textdiagram` is character-cell diagrams: the classifier and straightening a fence goes through when a note is opened or text pasted (`Ingest`), a grid of characters (`Canvas`), and a scene as box-drawing text (Copy as text, `fromscene.go`) |
| `mathtex` | LaTeX math: MicroTeX lays a formula out and this package draws it on a unison canvas with the same fonts (`draw.go`). MicroTeX is loaded as a shared library at run time without cgo, through purego on Linux and macOS and the system's loader on Windows (`dl_*.go`), found beside the program, in a package's layout or in `build/` (`locate.go`); `native/` is the library's C interface |
| `mathcmd` | The math command menu's list and ranking (the Qt `MathCommandModel`, with its tests) |
| `third_party/microtex` | MicroTeX (cLaTeXMath) at a pinned commit, with Kvit's fonts and fixes; its `res/` is what goes beside the program as `math-res` |
| `app` | The vault window. `window.go` ties the panes to a vault and the editor and arranges them; the sidebar's scopes (`sidebar.go`), the note list (`notelist.go`), the tag strip, the toolbar (`toolbar.go`), the File and View menus (`menus.go`), the note and folder menus and dialogs (`actions.go`), keeping work safe (`safety.go`), moving between notes (`navigate.go`), the outline (`outline.go`) and backlinks (`backlinks.go`) panes, the find bar (`find.go`), search across notes (`searchview.go`), links (`links.go`) and renames that keep them working (`rename.go`), templates, settings, statistics, quick capture, export and import (`templates.go`, `settings.go`, `stats.go`, `capture.go`, `exchange.go`), pictures (`images.go`), embed previews (`embed.go`), query blocks' notes (`queries.go`), acting on several notes (`bulk.go`), PDF export (`pdf.go`), HTML on the clipboard (`clipboard.go`), the ranking of notes by name (`fuzzy.go`), the tray icon's menu and closing to the tray (`tray.go`), note files dropped on a window (`drop.go`), where a diagram's picture is saved and what the status bar says after a diagram gesture (`diagram.go`), the math command menu's list shared by every window (`mathcommands.go`) and the keyboard shortcuts list (`shortcuts.go`); `prefs.go` reads and writes the app's settings |
| `cmd/kvit-notes` | The program: a vault window, or one note file on its own (`window.go`), one running copy (`instance.go`) and what a later copy or macOS's Finder and Dock ask it to open (`openPath` in `main.go`), the tray icon with the app's icon from `icons/` and `--tray-check`, which shows it on a real desktop (`tray.go`), the version it reports (`version.go`), a console for its output when started from one on Windows (`console_*.go`), and the Windows icon, version and manifest in `rsrc_windows_amd64.syso`; with the scripted scenarios (`scenarios.go`, the math storyboards in `scenarios_math.go`), the real-window check (`check.go`), the benchmark (`bench.go`), the screenshot comparison (`compare.go`) and `--math-selftest`, which renders formulas with the packaged math library |
| `packaging` | The downloads, under the Qt packages' names: `build-all.sh` makes the Windows installer and zip, the macOS app, the Linux tar.gz, AppImage and AUR package, and the Flatpak sources into `dist/`; `packaging/README.md` says what each holds and how each is tried |
| `tools/win-check.ps1` | Reads what Windows' UI Automation reports about the check window, and saves a picture of it |
| `tools/tray-check.ps1` | Opens the tray icon's menu, chooses a line and clicks the icon by posting to the app's own windows while `kvit-notes --tray-check` runs, and says where the shell has the icon |
| `tools/build-mathlib.sh` | Builds the math library for one platform with zig's C++ compiler, rebuilding it only when its sources change |
| `tools/roundtrip` | Reads Markdown files through the editor's parser and serializer and reports which come back changed, without writing them |

The editor started as a port of the Shirei prototype in `~/kvit-shirei`
(`model.go`, `inline.go`, `doc.go` nearly unchanged; the drawing and input
rewritten for unison).

## How the editor works

- **The note is a list of blocks, each holding its Markdown source.** The
  caret and the selection are positions in that source (`Pos`: a block id
  and a rune offset). What is drawn is the source with the markers of every
  span removed, except the spans the caret or selection touches
  (`project` in `inline.go`); a projection maps drawn offsets to source
  offsets and back.
- **One panel draws every row.** Rows are not panels: `measure` lays every
  block out once (cached by `layoutKey`), stacks the rows, and `draw` draws
  the ones in the dirty rectangle. Put the editor in a `kvitui.Region` to
  scroll it; it takes the width it is given and is as tall as the note plus a
  third of the window.
- **Line spacing follows Qt's text documents,** not Qt's labels: lines are
  the line height times the font's own line height (its ascent and descent
  rounded up to a pixel) apart, with the baseline at the font's ascent
  (`text.Options.Pitch`), and each block's text height is rounded down to a
  whole pixel, which is what keeps rows at Kvit's positions. The scenarios
  run at the typography Kvit's storyboards were taken at, 15 px and 1.0.
- **Screen readers see each block as an editable text** (a virtual child of
  the editor's node), and the keyboard focus is reported on the block with
  the caret (`FocusChild`). Text, lines and caret are in drawn offsets, so they
  match what is on the screen.
- **Menus.** The / menu, the `[[` list and the math command menu are the
  editor's own panels in the Kvit window's popup layer, because the keyboard
  stays in the editor while they filter. The block menu is kvit-ui's menu,
  with "Turn into" and "Copy as" as submenus.
- **Blocks that draw.** A Mermaid block and a display equation are drawn
  away from the caret and show their source with the caret in them, as a
  table and a task board do; the note stores only the fence and its text.

## Sharing vaults with the Qt app

During the migration the Qt and Go apps are used on the same vaults, so
`vault` follows the Qt app's formats exactly (the survey behind them is in
the migration log's step 6 entries):
- **The lock** is `flock` on `.kvit/vault.lock` on Linux and macOS (not
  `fcntl`, which would not exclude the Qt app) and `LockFileEx` on one byte at
  offset 0x40000000 on Windows. Never change either.
- **Front matter** is edited a key at a time and every other line kept; a key
  Kvit knows is written in the Qt app's form (`tags: [a, b]`, `pinned: true`,
  and `false` or no tags removes the key).
- **`collection.json`** is rewritten whole by the Qt app, so every field it
  has is kept in `Collection`, used or not.
- **Never reuse** `.kvit/index.json`, `.kvit/embedcache/` or
  `.kvit/cache/index.json`: the Qt app deletes the first two on every open and
  trusts the third by size and time.
- **Tests and trial runs never touch a real vault.** `./build.sh --win` opens
  a copy of the Qt repository's demo vault; `kvit-notes` with no argument opens
  the vault the Go app, or before it the Qt app, last had open.
- **The settings are the Go app's own file** (`ui.json` in the user's
  configuration folder, under `kvit-notes`), made the first time as a copy of
  the Qt app's `settings.json`, whose keys it keeps. The Go app never writes
  the Qt app's settings, so trying it cannot change what the Qt app opens.
- **`.kvit/redirects.json` and `.kvit/settings.json`** are the Qt app's
  files: the table of renamed notes the links follow, and a vault's picture
  folders. Both are written in its format.

## Building and checking

```sh
./build.sh               # build every package, the math library and build/kvit-notes
./build.sh --test        # also gofmt check, go vet and the headless tests
./build.sh --shots       # the scenarios' screenshots, each stacked under Kvit's
./build.sh --bench       # open, scroll and type in 1,237 blocks of Kvit's docs
./build.sh --cross       # also kvit-notes for Windows, macOS (both) and Linux
./build.sh --win         # kvit-notes for Windows onto D:, started on the Windows desktop
./build.sh --win-check   # the same, driven through a scripted check, read through UI Automation
./build.sh --run         # start kvit-notes here (needs a display)
packaging/build-all.sh   # the downloads for every platform into dist/ (--test tries them)
~/kvit-ui-go/tools/check-all.sh   # ./build.sh --test in every Kvit Go repository
```

The git hook in `.githooks/pre-commit` runs `./build.sh --test`
(`git config core.hooksPath .githooks` enables it in a fresh clone).

- **Tests run headless** on `unison.StartHeadless`: the real event loop,
  drawing and screen-reader tree, with input injected. They never open a
  window on the desktop.
- **The scenarios are the specification's storyboards.** Each replays one of
  Kvit's `tests/tst_visual.qml` storyboards with the same note and input and
  checks the note after each step. Kvit's reference images were copied to
  `~/kvit-qt-reference/kvit-notes-storyboards/`, outside every repository,
  because they are ignored by the Qt repository's git and cannot be made
  again once Qt is removed.
- **`--win-check` opens a window on the Windows desktop** for about 20
  seconds, twice. It drives the editor through the window's own key dispatch
  and never sends input to the desktop.
- **cgo is always off,** and every build must stay cross-compilable.
- **The math library needs zig.** Every build runs `tools/build-mathlib.sh`,
  which compiles MicroTeX with `zig c++` into `build/libkvitmath.so` (and,
  with `--cross`, `kvitmath.dll` and `libkvitmath.dylib` with a `math-res`
  folder into each `build/<os>-<arch>/`; with `--win`, beside the program).
  Without zig on the PATH the build stops. The program without the library
  runs with math off: formulas show as their TeX.

## Conventions

- **Module path.** `github.com/kvit-s/kvit-notes`, the repository's final
  name. kvit-ui comes from `../kvit-ui-go` through a `replace` line, and so
  does kvit-ui's patched go-text; keep both lines.
- **unison stays unmodified.** Anything missing goes in this repository or in
  kvit-ui-go, never into unison.
- **Go only, with one exception.** The math engine, MicroTeX, stays C++ in
  `third_party/microtex`, built as a shared library with a C interface
  (`mathtex/native`) and loaded at run time, so the Go build keeps cgo off.
  No other C or C++ goes into this repository.
- **The library's rules apply here too:** colours only from the theme's
  tokens, font sizes only from the interface's type roles or the document's
  typography. `TestSourceRules` checks the source.
- **History.** Commit on `main` and keep it linear, with no branches and no
  merge commits. The switch replays this history commit by commit.
