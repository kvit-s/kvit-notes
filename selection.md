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

Everything above is about the note the editor has open. Three places in the
application put a *different* note's text on screen: the backup dialog offers
the stored versions of the open note, the backlinks pane shows the lines a
referring note mentions this one on, and search results show snippets.
`qml/ReadOnlyDocument.qml` is the component that draws such a document the way
the editor draws one and lets the pointer sweep across it. The backup dialog's
preview of the version under the cursor uses it; the other two draw plain
`Text`, with the markdown showing as asterisks and nothing in them selectable,
and are listed under "What is not covered" below.

### What it is made of

A surface takes a markdown string and holds three things:

- **Its own `BlockModel`**, filled by `DocumentSerializer::loadIntoModel`.
  `parse` is a pure function of its argument, so the one shared serializer
  serves every surface. No undo stack is attached to this model and no path
  from the view writes to it; a caller that wants a document edited opens it
  as a note.
- **Its own `DocumentSelection`**, pointed at that model through
  `setModel()`. This is the same object the editor uses for a cross-block
  range, so `portionForBlock` tells each row what share of the range to paint
  and `rangeMarkdown()` is what a copy produces: whole blocks serialized with
  their prefixes, fences and ordinals, and a self-contained inline fragment at
  each partially covered end.
- **A row per block** (`qml/ReadOnlyBlock.qml`), whose running text goes
  through `BlockEditorEngine` with `cursorActive` false. That is what hides
  the inline markers, styles the spans from the theme's tokens, resolves
  wiki-links against the open collection and reserves a box for each `$…$`
  span that `qml/InlineMathOverlay.qml` paints the equation into. A code fence
  goes through the same object in verbatim mode and is highlighted by
  language. An image or media block is the exception: its content is a
  markdown expression rather than running text, so it goes to
  `qml/ReadOnlyPicture.qml` instead (see "Pictures" below).

Neither the model nor the selection is a singleton, which is the point: the
editor's `BlockModel` and `DocumentSelection` are the open note, one per
window, and a surface has to be able to exist several times over in the same
window. They reach QML as the creatable types `DocumentBlocks` and
`DocumentBlockSelection` (`src/qml/qmlsingletons.h`), the same
singleton-plus-creatable pair `SettingsStore` already has.

The rows are a `Column` rather than a `ListView`, so a surface sizes to its
content and can sit inside a scrolling area it does not own. Every block is
instantiated, which is acceptable at the sizes these callers have — one stored
version of one note, a handful of context lines. A `Column` places a row it
has just been given at its next polish, which is a frame away, so the surface
calls `forceLayout()` before it answers a geometry question or resolves a
press; without it a surface built and measured in the same turn reports every
row at the top.

### Why the rows are switched off

Each row's text sits in a `TextArea` with `enabled: false`. That is the same
decision `SelectableText.qml` makes and for the same reason: a `TextArea`
accepts the left mouse button whatever it intends to do with it, and an
accepted press is never offered to the handlers behind it, so an enabled one
would swallow every sweep that began on it. Disabling takes the item out of
event delivery and changes nothing about how it draws. The one thing it does
change is the palette a disabled `Control` draws from, where the selection
colour is close enough to the background to be invisible, so the row sets
`selectionColor` and `selectedTextColor` explicitly.

### The gesture, the keyboard, and what a copy contains

`qml/ReadOnlyDocumentDrag.qml` is `CrossBlockTextDrag.qml` for a surface, and
the two differ in one structural way. In the editor each block hosts a real
`TextArea`, so a drag inside one block is Qt's own in-block selection and the
document-level range only takes over once the pointer crosses into another
block. On a surface nothing is editable and no block selects anything by
itself, so the surface's own `DocumentSelection` holds every range from the
first pixel of travel, including one that never leaves the block it started
in. Everything else follows the gestures already in the tree: the five-pixel
travel gate the block drag and the cross-block drag both use, and press
multiplicity for word and whole-block granularity.

A surface usually sits inside a `Flickable` it does not own, and a `Flickable`
takes the grab away from its children once a drag has enough travel, which is
the same conflict the editor resolves by stopping the block list flicking
while a sweep runs. Here the sweep keeps the grab outright
(`preventStealing`), so dragging inside a surface always selects and the pane
around it is scrolled with the wheel or its scrollbar.

The keyboard lands on the surface itself, since every row is switched off and
nothing inside one can hold focus. Ctrl+C copies the selection in every
clipboard flavour, Escape drops it, and Ctrl+A takes the whole surface — a
second Ctrl+A with all of it already selected falls through, which is the two
stages Ctrl+A has inside a paragraph and over a rendered block. A surface is
also a tab stop and draws a focus ring when it holds the keyboard, since a tab
stop that shows nothing when it is reached cannot be used.

A copy is markdown, because there is markdown to copy. This is what separates
a surface from the block-private rendered selection above: a query's rows come
from other notes and have no markdown, whereas a stored version, a referring
note's context and a search snippet all do.

### Pictures

An image block keeps its markdown expression as its content, the way the table
and kanban types keep theirs. A surface that sent every non-verbatim block
through the text engine therefore drew the characters
`![Retention|180](charts/retention.png "Weekly retention")` where the picture
belongs.
`qml/ReadOnlyPicture.qml` is the read-only counterpart of `qml/ImageBlock.qml`:
the same `ImageAssets` parse, the same path resolution and the same remote
consent gate, without the resize handle, the effects popover, the editable
caption and the lightbox, none of which a surface has any way to act on. The
caption is drawn as text and the stored width is honoured, capped at the
pane's width so a picture sized for the editor is not cut off in a preview.
A media block draws a tile naming the file rather than a player, since a
surface cannot play anything.

Resolving a relative path needs to know where the document being drawn lives,
and that is not the open note's directory whenever the document came from
somewhere else: a stored version sits in the backup tree while its pictures
are still written against the note's own folder, and a caller may build a
surface from a string with no file behind it at all. So `baseDir` is a
property of the surface, defaulting to the open note's directory, handed down
to each row.

A remote image is not fetched because a preview drew it. A note is untrusted
input (docs/adr/0003-network-egress-policy.md) and a preview of one is no
different, so the source goes through `EgressPolicy::imageSourceFor` exactly
as the editor's does: empty until the reader approves the origin, with the
same consent tile offering to load it. That tile carries the one control a
surface's rows hold, which is why the rows stack above the sweep area rather
than below it — everything else in a row is inert, so every other press falls
straight through to the sweep. Approving an origin changes the reader's policy
rather than the document, so it leaves the surface as read-only as it was, and
the picture appears in place without the pane around it being rebuilt.

A picture holds no characters, so it takes part in a range the way a divider
does: `markdownPositionAt` answers 0, `DocumentSelection` reports a full
portion for a block the range covers end to end, the row itself is tinted, and
`rangeMarkdown()` contributes the expression. A sweep that crosses a picture
does not stop at it.

### Following a link

Every link on a surface was inert: the rows are switched off and the sweep's
`MouseArea` takes the press. A release that ended no selection is now treated
as a click, so the row under it is asked for
`BlockEditorEngine::linkAtDocumentPosition` and a non-empty answer goes to
`AppActions.requestOpenLink`, which is the editor's own route. Wiki-links come
back resolved against the open collection, which is the same resolver that
styled them.

The gesture rule is the one `qml/SelectableText.qml` and the editor's blocks
settled on: a press that turned into a selection activates nothing. A sweep
ends over whatever it ends over and very often ends over a link, so
`ReadOnlyDocumentDrag` records whether the gesture left a selection behind and
the surface asks that before following anything. A double or triple click
selects on the press, so those activate nothing either.

### How it interacts with the note's own selection

A sweep on a surface clears whatever `DocumentSelection` the note held, and a
document selection starting anywhere clears every surface's. That is the rule
the other mechanisms already follow, applied across the two documents. Two
*surfaces* are not mutually exclusive: a window may hold several, each with a
selection of its own, and nothing in one watches another.

### What a surface does not draw

A divider is a rule, whatever style the block stores, and it joins a range
that crosses it as a whole block the way an embed does. Code fences wrap
rather than scrolling sideways, because a preview pane is narrow and a line
scrolled out of view is a line the reader cannot see. Every other block kind
whose content is verbatim — a `$$` math fence, a pipe table, and the fence
kinds built on a code block such as `kanban`, `toc`, `mermaid` and `query` —
is drawn as its source on a panel rather than as the live thing the editor
draws, since the live thing is interactive and a surface is not. Internal
links (`[text](#slug)`) render as ordinary links rather than being checked
against the outline, because the outline belongs to the open note and this
document is not it, and a click on one opens it as the link it appears to be.

A picture is drawn without the editor's image effects. Rounded corners, the
drop shadow, the border and the stretch override are block attributes rather
than part of the expression, and a surface draws a document rather than a
note's presentation of one.

There is also no way for a caller to set a run of blocks apart with a band or
a glyph, which a diff view would want in order to show which part of a stored
version changed. The backup dialog does not offer a diff, so nothing has asked
for it yet.

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
  only that it has not been asked for. On a surface a media block is a tile
  naming the file, which is all a surface can offer for something it cannot
  play.

- **The backlinks pane and search results.** Both still draw their text as
  plain `Text`, so a `**bold**` phrase in a referring sentence still appears
  with its asterisks and none of it can be copied. `ReadOnlyDocument` is what
  they need and neither has been converted; the backup dialog is the only
  place using it so far.
