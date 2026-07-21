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

## Six behaviours the Qt Quick suite reports as wrong

Established 2026-07-20, when the Qt Quick suites were repointed at the QML
names the shell publishes. Before that, 284 of IntegrationTests' 312 cases
were dying at the first line that touched a service, so nothing these six say
had been visible. Each fails identically on the tree before the module split
and after it, so they are existing defects rather than anything that work
introduced.

- `test_19_editorEngineAttached`: a plain, unfocused text block instantiates a
  `TextArea`. The lazy-delegate contract says it should not need one until it
  is focused, and the cost of the extra item is the reason that contract
  exists.
- `test_22_markersHiddenWhenCursorOutside`: with the cursor outside the span,
  the document holds `Hello **world**` where display text is expected. The
  reveal transition is not collapsing.
- `test_v5_shiftClickSelectsRange`: shift-clicking three rows apart selects one
  block rather than three.
- `test_z4_blockMenuRecencyPersists`: a persisted block-menu recency value
  reads back as `NaN`.
- `test_z7_themePersistsAndRestylesShell`: a theme saved as `light` is restored
  as `system`.
- `test_z8_typographyScalesLiveDelegates`: a live delegate's size stays 0 after
  a typography change.

Neither Qt Quick suite is a merge gate, because both need a display and both
are unreliable under this machine's compositor: consecutive runs of one binary
have given 306, 305 and 306 passing, and also around 198, with inconsistent
failure sets. The six above are stable across the good runs and reproduce in
isolation, which is what separates them from that noise.
