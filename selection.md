# Selecting text that is drawn rather than edited

## What this document is about

Kvit Notes is a block editor: a note is a list of blocks, and each block type
has its own delegate, a QML component that draws it and answers the keyboard
and the pointer. Some of those delegates put the block's text into a text
editor, where the pointer selects it the way it selects text anywhere else.
Others draw the block instead. A web embed draws a card, a collection query
draws the rows its spec matched, a table of contents draws the note's headings
as a list.

Those three blocks are selected character by character and copied through a
mechanism of their own that sits alongside the two selections the document
already had. This describes what selection does, block by block, how that
mechanism works and what it copies, and what happens when a selection runs
across one of these blocks from the paragraphs around it.

The last section is about a different case with the same shape: a markdown
document drawn somewhere other than the editor pane, such as a stored version
of the open note or a referring note's context, which is a second document
with a selection of its own rather than a block of this one.

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

A fifth arrangement exists outside the editor pane entirely, described under
"A document drawn read-only" below: a second markdown document, drawn as
blocks with a `DocumentSelection` of its own over it. It is the cross-block
mechanism above, pointed at a document that is not the open note.

## Where the text lives, block by block

| Block | Its text on screen | Selectable with the pointer |
|---|---|---|
| Paragraph, headings | `TextArea` once the row is promoted; a plain `Text` in the reading state (`TextBlockDelegate.qml`) | Yes, and across blocks. A press promotes the row; the `TextArea` that replaces the `Text` never saw that press, so the row keeps it and selects between the press and the pointer itself, reporting the drag to the cross-block coordinator as the `TextArea`'s own observer would |
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
| **Collection query** | `SelectableText` per header cell, per result cell and per board card line | **Yes**, block-private, whether or not the spec editor is open above them. The spec source is separately selectable in its own editor (`QueryBlock.qml`) |
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
The rows those cards draw carry `TapHandler`s rather than `MouseArea`s for the
same reason: a heading entry scrolls to its heading and a result cell opens its
note, and a `MouseArea` accepts the press and
fires `onClicked` on release however far the pointer travelled, which would
mean sweeping a row also activated it.

One `MouseArea` per block cannot be converted, the delegate-wide catcher behind
the card that handles Ctrl+Click and Shift+Click block selection, so those three
ask the coordinator's `suppressClick` before acting. Without that, a sweep in a
query ended by opening the spec editor, because that catcher's answer to a click
is to focus the spec, and a Ctrl+drag across a table of contents ended by
toggling the block's selection.

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

## A document drawn read-only

Everything above is about the note the editor has open. Several places put a
*different* document on screen: the backup dialog offers the stored versions of
the open note, and an application built on this one draws transcripts.
`qml/DocumentView.qml` is the component for that, and what it is made of is the
whole of the design: **one `BlockEditor` with `readOnly` set, over a document
the surface owns.**

### Why the editor draws it and not something else

The alternative was a second renderer, and this repository had one for a year.
A second renderer draws a subset. Block kinds reach the screen through
`BlockKindRegistry::delegateChoices()`, so anything that does not go through
the registry needs a hardcoded list of the kinds somebody remembered — and the
kinds most easily forgotten are the ones that are not a block *type* at all. A
task board, a table of contents, a Mermaid diagram and a collection query are
each stored as a `Block::CodeBlock` with a language, so a renderer switching on
the type drew all four as the source inside their fence, a pipe table as its
pipe characters, and nothing whatever for a kind a linked module registered.

Drawing with the editor, a document drawn here is the document as the editor
draws it, including kinds that did not exist when the component was written.
`tests/test_documentview.cpp` asserts that block by block, by which delegate
drew each row.

### What read-only means

`BlockEditorSurface.readOnly` is the flag, and every delegate reads it through
`BlockDelegateBase.readOnly`. The rule is that the surface still reads and
never writes:

- **The text areas hold their text with `readOnly`, not with `enabled: false`.**
  That is the one decision that differs from the renderer this replaces, and it
  follows from where the selection coordinator sits. The old surface put its
  sweep *above* the rows, so the rows had to be out of event delivery entirely
  for a press to reach it; the editor puts the coordinator *inside* the rows —
  each `TextArea` hosts a passive `PointHandler` reporting to
  `qml/CrossBlockTextDrag.qml` — so a read-only area keeps drag-selection,
  Ctrl+C and the cross-block range working exactly as they do in the note.
- **The keys that read pass; the rest are refused where they are raised.**
  `BlockDelegateBase.readOnlyKeyPasses()` is the list: the arrows, Home, End,
  the page keys, Escape, Ctrl+A and Ctrl+C. Everything else is swallowed by the
  row, which matters beyond the document — Ctrl+Z reaches `AppActions` and the
  window's undo stack, so an ungated one would undo an edit to the *open note*
  from a row of some other document. Tab and Backtab are the exception and are
  left for the focus chain, since swallowing them would leave the keyboard no
  way out of the row.
- **Enter is gated twice.** Qt emits the key-specific signals before the
  general one, so a gate in `Keys.onPressed` alone never sees `Return`;
  `handleReturn` refuses it again, and swallows it rather than ignoring it,
  because an ignored Return goes to the text area underneath and is written
  into the block as a line break.
- **Ctrl+A selects the whole document as text** rather than as blocks. Block
  selection is a mode whose keys are commands — delete, duplicate, indent,
  paste — so a read-only surface never enters it, and what Ctrl+C then copies
  is the whole document as markdown.
- **The pointer does not light a block.** The hover tint marks the block the
  gutter and the block menu act on, and a read-only surface has neither, so no
  delegate draws it there, and a plain paragraph's row does not track the
  pointer at all. Drawn anyway, it lit each block in turn as the document
  scrolled under a resting pointer.
- **Every editing affordance is gone rather than inert.** No gutter strip and
  so no insert, delete, drag handle or block menu; no gap cursor, no drop area,
  no formatting bar, no find bar, no scrollbar and no typewriter mode. A
  to-do's checkbox is still drawn and still published to a screen reader with
  its state, and is not pressable. The pickers on a callout, a code fence, a
  picture and a diagram do not open, and the source editors behind a formula, a
  diagram and a query are reachable by keyboard and not writable, so their
  source can still be selected and copied.

### Sizing

Width comes from the container. Height is one of two things, chosen by
`growsWithDocument`:

- **True, the default: the surface is as tall as its document**, so it sits
  inside a scrolling area it does not own. A `ListView` whose height is its
  own `contentHeight` builds every row, since every row is inside it, so this
  suits a short document, or one among other content that the host scrolls
  together, and is slow for a long one.
- **False: the surface is as tall as its host makes it and scrolls the
  document itself**, with the note editor's own list, scroll bar and wheel
  handling. Only the rows on screen are built, and a row scrolled away is
  recycled, as in the note. The backup dialog's preview is drawn this way.
  `blockItem()` answers null for a block that is not on screen, and a
  decoration's rectangles are empty for one.

The difference for a long document, measured offscreen over 1,237 blocks of
this repository's own documentation, most of whose paragraphs carry inline
code:

| | Open | Rows built | Wheel step, median |
|---|---|---|---|
| Grows with the document | 3.8 s | 1,237 | 16.5 ms |
| Scrolls it | 0.18 s | 10–24 | 1.7 ms |

`forceLayout()` is still there for the same reason it was: a list places a row
it has just been given at its next polish, so a surface built and measured in
the same turn would report every row at the top.

### Marked ranges

A caller often knows something about part of the document it asked to be drawn:
which characters differ from the note as it stands, which phrase a search hit
fell on, which passage a panel beside the surface is about. Putting that into
the markdown is not an option — the surface exists to show a document
faithfully, and its `blockMarkdown()` and its clipboard are expected to give
back what was handed in.

The channel is `decorations`, the surface's own `DocumentDecorations`, which is
the same seam a linked module marks the *open note* through and takes the same
addressing: `addSpan(owner, block, start, length, style, colour)`, where
`start` and `length` are in the block's display text — the text with the inline
markers taken out, which is what a search hit and a module's span already use.
A span paints as a wash behind the characters, an outline around them (one box
per visual line), or both, and `spanRects(id)` says where it was drawn, so a
caller can anchor something beside the marked words. The registry is the
surface's own, so two surfaces in one window mark their own documents and
neither touches the note's.

### What the backup dialog marks

The consumer in this repository is the backup dialog. It draws a stored version
of the open note and washes the part of it the note no longer has, because two
timestamps tell two edits of the same afternoon apart only for a reader who
remembers what they changed — which is what they came to the dialog having
forgotten.

The comparison is `DocumentCompare::changedRanges(markdown, baseline)`
(src/domain/documentcompare.h), a pure function of two markdown strings that
answers in the coordinates a span is addressed in. Both strings are parsed into
blocks and the two block sequences are aligned by a longest common subsequence
of their per-block keys, so a paragraph inserted into either document shifts
nothing after it; a positional comparison would report the whole rest of the
document as changed. The common prefix and the common suffix of the two
sequences come off before any alignment work, which is what keeps the cost
proportional to the edit rather than to the note.

Per block, then: a block present in both unchanged contributes nothing; one
that corresponds to a block in the baseline but differs contributes the run
between their common prefix and their common suffix, so "the second draft"
against "the final draft" marks the word "second"; one whose words are the same
but whose kind, indent, to-do tick or fence language changed contributes its
whole display text, since no run of characters can say "this line is not the
line you have"; and one with no counterpart at all contributes the whole of
itself.

Two cases contribute nothing. A block the baseline only added to — the reader
wrote another sentence and changed nothing else — has no character of its own
that differs. And a divider or a picture holds no text a character range can
address, so a changed picture is not marked; the blocks around it still are.

The wash is `Theme.changedTextBackground`, its own token rather than a reuse of
the search tint: the two answer different questions, and the theme suite holds
it to the same contrast floor against body text that the search tint is held
to. A line under the pane says what the shading means, and appears only when
something is shaded.

### How it interacts with the note's own selection

A sweep on a surface clears whatever `DocumentSelection` the note held, and a
document selection starting anywhere clears every surface's. That is the rule
the other mechanisms already follow, applied across the two documents. Two
*surfaces* are not mutually exclusive: a window may hold several, each with a
selection of its own, and nothing in one watches another.

### The renderer this replaces is still in the tree

`qml/ReadOnlyDocument.qml` and the four files only it uses —
`ReadOnlyBlock.qml`, `ReadOnlyPicture.qml`, `ReadOnlyEquation.qml`,
`ReadOnlyDocumentDrag.qml` — are the second renderer described above, together
with `DocumentBlockMarks` (src/application/documentblockmarks.h), the
per-surface mark registry that `decorations` replaces. Nothing in this
repository draws with them any more. They are still here because the private
`kvit-notes-pro` tree draws its transcripts with `ReadOnlyDocument`, and they
go once it has been moved to `DocumentView`.

`qml/ReadOnlyTextFile.qml` is unrelated and stays: it is the viewer for a
plain-text file that is not markdown at all.

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
- **Media blocks in the editor.** An audio or video block's path, state and
  timecodes are still plain `Text` in the editor's own delegate and still
  cannot be selected; there is no reason it could not use the same mechanism,
  only that it has not been asked for.

- **The backlinks pane and search results.** Both still draw their text as
  plain `Text`, so a `**bold**` phrase in a referring sentence still appears
  with its asterisks and none of it can be copied. `DocumentView` is what they
  need and neither has been converted; the backup dialog is the only place
  drawing a document this way so far.
