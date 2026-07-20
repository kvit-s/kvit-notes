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
