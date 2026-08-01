# Selecting text in blocks that render rather than edit

## What this document is about

Kvit Notes is a block editor: a note is a list of blocks, and each block type
has its own delegate, a QML component that draws it and answers the keyboard
and the pointer. Some of those delegates put the block's text into a text
editor, where the pointer selects it the way it selects text anywhere else.
Others draw the block instead. A web embed draws a card, a collection query
draws the rows its spec matched, a table of contents draws the note's headings
as a list.

Those three blocks used to be pictures of text as far as the pointer was
concerned: nothing on them could be swept, and no part of them could be
copied. They can now be selected character by character and copied, through a
mechanism of their own that sits alongside the two selections the document
already had. This describes what selection does, block by block, how the third
mechanism works and what it copies, and what happens when a selection runs
across one of these blocks from the paragraphs around it.

## The three document selections, and the block-private fourth

**Inside one block's editor.** A block whose delegate hosts a `TextArea` gets
Qt's own selection: press, drag, double-click for a word, Shift+Arrow, Ctrl+A.
That selection belongs to the editor and ends at the block's edges. Copying it
produces markdown for the selected span, so `**bold**` comes out with its
asterisks, because the editor's document positions map back to the block's
markdown (`EditableBlock.qml`, `copySelectionAsMarkdown`).

**Across blocks.** A drag that leaves the block it started in becomes a
document-level range held by `DocumentSelection`
(`src/domain/documentselection.h`): an anchor and a head, each a block plus an
offset into that block's markdown. `qml/CrossBlockTextDrag.qml` turns pointer
travel into that range, and each block's `qml/CrossBlockTextSelection.qml`
paints its own share of it by asking `DocumentSelection.portionForBlock(index)`
and applying that span to its editor. Copying takes `rangeMarkdown()`, which
walks the blocks the range covers: a block fully inside contributes its whole
serialized markdown, and a partial block at either end contributes an inline
fragment (`documentselection.cpp`).

**Whole blocks.** Clicking a block's gutter handle, Ctrl+Clicking to toggle, or
Shift+Clicking to extend selects blocks as units
(`qml/BlockSelectionKeys.qml`). Rather than a text selection, this is the mode
in which blocks are moved, duplicated, indented, deleted, copied or exported as
a group, and every delegate takes part in it without implementing anything, by
reading `DocumentSelection.isBlockSelected(index)` for its tint.

**What one block drew.** A sweep inside a web embed, a query's results or a
table of contents is a fourth thing, held by `qml/RenderedTextSelection.qml`,
one instance per block. It is an anchor and a head, each a piece of rendered
text in that block plus a character offset into what that piece is showing. It
never leaves the block, and it addresses screen text rather than markdown,
which is the whole reason it is separate: a query's rows are computed from
other notes and have no position in this note's markdown at all.

All four are mutually exclusive. Starting a document-level selection of either
kind clears the block-private one, and starting a sweep in a block clears
whatever `DocumentSelection` held.

## Where the text lives, block by block

| Block | Its text on screen | Selectable with the pointer |
|---|---|---|
| Paragraph, headings | `TextArea` once the row is promoted; a plain `Text` in the reading state (`TextBlockDelegate.qml`) | Yes once promoted. A press promotes and places the caret, so the first drag on an untouched row selects nothing |
| Lists, to-do, quote, callout | the shared `TextArea`, always | Yes, and across blocks. A callout's title is a separate `TextField` (`CalloutBlockChrome.qml`), selectable but outside the range |
| Code block | the shared `TextArea`, no wrap, inside the code chrome | Yes, and across blocks |
| Math block | `TextArea` for the TeX while focused (`MathBlock.qml`); a rendered image otherwise | The source while editing. The rendered equation never |
| Mermaid diagram | `TextArea` for the source while focused (`DiagramBlock.qml`); a painted canvas otherwise | The source while editing. The drawn diagram never |
| Table | one `TextArea` moved into whichever cell is live (`TableBlock.qml`); rich-text `Text` per cell otherwise | Inside the live cell, plus whole cells by sweeping a rectangle |
| Task board | one `TextArea` moved into whichever card field is live (`KanbanBlock.qml`); rich-text `Text` otherwise | Inside the live field only |
| Image | the picture, with a caption `TextArea` (`ImageBlock.qml`) | The caption. Placeholder and error lines never |
| Audio/video | `MediaPlayer` with path, state and timecodes as plain `Text` | No |
| Divider | nothing | not applicable |
| **Web embed** | `SelectableText` runs: title, description, host, status | **Yes**, block-private. Button labels are not runs |
| **Collection query** | `SelectableText` per header cell, per result cell and per board card line | **Yes**, block-private. The spec source is separately selectable while editing (`QueryBlock.qml`) |
| **Table of contents** | `SelectableText` per heading entry, plus the card's own header | **Yes**, block-private. This block still has no editor at all |

Two block-private mechanisms therefore exist side by side, and they answer
different questions. The table's rectangle selection sweeps *cells* and copies
their values as a small markdown table, because a table is a grid whose
fragment is still a grid (`TableBlock.qml`, `selectionMarkdown`). The rendered
selection sweeps *characters* and copies the text on screen, because an embed
card is a piece of prose whose fragment is still prose.

## How the rendered selection works

Three pieces, two of them new and shared.

**`qml/SelectableText.qml`** is one line of rendered text. It is a read-only
`TextEdit` dressed as the `Text` it replaces: `Text` lays text out but cannot
say which character is under a point and cannot highlight a span of itself,
and a `TextEdit` does both. The `Text` properties a `TextEdit` lacks are
reproduced rather than dropped, since dropping them would relayout every card:
a single-line run is elided through `TextMetrics`, and a wrapped run with a
`maximumLineCount` is measured in a hidden twin holding the full text and cut
to the last character that fits. The editor inside is `enabled: false`. That is
not decoration — a `TextEdit` accepts the left mouse button whatever it intends
to do with it, and an accepted press is never offered to the handlers behind
it, so an enabled one would swallow every sweep that started on it and every
click on whatever the card draws beside it.

**`qml/RenderedTextSelection.qml`** is the per-block coordinator: it finds the
runs, resolves points to them, paints the span and produces the copy. The runs
are found rather than registered. They are `Repeater` output over results that
get replaced underneath the block, so a list kept between gestures would name
items that no longer exist; the item tree under the card is walked at the start
of every gesture instead. What it finds is ordered by where it sits on screen,
grouped into visual lines top to bottom and then left to right within each
line. That ordering is what makes a sweep across a grid read as one run of
text, and it decides whether two runs are joined by a tab or a newline when the
selection is copied. Each run's rectangle is kept from that walk rather than
measured again: hit testing scans every run and a drag hit-tests on every
pointer move, a query showing its full row window is several hundred cells, and
nothing in the card moves while a button is held. The selection is dropped
outright whenever the runs can be replaced underneath it, which is the other
half of the same problem.

**A passive `PointHandler` on each card** turns pointer travel into calls on
the coordinator. It is passive for the reason `CrossBlockTextDrag.qml` gives
about the block editors: it never takes the press away from the handlers on the
rows below it, and it goes on reporting the pointer after it has left the card.
The rows those cards draw — a heading entry that scrolls to its heading, a
result cell that opens its note — carry `TapHandler`s rather than
`MouseArea`s for the same reason, since a `MouseArea` accepts the press and
fires `onClicked` on release however far the pointer travelled, which would
mean sweeping a row also activated it.

The gesture itself follows the ones already in the tree. A five-pixel travel
gate, the same one the block drag and the cross-block text drag use, separates
a click from a sweep. Press multiplicity sets granularity: a second press
within 400 milliseconds and eight pixels takes the word under the pointer, a
third takes the whole run, and dragging on from either keeps the anchor's whole
word or run selected. Word boundaries use the three classes
`DocumentSelection::wordStart` uses — word characters, whitespace, everything
else — so a double-click on a space takes the run of spaces rather than
nothing. While a sweep is in flight the block list stops flicking, because a
downward drag with enough travel is a selection here and a flick to the list,
and the list wins by filtering its rows' events.

## What the keyboard does over one

The block's focus item calls `handleSelectionKey` first, the way it already
calls `handleContextMenuKey`. Escape drops the selection, Ctrl+C copies it, and
Ctrl+A takes everything the block drew — a second Ctrl+A, with all of it
already selected, falls through to the document's own select-all, which is the
two stages Ctrl+A has inside a paragraph. The query block needed somewhere to
put the keyboard for this: its only other focus target is the spec editor, and
focusing that is what opens it over the results being selected, so a sweep
focuses a separate zero-sized item instead.

A selection is dropped whenever the runs it names can go away underneath it:
the delegate being pooled, a query re-evaluating, an outline change rebuilding
a table of contents, an embed's metadata arriving.

## What the copy contains

The text on screen, as plain text. Runs that share a visual line are joined by
a tab and lines by a newline, so a swept query grid pastes as a grid.

Markdown is not on offer, and could not be. Half of an `![](url)` expression is
nothing, and a query's rows are not in the note to have markdown for. The same
rule covers eliding: a run cut to "Some very long ti…" selects and copies those
characters, because what the reader sees is what the reader gets.

Whole-block copying is unchanged and still reaches further. Every block can be
copied whole from the block menu, which opens from the gutter's menu button, a
right-click in the gutter, or Shift+F10 (`qml/EditorContextMenus.qml`). "Copy
as → Plain text" and "Copy as → HTML" there do not copy the markdown source;
they run the block through `DocumentExporter`, which renders each kind the way
the editor shows it: a collection query becomes its evaluated rows
(`fencekinds.cpp`, `queryPlainText`), a table of contents the note's heading
list (`tableOfContentsPlainText`), a web embed the card's text
(`mediakinds.cpp`, `embedCardPlainText`), a table a column-aligned text grid
rather than pipe markdown (`containerkinds.cpp`).

## A selection that runs across one of these blocks

Unchanged. Dragging from a paragraph above a web embed to a paragraph below it
produces a range covering all three blocks, and the embed joins it as a whole
unit:

- `DocumentSelection.portionForBlock()` reports the embed and a query block
  between the two paragraphs as `{selected: true, full: true}`.
- Both tint as a whole while the drag is live, because each delegate paints a
  block-level tint when it is inside the range. There is no character-level
  highlight from the range, since the range has no offsets into these blocks
  to place one at.
- `rangeMarkdown()`, which is what Ctrl+C copies, contains their **markdown
  source**: `![](https://example.com/page)` for the embed and the whole
  ```` ```query ```` fence for the query.

That is deliberate rather than left over. A document-level range is anchored by
markdown offsets into each block's own content, and these blocks have no screen
text at any such offset, so joining the range as an all-or-nothing unit is the
only thing they can do that is true. Each of them keeps answering
`markdownPositionAt` with 0 and `pointInText` with false, which is what says so.
The alternative, a second kind of range endpoint meaning "an offset into what
this block rendered", would have to be answered by every block that a range
covers rather than by these three alone, and would still not give the range
anything to serialize.

## What is not covered

- **Selecting from a paragraph into a card and out again.** A sweep is
  block-private by construction; a range that crosses these blocks still takes
  them whole.
- **The board view's horizontal drag.** A query rendered as a board puts its
  groups in a `Flickable`, and while that `Flickable` overflows, a sideways
  drag inside it is a flick rather than a sweep. Vertical sweeps and sweeps in
  a board narrow enough not to overflow work normally.
- **Keyboard extension of a rendered selection.** There are no Shift+Arrows
  over one. Ctrl+A is the only way to make one without the pointer.
- **Media blocks.** An audio or video block's path, state and timecodes are
  still plain `Text` and still cannot be selected; there is no reason it could
  not use the same mechanism, only that it has not been asked for.
