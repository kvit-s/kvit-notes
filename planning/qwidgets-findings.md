# QWidget shell experiment findings

Captured 2026-08-09 on Qt 6.10.1, GCC 13.3.0, CMake 3.28.3, Linux,
using the repository's Release-style builds and
`QT_QPA_PLATFORM=offscreen`.

## Verdict

Design (b), a `QListView` with painted resting rows and exactly one live text
editor, is viable. It opens and scrolls the War and Peace corpus without
falling back to a hand-rolled scroll area, preserves the existing hybrid
markdown editor, and produces a deterministic widget-level test surface.
Both performance gates pass.

The branch implements all seven planned stages far enough to answer the
experiment, including the shell, special blocks, structured table/kanban
editing, multi-vault windows, single-instance routing, and install metadata.
It is still an experiment rather than a drop-in product replacement. The
important remaining differences are media controls, fine-grained selection
and accessibility inside three painted rendered-text blocks, QML-only
extension presentation, and platform builds that cannot be executed from
Linux. The tracked coverage matrix at `src/widgets/PARITY.md` records those
boundaries.

## Performance comparison

Both shells used the repository's `PerfCorpus::warAndPeace()` fixture. The
QML figures are the refreshed reference sample from the same checkout. The
widget figures are means from the final 50-run reliability sample, with the
worst observed value called out where useful.

| Measurement | Qt Quick shell | QWidget shell |
|---|---:|---:|
| Note/model load | 53.86 ms | 50.03 ms mean (44.67–54.65) |
| Load through first frame | 217.00 ms | 275.86 ms mean (258.25–304.05) |
| Scroll average | 15.82 ms | 8.60 ms mean (7.90–9.66) |
| Scroll p95 | 18.71 ms | 10.06 ms mean (9.11–11.49) |
| Scroll maximum | 19.53 ms | 15.80 ms worst of 50 runs |

The first-frame result is 1.27 times the QML reference, inside the gate's
rough two-times limit. No widget scroll sample approached the 100 ms stall
limit. The widget test measures synchronous scrollbar updates through event
processing, while the QML test measures `afterFrameEnd` intervals, so these
are gate signals on one machine rather than interchangeable renderer
benchmarks.

The key implementation detail is the height cache. `QListView` requests a
size for every row, but unvisited rows receive a cheap estimate. Exact
`QTextDocument` layout is limited to rows that reach the viewport, and the
2,000-row regression test asserts that fewer than 120 are measured exactly
in a 400-pixel viewport. Widths are bucketed and the vertical scrollbar is
reserved, avoiding repeated invalidation when scrollbar chrome changes the
available width.

## Composition and architecture

`AppContext` composes and opens a vault without a `QQmlEngine`, QML type
registration, context properties, or Qt Quick style initialization. One
hidden presentation dependency did need to move: the service table used to be
populated only by `installContextProperties()`. It is now populated by
`AppContext::wire()`, and `installContextProperties()` only attaches the
already-wired table to an engine. This makes service lookup available to any
linked presentation without changing QML behavior.

The target graph is not as presentation-neutral as the plan initially
assumed. The widget shell reuses both `AppContext` and `BlockEditorEngine`,
which live in `kvit-qml`, so `widgets -> qml` is explicitly allowed by the
layering guard. A production port should extract the composition root,
`BlockEditorEngine`, and `CodeHighlighter` into a shell-support layer whose
name and dependencies do not imply Qt Quick.

Process-global services are owned once by `ProcessServices`; each
`WidgetVaultWindow` owns a separate `AppContext`, model set, and native
window. Canonical-path deduplication, in-place and new-window routing, tray
target selection, global quick capture, recent vaults, window close policy,
and orderly process quit follow the existing QML registry's ownership split.

## Editing, selection, and block kinds

`BlockEditorEngine::attachDocument(QTextDocument *)` worked unchanged. One
`BlockEdit` moves over the current text row; one `StructuredBlockEdit` is
created only for the active table or kanban row. Caret handoff traverses text,
divider, and painted special rows. Prefix conversion, splitting, merging,
type conversion, structural selection operations, drag reorder, and
cross-block copy remain stable by block id.

`DocumentSelection` stays the source of truth for in-block, cross-block, and
whole-block ranges. Painted text rows draw their portion while the live
editor uses an extra selection. The native find bar uses `DocumentSearch` for
case, word, regular-expression, preserve-case, selection-scoped, replace,
and replace-all operations, and the delegate paints matches on resting rows.

Resting renderers cover image, guarded remote media, math, Mermaid, table of
contents, embed, query, table, and kanban content. Table editing includes
cell editing and selection, markdown copy, sorting, alignment, row/column
mutation, and persisted widths. Kanban editing includes cards, details,
labels, due dates, descriptions, column mutation/reordering/collapse,
filtering, and wide-board scrolling.

## Shell, theming, and size

The shell uses native menus, actions, toolbars, status bars, splitters,
dialogs, lists, and standard shortcuts backed by `ShortcutCatalog`. It
includes folders and notes, scopes and sorting, global search, tags, trash,
favorites, recovery, bulk operations, outline, backlinks, find/replace,
quick switching, import/export, templates, backups, settings, links,
document statistics and goals, update status, automatic note titles, and
session state.

Theme changes rebuild one window palette and invalidate painted rows.
Delegates read `Theme` and `Typography` tokens directly; no large stylesheet
tree is re-polished. Native disabled menu items, splitters, focus rings, and
keyboard behavior require substantially less custom presentation logic than
their QML counterparts.

The experiment contains 8,601 lines under `src/widgets` and 1,189 lines of
widget tests plus the capture driver. The 3,206-line native main window is
well below the roughly 8,000 QML lines represented by Stage 3. The document,
special-block, and structured-editor code is also smaller than the related
QML files, but that comparison must be discounted for the explicit
fine-grained media/rendered-text gaps. The full 9,790-line addition is below
the plan's aggregate estimate for a Stage 0–6 port.

Visually, the captured 1280×820 shell is coherent, crisp, and pleasantly
dense. Native menus and splitters feel more predictable, and raster capture
is free of scene-graph artifacts. The result is also more utilitarian than
the QML shell: custom media treatment and the most polished internal special-
block interactions still favor QML. There is no decisive visual winner;
native chrome and deterministic tooling are the clearer widget advantages.

## Accessibility

Standard widget controls expose roles, names, state, focus, and keyboard
interaction without hundreds of per-item declarations. `BlockModel` now
answers `Qt::DisplayRole`, `Qt::AccessibleTextRole`, and
`Qt::AccessibleDescriptionRole`, and tests inspect the accessible row text
and the named gutter controls. The implementation rule is that every
interactive control must be a real widget rather than a painted glyph.

The cost is the predicted one: non-interactive painted content is represented
as a row, not as a tree of accessible children. In particular, the internal
runs in table-of-contents, embed, and query renderings are not character-
selectable or individually exposed. Production parity would require a custom
`QAccessibleInterface` plus `QTextLayout` hit testing, or real transient
widgets for the focused content. The existing QML source guard cannot verify
that; a widget-specific guard would need to enforce real widgets for actions
and accessible model roles for painted rows.

## Tests, build, and packaging

The widget slice contains 28 behavioral test methods (30 QtTest invocations
including setup and cleanup). It covers composition without QML, bounded
widget count, accessibility, hybrid editing, caret movement, selection,
structural clipboard and keyboard operations, table and kanban editing,
special rendering, shell navigation, find/replace, shortcuts, multi-vault
ownership, and the large-corpus gate.

The complete binary ran 50 times under `offscreen`: 50 passed, zero failed,
zero skipped, for 1,500 successful QtTest invocations. Every run executed the
large-corpus test. This is materially more deterministic than the recorded
Qt Quick focus/activation behavior on this machine.

The unchanged service suite passed 72/72 tests. A further 18/18 C++ shell,
multi-vault, single-instance, timing-budget, lint, and source-guard tests
passed. `LayeringGuard` and the full QWidget CTest target pass after a clean
rebuild.

A fresh static Release configuration with `KVIT_WIDGETS=OFF` built the
shipping `kvit-notes` executable and generated no widget targets. With the
option enabled, both applications built; `cmake --install` installed the
widget executable and shared math/license resources, and the installed
widget binary passed `--math-selftest`. This workstation's Qt SDK is not in
the system loader path, so the raw staged Linux install needs that SDK's
library path (or the project's normal deployment packaging) to launch.
Windows resource and macOS bundle declarations are present but were not
cross-compiled from Linux.

## Behavior that should move out of QML

The experiment reinforces the plan's extraction list:

- `NoteSession.qml`
- `CrossBlockTextSelection.qml` and `CrossBlockTextDrag.qml`
- `RenderedTextSelection.qml`
- `BlockSelectionKeys.qml`
- `AppShortcuts.qml`
- `SessionPersistence.qml`
- `MathEntryAssist.qml`

These coordinate models and services rather than merely drawing pixels. The
widget shell had to express the same note-switch transactions, selection
state, history, formatting, shortcut routing, and persistence rules in C++.
Moving those controllers to `src/application` would reduce presentation
duplication even if the shipping QML shell remains permanent.

## Explicit remaining gaps

- Media playback is guarded and shared through one lazy player, but there is
  no QWidget video surface, seek control, or volume control.
- Table-of-contents, embed, and query inner text lacks character selection
  and child-level accessibility.
- QML-only extension slots cannot render inside a QWidget hierarchy.
- Windows and macOS builds and native packaging need validation on those
  operating systems.

None of these invalidates design (b) or the performance, composition, test,
theming, and native-chrome conclusions. They do prevent describing the branch
as complete pixel-and-interaction parity with the shipping application.
