// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
#ifndef DOCUMENTMANAGER_H
#define DOCUMENTMANAGER_H

#include <QDateTime>
#include <QElapsedTimer>
#include <QFutureWatcher>
#include <QList>
#include <QObject>
#include <QString>
#include <QUrl>
#include <QTimer>
#include <QVariantMap>
#include <QAtomicInt>
#include <QSharedPointer>

#include "block.h"
#include "cancellationtoken.h"
#include "opendocumentsession.h"

class BlockModel;
class UndoStack;
class DocumentSerializer;

// Implements OpenDocumentSession: the repository reaches the live document
// only through that interface, so the note filesystem layer never has to
// know this class.
class DocumentManager : public QObject, public OpenDocumentSession
{
    Q_OBJECT

    Q_PROPERTY(QString currentFilePath READ currentFilePath NOTIFY currentFilePathChanged)
    Q_PROPERTY(QString currentFileName READ currentFileName NOTIFY currentFilePathChanged)
    Q_PROPERTY(bool isDirty READ isDirty NOTIFY isDirtyChanged)
    Q_PROPERTY(bool hasFile READ hasFile NOTIFY currentFilePathChanged)
    // §9.7 status bar: when the open document last reached disk (save
    // time, or the file's modification time right after open); invalid
    // for a new unsaved document.
    Q_PROPERTY(QDateTime lastSavedAt READ lastSavedAt NOTIFY lastSavedAtChanged)
    Q_PROPERTY(bool autoSaveEnabled READ autoSaveEnabled WRITE setAutoSaveEnabled NOTIFY autoSaveEnabledChanged)
    Q_PROPERTY(int autoSaveInterval READ autoSaveInterval WRITE setAutoSaveInterval NOTIFY autoSaveIntervalChanged)
    // Oversized-file guard: files over this many MiB are refused before
    // any read — the UI shows a placeholder with an explicit "Open
    // anyway". 0 or negative disables the cap. Stored in the settings
    // store as maxOpenFileSizeMiB, adjustable without a rebuild.
    Q_PROPERTY(int maxOpenFileSizeMiB READ maxOpenFileSizeMiB
                   WRITE setMaxOpenFileSizeMiB NOTIFY maxOpenFileSizeMiBChanged)
    Q_PROPERTY(bool openInProgress READ openInProgress NOTIFY openInProgressChanged)
    // Crash-recovery journal: while the document is dirty, a debounced
    // snapshot writes here; a clean save removes it. "" disables
    // (single-file mode, no collection).
    Q_PROPERTY(QString journalPath READ journalPath WRITE setJournalPath
                   NOTIFY journalPathChanged)

public:
    explicit DocumentManager(QObject *parent = nullptr);
    ~DocumentManager() override;

    // Set the model and undo stack to manage
    void setBlockModel(BlockModel *model);
    void setUndoStack(UndoStack *stack);

    // File path
    QString currentFilePath() const { return m_currentFilePath; }
    QString openFilePath() const override { return m_currentFilePath; }
    QString currentFileName() const;
    bool hasFile() const { return !m_currentFilePath.isEmpty(); }
    bool openInProgress() const { return m_asyncOpenInProgress; }

    // Dirty state (unsaved changes)
    bool isDirty() const;

    // Auto-save configuration
    bool autoSaveEnabled() const { return m_autoSaveEnabled; }
    void setAutoSaveEnabled(bool enabled);
    int autoSaveInterval() const { return m_autoSaveInterval; }
    void setAutoSaveInterval(int seconds);

    // Front-matter of the open note, exactly as split off the file on
    // load ("" when the file had none). Bodies flow through the block
    // model; this block flows through verbatim on save, so foreign
    // metadata survives editing. The collection replaces it when
    // metadata changes.
    QString frontMatter() const { return m_frontMatter; }
    // Replacing the metadata block is an edit to the open document like any
    // other: it makes the document differ from what is on disk, so it marks it
    // dirty and advances the revision. Anything less let a metadata change be
    // overwritten by an older body snapshot that was already in flight, or be
    // dropped entirely because the document still called itself clean.
    Q_INVOKABLE void setFrontMatter(const QString &block);
    // Persist repository-owned metadata together with the authoritative live
    // body. On failure the previous metadata state is restored while body
    // dirtiness is retained, so callers can roll back their index mutation
    // without leaving the document and repository disagreeing.
    bool saveWithFrontMatter(const QString &block) override;

    // OpenDocumentSession, the destructive-operation half. The repository asks
    // these before it moves the open note's file to the trash, because the
    // file is only the newest revision if the session says so: with autosave
    // off, or simply before the save debounce fires, everything typed since
    // the last write lives here and nowhere else.
    bool hasUnsavedChanges() const override;
    // Synchronous by contract — a rename follows immediately, and an
    // asynchronous save would be racing it. Waits out any write already in
    // flight, then writes and reports whether the file holds the current
    // document.
    bool persistCurrentRevision() override;

    // File operations
    QDateTime lastSavedAt() const { return m_lastSavedAt; }

    Q_INVOKABLE bool save();
    Q_INVOKABLE bool saveAsync();
    Q_INVOKABLE bool saveAs(const QUrl &fileUrl);
    // ignoreSizeCap is the "Open anyway" path: the same load, unmodified —
    // slow but correct, and the choice is informed consent.
    Q_INVOKABLE bool open(const QUrl &fileUrl, bool ignoreSizeCap = false);
    Q_INVOKABLE bool openAsync(const QUrl &fileUrl, bool ignoreSizeCap = false);
    Q_INVOKABLE void newDocument();
    void closeDocument() override { newDocument(); }

    int maxOpenFileSizeMiB() const { return m_maxOpenFileSizeMiB; }
    void setMaxOpenFileSizeMiB(int mib);

    // The open file was renamed or moved on disk (a collection rename of
    // the open note): rebind the path without reloading — content, undo
    // history, and dirty state all continue.
    Q_INVOKABLE void rebindFilePath(const QString &newPath) override;

    // Abandon any write still in flight and wait for its worker to stop.
    //
    // An async save owns the path it was handed. Once the note has been
    // renamed, moved, or deleted underneath it, letting that worker finish
    // recreates the file at the old location — outside the trash after a
    // delete, or as a duplicate after a rename. Every operation that changes
    // where the open note lives must call this first, so there is exactly one
    // writer and it is the one that knows the current path.
    Q_INVOKABLE void cancelPendingWrites() override;

    // Ask every editor holding uncommitted text to write it into the model now.
    //
    // Several block editors keep their text outside BlockModel until a debounce
    // timer fires or focus is lost: query source, diagram source, math source,
    // image captions, callout titles. Until that happens the model does not
    // have the edit and isDirty() does not know about it, so a save, an export,
    // a note switch or a shutdown could all act on a document that was missing
    // the user's most recent typing. Anything that reads or persists the
    // document must call this first.
    Q_INVOKABLE void flushPendingEdits() override;

    // Replace the whole document body with the given markdown as ONE
    // undo step (restore from backup, template instantiation).
    Q_INVOKABLE bool restoreBody(const QString &markdown);

    // Brackets a whole-document replacement that IS the new baseline rather
    // than an edit to the current one: this manager's own file loads, and the
    // sample document startup falls back to. Everything else that resets the
    // block model is a user replacement, and is treated as an unsaved change.
    //
    // The distinction has to exist because a model reset carries no
    // dataChanged and no row signals, and leaves the undo stack clean. Without
    // it a replaced document reported itself saved, wrote no recovery journal,
    // and an earlier asynchronous save that landed afterwards still matched on
    // path, revision and undo index — so it marked the replaced body clean and
    // deleted the journal that was the only other copy.
    Q_INVOKABLE void beginBaselineLoad();
    Q_INVOKABLE void endBaselineLoad();

    QString journalPath() const { return m_journalPath; }
    void setJournalPath(const QString &path);
    // Test seam: the journal debounce (default 2000 ms).
    Q_INVOKABLE void setJournalDebounceMs(int ms);
    void setAsyncPersistenceDelayMsForTests(int ms);
    void setAsyncPreCommitDelayMsForTests(int ms);
    bool asyncPreCommitReachedForTests() const;

    // C++ file dialogs (more reliable than QML FileDialog)
    Q_INVOKABLE void openFileDialog();
    // True only when the document reached disk. A cancelled dialog and a
    // failed write both return false, because both mean the in-memory
    // document is still the only copy.
    Q_INVOKABLE bool saveFileDialog();

    // For QML file dialogs (backup)
    Q_INVOKABLE QString getDefaultSavePath() const;
    Q_INVOKABLE QUrl toLocalFileUrl(const QString &path) const;
    // The inverse, and the only correct way to get a local path out of a URL
    // a file dialog handed QML. Stripping the "file://" prefix by hand yields
    // "/C:/notes/x.md" for a Windows drive URL, drops the host of a UNC URL
    // (file://server/share/x.md), and leaves percent escapes (%20, %23, %25)
    // in place, so the resulting "path" names a file that does not exist.
    // QUrl::toLocalFile() handles all three. A URL that is not a local file
    // yields an empty string, which callers must treat as "no path".
    Q_INVOKABLE QString toLocalPath(const QUrl &fileUrl) const;

signals:
    void lastSavedAtChanged();
    void currentFilePathChanged();
    void isDirtyChanged();
    void autoSaveEnabledChanged();
    void autoSaveIntervalChanged();
    void maxOpenFileSizeMiBChanged();
    // The file exceeds the size cap and was refused without a read; the UI
    // shows the placeholder with an "Open anyway" action.
    void openRejectedTooLarge(const QString &filePath, double sizeBytes,
                              double capBytes);
    void journalPathChanged();
    void openInProgressChanged();
    void openAsyncFinished(const QString &filePath, bool ok);

    // Emitted just before the file is overwritten — the backup
    // rotation's hook.
    void aboutToSave(const QString &filePath);

    // UI notifications
    void saveSucceeded(const QString &filePath);
    // The exact full-file snapshot that committed. Repository/index wiring
    // consumes this in C++ so QML never has to reconstruct persistence state.
    void saveSucceededWithText(const QString &filePath, const QString &fileText);
    void saveFailed(const QString &error);
    void openSucceeded(const QString &filePath);
    void openFailed(const QString &error);
    void documentModified();

    // Editors holding uncommitted text must commit it synchronously in
    // response to this. Connected on the QML side by the block delegates.
    void pendingEditsRequested();

private slots:
    void onAutoSaveTimer();
    void onUndoStackCleanChanged();

private slots:
    void onModelReset();
    void onDocumentChangedForJournal();
    void writeJournal();
    void onAsyncOpenFinished();
    void onAsyncSaveFinished();
    void onAsyncJournalFinished();

private:
    enum class SaveKind {
        Manual,
        AutoSave,
    };

    struct AsyncOpenResult {
        QString filePath;
        QString error;
        QString frontMatter;
        QDateTime lastModified;
        QList<Block::State> states;
        quint64 generation = 0;
        double workerDurationMs = 0.0;
        bool ok = false;
        bool loadDiverged = false;
    };

    // Shared between the GUI thread and one write worker. The worker reads it
    // immediately before committing, which is the last moment the write can
    // still be called off; QSaveFile discards its temporary file instead of
    // renaming it over the target, so nothing reaches the note's old path.
    //
    // This is the same primitive the collection's background walks use
    // (CancellationToken), deliberately placed differently. A walk checks
    // repeatedly because it can stop between any two files and lose nothing
    // but unfinished work. A write has exactly one safe point: before
    // commit() everything is confined to a temporary file and abandoning it
    // costs nothing, while after commit() the bytes are already at the
    // target and there is nothing left to call off. So the type is shared
    // and the placement is not.
    using WriteCancellationPtr = CancellationTokenPtr;

    struct PersistenceWriteResult {
        QString operation;
        QString path;
        QString error;
        QString fileText;
        QVariantMap context;
        quint64 generation = 0;
        quint64 contentHash = 0;
        quint64 documentRevision = 0;
        int contentChars = 0;
        int undoIndex = -1;
        int delayMs = 0;
        double openMs = 0.0;
        double writeMs = 0.0;
        double flushMs = 0.0;
        double commitMs = 0.0;
        bool opened = false;
        bool committed = false;
        // The write was called off before it could rename over the target,
        // because the note moved. Distinct from a failure: nothing went wrong
        // and there is nothing to report to the user.
        bool cancelled = false;
    };

    static AsyncOpenResult loadFileForOpen(const QString &filePath,
                                           quint64 generation);
    static PersistenceWriteResult writeSnapshotToFile(
        const QString &operation,
        const QString &filePath,
        const QString &fileText,
        QVariantMap context,
        quint64 generation,
        quint64 contentHash,
        quint64 documentRevision,
        int undoIndex,
        int delayMs,
        int preCommitDelayMs,
        QSharedPointer<QAtomicInt> preCommitReached,
        WriteCancellationPtr cancel);

    void setLastSavedAt(const QDateTime &when);
    QDateTime m_lastSavedAt;
    bool saveToFile(const QString &filePath,
                    SaveKind kind = SaveKind::Manual);
    bool saveToFileAsync(const QString &filePath,
                         SaveKind kind = SaveKind::Manual);
    bool loadFromFile(const QString &filePath);
    bool rejectIfOverSizeCap(const QString &filePath);
    QString currentFileText() const;
    void maybeBackupBeforeDivergingWrite(const QString &filePath);
    void removeJournal();
    void invalidateAsyncOpen();
    void setAsyncOpenInProgress(bool inProgress);
    QVariantMap persistenceContext(const QString &filePath) const;
    static quint64 textHash(const QString &text);
    void recordPersistenceWriteSplits(const PersistenceWriteResult &result);
    void waitForPendingPersistence();

    BlockModel *m_model = nullptr;
    UndoStack *m_undoStack = nullptr;
    DocumentSerializer *m_serializer = nullptr;

    QString m_currentFilePath;
    QString m_frontMatter;
    // Set at load when the freshly parsed model would serialize to bytes
    // that differ from what was read — the first save to the same path
    // copies the on-disk file to <filename>.bak first, then disarms.
    bool m_loadDiverged = false;
    // Front matter is not part of the block model, so the undo stack cannot
    // speak for it. Without this the document reported itself clean while
    // holding metadata that had never been written.
    bool m_frontMatterDirty = false;
    // A whole-document replacement the undo stack cannot speak for either,
    // for the same reason: the reset that performed it pushed no command.
    bool m_bodyReplaced = false;
    // Nesting depth of beginBaselineLoad()/endBaselineLoad().
    int m_baselineLoadDepth = 0;
    bool m_autoSaveEnabled = true;
    int m_autoSaveInterval = 30;  // seconds
    int m_maxOpenFileSizeMiB = 10;  // three War-and-Peaces (provisional,
                                    // validated by the perf corpora)
    QTimer m_autoSaveTimer;

    QString m_journalPath;
    QTimer m_journalTimer; // single-shot debounce

    QFutureWatcher<AsyncOpenResult> m_asyncOpenWatcher;
    QElapsedTimer m_asyncOpenTimer;
    quint64 m_asyncOpenGeneration = 0;
    bool m_asyncOpenInProgress = false;

    QFutureWatcher<PersistenceWriteResult> m_asyncSaveWatcher;
    QFutureWatcher<PersistenceWriteResult> m_asyncJournalWatcher;
    quint64 m_asyncSaveGeneration = 0;
    quint64 m_asyncJournalGeneration = 0;
    quint64 m_lastJournalHash = 0;
    int m_lastJournalChars = -1;
    quint64 m_pendingJournalHash = 0;
    int m_pendingJournalChars = -1;
    quint64 m_documentRevision = 0;
    // Held for as long as a note write is in flight, so a path change can call
    // it off. Journal writes are deliberately excluded: the journal lives at a
    // fixed control path that note renames do not move.
    WriteCancellationPtr m_activeWriteCancel;
    bool m_autosaveRequestedWhileRunning = false;
    bool m_journalRequestedWhileRunning = false;
    int m_asyncPersistenceDelayMs = 0;
    int m_asyncPreCommitDelayMs = 0;
    QSharedPointer<QAtomicInt> m_activePreCommitReached;
};

#endif // DOCUMENTMANAGER_H
