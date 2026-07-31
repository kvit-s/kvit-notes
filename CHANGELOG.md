# Changelog

All notable changes to Kvit Notes are documented here. The format follows
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/), and the project
uses [Semantic Versioning](https://semver.org/).

## [Unreleased]

### Added

- Each block gutter now has a visible menu button below its drag handle. The
  menu can copy the block normally, copy it explicitly as Markdown, plain text
  or HTML, and export just that block as Markdown, HTML, PDF or plain text. On
  a block selection the same commands act on the whole selection. Menu and
  Shift+F10 provide the keyboard route.
- The block menu opens on a right-click anywhere in a block's left gutter, for
  every block type. It answered the drag handle alone before, a fourteen-pixel
  target that appears on hover, so the commands on it — Turn into, Duplicate,
  Delete, Move, Indent, Remove line breaks — were reachable by whoever already
  knew where to aim. A gutter right-click on a block inside a block selection
  opens the selection menu, as the handle does.
- A caret in the space between two blocks. Pointing at the blank line between
  two blocks draws a line in it; clicking turns that into a blinking caret, and
  typing makes a paragraph there holding what was typed. "/" opens the block
  type menu instead, and Enter leaves the paragraph empty. Up and Down walk the
  caret between spaces and Escape leaves it. Ctrl+Enter on selected blocks
  places it after them, which is the keyboard route to the space below a table,
  code block or diagram, whose own Enter key belongs to editing them.
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

- The window's two menus sit next to each other at the left of the toolbar, and
  File holds what acts on documents. Export, Import, Settings, the keyboard
  shortcut reference, quick capture and the template commands were spread
  between a View menu at the far right of the toolbar and a Templates button
  beside it, so the menu named after what the window shows was also the way to
  write a PDF. View now holds the panels, the editor modes, the theme and the
  motion setting, and nothing else.
- The project is licensed under MPL-2.0 (LICENSE, per-file headers).
- A task-board card carries its labels and its due date in a strip under the
  title, which is also where both are set: "+ tag" opens a field offering the
  labels the board already uses, so one can be reused rather than retyped, and
  each label chip removes itself. "+ due date" opens a calendar, so the one
  date the storage grammar is strict about never has to be typed. Typing
  `#label` on the card's own line still works, since that is what the file
  holds, but nothing about a line of text said so.
- Task-board cards record the day they were added and the day they were last
  changed, and say so at their foot. Both ride in an HTML comment at the end of
  the card's line, where they stay out of the text being edited and render as
  nothing in every other markdown tool; a card written before this says only
  what it knows rather than claiming a day.
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

- A query block exports as its answer. HTML and PDF export printed the
  `from:`/`where:` spec as a code listing, in the board view as well as the
  table view, so what reached the file was the one part of the block a reader
  never sees on screen. Both views now carry what the block shows, evaluated
  against the collection as the export runs, and a spec that does not parse
  exports its error rather than passing for content. A note exported with no
  vault open still exports the spec, since there is nothing to ask.
- A web embed exports as a link. An `![](url)` naming a page rather than an
  image file was written out as an `<img>` pointing at the page, which every
  viewer draws as a broken image, so the card looked as though it had been
  dropped. It now carries the page title and description where the preview
  cache already held them, and the URL where it did not.
- A task-board card exports with what is on it. Only the title and the tick
  survived; the labels, the due date and the description went missing, and the
  columns carried no card counts.
- Images stay inside the page in an export. The stylesheet's width cap was
  written with a doubled percent sign, which the string formatter passes
  through as it stands, so browsers discarded the rule and a wide picture ran
  past the text and off the edge.
- Highlighted text is legible in an export made under a dark theme. The `==`
  background was a fixed pale yellow whatever the theme, and the dark themes
  export near-white body text onto it.
- Selecting text by dragging downward through a block works. The block list
  flicks on a drag with enough vertical travel, and it filters its rows' mouse
  events to do it, so it took the pointer away from the editor as soon as a
  downward drag passed the threshold. Nothing highlighted and the caret landed
  wherever the button came up, which looked like a selection starting in a
  random place; a sideways drag, which the list has no use for, always worked.
  Text under the pointer now holds the drag until the button is released.
  Wheel, scrollbar and edge auto-scrolling are unchanged.
- A task-board card whose title carries a formula lines its checkbox up with
  the title again. A formula makes that line taller than a line of text and
  the words inside it sit lower to make room, so a checkbox pinned to the top
  of the row floated above the title it belongs to.
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
