// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
#ifndef DOCUMENTIMPORTER_H
#define DOCUMENTIMPORTER_H

#include <QObject>
#include <QString>
#include <QStringList>
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

    // Cancellation for a run started from QML, which has no token to hold.
    // requestCancel() signals whichever run is in progress; the next run
    // starts from a clear token, so a cancel never leaks into it.
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

    NoteCollection *m_collection = nullptr;
    CancellationTokenPtr m_qmlCancel;
    int m_lastSkipped = 0;
};

#endif // DOCUMENTIMPORTER_H
