# 0004. Cancellation and root switching

**Status:** Accepted, with a named remainder

## Context

Several parts of Kvit Notes run work off the GUI thread: the vault scan, the
directory refresh, global search, note loading and saving, the search-index
reconcile, and diagram layout. All of them can be made obsolete while still in
flight, in two different ways.

The first is ordinary staleness. The user types another character, so the search
that was running is answering a question nobody is asking. The second is a root
switch: the user opens a different notes folder, and every piece of in-flight
work belongs to a collection that is no longer open. The second is the more
dangerous, because a reply that merely arrives late is a wasted result, while a
reply applied to the wrong root corrupts what the user sees or, if it reaches
disk, what they have.

Two properties of Qt's own facilities shaped the answer.

A `QFuture` returned by `QtConcurrent::run()` cannot be cancelled. Calling
`cancel()` marks the future cancelled for bookkeeping while the function runs to
completion, so the caller either waits out work whose result it has already
decided to discard, or abandons a task that goes on holding a pool thread.
`QtConcurrent::mapped()` does better, since `cancel()` stops further items being
scheduled, but it still cannot interrupt an item that has started.

A cancelled future also carries no results, and reading one dereferences
nothing. That was not a theoretical concern: closing or switching a vault during
a directory refresh segfaulted reproducibly, because the handler read result
zero before any generation check could reject it. The same hazard had already
been found and patched in one handler with a local guard while three others went
without.

## Decision

Cancellation is cooperative, staleness is handled by generation, and the two are
kept distinct.

1. **A shared `CancellationToken`.** The starter holds one end and the worker
   holds the other through a shared pointer, and the worker checks it at points
   where stopping is safe and cheap: between files, between directories, before
   a commit. Cancellation is bounded by the distance between two checks rather
   than being immediate, which is the property to keep in mind when placing
   them. The flag only moves from clear to set, and a token is never reused.
2. **Placement stays per subsystem, deliberately.** The primitive is shared;
   where a worker looks at it is not. A walk may stop between any two files and
   lose only unfinished work, whereas a write has exactly one safe point, before
   the commit that renames the temporary over the target.
   `DocumentManager`'s write cancellation is an alias for the shared token, with
   its own placement rule.
3. **A root switch is invalidated at three layers, because no one of them
   suffices.** The generation bump means a result applies only when its
   generation exactly matches the current one, which is the uniform rule. The
   token stops the abandoned work rather than merely ignoring its result, so it
   is not still holding a pool thread when the new vault starts scanning. An
   explicit cancelled flag on the result marks it partial, which matters
   because the directory refresh drives removals from its set of seen notes:
   applying a walk that stopped early would delete every note the walk had not
   reached. The generation check already prevented that, and the flag makes it
   safe by construction rather than by ordering.
4. **Generations stay separate per axis; only the rule is shared.** Search's
   generation is a per-keystroke counter answering whether a reply still matches
   what the user typed, which is a different question from which session a
   result belongs to. Collapsing them into one global counter would burden a
   counter that changes on every keystroke with a lifetime it has no business
   tracking. The exact-equality rule is uniform policy across subsystems; the
   counters are not.
5. **One guard for resultless futures, used by every handler.** The check that
   a future was neither cancelled nor empty lives in a single helper that all
   four async handlers call, rather than being restated wherever someone
   remembered it.
6. **Each subsystem keeps its own staleness counter.** Search submits under a
   monotonic generation, gets it back on completion, and keeps only the latest;
   the generation advances on every input change as well as every submission.
   The collection exposes a revision counter that view models bind to. Diagram
   rendering keys results by source hash and tracks a per-canvas revision.
7. **Persistence gets its own thread pool.** Everything else runs on the global
   pool, where a bulk scan can occupy every thread, and the one operation that
   must not wait behind background work is writing the user's text. The pool is
   small on purpose, because its writes are serialized against each other
   anyway and the point is separation rather than parallelism.

## Consequences

A vault switch during the listing stage blocked for 11 to 16 ms before this work
and 0 to 1 ms after. The directory refresh, which reads and parses every changed
body inside its future, was measured at 1075 ms in its worst observed run before
becoming cancellable. The measurements are reproducible from
`tests/test_asynccancellation.cpp`, whose `reportBlockingCostPerStage` prints
them; the figures above come from a 2000-note vault on a local disk.

The cost is that cancellation is now a contract a worker has to honor. A new
background stage that does not check its token is not cancellable, and nothing
in the type system says so. Placing checks too sparsely makes cancellation slow;
placing one in the wrong spot in a write path makes it unsafe. The header on
`CancellationToken` carries this warning, which is the only enforcement there is.

### What this decision does not cover

Two pieces of the original question were deliberately left rather than
half-built, and they remain open:

- **A bounded queue with an explicit executor model.** What exists is one
  dedicated pool for persistence and the global pool for everything else, which
  is a priority split rather than an executor model: work is submitted without a
  stated policy on queue depth or admission. Building one was judged larger than
  the finding, and half a second scheduling concept beside the one just
  consolidated would be worse than none.
- **An asynchronous root-switch state machine.** Root switching cancels
  in-flight work, but there is no explicit state machine governing the
  transition itself.

Two cancellation idioms exist rather than one, which is worth naming rather than
papering over. The collection's `QtConcurrent` workers use `CancellationToken`,
while the search index's worker threads use `std::atomic_bool`, because
`SearchIndexDb::query` already takes a `std::atomic_bool*` and the sibling read
worker already used that idiom in the same file. Unifying them means changing a
database signature, and that was left as a follow-up rather than done while the
files were open elsewhere.

One related hazard is documented rather than fixed:
`CollectionSearchIndex::revisionOf` blocks the caller behind any running query.
It has no caller anywhere in `src/`, `qml/` or `tests/`, so nothing indicates
what shape a non-blocking version should take, and the header records the
hazard instead of guessing.

## Evidence in the tree

- `src/cancellationtoken.h`: the token, and the reasoning about why Qt's own cancellation is insufficient
- `src/persistencepool.h`: the separate pool for saves and journal writes
- `src/notecollection.cpp`: the shared resultless-future guard and the token checks in the walk and refresh
- `src/documentmanager.h`: write cancellation and its single safe point
- `src/collectionsearchindex.h`, `src/collectionsearch.h`: query generations and the on-screen generation
- `tests/test_asynccancellation.cpp`: the cancellation tests and the blocking-cost measurements
- Commit `2fbdb9d` "Make background work cancellable and stop reading resultless futures"
