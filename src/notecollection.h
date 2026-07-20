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

#include <functional>

#include "notefrontmatter.h"

class QFileInfo;
class CollectionSearchIndex;

// The notes-collection object (phase8-plan.md decision 1): one GUI-free
// QObject owning everything above the open document — the notes root, the
// scanned note and folder index, all organization file operations, tags,
// manual order, and the collection.json/index.json sidecars. View models bind to it
// through the revision-counter contract; they never own collection state.
//
// Ownership contract with DocumentManager (decision 2): note BODIES flow
// through DocumentManager (the block model); everything else — metadata,
// create/rename/move/delete, trash — flows through this object. The scan
// writes only the performance index sidecar; front-matter is created lazily on
// the first metadata edit.
//
// Destructive operations move files into <root>/.kvit/trash rather than
// deleting (decision 3); they are not on the editor undo stack.
class NoteCollection : public QObject
{
    Q_OBJECT

    Q_PROPERTY(QString rootPath READ rootPath NOTIFY rootChanged)
    Q_PROPERTY(bool isOpen READ isOpen NOTIFY rootChanged)
    Q_PROPERTY(int revision READ revision NOTIFY revisionChanged)
    Q_PROPERTY(bool scanInProgress READ scanInProgress NOTIFY scanInProgressChanged)

public:
    struct NoteEntry {
        QString relPath;  // "Ideas/Reading list.md", '/'-separated
        QString folder;   // "Ideas"; "" at the root
        QString title;    // file name without ".md"
        QDateTime created;
        QDateTime modified;
        qint64 fileSize = -1;
        int wordCount = 0;
        QString snippet;  // body display text, markers stripped
        NoteFrontMatter::Metadata meta; // tags/pinned/favorite + foreign keys
        // Outgoing [[wiki-link]] targets, raw (heading anchors kept, aliases
        // stripped), in document order with duplicates — backlink counts come
        // from here (pre-launch-plan.md §3.2). Extracted from the file on every
        // (re)index and persisted in the sidecar so warm startup keeps the
        // backlink graph without reading every note (search.md §10 step 4).
        QStringList links;
        // Note bodies and per-block display text are NOT held resident: global
        // search reads them from the SQLite index, and features that need one
        // note's text read that file on demand (search.md §10).
    };

    struct FolderEntry {
        QString relPath;  // "Ideas/Projects"
        QString name;     // "Projects"
        QString color;    // "" = default
        bool expanded = true;
    };

    explicit NoteCollection(QObject *parent = nullptr);
    ~NoteCollection() override;

    // The disk-backed global-search index this collection feeds (search.md).
    // Optional: when unset the collection keeps no search index, so tests and
    // tools that only need collection metadata spawn no worker threads or
    // database. When set, the collection opens it per root and streams note
    // changes to it.
    void setSearchIndex(CollectionSearchIndex *index);

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

    // --- Wiki-links (pre-launch-plan.md §3.2) ------------------------------
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
    Q_INVOKABLE QString createNote(const QString &folder,
                                   const QString &title = QString());
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

    // §9.5 trash management (the Phase 8 deferral): how many top-level
    // items sit in .kvit/trash, and permanent removal of all of them.
    // Bumps the revision so the sidebar's trash row stays current.
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
    // Per-note writing goal (phase11 decision 7): a word target stored in
    // front-matter, byte-preserving the body and restoring mtime like every
    // metadata write. 0 clears it. goalFor returns 0 when unset or unknown.
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

    // --- Backups (decision 10): rotation on a time floor -----------------
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

    // --- Crash recovery (decision 11) -------------------------------------
    // Where the open note's dirty-state journal lives (DocumentManager
    // writes/removes it; the path encodes the relPath).
    Q_INVOKABLE QString journalPathFor(const QString &relPath) const;
    // Journals found at openRoot — crash evidence. [{relPath, title,
    // preview, journalPath}], shrinking as entries restore/discard.
    Q_INVOKABLE QVariantList recoveryEntries() const;
    Q_INVOKABLE bool restoreRecovery(const QString &relPath);
    Q_INVOKABLE void discardRecovery(const QString &relPath);

    // --- Open-note seams (DocumentManager wiring, step 3) ----------------
    // Refreshes one note's index entry after its file was (re)written.
    Q_INVOKABLE void noteSaved(const QString &absPath);
    Q_INVOKABLE void noteSaved(const QString &absPath,
                               const QString &fileText);
    // Canonical front-matter block for a note, from the index metadata.
    Q_INVOKABLE QString frontMatterFor(const QString &relPath) const;

    // Test seam: called when a note body is parsed for the collection index.
    void setIndexParseObserverForTesting(
        std::function<void(const QString &)> observer);

signals:
    void rootChanged();
    void revisionChanged();
    // Rename or move, including notes inside a renamed folder. Receivers
    // (open document, selections, journal) rebind their paths.
    void noteMoved(const QString &oldRelPath, const QString &newRelPath);
    void noteRemoved(const QString &relPath);
    // Rename-safe wiki-links (§3.3): after an in-app rename/move rewrote
    // referring [[links]] in other notes — the "Updated N links in M
    // notes" toast.
    void wikiLinksRewritten(int linkCount, int noteCount);
    void wikiLinkRewriteIncomplete(const QStringList &skipped,
                                   const QStringList &failed);
    void operationFailed(const QString &message);
    void scanInProgressChanged();
    void scanStarted();
    void scanFinished();

private:
    struct AsyncIndexTask {
        QString relPath;
        QString absPath;
        QDateTime createdFallback;
        QDateTime modified;
        qint64 fileSize = -1;
        quint64 generation = 0;
    };

    struct AsyncIndexResult {
        QString relPath;
        NoteEntry entry;
        bool ok = false;
        quint64 generation = 0;
    };

    struct AsyncScanRequest {
        QString rootPath;
        QHash<QString, NoteEntry> cachedNotes;
        bool indexOk = false;
        bool indexFileExists = false;
        quint64 generation = 0;
    };

    struct AsyncScanListing {
        QString rootPath;
        QList<FolderEntry> folders;
        QList<NoteEntry> entries;
        QList<AsyncIndexTask> tasks;
        bool indexDirty = false;
        quint64 generation = 0;
    };

    struct AsyncRefreshRequest {
        QString rootPath;
        QStringList relDirs;
        QHash<QString, NoteEntry> currentNotes;
        quint64 generation = 0;
    };

    struct AsyncRefreshResult {
        QString rootPath;
        QStringList relDirs;
        QStringList missingDirs;
        QList<FolderEntry> folders;
        QList<NoteEntry> entries;
        QSet<QString> seenNotes;
        QSet<QString> seenFolders;
        quint64 generation = 0;
    };

    struct AsyncSavedNoteTask {
        QString relPath;
        QString absPath;
        QString fileText;
        QDateTime createdFallback;
        QDateTime modified;
        qint64 fileSize = -1;
        quint64 generation = 0;
    };

    struct AsyncIndexSaveRequest {
        QString path;
        QHash<QString, NoteEntry> notes;
        quint64 generation = 0;
    };

    struct AsyncIndexSaveResult {
        QString path;
        int notes = 0;
        int bytes = 0;
        bool ok = false;
        quint64 generation = 0;
    };

    bool prepareRootPath(const QString &path);
    void loadRecoveryEntries();
    void scan();
    void scanDirectory(const QString &relDir,
                       const QHash<QString, NoteEntry> &cachedNotes);
    void scanAsync();
    void indexNote(const QString &relPath);
    void indexNote(const QString &relPath,
                   const QHash<QString, NoteEntry> &cachedNotes);
    void indexNoteFromText(const QString &relPath,
                           const QString &fileText,
                           const QFileInfo &info);
    bool tryIndexNoteFromCache(const QString &relPath,
                               const QFileInfo &info,
                               const QHash<QString, NoteEntry> &cachedNotes);
    static NoteEntry placeholderEntry(const QString &relPath,
                                      const QFileInfo &info);
    static NoteEntry cachedEntryForPath(const QString &relPath,
                                        const NoteEntry &cached,
                                        const QFileInfo &info);
    static NoteEntry entryFromText(const QString &relPath,
                                   const QString &fileText,
                                   const QFileInfo &info);
    static AsyncScanListing buildAsyncScanListing(
        const AsyncScanRequest &request);
    static AsyncIndexResult parseIndexTask(const AsyncIndexTask &task);
    static AsyncRefreshResult buildAsyncRefreshResult(
        const AsyncRefreshRequest &request);
    static AsyncIndexResult parseSavedNoteTask(
        const AsyncSavedNoteTask &task);
    static QByteArray buildIndexFileBytes(
        const QHash<QString, NoteEntry> &notes);
    static AsyncIndexSaveResult writeIndexFileSnapshot(
        const AsyncIndexSaveRequest &request);
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

    struct BodyStats {
        int wordCount = 0;
        QString snippet;
    };
    static BodyStats analyzeBody(const QString &markdownBody);

    // Read one note file from disk and return its body (front-matter stripped).
    // Bodies are no longer resident (search.md §10), so features that need one
    // note's text — headings, backlink contexts — read it on demand.
    QString readNoteBody(const QString &relPath) const;

    QString indexFilePath() const;
    QHash<QString, NoteEntry> loadIndexFile(bool *ok) const;
    void saveIndexFile() const;
    void saveIndexFileIfDirty();
    void markIndexDirty();

    void assignColorsToNewTags(const QStringList &tags);
    bool validName(const QString &name, QString *reason) const;
    QString uniqueUntitled(const QString &folder) const;
    bool moveToTrash(const QString &relPath);
    bool rewriteFrontMatter(const QString &relPath);
    void renamePathsUnderFolder(const QString &oldPrefix, const QString &newPrefix);

    void loadCollectionFile();
    void saveCollectionFile();

    void bump();

    // --- Search-index feed (search.md §5.2/§6) ---------------------------
    // Open the index for the current root (if needed) and reconcile it against
    // the on-disk listing: the cold build and warm-startup sync. Called at each
    // scan/refresh settle point.
    void syncSearchIndex();
    // Reparse one note into the index (save, create, tag write, rename target).
    void reindexNoteInSearch(const QString &relPath);
    void dropNoteInSearch(const QString &relPath);

    CollectionSearchIndex *m_searchIndex = nullptr;
    QString m_searchIndexRoot; // the root the index is currently open for

    QString m_rootPath;
    int m_revision = 0;

    QHash<QString, NoteEntry> m_notes;    // by relPath
    QMap<QString, FolderEntry> m_folders; // by relPath, sorted

    // Rename-safe links (§3.3): referrer relPath → the lowercased
    // note-part keys in it that resolve to `relPath` right now. Snapshot
    // BEFORE a rename/move, applied after.
    QHash<QString, QSet<QString>> collectWikiReferrers(
        const QString &relPath) const;
    struct RewriteSnapshot {
        QSet<QString> keys;
        QByteArray hash;
        QDateTime modified;
        int linkCount = 0;
    };
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
    QVariantMap applyWikiLinkRewrites(const RenamePlan &plan,
                                      const QString &openRelPath,
                                      const QString &openBody);
    RenamePlan m_pendingRenamePlan;

    // Lazy wiki-resolution cache (§3.2): lowercased basename → relPaths,
    // rebuilt when the revision or note count moves. Mutable because
    // resolveWikiTarget is a const query.
    void ensureWikiIndex() const;
    void invalidateWikiIndex() const { m_wikiIndexRevision = -1; }
    mutable QHash<QString, QStringList> m_wikiBasenames;
    mutable int m_wikiIndexRevision = -1;
    mutable int m_wikiIndexNoteCount = -1;
    QHash<QString, int> m_folderOwnNoteCounts;
    QHash<QString, int> m_folderRecursiveNoteCounts;

    QHash<QString, QString> m_tagColors;
    QHash<QString, QStringList> m_manualOrder; // folder -> file names
    QString m_lastOpenNote;

    QStringList m_pendingRecovery; // relPaths with startup journals
    std::function<QDateTime()> m_clock; // null = QDateTime::currentDateTime
    std::function<void(const QString &)> m_indexParseObserver;
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
