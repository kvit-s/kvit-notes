# Selecting text in blocks that render rather than edit

## What this document is about

Kvit Notes is a block editor: a note is a list of blocks, and each block type
has its own delegate, a QML component that draws it and answers the keyboard
and the pointer. Some of those delegates put the block's text into a text
editor, where the pointer selects it the way it selects text anywhere else.
Others draw the block instead. A web embed draws a card, a collection query
draws the rows its spec matched, a table of contents draws the note's headings
as a list. As far as the pointer is concerned, the text on those is a picture
of text. It cannot be swept with the mouse, and no part of it can be copied.

This describes what selection does today, block by block, what happens when a
selection runs across a block whose text cannot be selected, and what can be
copied out of those blocks by other means. It ends with the questions anyone
adding selection to them would have to answer. Nothing here is a plan. It is
the ground a plan would start from.

## The three selections

**Inside one block's editor.** A block whose delegate hosts a `TextArea` gets
Qt's own selection: press, drag, double-click for a word, Shift+Arrow, Ctrl+A.
That selection belongs to the editor and ends at the block's edges. Copying it
produces markdown for the selected span, so `**bold**` comes out with its
asterisks, because the editor's document positions map back to the block's
markdown (`EditableBlock.qml:1879`, `copySelectionAsMarkdown`).

**Across blocks.** A drag that leaves the block it started in becomes a
document-level range held by `DocumentSelection`
(`src/domain/documentselection.h:78`): an anchor and a head, each a block plus
an offset into that block's markdown. `qml/CrossBlockTextDrag.qml` turns
pointer travel into that range, and each block's
`qml/CrossBlockTextSelection.qml` paints its own share of it by asking
`DocumentSelection.portionForBlock(index)` and applying that span to its
editor. Copying takes `rangeMarkdown()`, which walks the blocks the range
covers: a block fully inside contributes its whole serialized markdown, and a
partial block at either end contributes an inline fragment
(`documentselection.cpp:451`).

**Whole blocks.** Clicking a block's gutter handle, Ctrl+Clicking to toggle, or
Shift+Clicking to extend selects blocks as units
(`qml/BlockSelectionKeys.qml:101`). This is not a text selection. It is the
mode in which blocks are moved, duplicated, indented, deleted, copied or
exported as a group, and every delegate takes part in it without implementing
anything, by reading `DocumentSelection.isBlockSelected(index)` for its tint.
The two kinds are mutually exclusive: starting one clears the other.

## What decides whether a block answers a text selection

`qml/BlockDelegateBase.qml:66` declares the functions the shell asks a row when
it is resolving a pointer position or a caret movement into text:
`markdownPositionAt`, `pointInText`, `lineStepPosition`, `entryPositionAtX`,
`xAtMarkdown`, and `reapplySelectionPortion` for repainting a range's share
after a drag. The base gives each a neutral answer, and that neutral answer is
what every non-prose delegate keeps:

```
TocBlock.qml:75      function markdownPositionAt(sceneX, sceneY) { return 0 }
TocBlock.qml:76      function pointInText(sceneX, sceneY) { return false }
QueryBlock.qml:139   function markdownPositionAt(sceneX, sceneY) { return 0 }
QueryBlock.qml:140   function pointInText(sceneX, sceneY) { return false }
EmbedBlock.qml:161   function markdownPositionAt(sceneX, sceneY) { return 0 }
EmbedBlock.qml:162   function pointInText(sceneX, sceneY) { return false }
```

The same pair appears in `ImageBlock.qml:133`, `MediaBlock.qml:122`,
`KanbanBlock.qml:539`, `DividerDelegate.qml:74`, `MathBlock.qml:131`,
`DiagramBlock.qml:71` and `TableBlock.qml:288`. Two files implement them
against a real editor: `EditableBlock.qml:602`, the component behind
paragraphs, the four heading levels, bulleted and numbered list items, to-dos,
quotes, callouts and code blocks, and `TextBlockDelegate.qml:248`, which
forwards to the editor it promotes.

So the dividing line is not which blocks contain text. It is which blocks put
their text in an editor.

A second limit follows from where a drag can begin. `beginPress` is called from
one place, the passive `PointHandler` inside the shared block `TextArea`
(`EditableBlock.qml:2196`), so a cross-block selection can only start in a
block that has an editor. It can pass over anything.

## Where the text lives, block by block

| Block | Its text on screen | Selectable with the pointer |
|---|---|---|
| Paragraph, headings | `TextArea` once the row is promoted; a plain `Text` in the reading state (`TextBlockDelegate.qml:407`) | Yes once promoted. A press promotes and places the caret, so the first drag on an untouched row selects nothing |
| Lists, to-do, quote, callout | the shared `TextArea`, always | Yes, and across blocks. A callout's title is a separate `TextField` (`CalloutBlockChrome.qml:155`), selectable but outside the range |
| Code block | the shared `TextArea`, no wrap, inside the code chrome | Yes, and across blocks |
| Math block | `TextArea` for the TeX while focused (`MathBlock.qml:398`); a rendered image otherwise (`MathBlock.qml:335`) | The source while editing. The rendered equation never |
| Mermaid diagram | `TextArea` for the source while focused (`DiagramBlock.qml:786`); a painted canvas otherwise (`DiagramBlock.qml:325`) | The source while editing. The drawn diagram never |
| Table | one `TextArea` moved into whichever cell is live (`TableBlock.qml:1203`); rich-text `Text` per cell otherwise | Inside the live cell, plus whole cells by sweeping a rectangle |
| Task board | one `TextArea` moved into whichever card field is live (`KanbanBlock.qml:1912`); rich-text `Text` otherwise | Inside the live field only |
| Image | the picture, with a caption `TextArea` (`ImageBlock.qml:642`) | The caption. Placeholder and error lines never |
| Audio/video | `MediaPlayer` with path, state and timecodes as plain `Text` | No |
| Divider | nothing | not applicable |
| **Web embed** | `Text` items: title, description, host, status, button labels | **No** |
| **Collection query** | `Text` per header cell, per result cell and per board card line | **No.** The spec source is selectable while editing (`QueryBlock.qml:573`), the results never |
| **Table of contents** | `Text` per heading entry, plus header and notice | **No.** This block has no editor at all |

The table's rectangle selection is the one place where something other than an
editor holds a selection. Dragging from one cell to another selects the
rectangle between them and Ctrl+C copies it as a small table
(`TableBlock.qml:548`). It is a per-block mechanism with its own state rather
than part of the document-level range, and it copies cell values rather than a
span of text.

## A selection that runs across one of these blocks

It is neither blocked nor empty. Dragging from a paragraph above a web embed to
a paragraph below it produces a range covering all three blocks. Measured in
the running application:

- `DocumentSelection.portionForBlock()` reports the embed and a query block
  between the two paragraphs as `{selected: true, full: true}`, so the range
  covers them entirely.
- Both tint as a whole while the drag is live, because each delegate paints a
  block-level tint when it is inside the range. There is no character-level
  highlight, since there is no text to highlight against.
- `rangeMarkdown()`, which is what Ctrl+C copies, contains their **markdown
  source**: `![](https://example.com/page)` for the embed and the whole
  ```` ```query ```` fence for the query. It does not contain the card's title
  or the rows the query matched.

So the visible text of those blocks cannot be copied by selecting it, and a
selection that passes over them silently substitutes their source. For a reader
copying a passage out of a note, the paragraphs arrive as expected and the
embed arrives as an image expression.

## What can be copied out of them today

Every block, whatever it draws, can be copied whole from the block menu, which
opens from the gutter's menu button, a right-click in the gutter, or Shift+F10
(`qml/EditorContextMenus.qml:280`). That menu offers Copy, "Copy as → Markdown
/ Plain text / HTML", and Export.

The interesting asymmetry is in what those entries produce. "Copy as → Plain
text" and "Copy as → HTML" do not copy the markdown source. They run the block
through `DocumentExporter`, which renders each kind the way the editor shows
it, and those renderings already exist:

- a collection query becomes its evaluated rows,
  `services->queryPlainText(state.content)` (`fencekinds.cpp:370`),
- a table of contents becomes the note's heading list,
  `services->tableOfContentsPlainText()` (`fencekinds.cpp:226`),
- a web embed becomes the card's text,
  `services->embedCardPlainText(path, alt, caption)` (`mediakinds.cpp:390`),
- a table becomes a column-aligned text grid rather than pipe markdown
  (`containerkinds.cpp:283`).

The application can already turn each of these blocks into the text a reader
sees. What it cannot do is let the reader point at part of that text and take
only that part. The whole block, through a menu, is the only route.

Two blocks carry copy affordances of their own. A code block has a Copy button
in its header (`CodeBlockChrome.qml:170`), and a Mermaid diagram has two chips,
Copy for the source and "Copy as text" for the rendered diagram as
box-drawing characters (`DiagramBlock.qml:714`). A web embed, a query, a table
of contents, a task board and a math block have none: those five files write to
the clipboard nowhere at all.

## What adding selection to them would have to answer

These are the decisions rather than a proposal.

**What is being selected.** A query's results are a grid, a table of contents
is a list of links, an embed card is a title with a description. Sweeping them
could mean selecting a run of text across the whole card, or selecting cells
the way the table's rectangle selection does. The three blocks do not obviously
want the same answer, and the answer decides everything below.

**Whether it joins the document-level range.** That range is anchored by
markdown offsets into each block's own content, and a query's result text has
no markdown offsets: it is not in the note, it is computed from other notes.
Either such a block joins the range as an all-or-nothing unit, roughly what
happens now, and keeps a private selection for partial copies, or the range
needs a second kind of endpoint meaning "an offset into what this block
rendered", which every block in the range would then have to answer.

**What the copy contains.** For a partial selection the source markdown is no
use, since half of an `![](url)` expression is nothing. The rendered text is
the only sensible payload, which points back at the exporter renderings above
and raises the question of what the markdown flavour of such a copy should be.

**How it is drawn.** A range's share is painted today by handing it to the
block's editor. A block with no editor needs its own highlight, so the
delegates that draw text as `Text` items would need either selectable text
items or a highlight layer positioned from the text's own line metrics. There
is no precedent for the first in this codebase: `qml/` contains no read-only
`TextEdit`, and the one read-only path in `EditableBlock.qml:2098` is
unreachable, because the flag that would enable it is only ever set false
(`TextBlockDelegate.qml:607`).

**What it costs at scale.** A query block renders a window of rows out of a
result set that can be large, and a table of contents follows a document's
headings live. Anything that gives those a per-character selection model has to
survive their contents being replaced underneath it.
