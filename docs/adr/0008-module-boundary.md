# 0008. Seven modules, and the direction between them

**Status:** Accepted

## Context

Every C++ file except `main.cpp` was compiled into one static library called
`kvit-core`, and every file in it published its directory as an include path.
Any file could therefore include any other, and the layering existed only as a
habit that reviewers kept in their heads.

Habits of that kind decay in a predictable direction. Measuring the include
graph across the tree's translation units found four dependencies pointing the
wrong way, none of them deliberate and none of them visible in review:

- the document model, selection, in-document search, statistics and export all
  reached `BlockEditorEngine` for its markdown-mapping functions, and got a
  class that also drags in `Theme`, `DocumentOutline`, `NoteCollection` and
  `QQuickTextDocument`;
- `EgressFetcher` included the embed service in order to implement the
  interface the embed service calls it through;
- `BlockModel` asked `EmbedMetadata` — a class that owns a network fetcher and
  a disk cache — a question about a URL's file extension;
- `NoteCollection`, the vault, held a pointer to `DocumentManager`, the
  document session built on top of it.

None of those was a defect on its own. Together they meant that two invariants
the project depends on could not be checked mechanically. ADR 0003 says nothing
outside `EgressFetcher` may open a network connection; ADR 0002 says one
component owns writes to the open note. Both were true, and both were true only
because everyone had so far remembered.

## Decision

`src/` is seven static libraries, one directory each, and the dependencies
point in one direction.

```text
kvit-content      pure format transforms — the markdown value parsers, the
                  inline-markdown transition tables, diagrams, math, HTML to
                  markdown. Depends on no other module.
      ↑
kvit-domain       the block document: model, serializer, undo stack and
                  commands, selection, in-document search, outline,
                  statistics.
      ↑
kvit-search   kvit-platform
                  the rebuildable note index, and the machine the app runs
                  on: settings, file watching, network policy, tray,
                  hotkeys, appearance tokens.
      ↑
kvit-repository   the vault: containment, note filesystem access, atomic
                  persistence, trash, backups, recovery, templates, import,
                  assets.
      ↑
kvit-application  use-case orchestration: the document session, startup, the
                  list/tree/switcher models, queries, export.
      ↑
kvit-qml          the presentation surface — the editor engine that drives a
                  QML TextArea and the `Kvit` module's registrations — and
                  the composition root that wires the object graph and hands
                  it to a QQmlEngine.
```

Each target publishes only its own directory as an include path and inherits
the directories of the modules it links. A wrong-direction include therefore
does not compile. Adding `#include "notecollection.h"` to a `kvit-domain`
source fails with "No such file or directory", so the boundary is checked by
the compiler rather than by whoever happens to review the change.

`kvit-core` survives as an interface target linking all seven, so the test
suite and an external consumer that wants "the editor" name one thing.

Two rules cannot be expressed as an include direction, because they are about
what a module does rather than what it names, and `tools/check-layering.py`
carries those. Qt's networking classes may appear only under `src/platform/`,
which is the one place `Qt6::Network` is linked. Filesystem mutation
may appear only in files listed in that script with the reason each is
allowed: the repository as a whole, the document session for the open note,
export output, three caches and the performance log. A stale entry fails the check
too, so the list cannot quietly outlive the code it describes. It runs as the
`LayeringGuard` test.

## Consequences

Four dependencies had to be turned around before the split could hold, and each
left the tree better than a bare move would have:

`BlockEditorEngine`'s pure transition tables are `InlineMarkdown` now, a
namespace of free functions in `kvit-content` covering every mapping between
storage markdown, display text and the revealed state. The engine went from 1,943 lines
to 1,262, and the six callers that wanted the mappings no longer link a QML
type to get them.

`EmbedFetcher` is its own header, so the implementation and its caller do not
include each other. `ImageAssets::isEmbedUrl` answers the URL question beside
`kindForExtension`, where it needs neither a network nor a cache. The
repository reaches the live document through `OpenDocumentSession`, an
interface six methods wide that `DocumentManager` implements, so the vault no
longer names the class built on top of it and a test can supply its own
session.

The checker also found two things nobody was looking for. `VaultLock` built its
lock-file description with `QHostInfo::localHostName()`, which is QtNetwork;
`QSysInfo::machineHostName()` in QtCore is the same value. And `ImageAssets`
was copying pasted and dropped files into the vault from `kvit-content`, the
layer furthest from being allowed to. That half is `AssetStore` in
`kvit-repository` now, and the split gave the containment check a home: the
asset's file name is built from a caller-supplied slug, and nothing had stopped
that slug from carrying `../`.

Three placements are not what a first reading of the layout suggests, and are
deliberate.

Import and export read as one subject, which argues for keeping them together
under content. Export is in `kvit-application` because it orchestrates the
collection, the theme and the format writers; import is in `kvit-repository`
because copying a file into the vault is a repository write. What both share —
the format transforms themselves — is in `kvit-content`.

`AppContext` and `KvitApplication` are in `kvit-qml` rather than
`kvit-application`. A composition root depends on everything by definition:
`AppContext`'s entire job is to construct the object graph and publish it to a
QQmlEngine, while `kvit-application` holds the use-case orchestration
underneath it. Putting the composition root at the top is what keeps the layer
below it free of QML.

`kvit-content` is the bottom layer rather than a middle one. The markdown value
parsers, the diagram engine and the math renderer have no dependency on the
block model, and the serializer depends on them, so placing them under the
domain describes what the code already was rather than changing it.

Two mechanical consequences are worth knowing before they surprise someone.

The `Kvit` QML module registers types compiled into four different module
libraries, and `qmltyperegistrar` builds its list from one target's own
metatypes. `Block`'s enum, the fence-kind namespace, the code-language
singleton, the diagram canvas and the settings store are therefore
`QML_FOREIGN` wrappers in `src/qml/qmlsingletons.h`, alongside the forty
singletons that already used that pattern. `kvit-domain`, `kvit-content` and
`kvit-platform` link no QML registration machinery at all. The wrappers resolve
against the other modules' `metatypes.json`, which `qt_extract_metatypes`
produces for each module in `CMakeLists.txt`; without that the registrar cannot
tell a namespace from a type and emits code that does not compile.

The qmldir's `depends QtQuick` is stated explicitly in `qt_add_qml_module`. Qt
derives that line from the QML modules a target links *directly*, and
`kvit-qml` reaches Quick transitively. Without it qmllint cannot resolve a
value type such as `QColor` in a Kvit type's property, and reports several
hundred findings on files that are correct.

## What this does not yet do

`NoteCollection` is 3,024 lines rather than 3,494. Workspace state
(`CollectionStateStore`), vault containment (`VaultPaths`) and the
worker-thread half of scanning (`VaultScan`) came out, and trash, backups, the
recovery journal, the index sidecar, the search feed and the vault lock were
already collaborators before this work. One class still holds the coordination
between them along with the note, folder, metadata and wiki-link operations,
which share the in-memory index directly. Splitting them
further means extracting that index first, which is a larger change than this
record covers.

## Evidence in the tree

- `CMakeLists.txt`: the seven targets, their link directions, and the include paths that enforce them
- `tools/check-layering.py`: the graph, the network rule, and the filesystem-mutation list with a reason per entry
- `tests/CMakeLists.txt`: the `LayeringGuard` entry
- `src/content/inlinemarkdown.h`: the transition tables that used to sit inside the QML editor engine
- `src/repository/opendocumentsession.h`: the six methods the vault asks of the live document
- `src/repository/vaultpaths.h`: the containment rule, in one place
- `src/repository/assetstore.h`: asset ingestion, moved to the module allowed to write
- `src/qml/qmlsingletons.h`: every `Kvit` type and singleton, as foreign wrappers
