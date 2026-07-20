# 0001. Files on disk are authoritative; derived state is rebuildable

**Status:** Accepted

## Context

Kvit Notes edits a folder of ordinary `.md` files. A user can point it at a
directory that predates the application, edit the same notes in another editor
between sessions, sync the folder with a tool that knows nothing about Kvit, and
expect all of that to work. The application therefore has to decide what it
considers the truth about a note when its own records and the file on disk
disagree.

The alternative most note applications take is an internal database, with files
either absent or exported as a convenience. That buys fast queries and cheap
metadata, at the cost of making the application the only thing that can read its
own notes, and making corruption of one file a loss of everything.

Fast operations still need indexes. Global full-text search over a large vault
cannot re-read every file on each keystroke, and a warm start cannot afford to
reparse notes that have not changed. So the question is not whether to keep
derived state, but what happens when it disagrees with the files or disappears.

## Decision

Markdown files are the store of record for note content, and per-note metadata
lives in each note's own YAML front matter. Everything computed from them is
derived, and every piece of derived state can be deleted and rebuilt from the
files without consulting a backup.

Concretely:

- A note's body lives only in its file. Bodies are not held resident in memory
  across the collection; features that need one note's text read that file on
  demand, and global search reads from the index (`src/notecollection.h`).
- Tags, pinned, favorite and created-at live in the note's front matter, and a
  rewrite preserves every line it does not understand. `NoteFrontMatter::split()`
  is byte-preserving, and keys written by other tools survive verbatim in
  `Metadata::unknownLines` (`src/notefrontmatter.h`), so Kvit cannot destroy
  metadata it was not designed to read.
- Folder structure is directory structure. There is no separate hierarchy.
- The full-text index is a SQLite FTS5 database keyed by a hash of the root path
  and stored under `QStandardPaths::CacheLocation`, deliberately outside the
  notes root so it is never synced or backed up with the vault
  (`CollectionSearchIndex::databasePathForRoot`). A schema-version mismatch or a
  corrupt database is removed and rebuilt rather than repaired.
- The note-list sidecar `<root>/.kvit/index.json` caches titles, snippets, word
  counts and the wiki-link graph so a warm start does not reparse every note. It
  is version-gated: a sidecar written by an older format is dropped and rebuilt
  from the markdown rather than trusted.

Derived state is invalidated by a file watcher over the root, by a monotonic
collection revision counter that view models bind to, by per-query generation
numbers in search, and by a startup reconcile that compares each file's size and
modification time against what the index recorded.

## Consequences

Deleting the search index or the note-list sidecar costs rebuild time and
nothing else. A vault copied to another machine, or restored from a backup that
captured only `.md` files, is complete. Another editor's changes are picked up
rather than overwritten, and a note Kvit has never opened is as valid as one it
wrote.

The costs are real and worth stating. Search cannot answer a query the index has
not yet caught up with, so freshness is a property the code has to maintain
rather than one it gets for free, and the size-and-modification-time heuristic is
not sufficient on its own. A front-matter rewrite deliberately restores the
file's modification time so that tagging a note does not reorder "recently
modified", which meant a same-length tag rename changed neither size nor
modification time and the index served the stale tag indefinitely. Every write
site that preserves a modification time now reindexes its note explicitly, which
makes this a standing obligation on anyone adding such a site rather than a
problem the design has solved once.

### One qualification: `.kvit/` is not a cache directory

The rule above covers note content and per-note metadata. It does not cover
everything the application stores, and the exception deserves to be explicit
because the directory's name invites the wrong assumption.

`<root>/.kvit/collection.json` holds state that is genuinely the user's and
exists nowhere else: per-folder manual note ordering, tag colors, folder colors
and expansion, and the last-open note. None of it is derivable from any markdown
file, because none of it is a property of a note. Manual ordering in particular
is pure user intent.

Alongside it, `.kvit/` also holds `trash/` (deleted notes awaiting an explicit
empty), `backups/` (rotating pre-overwrite copies), `recovery/` (the crash
journal, which is the only copy of unsaved work while a document is dirty) and
`templates/` (user-authored templates). Only `index.json` and `embedcache/` are
throwaway.

So `.kvit/` mixes one rebuildable cache with several stores of irreplaceable
user data, and anything that treats the whole directory as disposable (a
`.gitignore` entry, a sync exclusion, a "clear cache" button) destroys user
data. This is why `collection.json` is written atomically and why a failed write
surfaces to the user through `operationFailed` rather than being swallowed. It
is also the strongest argument for eventually splitting the directory into a
cache half and a state half; that has not been done, and until it is, the
hazard is contained only by nobody having written the offending line.

## Evidence in the tree

- `src/notecollection.h`: ownership contract and the statement that bodies are not resident
- `src/notefrontmatter.h`: byte-preserving split, `unknownLines`
- `src/collectionsearchindex.cpp`: `databasePathForRoot`, cache location choice
- `src/searchindexdb.cpp`: schema version gate, rebuild on corruption
- `src/filewatcher.h`: watch coverage and the `watchDegraded` signal
- `usage.md`: the user-facing description of `.kvit/`
