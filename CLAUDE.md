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
| `editor` | The block editor as a unison widget, public so kvit-works-go and kvit-hub-go can embed it. `model.go` (blocks, Markdown in and out), `inline.go` (inline spans and which markers show), `doc.go` (every operation, and undo) know nothing of the toolkit. `editor.go` (the panel, row geometry), `layout.go` (one block's text through kvit-ui's `text` package), `draw.go`, `keys.go`, `pointer.go`, `slashmenu.go` (the / menu), `blockmenu.go`, `access.go` (what screen readers are told) and `probe.go` (positions for tests and tools) |
| `cmd/kvit-notes` | The program: one window editing one note, with the scripted scenarios (`scenarios.go`), the real-window check (`check.go`), the benchmark (`bench.go`) and the screenshot comparison (`compare.go`) |
| `tools/win-check.ps1` | Reads what Windows' UI Automation reports about the check window, and saves a picture of it |

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
  `size × line height` apart with the baseline at the font's ascent
  (`text.Options.Pitch`), and each block's text height is rounded down to a
  whole pixel, which is what keeps rows at Kvit's positions.
- **Screen readers see each block as an editable text** (a virtual child of
  the editor's node), and the keyboard focus is reported on the block with
  the caret (`FocusChild`). Text, lines and caret are in drawn offsets, so they
  match what is on the screen.
- **Menus.** The / menu is the editor's own panel in the Kvit window's popup
  layer, because the keyboard stays in the editor while it filters. The block
  menu is kvit-ui's menu, which has no submenus, so "Turn into…" and
  "Copy as…" open a second menu.

## Building and checking

```sh
./build.sh               # build every package and build/kvit-notes
./build.sh --test        # also gofmt check, go vet and the headless tests
./build.sh --shots       # the scenarios' screenshots, each stacked under Kvit's
./build.sh --bench       # open, scroll and type in 1,237 blocks of Kvit's docs
./build.sh --cross       # also kvit-notes for Windows, macOS (both) and Linux
./build.sh --win         # kvit-notes for Windows onto D:, started on the Windows desktop
./build.sh --win-check   # the same, driven through a scripted check, read through UI Automation
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

## Conventions

- **Module path.** `github.com/kvit-s/kvit-notes`, the repository's final
  name. kvit-ui comes from `../kvit-ui-go` through a `replace` line, and so
  does kvit-ui's patched go-text; keep both lines.
- **unison stays unmodified.** Anything missing goes in this repository or in
  kvit-ui-go, never into unison.
- **The library's rules apply here too:** colours only from the theme's
  tokens, font sizes only from the interface's type roles or the document's
  typography. `TestSourceRules` checks the source.
- **History.** Commit on `main` and keep it linear, with no branches and no
  merge commits. The switch replays this history commit by commit.
