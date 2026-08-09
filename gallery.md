# Kvit Notes: a feature gallery

Kvit Notes is a markdown block editor. Your notes are ordinary `.md` files in
an ordinary folder; the application renders them as you write and puts the
result back on disk as markdown a person can read.

Every clip below is the application itself, unedited, playing at twice the
speed it was recorded at. Each section says what is happening well enough to
read without the picture.

[Download](README.md#download) · [How to use it](usage.md) ·
[What it is specified to do](features.md)

---

## Drag a node, and the markdown rewrites itself

![A Mermaid flowchart: the fence source is opened and read, a node is dragged to a new position, and the fence reopens with a position comment written at the end of it](screenshots/gallery/mermaid.gif)

A ` ```mermaid ` fence is drawn by Kvit itself, with no browser engine behind
it and no image to export, and the drawing it produces is editable. The clip
opens the fence to show the eight lines of Mermaid source, closes it, drags the
`Commit` node to a new place, and opens the fence again: a
`%% mermaid-flow:pos` comment now sits at the end of it, holding the
coordinates of every node. Because the position lives in a comment, the fence
is still valid Mermaid everywhere else, and a diagram you have arranged by
hand keeps its arrangement in git.

## Markdown syntax reveals itself around the caret

![A paragraph rendered with bold, italic, code and inline math; as the caret moves into each span in turn, that span's markdown markers appear and then disappear again when the caret leaves](screenshots/gallery/livepreview.gif)

The paragraph is stored as markdown and shown rendered. The asterisks around
**bold**, the backticks around `code` and the dollars around a formula are
hidden until the caret moves inside the span they belong to, and they come
back the moment it does, so you edit the real syntax without reading it.
Watch the caret step into each of the four spans in turn. This is one editing
mode rather than two: there is no preview pane to switch to and nothing to
toggle.

## TeX math, rendered as you type

![A sentence being typed into a note; as the closing dollar of an inline formula is typed, the TeX becomes a set formula, and typing continues after it](screenshots/gallery/math.gif)

`$e^{i\pi} + 1 = 0$` is typed into a paragraph and set as a formula as soon as
it closes. The TeX engine and its NewTX and XCharter faces are built into the
application, so a formula appears at typing speed rather than after a round
trip to a server, and nothing you write leaves your machine. An inline formula
sits on the text's own baseline at the size of the prose around it; `$$…$$` on
its own lines makes a centred display equation instead.

## Type `/` and choose what the block becomes

![An empty block; typing a slash opens a grouped catalogue of block kinds, typing two more letters narrows it to the to-do entries, and pressing Enter turns the block into a checkbox item that is then typed into and ticked](screenshots/gallery/palette.gif)

Every block kind the editor has is reachable from one menu. Typing `/` on an
empty block opens the catalogue grouped by kind; typing further letters
filters it; Enter converts the block. The clip picks the to-do item, types a
task into it and ticks the box. Nothing here is a special object: the result
is a `- [x]` line in the markdown file, which is why a note built this way
still reads correctly in any other editor.

## A markdown table, edited like a grid

![A three-row table; clicking a cell opens an editor in place with add-row and add-column chips beneath, Tab walks the caret across cells and on to the next row, and a new row is added and filled in](screenshots/gallery/tables.gif)

A GitHub-flavoured markdown table is drawn as a grid and edited in place.
Clicking a cell opens an editor on that cell alone; Tab walks across the row
and continues onto the next one; the `+ Row` and `+ Column` chips appear only
while a cell is being edited, so a table you are only reading stays clean. The
file underneath keeps its pipe-and-dash form throughout.

## A board whose cards are list items in the file

![A three-column kanban board; a card is dragged from the middle column into the right-hand one and dropped, and a card in the left column is ticked off](screenshots/gallery/kanban.gif)

A ` ```kanban ` fence is drawn as a board. Its columns are `##` headings and
its cards are `- [ ]` task items, with `#labels` and a `📅 date` written the way
a person would write them, so the board is legible as plain markdown. Every
gesture on it rewrites those lines as a single undo step, whether that is
carrying a card to another column, ticking one off, adding a card, renaming a
column or reordering them. The clip moves one card between columns and
finishes another.

## A live table built from front matter across the vault

![A query block showing its specification (a source folder, a filter, a sort) above the table of notes it produced; a note's front matter is then changed by another program, and a fourth row appears in the table without the editor being touched](screenshots/gallery/query.gif)

A ` ```query ` fence is a saved question about the vault's front matter: which
folder to read, which notes to keep, which fields to show, how to sort them.
The clip opens the block to show that specification above the table it
produced. Another program then changes one project's `status:` from `done` to
`active` on disk, the way a script or another editor would, and the row
appears without anything being clicked. A query block can render as a table,
or as a board grouped by one of the fields.

## Link notes by name, and see what links back

![Typing two square brackets in a note opens a picker of the vault's notes, narrowing as more letters are typed; choosing one writes a finished wiki-link, clicking it opens that note, and the backlinks pane lists the note the link came from with the sentence it appears in](screenshots/gallery/wikilinks.gif)

Typing `[[` opens a picker over every note in the vault, filtered as you type;
choosing a row writes the finished `[[Beta]]` link. Clicking a rendered link
opens that note. The View menu's Backlinks pane then answers the question from
the other side: which notes point at the one that is open, and the line each
link appears on. Backlinks are read from the files themselves, so a link
written by any other tool counts.

## Find in the note, then across every note

![The find bar opened over a note with every match tinted and a match counter beside the query; then a query typed into the sidebar's search field, which lists matching lines grouped by note, and clicking one opens that note at the match](screenshots/gallery/search.gif)

Ctrl+F searches the open note, tinting every match and stepping between them,
with case, whole-word and regular-expression switches beside the field. The
sidebar's search field asks the same question of the whole collection: results
come back grouped by note with the matching line under each, and choosing one
opens that note scrolled to the match, with the find bar already holding the
query. The index is built and kept inside the application: nothing is
uploaded and there is no service to sign up for.

## One choice repaints the whole window

![The View menu's Theme submenu; choosing Dark repaints the toolbar, both side panes, the note and its diagram, then Sepia does the same, then back to Light](screenshots/gallery/theme.gif)

Light, dark, sepia and a high-contrast theme, plus one that follows the
desktop. A theme reaches further than the page you are writing on: the
toolbar, the folder tree, the note list, the status bar, the colours code is
highlighted in and the colours a diagram is drawn with all change together,
which is what the clip shows as it cycles through three of them.

## Box art from a chat window, straightened on the way in

![A block of crooked box-drawing characters, with a box whose top edge stops short of its sides and a connector offset from the arrow below it; the block is copied and pasted into a block of its own, and the pasted copy has square boxes and a connector in one column](screenshots/gallery/asciipaste.gif)

Diagrams that come out of a language model are usually a column or two out of
true: a box's top edge stops short of its sides, a connector jogs sideways
between rows. Kvit recognises box-drawing characters as a diagram when they
arrive and squares them up as it stores them. The clip copies a crooked
diagram out of one block and pastes it into another, leaving the original
above the straightened copy so the two can be compared.

Every fix slides a wall or a corner through blank space, so nothing to the
right of it moves and no label text is ever touched. Anything the rules cannot
square up cleanly is stored exactly as it arrived, on the principle that a
diagram left alone is better than one silently mangled.

## Copy any diagram out as text

![A rendered flowchart is hovered, its Copy as text chip is clicked, and the same shape is pasted into a code block below it as box-drawing characters](screenshots/gallery/astext.gif)

Any diagram the application drew can be copied out as box-drawing characters,
for a commit message, a code comment, a terminal, or anywhere a picture cannot
go. The clip hovers the rendered flowchart, clicks **Copy as text**, and pastes
the result into a code block directly underneath, so the drawing and its text
version are on screen together.

The block stays open in the clip because box-drawing characters are also how
Kvit recognises a diagram: leave that block and it renders as a drawing again,
which is the same behaviour that turns box art you paste in from somewhere
else into a diagram you can edit.

## Export a note to PDF, HTML, markdown or text

![The File menu's Export entry opens a dialog offering four formats and a choice of scope; PDF is chosen, a destination is named, and the status bar reports the file that was written](screenshots/gallery/export.gif)

Export takes one note, the notes selected in the note list, or the whole
collection, and writes markdown, a self-contained HTML file, a PDF, or plain
text. The PDF is printed from the same rendering the editor shows, so
formulas, diagrams and tables come out as they look on screen. The clip
exports the open note to PDF and ends on the status bar naming the file.

## Open a single file with no vault around it

![The application opened directly on one markdown file: no folder tree and no note list, the document filling the window, with the file's own path in the status bar; a sentence is added and saved](screenshots/gallery/singlefile.gif)

`kvit-notes note.md` opens that one file with the whole editor intact and
none of the collection chrome: no folder tree, no note list, just the document
across the width of the window and its path in the status bar. Saving writes
that file and nothing else. It is the mode for opening a README from a
terminal, and the status bar offers to make a vault out of the surrounding
folder if you decide you want one.

---

## The rest of it

Fourteen clips are not the whole application, and some of what is missing is
hard to show in a few seconds. Notes live in folders and have tags; there is a
quick switcher, an outline pane, pinning and favourites, templates, import,
word-count goals, focus and typewriter modes, note history you can restore
from, and the whole editor is operable from the keyboard alone.

[usage.md](usage.md) walks through using it and lists the keyboard shortcuts.
[features.md](features.md) is the specification, feature by feature.
[accessibility.md](accessibility.md) covers screen readers, keyboard
operation and the contrast floor.
