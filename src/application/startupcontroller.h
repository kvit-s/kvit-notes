// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
#ifndef STARTUPCONTROLLER_H
#define STARTUPCONTROLLER_H

#include <QObject>
#include <QElapsedTimer>
#include <QString>
#include <QSet>

class BlockModel;
class DocumentManager;
class NoteCollection;
class UndoStack;

// Owns the collection-mode startup flow that used to live in main.cpp.
// The app invokes it after the first frame so collection scans and initial
// note loading cannot block window creation.
class StartupController : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool started READ started NOTIFY startedChanged)
    Q_PROPERTY(bool finished READ finished NOTIFY finishedChanged)

public:
    explicit StartupController(QObject *parent = nullptr);

    void setCollection(NoteCollection *collection);
    void setDocumentManager(DocumentManager *manager);
    void setBlockModel(BlockModel *model);
    void setUndoStack(UndoStack *stack);
    void setRootPath(const QString &path);

    bool started() const { return m_started; }
    bool finished() const { return m_finished; }

    Q_INVOKABLE void start();

signals:
    void startedChanged();
    void finishedChanged();

private:
    void initializeFallbackDocument();
    bool openStartupNote(const QString &relPath);
    void onStartupNoteOpenFinished(const QString &filePath, bool ok);
    void tryFinishStartup();
    void finishStartup();

    NoteCollection *m_collection = nullptr;
    DocumentManager *m_documentManager = nullptr;
    BlockModel *m_blockModel = nullptr;
    UndoStack *m_undoStack = nullptr;
    QString m_rootPath;
    QString m_pendingStartupRelPath;
    QElapsedTimer m_initialOpenTimer;
    QSet<QString> m_failedStartupNotes;
    // openAsync() can fail synchronously — an oversized first candidate is
    // refused before any read — and it reports that failure by emitting
    // openAsyncFinished(false) BEFORE it returns. The handler then starts the
    // next candidate from inside that call, so by the time the failing call
    // regains control, m_pendingStartupRelPath and m_initialOpenInProgress
    // already describe a newer, valid request. Stamping each request lets the
    // failing call recognise that and leave the newer one alone.
    quint64 m_openRequestGeneration = 0;
    bool m_started = false;
    bool m_finished = false;
    bool m_initialOpenInProgress = false;
};

#endif // STARTUPCONTROLLER_H
