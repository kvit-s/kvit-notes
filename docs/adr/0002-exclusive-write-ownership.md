# 0002. One writer owns the open note

**Status:** Accepted

## Context

Two objects in Kvit Notes can legitimately want to write a note file.
`DocumentManager` holds the open document: the block model, the undo stack, the
dirty flag and the save paths. `NoteCollection` owns everything above the open
document: the notes root, the scanned index, tags, manual ordering, and the file
operations that create, rename, move and trash notes.

Both need to touch files, and for most notes there is no conflict, because
`NoteCollection` acts on notes nobody has open. The conflict is over the one
note that is open, where the file on disk and the block model in memory are two
representations of the same thing, and where the in-memory copy is often the
only one that holds the user's most recent typing.

The failure modes this produces are not theoretical. A cluster of them was found
and fixed together, and they share a shape: some component wrote or discarded the
open note while believing it knew the document's state, when in fact it did not.

- Front matter is part of the note but not part of the block model, so setting
  metadata was a bare assignment that left the document reporting itself clean
  while holding metadata that had never reached disk. Every save, close and
  autosave decision consults the dirty flag, so each of them was entitled to
  throw the change away.
- Several block editors keep their text outside the block model until a 250 ms
  debounce fires or focus is lost. Until then the model does not hold the edit,
  so a save, an export, a note switch or a shutdown could act on a document
  missing the user's most recent typing.
- An atomic save replaces the note rather than editing it in place, which leaves
  the kernel watch pointing at the inode the temporary file replaced. The open
  note lost its watch on the first save, so external edits went unseen and the
  keep-mine/load-theirs banner could not appear at all.
- An asynchronous save owned the path it was handed. Renaming, moving or
  deleting the open note while that write was in flight let it commit
  afterwards and recreate the file at the old path, producing a duplicate beside
  the renamed note or resurrecting a deleted one outside the trash.
- Every destructive transition ignored whether the save it had just attempted
  actually worked. Switching notes called save, discarded the result, and
  replaced the model holding the only copy of the unsaved content.

## Decision

`DocumentManager` is the sole writer of the open note. Everything that changes
that note routes through it, including changes that are not part of the block
model.

The rules that follow from this:

1. **Note bodies flow through `DocumentManager`; everything else flows through
   `NoteCollection`.** Metadata, create, rename, move, delete and trash are the
   collection's, and the collection's scan writes only the performance index
   sidecar. This split is stated in `src/notecollection.h` and is the contract
   both objects are written against.
2. **A metadata change is an edit to the open document.** Setting front matter
   advances the document revision, marks the document dirty and feeds the crash
   journal, so metadata is covered by the same guarantees as the body rather
   than living in a gap between the two owners.
3. **Anything that reads the document flushes pending edits first.**
   `DocumentManager::flushPendingEdits()` asks every debounced editor to commit
   synchronously, and the save paths call it before serializing.
4. **A deferred write addresses its block by stable id, never by row.** Rows
   shift as blocks are added and removed, and a pooled delegate is rebound to a
   different block entirely, so a commit arriving 250 ms late could otherwise
   land in the wrong block or a different document. Late writes go through
   `BlockModel::updateContentById`, which refuses when the block is gone instead
   of guessing.
5. **An in-flight write can be cancelled and must be.** Writes carry a
   cancellation token the worker reads immediately before commit, at the point
   where the atomic-save temporary can still be dropped rather than renamed over
   the target. Rebinding the file path and starting a new document both cancel
   the write and wait for the worker before changing the path.
6. **A failed or cancelled save blocks the transition that depended on it.**
   Saving reports whether the document reached disk, and a cancelled dialog
   counts as a failure because both leave the in-memory copy as the only one.
   Note switching, the Open and New confirmations, and window close all wait for
   a successful or deliberate outcome; closing is refused outright when the save
   fails.
7. **The open note keeps exactly one live watch.** `FileWatcher` renews the
   watch after the application's own write, checks every registration result,
   and reports degraded coverage through `watchDegraded` rather than implying it
   sees changes it cannot see.

## Consequences

Unsaved work has one owner and one set of guarantees, so the question "can this
code path discard the user's typing?" has a single place to look for the answer.
The crash journal, the dirty flag and the undo stack all describe the same
document state rather than three approximations of it.

The cost is that a good deal of ceremony is now mandatory. Async writes need
cancellation tokens; every destructive transition has to check a save result and
be prepared to refuse; debounced editors have to be reachable by a synchronous
flush and have to address blocks by id. None of that is optional, and each rule
exists because its absence produced a specific data-loss bug rather than because
it is tidy.

This decision governs a single process. Two processes opening the same notes
root are outside its scope and are covered, as an open question, by
[ADR 0005](0005-multi-process-behaviour.md).

## Evidence in the tree

- `src/documentmanager.h`, `src/documentmanager.cpp`: the document session, journal, flush and cancellation
- `src/notecollection.h`: the ownership contract with `DocumentManager`
- `src/filewatcher.h`: single note watch, renewal, `watchDegraded`
- `src/blockmodel.h`: `updateContentById`
- Merge commit `1b72d73` "Merge the document-session cluster", and within it
  `e953dbd` (metadata as an edit), `1839c28` (flush before read), `0189d4c`
  (watch renewal and write cancellation), `6e93d36` (save result gates the
  transition)
