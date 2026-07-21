// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
#ifndef VAULTSCAN_H
#define VAULTSCAN_H

#include <QFileInfo>
#include <QHash>
#include <QList>
#include <QSet>
#include <QString>
#include <QStringList>

#include "cancellationtoken.h"
#include "noteentry.h"

// What a worker thread computes about a vault.
//
// Scanning a notes root, re-reading a changed subtree, parsing one saved note
// and writing the index sidecar all run off the GUI thread, and all of them
// are the same shape: a request value in, a result value out, no shared state
// and no signals. Keeping them here rather than on NoteCollection makes that
// property structural — a function in this namespace has nothing it could
// touch concurrently, because it has no members — and lets the parsing be
// tested without constructing a collection.
//
// NoteCollection owns the other half: the futures, the generation counters
// that decide whether a result is still wanted, and applying a result to the
// live index.
//
// Every request carries a generation, and the result carries it back. A
// cancelled walk additionally sets `cancelled`, because its folder and note
// lists are a prefix of the vault rather than all of it, and the removal
// passes that consume `seen` sets would otherwise delete entries the walk
// simply never reached.
namespace VaultScan {

struct IndexTask {
    QString relPath;
    QString absPath;
    QDateTime createdFallback;
    QDateTime modified;
    qint64 fileSize = -1;
    quint64 generation = 0;
};

struct IndexResult {
    QString relPath;
    NoteEntry entry;
    bool ok = false;
    quint64 generation = 0;
};

struct ScanRequest {
    QString rootPath;
    QHash<QString, NoteEntry> cachedNotes;
    bool indexOk = false;
    bool indexFileExists = false;
    quint64 generation = 0;
    // Checked between directories. QtConcurrent::run cannot interrupt
    // this walk, so without it a vault the user has already left goes on
    // being listed to the end.
    CancellationTokenPtr cancel;
};

struct ScanListing {
    QString rootPath;
    QList<FolderEntry> folders;
    QList<NoteEntry> entries;
    QList<IndexTask> tasks;
    bool indexDirty = false;
    quint64 generation = 0;
    // The walk stopped early, so the folder and note lists are a prefix
    // of the vault rather than all of it. The generation bump that
    // accompanies every cancel already keeps this from being applied;
    // the flag says so in the value itself, where a later reader cannot
    // miss it.
    bool cancelled = false;
};

struct RefreshRequest {
    QString rootPath;
    QStringList relDirs;
    QHash<QString, NoteEntry> currentNotes;
    quint64 generation = 0;
    // Checked between notes. This worker reads and parses every changed
    // body inline, so it is the one whose abandoned run costs the most.
    CancellationTokenPtr cancel;
};

struct RefreshResult {
    QString rootPath;
    QStringList relDirs;
    QStringList missingDirs;
    QList<FolderEntry> folders;
    QList<NoteEntry> entries;
    QSet<QString> seenNotes;
    QSet<QString> seenFolders;
    quint64 generation = 0;
    // As above: a stopped refresh saw only part of the subtree, and
    // seenNotes/seenFolders drive removals — applying a partial one
    // would delete entries the walk simply never reached.
    bool cancelled = false;
};

struct SavedNoteTask {
    QString relPath;
    QString absPath;
    QString fileText;
    QDateTime createdFallback;
    QDateTime modified;
    qint64 fileSize = -1;
    quint64 generation = 0;
};

struct IndexSaveRequest {
    QString path;
    QHash<QString, NoteEntry> notes;
    quint64 generation = 0;
};

struct IndexSaveResult {
    QString path;
    int notes = 0;
    int bytes = 0;
    bool ok = false;
    quint64 generation = 0;
};

// The body statistics the note list and status bar show, from a
// front-matter-stripped body.
struct BodyStats {
    int wordCount = 0;
    QString snippet;
};
BodyStats analyzeBody(const QString &markdownBody);

// An index entry for a note whose body has not been read yet. fileSize stays
// -1, so if parsing never replaces this the persisted sidecar forces a retry
// on the next launch rather than recording it as complete.
NoteEntry placeholderEntry(const QString &relPath, const QFileInfo &info);

// An index entry taken from the sidecar cache, with the path-derived fields
// recomputed for where the file actually is now.
NoteEntry cachedEntryForPath(const QString &relPath, const NoteEntry &cached,
                             const QFileInfo &info);

// A complete index entry parsed from a note file's text.
NoteEntry entryFromText(const QString &relPath, const QString &fileText,
                        const QFileInfo &info);

// Walk the vault: folders, note entries from the cache where they are still
// fresh, and a parse task for every note where they are not.
ScanListing buildScanListing(const ScanRequest &request);

// Parse one note the walk could not take from cache.
IndexResult parseIndexTask(const IndexTask &task);

// Re-read a set of directories after the file watcher reported changes.
RefreshResult buildRefreshResult(const RefreshRequest &request);

// Parse a note the application itself just saved.
IndexResult parseSavedNoteTask(const SavedNoteTask &task);

// Write the index sidecar.
IndexSaveResult writeIndexFileSnapshot(const IndexSaveRequest &request);

} // namespace VaultScan

#endif // VAULTSCAN_H
