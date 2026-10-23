# Making the block editor usable as a message composer

**Addressed to the `kvit-notes` repository.** Written in `kvit-works`, which is
the repository that needs it; nothing here can be done from there, because every
file it names belongs to the editor.

## Why this exists

Kvit Notes is a block-based markdown editor. Kvit Works is a separate,
proprietary application built on top of it: it compiles the editor as a
subdirectory and adds agent sessions, tracks and a project shell around it. One
of the things it draws is a chat with a coding agent — a transcript of turns,
and a box the reader types the next message into.

That box is a plain `TextArea`. It takes markdown, because the transcript is
markdown and the agent reads markdown, but it draws none of it: a heading is a
hash and a word, bold is a word between asterisks. The owner's request was that
the box behave like the editor — that what is typed is drawn as what it means,
and that what reaches the agent is still clean markdown.

Kvit Works cannot do that by embedding `BlockEditor`, and the reason is one
specific thing in the editor's QML registration rather than anything about the
editor's design. This document says exactly what that is and what would have to
change. It was written by reading the editor at the commit Kvit Works
pins today, `c57fd32` ("Take the shared build settings"); every file and line
reference below is to that source.

## What is already in place

Most of the work was done when `BlockEditor` was extracted from `main.qml`. Its
own header says so: "It was declared inline in main.qml … Nothing could
instantiate it, so nothing outside this application could use it, and two
documents could not be shown side by side within it." Four things follow from
that extraction, and all four are already true today.

**The component takes its document from its host.** `BlockEditorSurface` opens
with eight properties naming the objects that hold one document's state —
`blocks`, `selection`, `undoStack`, `search`, `outline`, `heights`, `stats`,
`decorations` — each defaulting to the singleton of the same name. A host that
wants a second document assigns its own.

**No row reaches for a singleton.** Inside `BlockEditor` and everything it
instantiates — the gap cursor, the selection keys, the bars, and every block
delegate — there is not one read of those eight names. Searching that set
returns two matches and both are inside comments (`BlockGapCursor.qml:56`,
`BlockSelectionKeys.qml:237`); the rows reach their document through
`BlockDelegateBase`'s `editor` property instead. The reads that do exist in the
editor's QML are in the window's own chrome — the toolbar, the status bar, the
outline and statistics panels, the shortcut map, the insert and link dialogs,
`main.qml` — which act on the open note and should go on acting on it.

**The host already says where the document lives.** The same file takes
`documentPath`, `assetRoot`, an `assetSink` (an `AssetStore`) and a
`linkResolver` (a `NoteCollection`), each with a default meaning "there is no
collection". A pasted image is written wherever the host points it;
`AssetStore` is stateless — every method takes the root and the note directory
as arguments — so the process-wide instance is fine for a second document.

**Markdown goes in and out without a singleton.** `DocumentSerializer` takes the
model explicitly: `loadIntoModel(model, markdown)` fills one,
`serialize(model)` reads one back. `ReadOnlyDocument.qml` already uses the first
of those on a second `BlockModel` to draw a document that is not the open note,
which is how Kvit Works draws its transcripts today.

## The gap

Of the eight objects a document needs, two can be constructed from QML and six
cannot.

`qmlsingletons.h` registers all eight as singletons — one per window, built by
`AppContext` and wired in eight lines of `AppContext::wire()`. Beside that, two
of the eight carry a second registration as a creatable type, added so a window
could DRAW a document that is not the open note: `BlockModel` as
`DocumentBlocks` and `DocumentSelection` as `DocumentBlockSelection`. (A third
creatable type, `DocumentBlockMarks`, belongs to a drawn surface rather than to
the eight.) The comment above them states the limit plainly: "Nothing
constructed this way is attached to an undo stack or to a file. The surface
never writes to its model, and a caller that wants a document edited opens it
as a note."

So an editor embedded in the chat box would run with its own blocks and its own
selection, and would share the other six with the open note. What that costs,
object by object:

| Object | What it holds | What sharing it does |
|---|---|---|
| `UndoStack` | The note's undo history | Ctrl+Z in the message box undoes an edit to the note, and an edit to the note is undone by a keystroke in the box |
| `DocumentHeights` | Measured row heights, remembered per document so the scrollbar settles | The message's rows are recorded as the note's, and the note's scrollbar is drawn from heights that belong to a different document |
| `DocumentOutline` | The heading tree behind the outline panel, the table-of-contents block and internal links | A heading typed into a message appears in the note's outline |
| `DocumentStats` | Word, character and paragraph counts behind the status bar | The status bar counts the message as part of the note |
| `DocumentSearch` | The query, the match list and the current match | The find bar matches text in the message box as though it were in the note |
| `DocumentDecorations` | Where a linked module draws inside the document, addressed by block index | Kvit Works' own annotation spans, placed on the note by index, are drawn at those indices inside the message box |

Two smaller facts complete the picture. `BlockModel` tolerates having no undo
stack — all sixty uses of `m_undoStack` in `blockmodel.cpp` are guarded — so an
editable second document without undo already works today. And
`BlockModel::setUndoStack`, along with the `setModel` of `DocumentSearch`,
`DocumentOutline`, `DocumentHeights` and `DocumentStats`, is a C++ setter with
no `Q_PROPERTY`, so even once those types are creatable, QML cannot wire them
together. `DocumentSelection` is the
exception and is the model to copy: it carries
`Q_PROPERTY(BlockModel *model READ model WRITE setModel NOTIFY modelChanged)`.

## What would change in kvit-notes

Four pieces of work, in the order they unblock each other.

### 1. Make the six constructible, and the wiring assignable

In `src/qml/qmlsingletons.h`, add a creatable wrapper for each of `UndoStack`,
`DocumentSearch`, `DocumentOutline`, `DocumentHeights`, `DocumentStats` and
`DocumentDecorations`, in the shape `DocumentBlocksForeign` already has. The
names are the editor's to choose; the existing pair suggests a `Document…`
prefix that says "this one belongs to a drawn or embedded document rather than
to the window".

Then add the properties that let QML connect them:

- `Q_PROPERTY(BlockModel *model …)` on `DocumentSearch`, `DocumentOutline`,
  `DocumentHeights` and `DocumentStats`, each delegating to the existing
  `setModel`, exactly as `DocumentSelection` does.
- `Q_PROPERTY(UndoStack *undoStack …)` on `BlockModel`, delegating to the
  existing `setUndoStack`.

Nothing else changes: `AppContext` keeps building the window's own instances and
wiring them the same way, and an editor that assigns none of the eight behaves
as it does now.

### 2. Decide what Enter means

This is the one behavioural conflict, and it is a decision for the editor's
owner rather than a mechanical change. `EditableBlock.qml:2925` handles Return by
creating a block below or splitting the block at the caret. A message box needs
Enter to send and Shift+Enter to start a new line, which is the convention every
chat in the estate follows and the one the current Kvit Works box implements.

Two ways to resolve it, both small:

- a flag on `BlockEditorSurface` — `returnCreatesBlock`, true by default —
  that `handleReturn` consults, with the host taking the keystroke when it is
  false; or
- a signal on the surface that `handleReturn` emits first, letting a host accept
  the event before the block is split.

The first is simpler to reason about. The second lets a host decide per
keystroke, which the composer does not need.

### 3. Give a compact editor somewhere to live

A message box is not a pane. It starts one line tall, grows with what is typed
to a cap of about twelve lines, and then scrolls inside itself; it draws no
gutter, no block handles, no formatting bar, no find bar and no scrollbar; and it
has no typewriter mode. `BlockEditor` already exposes what a host needs to do
this — `readonly property alias listView` at line 62, so a host can bind its
height to `listView.contentHeight` capped, and the floating bars are its own
children that a flag can hide.

Whether that becomes a `CompactEditor.qml` in the editor's own QML, or a set of
flags on `BlockEditor` that a host composes itself, is again the editor's call.
A component in the editor is the better home if a second consumer is likely —
the notes application's own quick-capture window is the obvious one.

### 4. Tests

The regression this opens is silent: a second editor that shares one of the six
does not fail, it corrupts the note's state quietly. Two editors in one window,
each with its own document, is therefore worth testing directly — type into the
second and assert that the first's undo stack, heights, outline, statistics,
search matches and decorations are untouched, and that undo in the second
affects only the second.

## What Kvit Works would do afterwards

`qml/agent/GrowingInput.qml` is the box: it is used by the reply input, the
per-note ask field, the new-session prompt in the sessions sidebar and three
comment composers. Replacing its `TextArea` with the compact editor means:

- filling the document with `DocumentSerializer.loadIntoModel` when a draft or a
  stored comment is read back, and reading it out with
  `DocumentSerializer.serialize` when the message is sent;
- pointing `assetRoot` at the project's `.kvit/pasted` folder, at which point a
  pasted screenshot becomes a real image block in the message rather than the
  attachment tile the box draws today — the tiles and the viewer would be
  reconsidered at that point, not before;
- deleting `src/agent/markdownhighlighter.{h,cpp}`, the 400-line painter that
  currently draws markdown over the plain field, and the `MarkdownHighlighter`
  entry in `CMakeLists.txt`.

## What this is worth, and what it costs

The gain is one editing surface across both applications: a message, a comment
and a note are written the same way, with the same keys, the same lists and
tables, and the same pictures, and Kvit Works stops carrying a second,
inferior renderer of the same markdown.

The cost to watch is that a composer inherits everything a block editor does —
slash menus, drag handles, tables, diagrams, code blocks. Some of that is
welcome in a message to a coding agent and some of it is noise in a two-line
reply, which is why the suppression flags in step 3 are part of the work rather
than an afterthought.

## Open questions for the editor's owner

1. Naming for the six creatable wrappers.
2. Whether the compact composer belongs in the editor's QML or is assembled by
   each host from flags.
3. Which of the editor's features a compact editor should refuse outright rather
   than merely hide.
4. Whether `DocumentDecorations` should take a model at all — it holds entries
   addressed by block index and has no model pointer today, so a second instance
   may be the whole of what it needs.

## What was built

All four pieces are in `kvit-notes` as of 2026-09-21. The answers to the four
questions above, and the one place the plan turned out to be wrong.

**1. Naming.** The creatable wrappers follow the pair that already existed:
`DocumentUndoStack`, `DocumentBlockSearch`, `DocumentBlockOutline`,
`DocumentBlockHeights`, `DocumentBlockStats` and `DocumentBlockDecorations`,
beside `DocumentBlocks`, `DocumentBlockSelection` and `DocumentBlockMarks`.
`DocumentSearch`, `DocumentOutline`, `DocumentHeights` and `DocumentStats` each
gained `Q_PROPERTY(BlockModel *model …)`, and `BlockModel` gained
`Q_PROPERTY(UndoStack *undoStack …)`; its `m_undoStack` became a `QPointer`,
since a stack built in QML beside the model can be destroyed before it.

**2. Where the composer lives.** In the editor, as `qml/CompactEditor.qml`. It
owns all eight objects, hosts one `BlockEditor` with the chrome off, sizes
itself from `listView.contentHeight` up to `maximumLines`, and takes markdown in
and out through `setMarkdown()` and `markdown()`. `BlockEditor` gained the flags
it is composed from — `showFindBar`, `showFormattingBar`, `showScrollBar`,
`contentMargin`, `trailingScrollSpace` — and `BlockEditorSurface` gained the two
the rows have to read: `showGutter` and `returnCreatesBlock`, plus
`paragraphPlaceholder` so a box can say what it is for.

**3. What a compact editor refuses.** Nothing outright. The gutter, the find
bar, the formatting bar, the scrollbar and typewriter mode are off, and every
one of them is a separate flag a host can turn back on. Refusing a block kind
outright would have to be a rule about the kind registry rather than about the
editor, which is a larger change than this needed and one no consumer has asked
for yet.

**4. `DocumentDecorations` takes no model,** for the reason the question gives.
It still has to be a second instance: `BlockEditor` installs its own block list
as the view a module's entries answer geometry from, so an embedded editor
sharing the window's would take those answers away from the note's editor.

**Where the plan was wrong: Enter.** The flag is there and is called
`returnCreatesBlock`, but "the host taking the keystroke when it is false" does
not work. The row's `TextArea` handles Return itself, so a keystroke the row
declines is taken by the text area underneath it and written into the block as a
newline; nothing outside the row ever sees it. The row therefore accepts the key
and emits `BlockEditorSurface.returnPressed(blockIndex)`, which is the second
option the plan offered — so the two are not alternatives, and only the signal
carries the keystroke out.

**Gutter width.** Turning the gutter off needed one more thing than the plan
names. Eleven delegates wrote the strip's reserved width out as the literal
`44`, and five more as `52` (that plus the content inset), so a row with no
strip would still have indented its text and drawn its hover tint past an empty
column. That measurement is now `BlockDelegateBase.gutterInset`, which is 44 or
0. With the gutter on the arithmetic is unchanged, and the notes window renders
byte-identically before and after.

**Tests.** `tests/test_embeddededitor.cpp` (the `EmbeddedEditorTests` CTest
entry, under the `shell` label) creates a compact editor inside the shipped
shell beside the note's own editor and checks the separation object by object:
that none of the eight is the window's, that typing into the box moves none of
the note's undo stack, outline, statistics, search matches, height table or
decoration view, that an undo in the box is an undo of the box alone, and that
two boxes in one window keep separate documents.
