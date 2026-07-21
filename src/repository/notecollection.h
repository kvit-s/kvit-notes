// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
#ifndef NOTECOLLECTION_H
#define NOTECOLLECTION_H

#include <QObject>
#include <QString>
#include <QStringList>
#include <QDateTime>
#include <QElapsedTimer>
#include <QFutureWatcher>
#include <QHash>
#include <QMap>
#include <QSet>
#include <QTimer>
#include <QVariantMap>
#include <QtQml/qqmlregistration.h>

#include <functional>

#include "cancellationtoken.h"
#include "linkredirects.h"
#include "notebackupstore.h"
#include "collectionstatestore.h"
#include "vaultscan.h"
#include "noteentry.h"
#include "noteindexfile.h"
#include "notefrontmatter.h"
#include "notetrashstore.h"
#include "operationjournal.h"
#include "recoveryjournalstore.h"
#include "searchindexfeed.h"
#include "wikilinkindex.h"
#include "vaultlock.h"

class QFileInfo;
class CollectionSearchIndex;
class OpenDocumentSession;

// The notes-collection object: one GUI-free
// QObject owning everything above the open document — the notes root, the
// scanned note and folder index, all organization file operations, tags,
// manual order, and the collection.json/index.json sidecars. View models bind to it
// through the revision-counter contract; they never own collection state.
//
// Ownership contract with DocumentManager: note BODIES flow through
// DocumentManager (the block model); everything else — metadata,
// create/rename/move/delete, trash — flows through this object. The scan
// writes only the performance index sidecar; front-matter is created lazily on
// the first metadata edit.
//
// Destructive operations move files into <root>/.kvit/trash rather than
// deleting; they are not on the editor undo stack.
class NoteCollection : public QObject
{
    Q_OBJECT

    Q_PROPERTY(QString rootPath READ rootPath NOTIFY rootChanged)
    Q_PROPERTY(bool isOpen READ isOpen NOTIFY rootChanged)
    Q_PROPERTY(int revision READ revision NOTIFY revisionChanged)
    Q_PROPERTY(bool scanInProgress READ scanInProgress NOTIFY scanInProgressChanged)

public:
    // The indexed-note and indexed-folder records, defined in noteentry.h so
    // the layers above the repository can name them without depending on this
    // class. Re-exported here because `NoteCollection::NoteEntry` is how the
    // view models, the query engine and the tests refer to them.
    using NoteEntry = ::NoteEntry;
    using FolderEntry = ::FolderEntry;

    explicit NoteCollection(QObject *parent = nullptr);
    ~NoteCollection() override;

    // The disk-backed global-search index this collection feeds.
    // Optional: when unset the collection keeps no search index, so tests and
    // tools that only need collection metadata spawn no worker threads or
    // database. When set, the collection opens it per root and streams note
    // changes to it.
    void setSearchIndex(CollectionSearchIndex *index);
    // The open-document session is the exclusive writer for its note. The
    // repository uses it to serialize live metadata with the live body and
    // to drain active saves before a path mutation. Held as the
    // OpenDocumentSession interface (opendocumentsession.h), so the
    // repository does not depend on the class that implements it.
    void setOpenDocument(OpenDocumentSession *session);

    // Open the next root for reading only. A read-only collection takes no
    // vault lock — it cannot lose anybody's update because it writes
    // nothing — and refuses every mutation with operationFailed.
    //
    // This exists because "one process, several collections on one root" is
    // two different situations. A second editing session is the lost-update
    // problem the cross-process lock prevents, reproduced inside one process;
    // a preview, an exporter or a tool that only reads is not. Without a mode
    // to say which it is, the reference-counted lock has to admit both.
    // Set before openRoot(); changing it does not reopen the current root.
    void setReadOnly(bool readOnly) { m_readOnly = readOnly; }
    bool isReadOnly() const { return m_readOnly; }

    // --- Root -----------------------------------------------------------
    // Opens (creating if missing) a notes root and scans it. Loading may refresh
    // the performance index sidecar, but does not rewrite notes or collection
    // state.
    Q_INVOKABLE bool openRoot(const QString &path);
    // App-start path: opens the root with filename/sidecar metadata
    // immediately and parses changed/new notes on worker threads.
    Q_INVOKABLE bool openRootAsync(const QString &path);
    Q_INVOKABLE void closeRoot();
    Q_INVOKABLE void refresh(); // full rescan (external changes)
    Q_INVOKABLE void refreshPaths(const QStringList &absPaths);

    // First-run seeding: an empty collection gets a Welcome note.
    Q_INVOKABLE void initializeIfEmpty();

    QString rootPath() const { return m_rootPath; }
    bool isOpen() const { return !m_rootPath.isEmpty(); }
    int revision() const { return m_revision; }
    bool scanInProgress() const { return m_scanInProgress; }

    // §9.7 status-bar counts: the same display-text rules the scan
    // uses (analyzeBody), so the status bar and note list never
    // disagree. Verbatim = code-block content (identity mapping).
    Q_INVOKABLE int wordCountForMarkdown(const QString &markdown,
                                         bool verbatim = false) const;
    Q_INVOKABLE int charCountForMarkdown(const QString &markdown,
                                         bool verbatim = false) const;

    Q_INVOKABLE QString absolutePath(const QString &relPath) const;
    // "" if outside the root.
    Q_INVOKABLE QString relativePath(const QString &absPath) const;
    // Repository-boundary validation for callers that construct a relative
    // destination (imports, recovery state). Reports and rejects absolute,
    // dot-segmented, symlink-escaping, or otherwise out-of-root paths.
    Q_INVOKABLE bool ensureWithinRoot(const QString &relPath);

    // --- Wiki-links --------------------------------------------------------
    // Obsidian-compatible resolution: a target matches a note by path
    // suffix — bare "note" matches any **/note.md, "folder/note" requires
    // that suffix; matching is case-insensitive and the ".md" extension is
    // implied. Returns the note's relPath only for exactly one suffix match;
    // ambiguous and missing targets both return "".
    Q_INVOKABLE QString resolveWikiTarget(const QString &target) const;
    // Rich resolver used by link-following UI so an ambiguous target is not
    // mistaken for a missing target and auto-created.
    Q_INVOKABLE QVariantMap wikiTargetResolution(const QString &target) const;
    // Referring notes for the backlinks panel: [{relPath, title, count,
    // contexts}], sorted by relPath. `contexts` are the referring note's
    // raw body lines containing a link that resolves to `relPath`.
    Q_INVOKABLE QVariantList backlinksTo(const QString &relPath) const;
    // Raw outgoing targets of one indexed note (testing/QML convenience).
    Q_INVOKABLE QStringList linksFrom(const QString &relPath) const;
    // Heading texts of one indexed note, document order — the [[target#
    // completion's source (§3.5). A fence-aware line scan of the indexed
    // body, computed on demand for the picked note only.
    Q_INVOKABLE QStringList headingsFor(const QString &relPath) const;
    // Lightweight fence-aware scan for [[...]] targets — public and static
    // so tests can pin the extraction grammar directly.
    static QStringList extractWikiLinks(const QString &body);
    // Rewrite the note-part of matching [[...]] occurrences in `text`
    // (outside code fences, alias and #anchor preserved byte-exactly).
    // `oldKeys` holds the lowercased ".md"-stripped note-parts to replace.
    // Returns the number of links rewritten. Public and static for tests.
    static int rewriteWikiTargetsInText(QString *text,
                                        const QSet<QString> &oldKeys,
                                        const QString &replacement);

    // Where a note that used to live at `relPath` lives now, or "" when
    // nothing was renamed away from there. Renaming records a redirect
    // instead of rewriting every linking note on the spot (linkredirects.h),
    // so a `[[link]]` naming the old path keeps resolving from the moment of
    // the rename; the linking notes are rewritten afterwards, a few at a
    // time, and the redirect is dropped once none of them names it any more.
    Q_INVOKABLE QString redirectedNotePath(const QString &oldRelPath) const;
    // Test seam: the recorded redirects, oldRelPath -> newRelPath.
    QVariantMap linkRedirectsForTesting() const;
    // Test seam: whether the background rewrite still has notes to visit.
    bool linkRewriteInProgressForTesting() const { return m_redirectTimer.isActive(); }

    // --- Index queries ---------------------------------------------------
    Q_INVOKABLE QStringList noteRelPaths() const;           // sorted
    Q_INVOKABLE QStringList notesInFolder(const QString &folder) const; // manual order
    const NoteEntry *note(const QString &relPath) const;
    Q_INVOKABLE QStringList folderRelPaths() const;         // sorted
    const FolderEntry *folder(const QString &relPath) const;
    // QML/testing view of one index entry (empty map when absent).
    Q_INVOKABLE QVariantMap noteInfo(const QString &relPath) const;
    int noteCount() const { return m_notes.size(); }
    Q_INVOKABLE int noteCountInFolder(const QString &folder,
                                      bool recursive) const;

    // --- Note operations --------------------------------------------------
    // Returns the new note's relPath, or "" on failure (operationFailed
    // names the reason). Untitled notes get unique "Untitled N" names.
    // `body` reaches disk in the same write that creates the file, so a
    // caller holding the only copy of some text never ends up with the note
    // created and the text lost.
    Q_INVOKABLE QString createNote(const QString &folder,
                                   const QString &title = QString(),
                                   const QString &body = QString());
    // Quick capture (§15.1): create a root-folder note whose body is `text`,
    // titled from its first line (falling back to an Untitled name). Returns the
    // new note's relPath, or "" on failure.
    Q_INVOKABLE QString captureNote(const QString &text);
    Q_INVOKABLE bool renameNote(const QString &relPath, const QString &newTitle);
    Q_INVOKABLE bool moveNote(const QString &relPath, const QString &targetFolder);
    // Two-phase rename-safe operations. Planning is read-only and snapshots
    // every referrer. Applying with updateLinks=false performs only the file
    // operation; true performs conflict-checked atomic referrer rewrites.
    Q_INVOKABLE QVariantMap planNoteRename(const QString &relPath,
                                           const QString &newTitle);
    Q_INVOKABLE QVariantMap planNoteMove(const QString &relPath,
                                         const QString &targetFolder);
    Q_INVOKABLE QVariantMap applyRenamePlan(const QString &planId,
                                            bool updateLinks,
                                            const QString &openRelPath = QString(),
                                            const QString &openBody = QString());
    Q_INVOKABLE void cancelRenamePlan(const QString &planId);
    Q_INVOKABLE bool deleteNote(const QString &relPath); // to trash

    // --- Folder operations -------------------------------------------------
    Q_INVOKABLE QString createFolder(const QString &parent, const QString &name);
    Q_INVOKABLE bool renameFolder(const QString &relPath, const QString &newName);
    Q_INVOKABLE QVariantMap planFolderRename(const QString &relPath,
                                             const QString &newName);
    Q_INVOKABLE bool deleteFolder(const QString &relPath); // recursive, to trash

    // Trash management: how many top-level items sit in .kvit/trash, and
    // permanent removal of all of them. Bumps the revision so the sidebar's
    // trash row stays current.
    Q_INVOKABLE int trashItemCount() const;
    Q_INVOKABLE bool emptyTrash();
    Q_INVOKABLE void setFolderExpanded(const QString &relPath, bool expanded);
    Q_INVOKABLE void setFolderColor(const QString &relPath, const QString &color);

    // --- Per-note metadata (rewrites the file's front-matter in place,
    // byte-preserving the body; the file's modification time is restored
    // so metadata edits do not masquerade as content edits) --------------
    Q_INVOKABLE bool setTags(const QString &relPath, const QStringList &tags);
    Q_INVOKABLE bool addTag(const QString &relPath, const QString &tag);
    Q_INVOKABLE bool removeTag(const QString &relPath, const QString &tag);
    Q_INVOKABLE bool setPinned(const QString &relPath, bool pinned);
    Q_INVOKABLE bool setFavorite(const QString &relPath, bool favorite);
    // Per-note writing goal: a word target stored in front-matter,
    // byte-preserving the body and restoring mtime like every metadata
    // write. 0 clears it. goalFor returns 0 when unset or unknown.
    Q_INVOKABLE bool setGoal(const QString &relPath, int goal);
    Q_INVOKABLE int goalFor(const QString &relPath) const;

    // --- Tag registry (derived from the scanned front-matter) ------------
    QStringList allTags() const; // sorted case-insensitively
    // Sidebar/QML view: [{name, count, color}, ...] sorted like allTags().
    Q_INVOKABLE QVariantList tagListing() const;
    Q_INVOKABLE int tagCount(const QString &tag) const;
    Q_INVOKABLE QString tagColor(const QString &tag) const;
    Q_INVOKABLE void setTagColor(const QString &tag, const QString &color);
    // Rename merges when newName already exists (union, deduplicated).
    Q_INVOKABLE bool renameTag(const QString &oldName, const QString &newName);
    Q_INVOKABLE bool deleteTag(const QString &tag);

    // --- Manual order (per folder, persisted in collection.json) ---------
    QStringList manualOrder(const QString &folder) const; // relPaths, reconciled
    Q_INVOKABLE bool setManualPosition(const QString &relPath, int position);

    // --- Workspace state --------------------------------------------------
    Q_INVOKABLE QString lastOpenNote() const { return m_lastOpenNote; }
    Q_INVOKABLE void setLastOpenNote(const QString &relPath);

    // --- Backups: rotation on a time floor -------------------------------
    // Called just before a note file is overwritten (DocumentManager's
    // aboutToSave hook): copies the CURRENT on-disk file into
    // .kvit/backups/<relPath>/<stamp>.md when the newest backup is older
    // than the floor; prunes past the cap.
    Q_INVOKABLE void backupBeforeOverwrite(const QString &absPath);
    // Newest first: [{fileName, timestamp, preview}]
    Q_INVOKABLE QVariantList backupsFor(const QString &relPath) const;
    // The chosen backup's BODY (front-matter stripped) — restore applies
    // it through the block model as one undo step.
    Q_INVOKABLE QString backupBody(const QString &relPath,
                                   const QString &fileName) const;
    // Test seams: rotation decisions read this clock. The offset variant
    // is reachable from QML tests.
    void setClockForTesting(std::function<QDateTime()> clock);
    Q_INVOKABLE void setClockOffsetForTesting(int secs);
    // Test seam: hold the snapshot write instead of dispatching it to the
    // pool, so a test can drive what happens while an earlier backup has not
    // landed yet (NoteBackupStore::setSnapshotWriterForTesting).
    void setBackupWriterForTesting(
        std::function<void(const QString &dirPath, const QString &target,
                           const QByteArray &bytes)> writer);

    // --- Crash recovery ----------------------------------------------------
    // Where the open note's dirty-state journal lives (DocumentManager
    // writes/removes it; the path encodes the relPath).
    Q_INVOKABLE QString journalPathFor(const QString &relPath) const;
    // Journals found at openRoot — crash evidence. [{relPath, title,
    // preview, journalPath}], shrinking as entries restore/discard.
    Q_INVOKABLE QVariantList recoveryEntries() const;
    Q_INVOKABLE bool restoreRecovery(const QString &relPath);
    Q_INVOKABLE void discardRecovery(const QString &relPath);

    // --- Open-note seams (DocumentManager wiring) ------------------------
    // Refreshes one note's index entry after its file was (re)written.
    Q_INVOKABLE void noteSaved(const QString &absPath);
    Q_INVOKABLE void noteSaved(const QString &absPath,
                               const QString &fileText);
    // Canonical front-matter block for a note, from the index metadata.
    //
    // "" is ambiguous on its own and always has been: a note with no metadata
    // and a note the index has not parsed yet both serialize to nothing. Ask
    // hasParsedMetadata() before projecting this into a live document.
    Q_INVOKABLE QString frontMatterFor(const QString &relPath) const;

    // Whether the index holds real, parsed metadata for `relPath`.
    //
    // False covers two situations that must not be mistaken for "this note
    // has no front matter": there is no entry at all, and there is a
    // placeholder entry standing in for a note the background scan has not
    // read yet. An asynchronous open publishes such a placeholder for the
    // remembered note so it can be opened before the scan finishes, and its
    // metadata is empty because nothing has looked at the file, not because
    // the file has none.
    Q_INVOKABLE bool hasParsedMetadata(const QString &relPath) const;

    // Test seam: called when a note body is parsed for the collection index.
    void setIndexParseObserverForTesting(
        std::function<void(const QString &)> observer);

    // Test seam: whether the index sidecar still holds unwritten changes.
    // Lets the I/O-failure tests observe that a failed write is retained
    // for a later retry rather than forgotten.
    bool indexDirtyForTesting() const { return m_indexDirty; }
    bool collectionFileDirtyForTesting() const
    { return m_collectionState.isDirty(); }
    // Test seam: whether the directory-listing future has actually been
    // picked up by a pool thread. Cancellation only waits on a watcher that
    // reports itself running, so measuring the cost of a cancel has to know
    // whether the wait was entered at all.
    bool listingWatcherIsRunningForTesting() const;
    bool refreshWatcherIsRunningForTesting() const;
    // Test seam: the asynchronous index-save sequence, which is otherwise
    // only reachable through a scan. Lets a test show that cancelling a save
    // leaves the index marked as still owing a write.
    void markIndexDirtyForTesting() { markIndexDirty(); }
    void saveIndexFileIfDirtyAsyncForTesting() { saveIndexFileIfDirtyAsync(); }
    void cancelAsyncIndexSaveForTesting() { cancelAsyncIndexSave(); }

signals:
    void rootChanged();
    void revisionChanged();
    // Rename or move, including notes inside a renamed folder. Receivers
    // (open document, selections, journal) rebind their paths.
    void noteMoved(const QString &oldRelPath, const QString &newRelPath);
    void noteRemoved(const QString &relPath);
    // This note's index entry now holds metadata parsed from its file, where
    // before there was none or only a placeholder. Path-bearing and targeted:
    // a receiver holding one note open compares the path and ignores the
    // rest, rather than re-reading everything on each revision bump. Emitted
    // once per note as a scan reaches it, so a receiver should do no more
    // than that comparison.
    void noteMetadataReady(const QString &relPath);
    // The removed path was the live document and the session has already
    // detached it. QML only chooses the next note; it no longer participates
    // in persistence ordering.
    void openNoteRemoved(const QString &relPath);
    // Direct repository writes (closed-note metadata, link rewrites,
    // recovery) use the same watcher own-write registration as session saves.
    void aboutToWrite(const QString &absPath);
    // Rename-safe wiki-links (§3.3): the background pass that follows an
    // in-app rename or move has finished rewriting the referring [[links]] —
    // the "Updated N links in M notes" toast. It arrives once the pass is
    // done rather than as the rename returns, because the rename itself only
    // records the redirect that keeps those links working meanwhile.
    void wikiLinksRewritten(int linkCount, int noteCount);
    // Notes the pass could not rewrite: unreadable, or unwritable. Nothing
    // is broken by that — the redirect stays and their links still resolve —
    // and the next open tries them again.
    void wikiLinkRewriteIncomplete(const QStringList &skipped,
                                   const QStringList &failed);
    void operationFailed(const QString &message);
    // A note this collection was about to rewrite changed on disk between
    // being read and being written, so the write was abandoned rather than
    // silently discarding the other change. The application layer decides
    // what to offer the user; the repository's job is to refuse the write and
    // say why.
    void noteChangedExternally(const QString &relPath);
    // An operation that spans several files did not finish before the last
    // session ended, and could not be completed on this open. The listed
    // notes may still hold the pre-operation text.
    void operationIncomplete(const QStringList &relPaths);
    // The vault is already open for editing — in another process, or in
    // another collection here — so this session refused it rather than
    // becoming a second writer whose saves would discard the other's.
    // `detail` is the sentence to show: who holds it, where the lock file
    // said, or that it is this application when the holder is one of ours.
    void vaultInUse(const QString &path, const QString &detail);
    // The vault opened, but locking is not available on the filesystem it
    // lives on, so nothing prevents a second session from writing to it.
    // Fail-open is deliberate — refusing to open the vault would be worse —
    // and this is how the user gets told, rather than only the log.
    void vaultUnprotected(const QString &path, const QString &detail);
    void scanInProgressChanged();
    void scanStarted();
    void scanFinished();

private:
    // The worker-thread half of scanning is VaultScan (vaultscan.h): request
    // in, result out, no shared state. Its value types keep their names here
    // so the watchers and handlers below read as they did.
    using AsyncIndexTask = VaultScan::IndexTask;
    using AsyncIndexResult = VaultScan::IndexResult;
    using AsyncScanRequest = VaultScan::ScanRequest;
    using AsyncScanListing = VaultScan::ScanListing;
    using AsyncRefreshRequest = VaultScan::RefreshRequest;
    using AsyncRefreshResult = VaultScan::RefreshResult;
    using AsyncSavedNoteTask = VaultScan::SavedNoteTask;
    using AsyncIndexSaveRequest = VaultScan::IndexSaveRequest;
    using AsyncIndexSaveResult = VaultScan::IndexSaveResult;

    bool prepareRootPath(const QString &path);
    void attachStoresToRoot();
    void loadRecoveryEntries();
    void scan();
    // `visitedDirs` carries the canonical directories already walked, so a
    // directory reachable twice (a bind mount, a junction) is entered once.
    void scanDirectory(const QString &relDir,
                       const QHash<QString, NoteEntry> &cachedNotes,
                       QSet<QString> *visitedDirs);
    void scanAsync();
    // A full rescan that does not block the GUI thread, used by the external
    // file watcher's refresh path when the change cannot be narrowed to known
    // notes. It is the async counterpart of refresh(): it cancels the same
    // in-flight work and then rebuilds through scanAsync(), which finishes on
    // a worker thread with the same bump()/syncSearchIndex() a refresh() does.
    void fullRefreshAsync();
    void indexNote(const QString &relPath);
    void indexNote(const QString &relPath,
                   const QHash<QString, NoteEntry> &cachedNotes);
    void indexNoteFromText(const QString &relPath,
                           const QString &fileText,
                           const QFileInfo &info);
    bool tryIndexNoteFromCache(const QString &relPath,
                               const QFileInfo &info,
                               const QHash<QString, NoteEntry> &cachedNotes);
    void setScanInProgress(bool inProgress);
    void applyAsyncScanListing();
    void applyAsyncIndexResult(int index);
    void finishAsyncScan();
    void flushAsyncIndexUpdates();
    void cancelAsyncScan();
    void startAsyncDirectoryRefresh(const QStringList &relDirs);
    void applyAsyncRefreshResult();
    void cancelAsyncRefresh();
    void startAsyncSavedNoteIndex(AsyncSavedNoteTask task);
    void applyAsyncSavedNoteResult();
    void cancelAsyncSavedNote();
    void saveIndexFileIfDirtyAsync();
    void startAsyncIndexSave(AsyncIndexSaveRequest request);
    void applyAsyncIndexSaveResult();
    void cancelAsyncIndexSave();
    void ensureFolderEntriesFor(const QString &folderPath);
    void insertNoteEntry(const QString &relPath, const NoteEntry &entry);
    void removeNoteEntry(const QString &relPath);
    void adjustFolderNoteCounts(const QString &folderPath, int delta);
    void rebuildFolderNoteCounts();
    void clearFolderNoteCounts();


    // Read one note file from disk and return its body (front-matter stripped).
    // Bodies are no longer resident, so features that need one
    // note's text — headings, backlink contexts — read it on demand.
    QString readNoteBody(const QString &relPath) const;

    // The sidecar's format and file are NoteIndexFile's; the dirty flag and
    // the decision of when to write are this class's, because a failed write
    // has to leave the collection still owing one.
    NoteIndexFile m_indexFile;
    void saveIndexFileIfDirty();
    void markIndexDirty();

    void assignColorsToNewTags(const QStringList &tags);
    // Containment gate for every operation that writes. Returns true when
    // `relPath` resolves inside the vault; otherwise reports the refusal and
    // returns false. Scans already exclude symbolic links, so in practice
    // nothing outside reaches the index — this is the second lock, for paths
    // that arrive from QML or a caller rather than from a scan.
    bool validName(const QString &name, QString *reason) const;
    // True (having reported it) when this collection may not write. The
    // containment gate above asks this first; the operations that change only
    // collection.json do not go through it and ask here directly.
    bool refuseWhenReadOnly();
    QString uniqueUntitled(const QString &folder) const;
    bool moveToTrash(const QString &relPath);
    // Write `relPath`'s cached metadata into its file, merging it over what
    // the file says now: `before` is what this collection believed the file's
    // metadata was when the edit started, so the fields the edit did not
    // touch keep whatever the file has gained since. The body is spliced back
    // byte for byte, and the write is abandoned if the file changed between
    // the read and the commit.
    bool rewriteFrontMatter(const QString &relPath,
                            const NoteFrontMatter::Metadata &before);
    bool prepareOpenDocumentMutation(const QString &relPath);
    // Write the live document out before its file is moved away, so the trash
    // holds the newest revision rather than the last saved one. True when
    // there was nothing to write or the write succeeded.
    bool persistOpenDocumentBeforeRemoval(const QString &relPath);
    void rebindOpenDocument(const QString &oldRelPath,
                            const QString &newRelPath);
    bool openDocumentIs(const QString &relPath) const;
    void renamePathsUnderFolder(const QString &oldPrefix, const QString &newPrefix);

    // Read workspace state back into the index. indexSafeLastOpen keeps a
    // remembered note the background listing has not reached yet, as a
    // placeholder entry, so startup can open it before the scan finishes.
    void loadCollectionFile(bool indexSafeLastOpen = false);
    // Build the snapshot CollectionStateStore writes. Called by the store,
    // including on a retry, so it reads live state rather than a copy taken
    // when a failed write started.
    CollectionStateStore::Snapshot collectionStateSnapshot() const;

    void bump();

    // --- Durable operation plans (operationjournal.h) --------------------
    // One plan covers one user-level operation, however many files it
    // rewrites. It is recorded before the first write and removed once the
    // index sidecar agrees with the files again.
    void beginOperationPlan(const QString &kind, const QJsonObject &payload,
                            const QStringList &files);
    void abandonOwnOperationPlan();
    void finishOperationPlanAfterIndexSave();
    // Forget what the sidecar says about notes named by an unfinished plan,
    // so they are read from disk instead of trusted.
    void dropJournalledEntriesFromCache(QHash<QString, NoteEntry> *cachedNotes);
    // Finish what an interrupted operation left, and report what could not be
    // finished. Runs once per open, after the scan.
    void resumePendingOperations();

    // --- Search-index feed -------------------------------------------------
    // Thin forwards onto SearchIndexFeed, which owns the channel and the
    // root-matching guard. They stay as named methods because the call sites
    // that need them are scattered across scanning, CRUD and metadata writes,
    // and each one reads as a statement about this collection rather than
    // about the index.
    //
    // Open the index for the current root (if needed) and reconcile it against
    // the on-disk listing: the cold build and warm-startup sync. Called at each
    // scan/refresh settle point.
    void syncSearchIndex();
    // Reparse one note into the index (save, create, tag write, rename target).
    void reindexNoteInSearch(const QString &relPath);
    void dropNoteInSearch(const QString &relPath);
    // Every indexed note as the freshness check sees it.
    QList<ReconcileEntry> searchReconcileListing() const;

    SearchIndexFeed m_searchFeed;
    CollectionStateStore m_collectionState;
    OpenDocumentSession *m_openDocument = nullptr;
    OperationJournal m_operations;
    QString m_activeOperationPlan;
    QList<OperationJournal::Plan> m_pendingOperations;

    QString m_rootPath;
    // m_rootPath with every symbolic link resolved. Containment is decided
    // against this, never against m_rootPath's text, so a vault reached
    // through a link still recognizes its own contents.
    QString m_canonicalRoot;
    // Held for as long as this collection has the vault open, so no second
    // process can load the same state and save over this session's writes.
    VaultLock m_vaultLock;
    // <root>/.kvit/trash: where deleted notes and folders land.
    NoteTrashStore m_trash;
    // <root>/.kvit/backups: rotated copies taken before an overwrite.
    NoteBackupStore m_backups;
    // <root>/.kvit/recovery: dirty-state journals, and the pending list a
    // crash leaves behind.
    RecoveryJournalStore m_recoveryJournals;
    int m_revision = 0;

    QHash<QString, NoteEntry> m_notes;    // by relPath
    QMap<QString, FolderEntry> m_folders; // by relPath, sorted

    // Rename-safe links (§3.3). The graph itself — resolution, referrers,
    // backlinks — lives in WikiLinkIndex; what stays here is the two-phase
    // plan built on top of it, because applying one writes files, emits the
    // toast signals and refreshes the index entries.
    using RewriteSnapshot = WikiLinkIndex::RewriteSnapshot;
    struct RenamePlan {
        QString id;
        QString kind; // noteRename, noteMove, folderRename
        QString oldPath;
        QString argument;
        QString newPath;
        QHash<QString, RewriteSnapshot> referrers;
        QString oldFolderPrefix;
        QString newFolderPrefix;
    };
    QVariantMap renamePlanMap(const RenamePlan &plan) const;
    QHash<QString, RewriteSnapshot> snapshotNoteReferrers(
        const QString &relPath) const;
    QHash<QString, RewriteSnapshot> snapshotFolderReferrers(
        const QString &oldPrefix) const;
    RenamePlan m_pendingRenamePlan;

    // --- Rename redirects, and the rewrite they schedule -----------------
    //
    // Applying a rename plan with link updating records one redirect per
    // moved note and returns. Resolution consults the table at once, so no
    // link is ever broken; the files that hold those links are rewritten by
    // a pass that yields to the event loop between short bursts, the same
    // shape DocumentImporter's stepped folder import uses. The open note is
    // the exception: its body belongs to the document session, so it is
    // rewritten in memory here and handed back to the caller to apply.
    // A note an existing redirect points at has moved again; the redirect
    // follows it rather than being left on a path that holds nothing.
    void followRenameInRedirects(const QString &oldRelPath,
                                 const QString &newRelPath);
    // True when something was recorded; the caller decides when to write
    // the table out, so one rename costs one write however many notes moved.
    bool recordRenameRedirects(const RenamePlan &plan);
    QVariantMap startRedirectedLinkRewrite(const RenamePlan &plan,
                                           const QString &openRelPath,
                                           const QString &openBody);
    // Where a link target points once the redirect table is consulted, for
    // targets that name no note at all. "" for everything else, including a
    // target that is ambiguous on its own.
    QString redirectedTargetFor(const QString &rawTarget) const;
    // The text to write in place of `notePart` for a note now at `newPath`:
    // the bare title when that is unambiguous and the link was bare, the
    // full path when the link was qualified or the title would not resolve.
    QString redirectReplacementFor(const QString &notePart,
                                   const QString &newPath) const;
    // Rewrite every target in `text` that only resolves through a redirect,
    // and return how many were rewritten.
    int rewriteRedirectedTargetsInText(QString *text) const;
    bool noteLinksThroughARedirect(const NoteEntry &entry) const;
    // Queue the notes that still name a redirected path and start stepping.
    // Cheap when there are no redirects, which is the normal case.
    void scheduleRedirectRewrite();
    void stepRedirectRewrite();
    void finishRedirectRewrite();
    void cancelRedirectRewrite();
    // Drop the redirects nothing refers to any more. True when that changed
    // the table; writing it out is the caller's.
    bool pruneSettledRedirects();

    LinkRedirects m_redirects;
    QTimer m_redirectTimer;
    QStringList m_redirectQueue;
    int m_redirectIndex = 0;
    int m_redirectLinkCount = 0;
    int m_redirectNoteCount = 0;
    QStringList m_redirectFailed;

    // The [[wiki-link]] graph over m_notes (§3.2/§3.3): target resolution,
    // referrers, backlinks and heading lists. Reads m_notes and never writes
    // it, so it is rebuildable and holds nothing this class does not.
    WikiLinkIndex m_wikiLinks;
    QHash<QString, int> m_folderOwnNoteCounts;
    QHash<QString, int> m_folderRecursiveNoteCounts;

    QHash<QString, QString> m_tagColors;
    QHash<QString, QStringList> m_manualOrder; // folder -> file names
    QString m_lastOpenNote;

    std::function<void(const QString &)> m_indexParseObserver;
    bool m_readOnly = false;
    bool m_indexDirty = false;
    bool m_scanInProgress = false;
    int m_asyncPendingUpdates = 0;
    quint64 m_asyncScanGeneration = 0;
    quint64 m_asyncParseGeneration = 0;
    quint64 m_asyncRefreshGeneration = 0;
    quint64 m_asyncSavedNoteGeneration = 0;
    quint64 m_asyncIndexSaveGeneration = 0;
    QElapsedTimer m_asyncScanTimer;
    QElapsedTimer m_asyncRefreshTimer;
    // The GUI-thread end of the token each running worker holds. Cancelling
    // signals the token and drops the reference; the next task gets a fresh
    // one, so a token is never reused across generations.
    CancellationTokenPtr m_scanCancel;
    CancellationTokenPtr m_refreshCancel;
    QFutureWatcher<AsyncScanListing> m_asyncListingWatcher;
    QFutureWatcher<AsyncIndexResult> m_asyncWatcher;
    QFutureWatcher<AsyncRefreshResult> m_asyncRefreshWatcher;
    QFutureWatcher<AsyncIndexResult> m_asyncSavedNoteWatcher;
    QList<AsyncSavedNoteTask> m_pendingSavedNoteTasks;
    QFutureWatcher<AsyncIndexSaveResult> m_asyncIndexSaveWatcher;
    AsyncIndexSaveRequest m_pendingIndexSaveRequest;
    bool m_indexSaveQueued = false;
    bool m_asyncSavedNotePendingFlush = false;
    QTimer m_asyncRevisionTimer;
};

#endif // NOTECOLLECTION_H
