# Changelog

All notable changes to Kvit Notes are documented here. The format follows
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/), and the project
uses [Semantic Versioning](https://semver.org/).

## [Unreleased]

### Changed

- macOS packages from release tags and manual `main` packaging runs are now
  Developer ID-signed, notarized and stapled. The packaging job fails instead
  of uploading an ad-hoc artifact when credentials, signing, notarization or
  Gatekeeper verification fails.
- The default gap between editor blocks is now 4 px instead of 8 px. It can
  still be adjusted from 0–40 px under Settings → Typography → Block spacing;
  previously saved choices are left unchanged.
- The caret in the space between blocks now accepts Ctrl+V / Cmd+V. Plain
  lines become paragraphs at that exact seam, structured Clipboard content
  keeps its block types, and paste-as-plain drops formatting; the caret then
  continues at the end of the pasted blocks.
- A collection query's spec editor now opens above its results instead of in
  place of them, the way a diagram and an equation keep their preview while
  their source is being written. The results are the only preview a query has,
  so putting them away for the duration hid the answer to the question being
  asked.

### Added

- The text on a web embed card, in a collection query's results, and in a
  table of contents can now be selected with the pointer and copied. Those
  blocks draw their text rather than putting it in an editor, so it behaved
  like a picture of text: it could not be swept, and the only way to take any
  of it was the block menu's "Copy as → Plain text", which takes all of it.
  A drag now selects a span the way it does in a paragraph, from mid-word in
  one line to mid-word in another, with a double-click taking the word under
  the pointer and a third click the whole line. Ctrl+A takes everything the
  block drew before it takes the document, Ctrl+C copies what is highlighted,
  and Escape drops it. What lands on the clipboard is the text on screen:
  cells that share a line are joined by tabs and lines by newlines, so a
  swept query grid pastes as a grid. Selecting a query row no longer opens the
  note it names, and sweeping a table-of-contents entry no longer jumps to
  that heading. A selection that runs across one of these blocks from the
  paragraphs around it is unchanged: the block joins that range whole and
  still contributes its markdown source.
- A new note names itself from its first block. Notes are created before
  there is anything in them to name them after, so they arrive as "Untitled",
  "Untitled 2" and so on, and a list of those says nothing about what is in
  them. The first block usually does: the note takes that heading, or the
  first 60 characters of the paragraph where there is no heading, as soon as
  the caret leaves the block or the note is saved. It happens once — only a
  note still carrying the automatic name is renamed this way, so a name you
  typed, or one this produced, stays put however the heading is edited later.
  A name already taken, or a first block that is a table, an image or a code
  fence, leaves the note called what it was called.
- A rectangle of table cells can be selected by dragging from one cell to
  another, and copied with Ctrl+C. What lands on the clipboard is a table of
  its own: the selected cells under the header cells of the columns they came
  from, since markdown has no notation for a fragment of a table. Ctrl+X
  copies and empties the rectangle, Backspace or Delete empties it, Escape or
  a click in a cell drops it, and the right-click menu carries the same copy
  and clear commands while cells are selected. A selection could only ever be
  the text inside a single cell before, so there was no way to take part of a
  table anywhere.
- Ctrl+Enter in a table cell makes a block below the table and puts the caret
  in it, the same key the code, equation, diagram and query blocks use where
  their own Enter belongs to their content. A hint under the grid names it
  while a cell is being edited, as those blocks name theirs.
- The table size grid answers the arrow keys: left and right change the number
  of columns, up and down the number of rows, within the 8 by 8 grid. Enter
  inserts the selected size and Escape dismisses the grid, as before. Each
  opening starts from the 3 by 3 default rather than from the last selection.
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

- Enter in a table cell moves down the column, the way it does in a
  spreadsheet and the way Down now does. It ended the edit and left the caret
  on the block before, which meant filling a column was a press of Enter
  followed by a click or a Tab across the whole row. Shift+Enter still breaks
  a line inside the cell.
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

- An equation is drawn at the size of the text around it on a high-resolution
  screen. Formulas are rasterized at the display's device pixel ratio so they
  stay sharp, but a Qt image reports such a bitmap's size in physical pixels,
  and that number was used as the drawn size. Every inline and display formula
  therefore came out at the ratio's multiple of the size it should have had:
  double on a Retina Mac or a 4K screen at the default scaling, where a
  formula overlapped the words beside it and the block below, and half again
  as large on Windows at 125%. The equation blocks, the inline overlay and the
  `\` completion menu's preview glyphs now convert that pixel count back to
  logical units, so a formula matches its text at every ratio and still uses
  the full-resolution bitmap.
- A block that grows just after it is added no longer draws over the blocks
  below it. The note list used to slide the rows under an insertion to
  positions measured from the new row's height at that instant. A pasted
  Mermaid diagram has not rendered its picture or opened its source editor at
  that point, so it could later cover about 160 pixels of the text below it.
  Displaced rows now take their positions directly from the list as delegate
  heights change instead of animating toward stale coordinates.
- A fenced block copied from outside Kvit and pasted into a note becomes the
  block it describes. A ```mermaid diagram used to arrive either as a stack of
  paragraphs with the backticks still in them, or as a single code block
  holding the whole thing, fence markers and all, whenever the source offered
  an HTML flavour of the selection the way every code editor does. Neither is
  what anyone pastes triple backticks meaning to get, so a payload opening one
  is now read from the clipboard's plain text and parsed the way Kvit's own
  copied blocks are, whatever else the source put on the clipboard. The fence
  may be indented, as one under a list item is; the indent is stripped with
  it. Prose with no fence in it still splices in line by line, and a paste
  into a code block still keeps every character, because there a fence is part
  of the listing.
- Ctrl+Enter out of a diagram, an equation, a query or a table leaves the new
  block directly under it. These blocks are a different height while they are
  being edited — a diagram shows its source above its preview, a query puts
  its results away while its spec is written — and the block list positions an
  inserted row against the height its neighbour has at that moment, without
  re-reading it afterwards. Folding the editor away in the same frame as the
  insert left a band of empty space the height of the editor between the block
  and the new one, and nothing reclaimed it: not scrolling, not a forced
  relayout. The block now folds first and the row is inserted a frame later.
- A part of a block that only exists while it is being edited opens where it
  can be seen. A task-board card grows a description field when it is clicked,
  and at the end of a note that field appeared below the bottom of the window:
  scrolling to it was impossible, because the moment the pointer left the card
  the field folded away and the note was short again. The editor now scrolls
  the field into view as it opens and while typing grows it, and it does the
  same for a table row growing around its live cell.
- The note scrolls past its last block, by about a third of the window. The
  end of a note could only ever sit against the bottom edge of the window,
  which is the worst place to work on it — anything a block grows downwards
  there had nowhere to go.
- A table fills the same content column as the blocks around it. Its grid
  stopped 36 pixels short of the right edge a paragraph's text reaches, so a
  table read as indented from the right for no reason, and the rounding of
  the per-column shares could leave it a pixel or two short of that edge even
  once the width was right.
- The arrow keys move around a table. Up and Down walk the column, from the
  header row through the data rows and out of the table into the blocks above
  and below it; Left and Right cross to the neighbouring cell from the two
  ends of a cell's text, wrapping to the next or previous row past the last
  and first column, and the caret enters each cell on the side it came in
  from. Each key belongs to the cell's own text until the caret reaches that
  edge of it, so a cell holding line breaks or a long wrapped value is still
  edited normally. None of the four did anything for the table before: they
  reached the block list's own navigation instead, which moved the view to
  whichever row it still called current, so pressing Down in a table at the
  end of a note scrolled to the top of the note.
- Clicking a cell of a table that already had one live puts the caret in the
  cell that was clicked. The block took the keyboard back off the cell's
  editor immediately after handing it over, so what was typed went nowhere.
- A block that grows when it is inserted is scrolled into view at its full
  height. An inserted table replaces a one-line paragraph with a grid several
  times taller, and the view was positioned before the grid had that height,
  so a table inserted at the foot of the view showed its header row and
  nothing else. The focused row is now followed for a few frames and brought
  back into view as its height settles: fully visible where it fits, header
  row at the top of the view where it does not.
- An insert dialog opened from the slash menu keeps the keyboard. Choosing
  Table, Image, Audio / Video or Web Embed put the caret back into the block
  behind the dialog one tick after it appeared, so keys went to the block
  rather than to the dialog — visibly so on the table size grid, whose arrow
  keys moved the caret instead of the selection.
- A chooser that drops out of a button stays inside the window. On a callout
  at the foot of a long note, the list of kinds ran past the bottom edge and
  was cut off there: of the seven kinds only the first few were visible, and
  the rest could not be reached at all. Qt does not push a popup within its
  window unless it is given a margin, and nine of them had none, so the same
  thing happened to the callout colour dot, the divider style picker, the
  image effects popover, a to-do's due-date calendar, a task-board card's
  label list, the tag suggestions, the replace-all preview and the diagram
  label editor. Each is now moved up rather than clipped, which can overlap
  the block it belongs to. The block menu, the slash menu and the two
  completion menus already did their own edge handling and are unchanged.
- A block equation, a Mermaid diagram and a query spec can be left from the
  keyboard. All three edit a fenced source where Enter is a line break, since
  an `aligned` body, a list of diagram statements and a `from:`/`where:` spec
  each need more than one line, and none of them offered the Ctrl+Enter exit
  the code block has had. The caret could only get out by an arrow key onto an
  existing neighbour, or not at all when the block was the last one in the
  note.
  Ctrl+Enter now commits the source and starts a paragraph below in all three,
  and each names the key in its own bottom-right corner while its editor is
  open, where the code block already names it. In a Mermaid block Ctrl+Enter
  previously forced the preview to re-render, which the 250 ms debounce does by
  itself moments later.
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
