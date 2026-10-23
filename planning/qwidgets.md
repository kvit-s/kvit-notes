# Reimplementing the shell with QWidgets: an experiment

## What this document is

Kvit Notes is an Obsidian-style block editor for Markdown notes, written in
C++ against Qt 6, with its user interface written in QML and Qt Quick. This
document plans an experiment that answers one question: what would it cost to
replace that QML interface with one built from QWidgets, and would the result
be better or worse than what exists.

The experiment is not a decision to port. It produces three things: a working
vertical slice of the editor built on widgets, a set of measurements against
baselines the repository already records, and a written verdict. All of it
lives on a branch that we expect to delete. The value that survives deletion
is the verdict and the findings, which is why the last section of this plan is
about what to write down before throwing the code away.

Two terms are used throughout with specific meanings. **Shell** means the
presentation layer: everything that draws, accepts input, and routes it to a
service; today that is the 37,000 lines under `qml/` plus the 4,700 lines of
binding code under `src/qml/`. **Services** means the seven C++ libraries
underneath it (`content`, `domain`, `search`, `platform`, `repository`,
`application`, and the binding layer `qml`), which hold the block model, the
markdown parsing and serialization, the vault repository, the search index and
everything else that has no pixels in it.

## The short answer, before the detail

The services are in unusually good shape for this. Of roughly 70,000 lines of
C++, about 65,000 have no dependency on Qt Quick at all, and the pieces that
do are small and mechanical to convert. The single hardest thing in the
product, the hybrid editing engine that reveals and hides markdown markers
around the cursor, already has a supported path to a plain `QTextDocument`
and needs no porting.

The cost is concentrated in two places. The first is the 37,000 lines of QML,
of which about 8,700 lines are four files that would each be a substantial
custom widget. The second is the test suite: 479 test cases are written
against QML item lookup and would all need rewriting, and they are the only
thing that currently pins the interface's behaviour.

Underneath both sits one architectural question that decides whether the
experiment is worth continuing past its first week, described under "The
decision that governs everything" below.

## What the experiment must answer

These are the five risks, ordered by how likely each is to end the experiment.
Everything in the staged plan further down exists to answer one of them.

**1. Can a widget document view scroll a large note?** The document is a list
of blocks with variable heights, mixed kinds, and interactive content inside
individual rows. Qt Quick's `ListView` gives row recycling, an off-screen
cache window, and lazy height resolution; `qml/main.qml` uses all three
(`reuseItems: true`, `cacheBuffer: 240`). `QListView` with non-uniform item
sizes computes its layout eagerly, which means asking the delegate for every
row's height before it can draw the first one. On a note the size of the
existing War and Peace performance corpus that is tens of thousands of text
layouts on open. This is the risk that can kill the approach outright.

**2. Does the hybrid editor survive the move?** The reveal behaviour is
implemented by `BlockEditorEngine` (`src/qml/blockeditorengine.{h,cpp}`),
which attaches to the editor's `QTextDocument` and inserts or removes marker
characters as the cursor moves. It exposes `document` as a
`QQuickTextDocument*` for QML, but the class already has
`attachDocument(QTextDocument*)` as a test hook, and `setDocument` is a
two-line forwarder onto it. A `QTextEdit` hands out exactly that type. The
engine itself should therefore work unchanged; what has to be re-proven is
everything around it: caret handoff between blocks, focus, and the
promote-on-click behaviour.

**3. Can a text selection cross block boundaries?** Today each block's editor
paints its own share of a document-level range by asking
`DocumentSelection::portionForBlock()`. In a widget shell where only one
editor exists at a time and the other blocks are painted, the same range has
to be expressed two ways at once: as a real selection in the live editor, and
as painted highlight rectangles on the drawn rows. The `DocumentSelection`
service does not change; the drawing does.

**4. What does theming cost without property bindings?** `Theme` exposes 66
properties and 64 of the 90 QML files read from it. QML re-evaluates a binding
when a token changes; widgets do not. Every token read becomes either a
`QPalette` entry, a regenerated stylesheet, or a direct read inside a
`paintEvent`. Setting a stylesheet on a window with a large child tree forces a
full style re-polish, and that cost has to be measured rather than assumed.

**5. What happens to accessibility?** The QML tree carries 427 explicit
`Accessible.*` assignments, and `tools/check-accessible-names.py` enforces
that new controls get them. Widgets change the shape of this problem in both
directions. Standard controls report themselves to the accessibility tree
without any code, which removes most of that burden. Custom-painted content
does not exist to a screen reader at all, which is the same failure mode
CLAUDE.md already warns about for a `Rectangle` with a `MouseArea` on it — and
a painted document view makes it the default rather than the exception.

## What the code already gives you

These figures come from the tree as it stands and are what the estimates later
in this document rest on.

| Layer | Lines | Status under a widget shell |
|---|---|---|
| `src/content` | 23,983 | Unchanged |
| `src/domain` | 12,903 | Unchanged |
| `src/repository` | 10,053 | Unchanged |
| `src/application` | 8,713 | Unchanged |
| `src/platform` | 6,341 | Three image providers need widget-side equivalents |
| `src/search` | 2,980 | Unchanged |
| `src/qml` | 4,669 | Replaced, except `BlockEditorEngine` and `CodeHighlighter` |
| `qml/*.qml` | 37,117 | Replaced |

Specific things that make the port cheaper than the QML line count suggests:

- **Only four classes derive from a Qt Quick type.** `DiagramCanvas`
  (`src/content/diagrams/diagramcanvas.h`) is a `QQuickPaintedItem`, meaning
  it already draws with `QPainter` and its `paint()` body transfers to a
  `QWidget::paintEvent` almost verbatim. The other three (`MathImageProvider`,
  `LocalImageProvider` and `RemoteImageProvider`) are image providers whose job
  is to return a `QImage` for a URL, which a widget consumes directly with no
  provider indirection at all.

- **The four models are `QAbstractListModel` subclasses.** `BlockModel`,
  `DocumentOutline`, `NoteListModel` and `FolderTreeModel` work as-is behind
  `QListView` or `QTreeView`. Note that `FolderTreeModel` is a flattened list
  rather than a tree, so it wants a `QListView` with indentation drawn by the
  delegate rather than a `QTreeView`.

- **The roughly 490 `Q_INVOKABLE` methods across the service libraries are
  ordinary public methods.** The macro only matters to QML's meta-object
  lookup; C++ calls them directly. Nothing has to be added or removed for a
  widget caller.

- **`kvit-core` already exists** as an `INTERFACE` library aggregating the
  service libraries, and `AppContext` is already documented as a composition
  root intended to be reused by a binary other than the stock editor. A widget
  executable is exactly that case, so the composition wiring is available
  without copying it.

- **`Qt6::Widgets` is already linked** by `kvit-platform`, `kvit-application`
  and `kvit-qml`, so nothing about the dependency is new.

- **`BlockKindDef`** (`src/domain/blockkinddef.h`) holds all per-kind
  behaviour behind nineteen pure virtuals, including the three text
  projections and the HTML output. A widget delegate asks the same questions
  the QML delegate chooser asks, through `delegateKind()`.

## What has to be rewritten

- **37,117 lines of QML.** The four largest files are where the work is
  concentrated: `EditableBlock.qml` (3,075), `KanbanBlock.qml` (2,627),
  `main.qml` (2,292) and `TableBlock.qml` (1,694).

- **479 interface test cases**, split across `tests/tst_integration.qml`
  (384), `tests/tst_visual.qml` (66) and `tests/tst_uiremediation.qml` (29).
  Each finds items by `objectName` in a QML scene, so none of them transfer.
  The C++ tests under `tests/test_*.cpp` are a different matter: everything
  except `test_shell.cpp`, `test_visual.cpp`, `test_integration.cpp` and
  `test_performance_app.cpp` tests services and runs unchanged.

- **Roughly 3,500 lines of QML that hold behaviour rather than pixels**, in
  about seventeen files. `NoteSession.qml` (411), `RenderedTextSelection.qml`
  (505), `MathEntryAssist.qml` (398), `CrossBlockTextSelection.qml` (383),
  `BlockSelectionKeys.qml` (272), `AppShortcuts.qml` (264) and
  `SessionPersistence.qml` (189) are the largest. These become C++ controller
  classes. Several of them belong in `src/application` regardless of
  which shell is on top, and that observation is worth recording whatever the
  experiment concludes.

- **46 scenarios in `tools/uidriver.cpp`**, which drives the shipped UI for
  screenshots and demo recordings. A widget equivalent is simpler, for the
  reason given under "Verification" below, but it is new code.

- **`tools/check-accessible-names.py`** parses QML source and has no
  equivalent meaning against C++. A widget tree needs a different guard or a
  rule that interactive elements must be real widgets.

## The decision that governs everything

How the document view is built determines whether this is a large but ordinary
port or an open-ended research project. There are four candidate shapes.

**(a) One widget per block, stacked in a `QVBoxLayout` inside a
`QScrollArea`.** This is a direct structural translation of the QML: each
block delegate becomes a widget, and the layout does what the `ListView` did.
It is the fastest way to get pixels on screen and it does not scale, because
a note with tens of thousands of blocks builds as many widget trees,
each containing a `QTextEdit`, and both construction and layout invalidation
become the bottleneck. Useful only as a throwaway.

**(b) `QListView` over `BlockModel`, with a painting delegate and exactly one
live editor widget.** Every block is painted by a `QStyledItemDelegate` that
lays out a `QTextDocument` and calls `drawContents`. One `QTextEdit` exists in
the viewport and is moved to whichever block currently has the caret, with
`BlockEditorEngine` attached to its document. Rows that need real interaction
(a table, a task board, an image with controls) get a real widget through
`QListView::setIndexWidget` while they are the current row, and are painted
otherwise.

**(c) A `QGraphicsView` scene** with `QGraphicsProxyWidget` for interactive
parts. It gives item-level hit testing and transforms, at the cost of awkward
text input handling and a framework Qt has been steering away from for years.

**(d) A hand-rolled `QAbstractScrollArea`** that owns layout, painting, hit
testing, scrolling and the live editor itself. Maximum control and maximum
code.

**Recommendation: (b), with (d) as the named fallback.** Two things support
it. The first is that the application already uses this pattern internally:
`TableBlock.qml` and `KanbanBlock.qml` each keep one `TextArea` and move it
into whichever cell or card field is live, drawing the rest as rich text, and
`TextBlockDelegate.qml` only promotes a paragraph to a real editor when it is
pressed. Generalizing that from a cell to a whole document widens the scope of a
pattern the design already has. The second is that it keeps the widget count independent
of the document size, which is the property option (a) lacks.

The specific thing that could force the fallback to (d) is `QListView`'s
layout model. With `uniformItemSizes` off, the view resolves every row's
height to compute its scroll range, and it does this eagerly rather than as
rows come into view. The mitigation is a height cache keyed by block id,
viewport width and font revision, seeded with a cheap estimate derived from
the block kind and its character count, and corrected as rows are actually
laid out. Whether `QListView` tolerates estimated heights well enough to keep
the scrollbar honest is the first thing the spike measures, and if it does
not, (d) is the answer, because a hand-rolled scroll area can resolve heights
lazily by construction.

## Branch and build layout

Work happens on `experiment/qwidgets`, branched from `main` rather than from
any documentation branch. This plan file itself is written on the current
branch and is not part of the experiment.

New paths, all additive:

```
src/widgets/               the widget shell library, kvit-widgets
src/widgets/main.cpp       the widget executable's entry point
tests/widgets/             QTest-based tests for the widget shell
tools/widgetdriver.cpp     the screenshot/scenario driver, if needed
```

Existing files that must be touched, and the only edits allowed in them:

- **`CMakeLists.txt`**: add `option(KVIT_WIDGETS ... OFF)`, the
  `kvit-widgets` library, and the `kvit-notes-widgets` executable, all inside
  one `if(KVIT_WIDGETS)` block modelled on the existing `KVIT_UI_DRIVER`
  block. Nothing outside that block changes, so a default configure on the
  branch builds exactly what `main` builds.
- **`tools/check-layering.py`**: add `"widgets"` to `DEPENDS` as a peer of
  `"qml"`, with the same set of permitted dependencies. Without this the
  `LayeringGuard` test fails on the first file added.

Deliberately not touched: `resources.qrc` (the widget shell has no QML files,
and `QrcSyncGuard` only checks that `qml/` and the resource list agree),
`qml/`, and everything under `src/qml/` other than reading from it.

One constraint carries over unchanged. `KvitApplication::applyPlatformWorkarounds()`
pins `GALLIUM_DRIVER=llvmpipe` under WSL and must still be called before the
`QApplication` is constructed in `src/widgets/main.cpp`, exactly as
`src/main.cpp` and `tools/uidriver.cpp` do. Widgets draw through Qt's raster
paint engine rather than the scene graph, so the Mesa text corruption
described in CLAUDE.md may not reach them at all, but confirming that is a
finding to record and does not license removing the pin.

## Stage 0: skeleton and composition

The goal is to prove that `AppContext` composes and opens a vault with no QML
engine in the process.

1. Branch: `git checkout -b experiment/qwidgets main`.
2. Add the `KVIT_WIDGETS` option, `kvit-widgets` library and
   `kvit-notes-widgets` executable to `CMakeLists.txt`, linking `kvit-core`,
   `Qt6::Widgets`.
3. Add `"widgets"` to `DEPENDS` in `tools/check-layering.py`.
4. Write `src/widgets/main.cpp`: call `applyPlatformWorkarounds()`, construct
   `QApplication`, construct `AppContext`, call `openSettings()` and
   `applyStartupArguments()`, then show a `QMainWindow` whose status bar
   prints the open note's path and block count.

Do not call `AppContext::installContextProperties()`, `registerQmlTypes()` or
`applyQuickStyle()` from the widget binary; all three are QML-engine
machinery. If any of them turns out to have a side effect the rest of the
composition depends on, that is itself a finding worth writing down, because
it means service state is hiding in the QML binding layer.

**Exit check:** `./build/kvit-notes-widgets /tmp/kvit-demo/vault` opens,
reports a sensible block count, and exits cleanly. `ctest -R LayeringGuard`
passes.

## Stage 1: the spike, and the first gate

This is the stage that decides whether the experiment continues. Scope it to
the block kinds that need no custom chrome: paragraph, the four heading
levels, bullet and numbered lists, to-do, quote, code block, divider and
callout. That is twelve of the twenty-one built-in kinds and it covers the
entire hybrid editing story.

1. **`BlockView : QListView`** bound directly to the window's `BlockModel`.
   Set `ScrollPerPixel` vertical scrolling and disable `uniformItemSizes`.

2. **`BlockDelegate : QStyledItemDelegate`.** In `paint()`, build a
   `QTextDocument` for the block, style it from `Theme` and `Typography` using
   the same tokens the QML delegates read, and draw it with `drawContents()`.
   Read the kind through the model's `delegateKind` role, the same role
   `qml/main.qml`'s `DelegateChooser` watches, so paragraph-to-heading
   conversion does not change the delegate.

3. **The height cache.** A `QHash` keyed by block id holding the laid-out
   height for a given viewport width and font revision. Invalidate on
   `dataChanged`, `modelReset`, viewport resize, and the `Theme` and
   `Typography` change signals. Seed unmeasured rows with an estimate from
   kind plus character count.

4. **`BlockEdit : QTextEdit`**, one instance, a child of the view's viewport.
   On current-row change: position it over the row's rectangle, call
   `engine->attachDocument(edit->document())`, set the markdown, give it
   focus, and skip painting that row in the delegate. On focus loss or row
   change: detach and hide. Reposition on scroll.

5. **Caret handoff.** Up-arrow at the first line of a block moves to the
   previous block's last line at the same horizontal offset, and the mirror
   case downward. `qml/BlockExitBelow.qml` (115 lines) is the reference for
   the rules.

6. **Theme application.** Build a `QPalette` from the `Theme` tokens and apply
   it; regenerate it on `Theme::changed`. The painted rows read tokens
   directly in `paint()` and need no stylesheet at all, which keeps the
   stylesheet cost confined to the chrome added in Stage 3.

**Exit checks and measurements at gate 1.** Open the War and Peace corpus that
`tests/test_performance_warandpeace.cpp` and
`TestPerformanceApp::guiScrollWarAndPeaceFrameSample` already use, and record
time-to-first-frame and scroll frame timings (average, 95th percentile,
maximum) against the numbers that test logs today. Type into a paragraph and
confirm markers reveal and hide. Type `# ` at the start of a paragraph and
confirm the caret survives the conversion. Select across three blocks with the
pointer and confirm the range is at least computed correctly, even if it is
not yet painted.

**The gate.** Continue if time-to-first-frame on the large corpus is within
roughly twice the QML shell's, scroll frame timings show no stall above 100 ms,
and reveal editing behaves. Fall back to design (d) if the failure is
specifically in `QListView`'s eager layout. Stop the experiment if the reveal
engine cannot be driven from a `QTextEdit` without modification, because that
would mean re-implementing the hardest component in the product rather than
reusing it.

## Stage 2: selection and block gestures

1. **In-block selection** comes from `QTextEdit` at no cost.
2. **Cross-block selection.** Port `qml/CrossBlockTextDrag.qml` (162 lines) to
   a mouse-tracking controller on the view that converts pointer travel into
   an anchor and head on `DocumentSelection`. Paint each drawn row's share
   using `portionForBlock()`, and express the live editor's share as a
   `QTextEdit::ExtraSelection`. Copy goes through the existing
   `rangeMarkdown()` and needs no new logic.
3. **Whole-block selection.** `DocumentSelection` stays the source of truth
   for which blocks are selected, with the view's `QItemSelectionModel`
   mirroring it for painting only. Do not let the two disagree about
   ownership.
4. **The gutter.** The insert and drag-handle buttons on hover. Make these
   real `QToolButton`s on the hovered row rather than painted glyphs, for the
   accessibility reason in risk 5. The existing memory note about the hover
   fold-in, where the buttons vanish under the pointer unless the row folds
   the button's own hover into its hovered state, applies identically here.
5. **Block drag to reorder**, with the gap cursor and drop indicator
   (`qml/BlockGapCursor.qml`, 489 lines, is the reference) and edge
   auto-scrolling.

**Exit check:** a sweep from the middle of one paragraph through two blocks
into a third highlights correctly in all three, copies markdown that matches
what the QML shell produces for the same range, and dragging a block between
two others lands it where the indicator showed.

## Stage 3: the shell around the document, and the second gate

This is where widgets are expected to be cheaper than QML, and the stage
should be sized to find out whether that holds.

- `Sidebar.qml` (1,163) → a `QListView` on `FolderTreeModel` with indentation
  drawn by the delegate.
- `NoteListPane.qml` (1,127) → a `QListView` on `NoteListModel`.
- `Toolbar.qml` (616) → `QToolBar` with `QAction`s.
- `EditorStatusBar.qml` (595) → `QStatusBar`.
- `FileMenu.qml`, `ViewMenu.qml`, `EditorContextMenus.qml` (537) →
  `QMenuBar`, `QMenu`, `QAction`. The `DiscoverableMenuItem` requirement
  disappears here: the widget styles draw a disabled menu entry as disabled
  without help, which is precisely what Fusion's Qt Quick style refuses to do.
  Record this as a concrete advantage.
- `PanelSeam.qml` (64) → `QSplitter`, which also removes the hand-written
  drag handling.
- `FindBar.qml` (581), `QuickSwitcher.qml` (252), `SettingsDialog.qml` (793),
  `ExportDialog.qml` (315), `ImportDialog.qml` (328), `ShortcutReference.qml`,
  `TemplateDialog.qml`, `BackupRestoreDialog.qml`, `LinkDialog.qml` → ordinary
  `QDialog` subclasses.
- `AppShortcuts.qml` (264) → `QAction` shortcuts driven from
  `ShortcutCatalog`, which already holds the definitions.

**Gate 2.** By this point the widget shell either feels like a smaller amount
of code doing the same job, or it does not. Compare the lines written against
the QML lines replaced, and record the ratio per area, where chrome and
dialogs are expected to be favourable and the document view unfavourable. This is also the
first point where a fair subjective judgement about the interface is possible,
so write it down while the comparison is fresh.

## Stage 4: the remaining block kinds

Ordered by the ratio of work to information gained.

- **Divider, image, media**: mostly painting and a caption editor.
- **Math** (`MathBlock.qml`, 798): a `QLabel` showing the `QImage` that
  `MathTools` already produces, with a `QTextEdit` for the source while
  focused. The image provider indirection disappears entirely.
- **Mermaid diagram** (`DiagramBlock.qml`, 1,307): a `QWidget` whose
  `paintEvent` calls the same painting `DiagramCanvas::paint()` performs.
- **Table of contents, embed, query**: the three blocks that draw selectable
  rendered text. These need a widget analogue of `SelectableText.qml` and
  `RenderedTextSelection.qml`, roughly 690 lines of QML between them. A
  read-only `QTextEdit` is not the right answer in widgets; a `QTextLayout`
  per run with hit testing on the view is closer, because the character-level
  hit testing that QML lacked on `Text` is directly available from
  `QTextLayout::lineForTextPosition` and `QTextLine::xToCursor`.
- **Table** (`TableBlock.qml`, 1,694): cell painting, one live editor moved
  between cells, and rectangle cell selection with its own markdown copy.
- **Task board** (`KanbanBlock.qml`, 2,627): the largest single file, with
  columns, card drag and drop, and per-card fields. Do this last; if the
  experiment is going to be abandoned it should be abandoned before this.

## Stage 5: accessibility, tests and tooling

1. **Model-driven accessibility.** `QListView` reports rows through
   `QAccessibleTable`, so adding `Qt::AccessibleTextRole` and
   `Qt::AccessibleDescriptionRole` to `BlockModel::data()` gives every block a
   name and description with very little code. This is cheaper than the 427
   per-item assignments in the QML tree.
2. **The gap it leaves.** Anything painted inside a row is invisible to a
   screen reader. Decide a rule, such as requiring every interactive element
   to be a real widget, and write a guard that can check it, or accept the gap explicitly and
   record it as a cost of the approach.
3. **A widget test target** using `QTest::mouseClick` and `QTest::keyClick`
   against real widgets. Port a representative slice of
   `tests/tst_integration.qml`, twenty to thirty cases covering editing,
   selection, block manipulation and navigation, rather than all 384. The
   point is to establish what a ported case costs rather than to reach
   parity.
4. **Screenshots.** `QWidget::grab()` renders through the raster engine with
   no GPU involvement, which removes the entire class of problem the
   `GALLIUM_DRIVER` pin exists for and makes reference PNGs reproducible
   across machines.

## Stage 6: multi-window, platform, packaging

Only if the experiment has survived every gate. `VaultWindow`,
`WindowRegistry` and `WindowRouter` (`src/qml/`) hold the multi-vault window
composition and need widget equivalents; the per-vault and process-global
service split that `qmlsingletons.h` encodes as two macro lists is exactly the
partition the widget shell must preserve, and getting it wrong produces shared
state between vaults rather than a compile error. Windows builds, packaging
and the single-instance handling come after that.

## Verification

What still runs unchanged on the branch: every C++ test in `tests/` that
exercises services rather than the shell, which is the large majority of the
93 test files. Run them as a regression check that nothing in the services was
disturbed.

What the branch adds: a widget test target and, at the gates, the performance
comparison. The baselines already exist and are logged by
`tests/test_performance_app.cpp` as `PERF GUI` lines: first frame, note open,
cold start, and the War and Peace scroll frame sample with average, 95th
percentile and maximum. Capture those numbers from `main` before starting, so
the comparison is against a recorded figure rather than a memory.

One asymmetry is worth stating in advance. CLAUDE.md records that the Qt Quick
suites produce well over a hundred unstable failures on this machine's display
because WSLg drops window activation part-way through, which is why the
authoritative signal is `IntegrationTestsIsolated` running one process per case
under a VNC platform for about eight minutes. Widget tests under
`QT_QPA_PLATFORM=offscreen` do not have that problem: widgets need no scene
graph and no compositor, and activation is synchronous. If the experiment
produces nothing else, a deterministic interface test suite is a real result,
and it should be measured rather than assumed, by running the ported slice
fifty times and counting the failures.

## Estimating the size

These are estimates derived from line counts and the structure described
above rather than measurements. They assume the services are not modified.

| Area | QML replaced | Widget code, estimated | Confidence |
|---|---|---|---|
| Stage 0, skeleton | — | 200–400 | High |
| Document view and editor (Stage 1) | ~4,000 | 2,500–4,000 | Low |
| Selection and gestures (Stage 2) | ~2,500 | 1,500–2,500 | Medium |
| Chrome, dialogs, menus (Stage 3) | ~8,000 | 3,500–5,000 | Medium |
| Remaining block kinds (Stage 4) | ~12,000 | 8,000–12,000 | Low |
| Accessibility, tests, tooling (Stage 5) | ~2,000 tests | 3,000–5,000 | Low |
| Multi-window and platform (Stage 6) | ~2,000 | 1,500–2,500 | Low |

The confidence column matters more than the numbers. Stage 1 is low-confidence
because it depends on which of designs (b) and (d) the measurements force.
Stage 4 is low-confidence because the task board and the table are each large
enough to behave like their own projects. Stage 5 is low-confidence because
nobody has yet ported a single test case and found out what one costs.

The experiment proper, Stages 0 through 3, is the part worth committing to
in advance. It answers all five risks and produces an editor that opens a
vault, edits text blocks, selects across them, and has a working sidebar and
menus. Stages 4 through 6 are a port, and should only begin after a separate
decision.

## What would make us stop

- The reveal engine cannot be driven from a `QTextEdit` without changes to
  `BlockEditorEngine`. Reusing it is the main premise of the whole estimate.
- Neither design (b) nor design (d) can open the large corpus within roughly
  twice the QML shell's time-to-first-frame.
- Cross-block selection cannot be painted on drawn rows and expressed in the
  live editor without visible disagreement between the two.
- Gate 2 shows the widget shell taking more code than the QML it replaced,
  in the chrome and dialogs where widgets are expected to be strongest.
- Accessibility of the document view requires so many real widgets per row
  that the widget count becomes proportional to document size again, which
  removes the reason for choosing design (b).

## What to keep when the branch is deleted

Write these into `planning/qwidgets-findings.md` before deleting anything.

1. The performance comparison, with the actual `PERF GUI` numbers from both
   shells and the design, (b) or (d), that produced the widget figures.
2. Whether `AppContext` composed cleanly with no QML engine, and if not,
   exactly which service turned out to depend on one. That is a defect in the
   current tree regardless of which shell wins.
3. The list of QML files that hold behaviour rather than pixels and would be
   better as C++ controllers in `src/application`. Moving them is worth doing
   on `main` whether or not the widget shell is ever built again.
4. The flakiness measurement from running the ported test slice fifty times
   under `offscreen`, against the known instability of the Qt Quick suites on
   this machine.
5. The accessibility trade: what widgets give for free, what they lose, and
   what rule would have to replace `tools/check-accessible-names.py`.
6. A plain judgement of how the widget interface looked and felt beside the
   QML one, written at gate 2 while both were open on the same screen.
