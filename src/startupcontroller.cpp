// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
#include "startupcontroller.h"

#include "blockmodel.h"
#include "documentmanager.h"
#include "notecollection.h"
#include "perflog.h"
#include "undostack.h"

#include <QUrl>
#include <QStringList>
#include <QVariantMap>

StartupController::StartupController(QObject *parent)
    : QObject(parent)
{
}

void StartupController::setCollection(NoteCollection *collection)
{
    m_collection = collection;
}

void StartupController::setDocumentManager(DocumentManager *manager)
{
    if (m_documentManager)
        disconnect(m_documentManager, nullptr, this, nullptr);
    m_documentManager = manager;
    if (m_documentManager) {
        connect(m_documentManager, &DocumentManager::openAsyncFinished,
                this, &StartupController::onStartupNoteOpenFinished,
                Qt::UniqueConnection);
    }
}

void StartupController::setBlockModel(BlockModel *model)
{
    m_blockModel = model;
}

void StartupController::setUndoStack(UndoStack *stack)
{
    m_undoStack = stack;
}

void StartupController::setRootPath(const QString &path)
{
    m_rootPath = path;
}

void StartupController::start()
{
    if (m_started)
        return;
    m_started = true;
    emit startedChanged();

    if (m_rootPath.isEmpty()) {
        finishStartup();
        return;
    }

    if (!m_collection || !m_documentManager || !m_blockModel || !m_undoStack) {
        initializeFallbackDocument();
        finishStartup();
        return;
    }

    if (m_collection->openRootAsync(m_rootPath)) {
        connect(m_collection, &NoteCollection::revisionChanged,
                this, &StartupController::tryFinishStartup,
                Qt::UniqueConnection);
        connect(m_collection, &NoteCollection::scanFinished,
                this, &StartupController::tryFinishStartup,
                Qt::UniqueConnection);
        tryFinishStartup();
        return;
    }

    initializeFallbackDocument();
    finishStartup();
}

void StartupController::initializeFallbackDocument()
{
    if (!m_blockModel || !m_undoStack)
        return;
    m_blockModel->initializeWithSampleData();
    m_undoStack->clear();
    m_undoStack->setClean();
}

bool StartupController::openStartupNote(const QString &relPath)
{
    if (relPath.isEmpty())
        return false;
    if (m_failedStartupNotes.contains(relPath))
        return false;
    if (m_initialOpenInProgress)
        return true;

    m_pendingStartupRelPath = relPath;
    m_initialOpenInProgress = true;
    m_initialOpenTimer.restart();

    if (!m_documentManager->openAsync(QUrl::fromLocalFile(
            m_collection->absolutePath(relPath)))) {
        m_initialOpenInProgress = false;
        m_pendingStartupRelPath.clear();
        return false;
    }

    return true;
}

void StartupController::onStartupNoteOpenFinished(const QString &filePath,
                                                  bool ok)
{
    if (!m_initialOpenInProgress || !m_collection)
        return;

    const QString expectedPath =
        m_collection->absolutePath(m_pendingStartupRelPath);
    if (filePath != expectedPath)
        return;

    const QString relPath = m_pendingStartupRelPath;
    m_pendingStartupRelPath.clear();
    m_initialOpenInProgress = false;

    PerfLog::instance().record(
        QStringLiteral("startup.initial_open"),
        m_initialOpenTimer.elapsed(),
        QVariantMap{{QStringLiteral("note"), relPath},
                    {QStringLiteral("blocks"),
                     m_blockModel ? m_blockModel->count() : 0},
                    {QStringLiteral("async"), true},
                    {QStringLiteral("ok"), ok}});

    if (ok) {
        m_collection->setLastOpenNote(relPath);
        finishStartup();
        return;
    }

    m_failedStartupNotes.insert(relPath);
    tryFinishStartup();
}

void StartupController::tryFinishStartup()
{
    if (m_finished)
        return;
    if (m_initialOpenInProgress)
        return;
    if (!m_collection || !m_collection->isOpen()) {
        initializeFallbackDocument();
        finishStartup();
        return;
    }

    QString startNote = m_collection->lastOpenNote();
    if (m_failedStartupNotes.contains(startNote))
        startNote.clear();
    if (startNote.isEmpty()) {
        const QStringList all = m_collection->noteRelPaths();
        for (const QString &candidate : all) {
            if (!m_failedStartupNotes.contains(candidate)) {
                startNote = candidate;
                break;
            }
        }
    }

    if (openStartupNote(startNote)) {
        return;
    }

    if (m_collection->noteCount() > 0) {
        if (m_collection->scanInProgress())
            return;
        initializeFallbackDocument();
        finishStartup();
        return;
    }

    if (m_collection->scanInProgress())
        return;

    m_collection->initializeIfEmpty();
    const QStringList all = m_collection->noteRelPaths();
    if (!all.isEmpty() && openStartupNote(all.first())) {
        finishStartup();
        return;
    }

    initializeFallbackDocument();
    finishStartup();
}

void StartupController::finishStartup()
{
    if (m_finished)
        return;
    m_finished = true;
    emit finishedChanged();
}
