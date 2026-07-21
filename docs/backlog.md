# Backlog

Known gaps between what Kvit Notes does today and what it is specified to do.

This document exists so that the specification in [features.md](../features.md)
can describe intended behavior without also carrying a list of what is unfinished,
and so that a reader can tell the two apart. A specification that mixes in its own
gap list leaves the reader unable to judge whether a paragraph describes shipped
behavior or an intention. See [docs/adr/README.md](adr/README.md) for how the
project's documents divide up.

Entries here are differences from a written specification, established by audit.
An entry being listed does not mean the feature is broken or absent; the sections
below say what does work before saying what is missing.

## Pre-launch implementation follow-ups

Moved verbatim from features.md section 21.9. The audit date and wording are
unchanged from the original. The "three additions" it opens by referring to are
the query block, wiki-links with backlinks, and Mermaid text export, which are
the three subjects of the entries below.

The launch-facing behavior of all three additions is implemented and covered by focused unit
and integration tests. The 2026-07-12 audit found these remaining differences from the
specification they were built to:

- Query blocks currently evaluate synchronously on every relevant content or collection
  revision. The planned 150 ms coalescing timer, `(spec, revision)` result cache, and explicit
  1,000-note / 25 ms performance gate have not been implemented.
- Wiki-link backlink extraction skips fenced code, but does not yet share the formatter's full
  opaque-region rules for inline code and inline/display math. Those literal examples can be
  indexed or rewritten as links even though the editor does not render them as wiki-links.
- Rename-safe rewriting is atomic per file but currently automatic: there is no preflight
  confirmation, modified-stamp conflict check, partial-failure report, open-document undo path,
  or folder-rename target rewrite.
- Bare duplicate wiki targets currently resolve deterministically to the shortest path instead
  of remaining unresolved until the user supplies enough path to make the suffix unique.
- The backlinks integration test covers revision-driven live updates, but not the plan's exact
  external `FileWatcher::feedChange` panel path. The query-block integration test does cover that
  external watcher path.
- Mermaid text export is unit-tested across all five families and exposed in QML, but
  `DiagramCanvas::textDiagram()` checks for a non-empty scene rather than using the stricter
  `sceneCurrent()` guard specified by the plan; the visible action itself is hidden while a
  render is pending or errored.

## What the six Qt Quick failures turned out to be

Recorded 2026-07-20, when the Qt Quick suites were repointed at the QML names
the shell publishes and 284 previously-dead cases started running. Six failed.
Investigated the same day: none was a defect in the application. Five were
defects in the tests and are fixed; the sixth is the suite's own
unreliability.

Three tests asserted contracts the code had deliberately moved on from, which
nobody noticed because the suite could not run:

- `test_z4_blockMenuRecencyPersists` asserted that block-menu recency is
  stored as block-type numbers. It is stored per catalog entry, because five
  catalog entries share `Block.CodeBlock` and recording the type alone brought
  all of them back as plain Code Block. The test asserts entry ids now, and
  still covers the documented path where a settings file written before entry
  ids existed loads its plain type numbers.
- `test_z7_themePersistsAndRestylesShell` opened by asserting that the theme
  is `light` with nothing persisted. It is `system`: a first start follows the
  OS colour scheme. The test sets the theme it needs now; the default has unit
  coverage in `test_theme`, where the settings store is genuinely fresh.
- `test_z8_typographyScalesLiveDelegates` read the settings key
  `Typography.fontSize`. The key is `typography.fontSize`. That capitalisation
  came from the rename that repointed the suites, which rewrote string
  literals as well as identifiers; it damaged ten literals in all, and all ten
  are restored.

Two tests were order-dependent, passing alone and failing after their
predecessors:

- `test_19_editorEngineAttached` asserted that an unedited row instantiates no
  TextArea, using block 0 — which earlier tests click into, and a delegate
  stays promoted once it has been edited. It starts from a fresh document now.
- `test_22_markersHiddenWhenCursorOutside` compared the document text
  immediately after blurring. Reveal transitions are deferred to a clean stack
  by design, so the collapse lands a turn later, and the assertion waits for
  it.

The sixth, `test_v5_shiftClickSelectsRange`, is not a defect in the note list.
With roughly a hundred GUI tests ahead of it, synthesized clicks into the list
are intermittently not delivered: the model holds the right three notes in the
right order with no filter, the click resolves to the right delegate, and the
same clicks select correctly when the test runs alone or in a small group.
Which of the two clicks goes missing varies between runs of the same binary.
That is the input-delivery problem described below rather than something the
note list does wrong.

## The Qt Quick suites are not a merge gate

Neither suite is a merge gate, because both drive a real window with
synthesized key and mouse events, and they need that window to keep keyboard
focus for the whole three-minute run. Anything that takes focus — switching
windows, a notification, another test binary starting — sends the rest of the
run's input somewhere else. Consecutive runs of one binary have given 306, 305
and 306 of 312 passing, and also 198 and 179, and in the low runs essentially
every failure is a keyboard or mouse case while the pure-model cases still
pass, which is the signature of that rather than of a defect.

So a full-run number means little on its own. To judge a case, run it by name
in its own process and leave the window alone:

    QT_QUICK_BACKEND=software ./build/tests/test_integration \
        -input tests/tst_integration.qml IntegrationTests::test_x

Several names can go on one command line, which keeps it to one window. A case
that fails in a full run and passes that way is focus loss; a case that fails
both ways is worth investigating.
