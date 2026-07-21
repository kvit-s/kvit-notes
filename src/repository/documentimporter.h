// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
#ifndef DOCUMENTIMPORTER_H
#define DOCUMENTIMPORTER_H

#include <QList>
#include <QObject>
#include <QPair>
#include <QString>
#include <QStringList>
#include <QTimer>
#include <QVariantMap>

#include "cancellationtoken.h"

class NoteCollection;

// Import into the collection (features.md §12.6): copy markdown/text files —
// one, a batch, or a whole folder tree — into a target folder, parsing each
// through the same front-matter split and serializer as a native note, so an
// imported note is indistinguishable from one written in Kvit. Whole-folder
// import recreates the subfolder tree; name collisions get the collection's
// " N" suffixing; the Obsidian-vault case is free because front-matter is
// preserved verbatim.
//
// Import is a collection operation, not an editor undo step (the trash is the
// safety net); a dry-run summary precedes the copy. Exposed as the
// `documentImporter` context property.
class DocumentImporter : public QObject
{
    Q_OBJECT

    // How many files the run in progress (or the last one) declined. A
    // property rather than a plain method because it climbs as the run
    // proceeds and the dialog reports it live, and because QML cannot call a
    // method that is not invokable.
    Q_PROPERTY(int lastSkippedCount READ lastSkippedCount
                   NOTIFY lastSkippedCountChanged)
    // True between startImportFolder() and importFinished().
    Q_PROPERTY(bool importInProgress READ importInProgress
                   NOTIFY importInProgressChanged)

public:
    explicit DocumentImporter(QObject *parent = nullptr);

    void setCollection(NoteCollection *collection);

    // Dry-run preview for a set of files into targetFolder: a map of
    // {"files", "collisions", "folder"}.
    Q_INVOKABLE QVariantMap dryRunFiles(const QStringList &paths,
                                        const QString &targetFolder) const;
    // Dry-run for a folder (recursive .md/.txt): {"files", "collisions",
    // "folders"}.
    Q_INVOKABLE QVariantMap dryRunFolder(const QString &dirPath,
                                         const QString &targetFolder) const;

    // Copy the given files into targetFolder, collision-suffixed, front-matter
    // preserved; refresh the index. Returns the number imported.
    Q_INVOKABLE int importFiles(const QStringList &paths,
                                const QString &targetFolder);
    // Import a folder recursively, recreating the subfolder tree under
    // targetFolder. Returns the number imported.
    Q_INVOKABLE int importFolder(const QString &dirPath,
                                 const QString &targetFolder);

    // The same two imports, callable off the GUI thread and stoppable part
    // way. A run that is called off keeps the notes it has already committed
    // — each file is one atomic copy — and reports how many those were.
    // Passing a null token is exactly the two calls above.
    int importFiles(const QStringList &paths, const QString &targetFolder,
                    const CancellationTokenPtr &cancel);
    int importFolder(const QString &dirPath, const QString &targetFolder,
                     const CancellationTokenPtr &cancel);

    // Import a folder without holding the GUI thread.
    //
    // importFolder() above copies every file before it returns, so nothing
    // else runs while it does: the progress label cannot repaint and the
    // cancel button cannot be pressed, which makes both of them decorative on
    // exactly the imports big enough to need them. This starts the same
    // import and returns immediately, then copies in short bursts between
    // event-loop turns until it is done. Everything stays on the GUI thread —
    // the copy goes through the collection, which is not thread-safe — so
    // what buys the responsiveness is yielding often rather than moving the
    // work elsewhere.
    //
    // importProgress carries the real totals for the whole folder, and
    // requestCancel() stops it at the next file; importFinished() reports the
    // outcome exactly once. A call while a run is already in progress is
    // ignored.
    Q_INVOKABLE void startImportFolder(const QString &dirPath,
                                       const QString &targetFolder);
    bool importInProgress() const { return m_running; }

    // The importable files under a directory, as absolute paths, in the order
    // an import would take them. The dialog uses it to size its progress bar
    // and to show what a folder import would cover.
    Q_INVOKABLE QStringList listImportableFiles(const QString &dirPath) const;

    // Cancellation for a run started from QML, which has no token to hold.
    // requestCancel() signals whichever run is in progress; the next run
    // starts from a clear token, so a cancel never leaks into it. Safe to
    // call when nothing is running.
    Q_INVOKABLE void requestCancel();

    // The largest single file this importer will copy. A note is text a
    // person wrote, and the previous behaviour — read the whole file into
    // memory, whatever it was — turned one mistaken selection of a disk image
    // into an out-of-memory failure. Anything larger is skipped and counted
    // in `skipped`. 0 removes the cap.
    static qint64 maxFileBytes();
    static void setMaxFileBytes(qint64 bytes);

    // How many files the last run skipped because they were too large or
    // could not be read completely. Imported + skipped is what was attempted.
    int lastSkippedCount() const { return m_lastSkipped; }

    // Whether a path is an importable note file (.md or .txt).
    static bool isImportable(const QString &path);

signals:
    // Emitted after each file the run finishes with, so a caller driving the
    // import from a worker thread can show progress and offer the cancel that
    // requestCancel() implements.
    void importProgress(int done, int total);
    // A startImportFolder() run is over, once per run. `cancelled` says
    // whether it stopped early; whatever it had already copied is on disk
    // either way.
    void importFinished(int imported, int skipped, bool cancelled);
    void lastSkippedCountChanged();
    void importInProgressChanged();

private:
    // A collision-free note relPath under `folder` (relPath, "" = root) for the
    // desired base name (without extension): base.md, then "base 2.md", …
    QString uniqueRelPath(const QString &folder, const QString &baseName) const;
    bool noteFileExists(const QString &relPath) const;
    // Copy source bytes to <root>/<targetRelPath>, creating parent dirs. The
    // copy is streamed in fixed-size blocks rather than materialized, so the
    // memory it costs does not depend on the size of the file, and it stops
    // at maxFileBytes() rather than reading whatever it was handed.
    bool copyInto(const QString &sourcePath, const QString &targetRelPath);
    // The list of importable files under a directory, as (absPath, relSubDir).
    QList<QPair<QString, QString>> importableFilesUnder(
        const QString &dirPath, const CancellationTokenPtr &cancel = {}) const;
    // The token a run should watch: the caller's if it passed one, otherwise
    // the one requestCancel() signals.
    CancellationTokenPtr tokenForRun(const CancellationTokenPtr &cancel);

    // Copy one enumerated file into its mirrored destination folder. True
    // when it was imported; a false answer has already been counted as
    // skipped. Shared by the blocking folder import and the stepped one, so
    // the two cannot drift on where a file lands.
    bool importOne(const QString &absSource, const QString &relSubDir,
                   const QString &targetFolder);
    // One burst of the stepped folder import, scheduled between event-loop
    // turns until the queue is empty or the run is called off.
    void stepImportFolder();
    void finishSteppedImport(bool cancelled);
    void setLastSkipped(int skipped);

    NoteCollection *m_collection = nullptr;
    CancellationTokenPtr m_qmlCancel;
    int m_lastSkipped = 0;

    // The stepped folder import in flight: what is left to copy, where it
    // goes, and what has happened so far.
    QTimer m_stepTimer;
    QList<QPair<QString, QString>> m_queue;   // (absPath, relSubDir)
    int m_queueIndex = 0;
    QString m_stepTargetFolder;
    int m_stepImported = 0;
    bool m_running = false;
};

#endif // DOCUMENTIMPORTER_H
