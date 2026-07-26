# Changelog

All notable changes to Kvit Notes are documented here. The format follows
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/), and the project
uses [Semantic Versioning](https://semver.org/).

## [Unreleased]

### Added

- Single-file mode: `kvit-notes <file.md>` opens a lone markdown file with
  no vault. Collection chrome stays hidden, startup is immediate, and the
  full block editor (math, diagrams, tables, kanban) works in-file. A quiet
  status-bar line, "Create vault from this folder…", upgrades the file's
  folder into a vault without touching its files.
- Disclosed, opt-out update check: at startup, at most once per day, the
  editor asks the GitHub Releases API whether a newer version exists and
  shows a passive status-bar notice if so. Settings → General turns it off.
  No telemetry, no automatic download.
- `--math-selftest` flag: headless probe that verifies the installed math
  resources resolve and render (packaging QA).
- Cross-platform build presets (`CMakePresets.json`), install targets, and a
  relocatable math-resource path for packaged builds.
- Application icon.

### Changed

- The project is licensed under MPL-2.0 (LICENSE, per-file headers).
- Task boards are edited on the board. Clicking a card's text opens an editor
  on the card's own line, where a typed `#label` becomes a label and a typed
  `📅 2026-08-01` a due date; clicking under it opens one on the description,
  which now shows on the card, holds as many lines as it is given, and
  renders formulas and `[[wiki-links]]` like the rest of a note. The dialog
  that used to be the only way in keeps the structured fields (labels, due
  date) and the pointer-free "Move to column" and "Delete card", and is
  reached by right-clicking a card.
- A task board's filter chips do something visible: the cards without the
  chosen label leave the columns instead of dimming, the column counts read
  "2 of 5", and a line beside the chips reports how much of the board is
  showing, with a way back. A "Hide done" chip keeps finished cards out.
- A task board column can be renamed by clicking its name, collapsed by the
  triangle beside it, and deleted with its cards; its header controls
  highlight and name themselves under the pointer. A new column and a new
  card open their editor rather than arriving called "New column" and
  "New card".

### Fixed

- Dragging a card between task-board columns works, as does dragging a column
  by its name to reorder it. Two things were taking the gesture: the editor's
  own drop area, which covers the page above every block and accepted the
  drag before the board's targets could see it, and the board's sideways
  scroll and the document's downward scroll, which took the grab as soon as
  the pointer moved so the view slid while the card stayed put. A drop now
  lands where the pointer is, with a line showing the slot it will take.
- A `[[wiki-link]]` written in a table cell or on a task-board card renders as
  a link and opens the note, rather than showing as ordinary text that did
  nothing when clicked. Links in both are drawn in the theme's link colour
  instead of Qt's default blue.
- The task-board card dialog follows the light and dark theme. Its fields
  were left to the Fusion style, which reads the desktop palette rather than
  the note theme, so a dark note opened a light dialog.
- Settings dialog: the title bar now drags the dialog, so it can be pushed
  aside to watch a theme or typography change land in the document behind
  it. The tab strip no longer spills past the dialog's right border — the
  theme cards had demanded more width than the dialog had, and now wrap.
- Diagram labels now use the same font as the text around them. With no
  editor font chosen — the default — the diagram canvas was handed an empty
  family name, which Qt matches against nothing and resolves to whatever the
  font database offers first, a serif face on a stock Linux install.
- Inline math is no longer cut off or blurred. Equation bitmaps were exactly
  as wide as the formula's advance, so glyphs that paint outside it lost
  their ends (an italic `f` lost both its hook and its tail), and they were
  drawn at whatever sub-pixel offset the line's text advances added up to,
  which filtered them across neighbouring pixels. The renderer now leaves a
  transparent side margin, and the overlay places each image on a whole
  device pixel at its bitmap's own size.

## [1.0.0] - unreleased

The first public release: the full block editor (hybrid live-preview
markdown, the complete block palette, tables and kanban, callouts, toggles,
embeds, drop caps), a notes collection with folders, tags, global search,
wiki-links with backlinks and a quick switcher, LaTeX math with the NewTX
default face, five natively rendered Mermaid diagram families with
on-diagram editing, ASCII-diagram repair, import/export (Markdown, HTML,
PDF, plain text), themes and typography settings, autosave with backups and
crash recovery, and War-and-Peace-scale performance.
