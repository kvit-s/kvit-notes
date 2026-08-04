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

## Pre-launch implementation follow-ups (resolved)

This section originated as features.md section 21.9. A 2026-07-12 audit of the
three then-new additions — the collection query block, wiki-links with
backlinks, and Mermaid text export — recorded six differences from the
specification they were built to. A re-audit on 2026-07-23 against the current
tree found all six implemented; the section is kept, rather than deleted, so a
reader following the features.md pointer sees what those follow-ups were and
where each now lives.

- **Query-block evaluation is coalesced, cached, and budgeted.** `QueryBlock.qml`
  debounces re-evaluation with a 150 ms single-shot timer (`scheduleRefresh` →
  `refreshTimer`, restarted on every content or collection revision).
  `QueryTools` holds a `(body, revision)` result cache that coalesces identical
  requests and prunes entries from superseded revisions, bounded by
  `MaxCacheEntries`/`MaxCacheRows`. `TestQueryData::testEvaluate1000NoteBudget`
  enforces the 1,000-note evaluation budget (expressed in CPU time under the
  timing-budget policy in `tests/timingbudget.h`).
- **Wiki-link scanning shares the formatter's opaque regions.** `WikiLinkScanner`
  excludes fenced code, variable-length inline-code backticks, inline math and
  display math; `inlineMathEnd`/`displayMathEnd` mirror `MarkdownFormatter`'s
  Pandoc-style adjacency rule. Those literals are therefore neither indexed as
  backlinks nor rewritten into links.
- **Renaming is a confirmed plan, not an automatic rewrite.** A rename builds a
  `RenamePlan` (note rename, note move, or folder rename) that the UI confirms
  before applying (`NoteRenameWorkflow.qml`). Each referrer is snapshotted with
  its content hash and modification time, which is what the confirmation shows.
  The rewrite itself is a background pass that reads each referrer immediately
  before writing it and compares again immediately before committing, so a save
  or an outside edit that lands mid-pass leaves that file alone rather than
  being overwritten; the redirect survives, and the next scan offers the file
  again. The open note's rewrite is returned to the editor as one undoable body
  replacement; a pass that cannot finish a file reports it through
  `wikiLinkRewriteIncomplete`; and a folder rename rewrites the qualified links
  beneath the moved prefix.
- **Ambiguous wiki targets stay unresolved.** `WikiLinkIndex` resolution returns
  `{status: unique|ambiguous|missing, candidates}` and leaves a bare target that
  two notes answer unresolved together with its candidates, rather than binding
  the shortest path or auto-creating a note.
- **The backlinks panel is tested through the external watcher.**
  `test_wiki2_backlinksPanelListsAndUpdatesLive` drives `FileWatcher.feedChange`
  for the panel, and `test_zzn_externalChangeConflictBanner` exercises the
  guarded and unguarded external-change paths, so the panel's live update is
  covered on the same `feedChange` route as the query block.
- **Mermaid text export uses the stricter scene guard.**
  `DiagramCanvas::textDiagram()` returns empty unless `sceneCurrent()` holds and
  the scene is non-empty, and the QML action stays hidden while a render is
  pending or errored.

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

## Accessibility: what remains after the 2026-08-04 work

The six phases of [accessibility.md](../accessibility.md) all landed. Every
control the application draws now carries a role, a name and its state; the
choice popups take the keyboard; every token pair holds its WCAG floor in all
four themes; dialogs place and restore focus; the operating system's
high-contrast and reduce-motion preferences are followed; and the chrome has
its own size setting. Three things are deliberately not done, and they are
here so nobody rediscovers them as defects.

**Nobody has heard the application yet.** Every automated check asserts what
is *supplied* to a screen reader — the role, the name, the state, the live
announcement — because the development machine's WSL session has no
accessibility bus, which `tests/test_accessibility.cpp` already notes. What
Narrator, VoiceOver and Orca actually say is unverified. The three tours in
[docs/qa-checklist.md](qa-checklist.md) are how that gets closed, and until
one has been run the honest claim is "the tree is well-formed", not "the
application reads well".

**Two of the three platforms pick up a preference change late.** Windows
announces a change to high contrast or animation with `WM_SETTINGCHANGE`, and
`SystemAppearance` follows it live. macOS and Linux are read on demand
instead: `defaults read` and `gsettings get` answer the question, but neither
is subscribed to, so a change made while Kvit is running is picked up the next
time something calls `refresh()` — which today means opening the Settings
dialog. Subscribing properly would mean an Objective-C++ source for
`NSWorkspace`'s notification (the build has no Objective-C++ at all today) and
a GIO binding or a long-lived `gsettings monitor` process for Linux. Both are
more machinery than the gap costs, and the gap is one dialog opening wide.

**The macOS block-menu chord is unconfirmed.** The block context menu is bound
to `Menu` and `Shift+F10`, and Mac keyboards have no `Menu` key. Whether
`Shift+F10` reaches it on Apple hardware is a checklist item rather than a
known answer; if it does not, that platform needs a chord that exists on its
keyboards.

**The Windows text-scale factor does not seed the interface size.** Windows
has a text-size setting separate from display scaling (Settings →
Accessibility → Text size), which Qt does not apply to a Qt Quick window.
Reading it would be a reasonable default for `interface.fontSize` on a first
run, rather than starting every Windows installation at 12 and making the
person set it a second time. It is listed in accessibility.md Finding 4 as an
optional extra and is not implemented, because seeding a setting from a value
that cannot be read or verified on this machine risks getting the default
wrong for every Windows user at once.
