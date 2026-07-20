// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
#include "documentmanager.h"
#include "blockmodel.h"
#include "undostack.h"
#include "documentserializer.h"
#include "notefrontmatter.h"
#include "block.h"
#include "perflog.h"
#include "insertblockcommand.h"

#include <QFile>
#include <QSaveFile>
#include <QFileInfo>
#include <QTextStream>
#include <QStandardPaths>
#include <QDir>
#include <QFileDialog>
#include <QGuiApplication>
#include <QHash>
#include <QWindow>
#include <QThread>
#include <QtConcurrent/QtConcurrent>

#include <memory>
#include <optional>

DocumentManager::DocumentManager(QObject *parent)
    : QObject(parent)
    , m_serializer(new DocumentSerializer(this))
{
    // Setup auto-save timer
    m_autoSaveTimer.setInterval(m_autoSaveInterval * 1000);
    connect(&m_autoSaveTimer, &QTimer::timeout, this, &DocumentManager::onAutoSaveTimer);

    if (m_autoSaveEnabled) {
        m_autoSaveTimer.start();
    }

    // Crash-recovery journal: 2 s after the last change,
    // off the keystroke path.
    m_journalTimer.setSingleShot(true);
    m_journalTimer.setInterval(2000);
    connect(&m_journalTimer, &QTimer::timeout,
            this, &DocumentManager::writeJournal);
    connect(&m_asyncOpenWatcher, &QFutureWatcher<AsyncOpenResult>::finished,
            this, &DocumentManager::onAsyncOpenFinished);
    connect(&m_asyncSaveWatcher,
            &QFutureWatcher<PersistenceWriteResult>::finished,
            this, &DocumentManager::onAsyncSaveFinished);
    connect(&m_asyncJournalWatcher,
            &QFutureWatcher<PersistenceWriteResult>::finished,
            this, &DocumentManager::onAsyncJournalFinished);
}

DocumentManager::~DocumentManager()
{
    invalidateAsyncOpen();
    ++m_asyncSaveGeneration;
    ++m_asyncJournalGeneration;
    if (m_asyncOpenWatcher.isRunning()) {
        m_asyncOpenWatcher.cancel();
        m_asyncOpenWatcher.waitForFinished();
    }
    waitForPendingPersistence();
}

void DocumentManager::setBlockModel(BlockModel *model)
{
    if (m_model)
        disconnect(m_model, nullptr, this, nullptr);
    m_model = model;
    if (m_model) {
        // Any content or structure change restarts the journal debounce.
        connect(m_model, &QAbstractItemModel::dataChanged,
                this, &DocumentManager::onDocumentChangedForJournal);
        connect(m_model, &QAbstractItemModel::rowsInserted,
                this, &DocumentManager::onDocumentChangedForJournal);
        connect(m_model, &QAbstractItemModel::rowsRemoved,
                this, &DocumentManager::onDocumentChangedForJournal);
        connect(m_model, &QAbstractItemModel::rowsMoved,
                this, &DocumentManager::onDocumentChangedForJournal);
    }
}

void DocumentManager::setUndoStack(UndoStack *stack)
{
    if (m_undoStack) {
        disconnect(m_undoStack, nullptr, this, nullptr);
    }

    m_undoStack = stack;

    if (m_undoStack) {
        connect(m_undoStack, &UndoStack::cleanChanged,
                this, &DocumentManager::onUndoStackCleanChanged);
    }
}

QString DocumentManager::currentFileName() const
{
    if (m_currentFilePath.isEmpty()) {
        return QStringLiteral("Untitled");
    }
    return QFileInfo(m_currentFilePath).fileName();
}

bool DocumentManager::isDirty() const
{
    if (m_undoStack) {
        return !m_undoStack->isClean();
    }
    return false;
}

void DocumentManager::setAutoSaveEnabled(bool enabled)
{
    if (m_autoSaveEnabled == enabled) return;

    m_autoSaveEnabled = enabled;

    if (enabled) {
        m_autoSaveTimer.start();
    } else {
        m_autoSaveTimer.stop();
    }

    emit autoSaveEnabledChanged();
}

void DocumentManager::setAutoSaveInterval(int seconds)
{
    if (m_autoSaveInterval == seconds) return;

    m_autoSaveInterval = qMax(5, seconds);  // Minimum 5 seconds
    m_autoSaveTimer.setInterval(m_autoSaveInterval * 1000);

    emit autoSaveIntervalChanged();
}

bool DocumentManager::save()
{
    if (m_currentFilePath.isEmpty()) {
        // No file path - need to use saveAs
        return false;
    }

    return saveToFile(m_currentFilePath);
}

bool DocumentManager::saveAsync()
{
    if (m_currentFilePath.isEmpty()) {
        // No file path - need to use saveAs
        return false;
    }

    return saveToFileAsync(m_currentFilePath);
}

bool DocumentManager::saveAs(const QUrl &fileUrl)
{
    QString filePath = fileUrl.toLocalFile();

    // Ensure .md extension
    if (!filePath.endsWith(QStringLiteral(".md"), Qt::CaseInsensitive)) {
        filePath.append(QStringLiteral(".md"));
    }

    if (saveToFile(filePath)) {
        m_currentFilePath = filePath;
        emit currentFilePathChanged();
        return true;
    }

    return false;
}

void DocumentManager::setMaxOpenFileSizeMiB(int mib)
{
    if (m_maxOpenFileSizeMiB == mib)
        return;
    m_maxOpenFileSizeMiB = mib;
    emit maxOpenFileSizeMiBChanged();
}

// Oversized-file guard: the check runs on QFileInfo::size() BEFORE any
// read. The measured parse path is not the
// hazard — memory (UTF-16 doubles the bytes), the save path (autosave and
// the crash journal rewrite the whole document every dirty tick), and
// single-block pathology (one-line whales stall QTextDocument layout) are.
// Over the cap the editor shows a placeholder, not a degraded text-only
// mode; deliberate whale-editing belongs in a plain-text editor.
bool DocumentManager::rejectIfOverSizeCap(const QString &filePath)
{
    if (m_maxOpenFileSizeMiB <= 0)
        return false;
    const qint64 capBytes = qint64(m_maxOpenFileSizeMiB) * 1024 * 1024;
    const qint64 size = QFileInfo(filePath).size();
    if (size <= capBytes)
        return false;
    emit openRejectedTooLarge(filePath, double(size), double(capBytes));
    return true;
}

bool DocumentManager::open(const QUrl &fileUrl, bool ignoreSizeCap)
{
    invalidateAsyncOpen();
    ++m_asyncSaveGeneration;
    ++m_asyncJournalGeneration;
    QString filePath = fileUrl.toLocalFile();
    if (!ignoreSizeCap && rejectIfOverSizeCap(filePath))
        return false;
    PerfLog::ScopedTimer perf(QStringLiteral("note.open"),
                              QVariantMap{{QStringLiteral("path"), filePath}});

    if (loadFromFile(filePath)) {
        if (m_model)
            perf.addContext(QStringLiteral("blocks"), m_model->count());
        m_currentFilePath = filePath;
        emit currentFilePathChanged();
        // The freshly loaded document matches disk as of the file's
        // modification time.
        setLastSavedAt(QFileInfo(filePath).lastModified());

        // Clear undo stack for new document
        if (m_undoStack) {
            m_undoStack->clear();
            m_undoStack->setClean();
        }

        emit openSucceeded(filePath);
        return true;
    }

    return false;
}

bool DocumentManager::openAsync(const QUrl &fileUrl, bool ignoreSizeCap)
{
    const QString filePath = fileUrl.toLocalFile();
    if (!ignoreSizeCap && rejectIfOverSizeCap(filePath)) {
        emit openAsyncFinished(filePath, false);
        return false;
    }
    if (!m_model) {
        emit openFailed(QStringLiteral("No document model"));
        emit openAsyncFinished(filePath, false);
        return false;
    }
    if (filePath.isEmpty()) {
        emit openFailed(QStringLiteral("No file path"));
        emit openAsyncFinished(filePath, false);
        return false;
    }
    if (m_asyncOpenWatcher.isRunning()) {
        emit openFailed(QStringLiteral("A document open is already in progress"));
        emit openAsyncFinished(filePath, false);
        return false;
    }

    const quint64 generation = ++m_asyncOpenGeneration;
    m_asyncOpenTimer.restart();
    setAsyncOpenInProgress(true);
    m_asyncOpenWatcher.setFuture(
        QtConcurrent::run(&DocumentManager::loadFileForOpen, filePath,
                          generation));
    return true;
}

void DocumentManager::newDocument()
{
    invalidateAsyncOpen();
    // Bumping the generation stops a late result from being applied, but the
    // worker's write happens regardless of whether anyone reads the result.
    // Abandoning the document is not a reason to let the file it came from be
    // rewritten, so stop the write itself.
    cancelPendingWrites();
    ++m_asyncSaveGeneration;
    ++m_asyncJournalGeneration;
    if (!m_model) return;

    m_model->clear();
    m_model->insertBlockInternal(0, Block::Paragraph, QString());

    m_currentFilePath.clear();
    m_frontMatter.clear();
    m_loadDiverged = false;
    emit currentFilePathChanged();
    setLastSavedAt(QDateTime());

    if (m_undoStack) {
        m_undoStack->clear();
        m_undoStack->setClean();
    }
}

QString DocumentManager::getDefaultSavePath() const
{
    QString documentsPath = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
    return QDir(documentsPath).filePath(QStringLiteral("untitled.md"));
}

QUrl DocumentManager::toLocalFileUrl(const QString &path) const
{
    return QUrl::fromLocalFile(path);
}

void DocumentManager::openFileDialog()
{
    QFileDialog dialog(nullptr, tr("Open Document"));
    dialog.setNameFilter(tr("Markdown files (*.md);;All files (*)"));
    dialog.setFileMode(QFileDialog::ExistingFile);
    dialog.setOption(QFileDialog::DontUseNativeDialog, true);

    // Center the dialog on the main QML window
    QWindow *focusWindow = QGuiApplication::focusWindow();
    if (focusWindow) {
        dialog.winId(); // Ensure the dialog has a native window handle
        if (QWindow *dialogWindow = dialog.windowHandle()) {
            dialogWindow->setTransientParent(focusWindow);
        }
        int x = focusWindow->x() + (focusWindow->width() - dialog.width()) / 2;
        int y = focusWindow->y() + (focusWindow->height() - dialog.height()) / 2;
        dialog.move(x, y);
    }

    if (dialog.exec() == QDialog::Accepted) {
        QStringList files = dialog.selectedFiles();
        if (!files.isEmpty()) {
            open(QUrl::fromLocalFile(files.first()));
        }
    }
}

void DocumentManager::saveFileDialog()
{
    QFileDialog dialog(nullptr, tr("Save Document"));
    dialog.setNameFilter(tr("Markdown files (*.md);;All files (*)"));
    dialog.setFileMode(QFileDialog::AnyFile);
    dialog.setAcceptMode(QFileDialog::AcceptSave);
    dialog.setDefaultSuffix(QStringLiteral("md"));
    dialog.setOption(QFileDialog::DontUseNativeDialog, true);

    // Set default filename
    QString defaultPath = getDefaultSavePath();
    dialog.selectFile(defaultPath);

    // Center the dialog on the main QML window
    QWindow *focusWindow = QGuiApplication::focusWindow();
    if (focusWindow) {
        dialog.winId();
        if (QWindow *dialogWindow = dialog.windowHandle()) {
            dialogWindow->setTransientParent(focusWindow);
        }
        int x = focusWindow->x() + (focusWindow->width() - dialog.width()) / 2;
        int y = focusWindow->y() + (focusWindow->height() - dialog.height()) / 2;
        dialog.move(x, y);
    }

    if (dialog.exec() == QDialog::Accepted) {
        QStringList files = dialog.selectedFiles();
        if (!files.isEmpty()) {
            saveAs(QUrl::fromLocalFile(files.first()));
        }
    }
}

bool DocumentManager::saveToFile(const QString &filePath, SaveKind kind)
{
    waitForPendingPersistence();
    ++m_asyncSaveGeneration;
    ++m_asyncJournalGeneration;

    const QString operation =
        kind == SaveKind::AutoSave ? QStringLiteral("note.autosave")
                                   : QStringLiteral("note.save");
    QVariantMap context = persistenceContext(filePath);

    PerfLog::ScopedTimer perf(operation, context);
    if (!m_model) {
        emit saveFailed(QStringLiteral("No document model"));
        return false;
    }

    // The backup rotation's pre-overwrite hook.
    {
        PerfLog::ScopedTimer split(operation + QStringLiteral(".backup"),
                                   context);
        maybeBackupBeforeDivergingWrite(filePath);
        emit aboutToSave(filePath);
    }

    // Atomic write: a crash or error mid-write leaves the previous file
    // intact.
    QSaveFile file(filePath);
    bool opened = false;
    {
        PerfLog::ScopedTimer split(operation + QStringLiteral(".open"),
                                   context);
        opened = file.open(QIODevice::WriteOnly | QIODevice::Text);
        split.addContext(QStringLiteral("ok"), opened);
    }
    if (!opened) {
        emit saveFailed(file.errorString());
        return false;
    }

    QString fileText;
    {
        PerfLog::ScopedTimer split(operation + QStringLiteral(".serialize"),
                                   context);
        fileText = currentFileText();
        split.addContext(QStringLiteral("chars"), fileText.size());
    }
    perf.addContext(QStringLiteral("chars"), fileText.size());

    QTextStream stream(&file);
    stream.setEncoding(QStringConverter::Utf8);
    {
        PerfLog::ScopedTimer split(operation + QStringLiteral(".write"),
                                   context);
        stream << fileText;
        split.addContext(QStringLiteral("chars"), fileText.size());
    }
    {
        PerfLog::ScopedTimer split(operation + QStringLiteral(".flush"),
                                   context);
        stream.flush();
    }

    bool committed = false;
    {
        PerfLog::ScopedTimer split(operation + QStringLiteral(".commit"),
                                   context);
        committed = file.commit();
        split.addContext(QStringLiteral("ok"), committed);
    }
    if (!committed) {
        emit saveFailed(file.errorString());
        return false;
    }

    // A clean copy is on disk: the crash journal has nothing to say.
    removeJournal();

    // Mark as clean
    if (m_undoStack) {
        m_undoStack->setClean();
    }

    emit saveSucceeded(filePath);
    emit isDirtyChanged();
    setLastSavedAt(QDateTime::currentDateTime());

    return true;
}

bool DocumentManager::saveToFileAsync(const QString &filePath, SaveKind kind)
{
    const QString operation =
        kind == SaveKind::AutoSave ? QStringLiteral("note.autosave")
                                   : QStringLiteral("note.save");
    QVariantMap context = persistenceContext(filePath);
    context.insert(QStringLiteral("async"), true);
    PerfLog::ScopedTimer perf(operation, context);
    if (!m_model) {
        emit saveFailed(QStringLiteral("No document model"));
        return false;
    }
    if (m_asyncSaveWatcher.isRunning()) {
        m_autosaveRequestedWhileRunning = true;
        perf.addContext(QStringLiteral("queued"), true);
        return kind == SaveKind::Manual;
    }

    {
        PerfLog::ScopedTimer split(operation + QStringLiteral(".backup"),
                                   context);
        maybeBackupBeforeDivergingWrite(filePath);
        emit aboutToSave(filePath);
    }

    QString fileText;
    {
        PerfLog::ScopedTimer split(operation + QStringLiteral(".serialize"),
                                   context);
        fileText = currentFileText();
        split.addContext(QStringLiteral("chars"), fileText.size());
    }
    perf.addContext(QStringLiteral("chars"), fileText.size());

    const int undoIndex = m_undoStack ? m_undoStack->index() : -1;
    const quint64 generation = ++m_asyncSaveGeneration;
    m_autosaveRequestedWhileRunning = false;
    m_activeWriteCancel = WriteCancellationPtr::create();
    m_asyncSaveWatcher.setFuture(QtConcurrent::run(
        &DocumentManager::writeSnapshotToFile,
        operation,
        filePath,
        fileText,
        context,
        generation,
        0,
        m_documentRevision,
        undoIndex,
        m_asyncPersistenceDelayMs,
        m_activeWriteCancel));
    return true;
}

void DocumentManager::cancelPendingWrites()
{
    if (m_activeWriteCancel)
        m_activeWriteCancel->cancelled.storeRelease(1);
    // The flag alone is a race: the worker may already be past its check. Wait
    // for it to finish so the caller can change the path knowing no write is
    // still running against the old one.
    waitForPendingPersistence();
    m_activeWriteCancel.reset();
    m_autosaveRequestedWhileRunning = false;
}

void DocumentManager::setLastSavedAt(const QDateTime &when)
{
    if (m_lastSavedAt == when)
        return;
    m_lastSavedAt = when;
    emit lastSavedAtChanged();
}

bool DocumentManager::loadFromFile(const QString &filePath)
{
    if (!m_model) {
        emit openFailed(QStringLiteral("No document model"));
        return false;
    }

    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        emit openFailed(file.errorString());
        return false;
    }

    QTextStream stream(&file);
    stream.setEncoding(QStringConverter::Utf8);
    QString content = stream.readAll();

    file.close();

    // Only the body becomes blocks; the front-matter block is held raw
    // and re-emitted byte-identically by saveToFile.
    NoteFrontMatter::Split split = NoteFrontMatter::split(content);
    m_frontMatter = split.block;
    m_serializer->loadIntoModel(m_model, split.body);

    // The divergence test runs at load, not save: by the first save the
    // model already holds user edits, so comparing there would back up
    // everything. Layer-agnostic by design — any transformation that makes
    // the bytes we would write differ from the bytes we read (normalizer
    // rewrites, squared tables, demoted headings) arms the one-time .bak.
    m_loadDiverged = currentFileText() != content;

    return true;
}

DocumentManager::AsyncOpenResult
DocumentManager::loadFileForOpen(const QString &filePath, quint64 generation)
{
    AsyncOpenResult result;
    result.filePath = filePath;
    result.generation = generation;
    std::optional<QElapsedTimer> workerTimer;
    if (PerfLog::isEnabled()) {
        workerTimer.emplace();
        workerTimer->start();
    }
    const auto finishWorkerTiming = [&]() {
        if (workerTimer) {
            result.workerDurationMs =
                double(workerTimer->nsecsElapsed()) / 1000000.0;
        }
    };

    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        result.error = file.errorString();
        finishWorkerTiming();
        return result;
    }

    QTextStream stream(&file);
    stream.setEncoding(QStringConverter::Utf8);
    const QString content = stream.readAll();
    file.close();

    const NoteFrontMatter::Split split = NoteFrontMatter::split(content);
    DocumentSerializer serializer;
    const QList<DocumentSerializer::BlockData> blocks =
        serializer.parse(split.body);

    result.frontMatter = split.block;
    result.states.reserve(qMax(1, blocks.size()));
    for (const DocumentSerializer::BlockData &data : blocks) {
        Block::State state;
        state.type = data.type;
        state.content = data.content;
        state.indentLevel = data.indentLevel;
        state.checked = data.checked;
        state.language = data.language;
        state.calloutTitle = data.calloutTitle;
        state.attributes = data.attributes;
        result.states.append(state);
    }
    if (result.states.isEmpty()) {
        Block::State empty;
        result.states.append(empty);
    }

    // Divergence test for the one-time .bak (see loadFromFile): both
    // strings are in hand on this worker thread, so this is one extra
    // serialize and a comparison. The probe model lives and dies here.
    {
        BlockModel probe;
        probe.replaceAllBlocksInternal(result.states);
        result.loadDiverged =
            (split.block + serializer.serialize(&probe)) != content;
    }

    result.lastModified = QFileInfo(filePath).lastModified();
    result.ok = true;
    finishWorkerTiming();
    return result;
}

DocumentManager::PersistenceWriteResult
DocumentManager::writeSnapshotToFile(const QString &operation,
                                     const QString &filePath,
                                     const QString &fileText,
                                     QVariantMap context,
                                     quint64 generation,
                                     quint64 contentHash,
                                     quint64 documentRevision,
                                     int undoIndex,
                                     int delayMs,
                                     WriteCancellationPtr cancel)
{
    PersistenceWriteResult result;
    result.operation = operation;
    result.path = filePath;
    result.context = context;
    result.generation = generation;
    result.contentHash = contentHash;
    result.documentRevision = documentRevision;
    result.contentChars = fileText.size();
    result.undoIndex = undoIndex;
    result.delayMs = delayMs;

    if (delayMs > 0)
        QThread::msleep(static_cast<unsigned long>(delayMs));

    QSaveFile file(filePath);
    QElapsedTimer timer;
    timer.start();
    result.opened = file.open(QIODevice::WriteOnly | QIODevice::Text);
    result.openMs = double(timer.nsecsElapsed()) / 1000000.0;
    if (!result.opened) {
        result.error = file.errorString();
        return result;
    }

    QTextStream stream(&file);
    stream.setEncoding(QStringConverter::Utf8);

    timer.restart();
    stream << fileText;
    result.writeMs = double(timer.nsecsElapsed()) / 1000000.0;

    timer.restart();
    stream.flush();
    result.flushMs = double(timer.nsecsElapsed()) / 1000000.0;

    // Last chance to call the write off. Everything up to here touched only
    // QSaveFile's temporary file; commit() is the rename that would put these
    // bytes at filePath. If the note has moved since this worker started, that
    // rename would resurrect it at the old location, so drop the temporary
    // file and report a cancellation rather than a failure.
    if (cancel && cancel->cancelled.loadAcquire() != 0) {
        file.cancelWriting();
        result.cancelled = true;
        result.committed = false;
        return result;
    }

    timer.restart();
    result.committed = file.commit();
    result.commitMs = double(timer.nsecsElapsed()) / 1000000.0;
    if (!result.committed)
        result.error = file.errorString();
    else if (result.operation == QLatin1String("note.journal_write")
             && result.contentHash == 0)
        result.contentHash = textHash(fileText);

    return result;
}

void DocumentManager::onAsyncOpenFinished()
{
    const AsyncOpenResult result = m_asyncOpenWatcher.result();
    if (result.generation != m_asyncOpenGeneration)
        return;

    setAsyncOpenInProgress(false);

    if (!result.ok) {
        emit openFailed(result.error);
        emit openAsyncFinished(result.filePath, false);
        return;
    }
    if (!m_model) {
        emit openFailed(QStringLiteral("No document model"));
        emit openAsyncFinished(result.filePath, false);
        return;
    }

    PerfLog::instance().record(
        QStringLiteral("note.open.worker"),
        result.workerDurationMs,
        QVariantMap{{QStringLiteral("path"), result.filePath},
                    {QStringLiteral("blocks"), result.states.size()},
                    {QStringLiteral("async"), true},
                    {QStringLiteral("ok"), true}});

    m_frontMatter = result.frontMatter;
    m_loadDiverged = result.loadDiverged;
    std::optional<QElapsedTimer> applyTimer;
    if (PerfLog::isEnabled()) {
        applyTimer.emplace();
        applyTimer->start();
    }
    m_model->replaceAllBlocksInternal(result.states);
    if (applyTimer) {
        PerfLog::instance().record(
            QStringLiteral("note.open.apply"),
            double(applyTimer->nsecsElapsed()) / 1000000.0,
            QVariantMap{{QStringLiteral("path"), result.filePath},
                        {QStringLiteral("blocks"), m_model->count()},
                        {QStringLiteral("async"), true}});
    }

    m_currentFilePath = result.filePath;
    emit currentFilePathChanged();
    setLastSavedAt(result.lastModified);

    if (m_undoStack) {
        m_undoStack->clear();
        m_undoStack->setClean();
    }

    PerfLog::instance().record(
        QStringLiteral("note.open"),
        m_asyncOpenTimer.elapsed(),
        QVariantMap{{QStringLiteral("path"), result.filePath},
                    {QStringLiteral("blocks"), m_model->count()},
                    {QStringLiteral("async"), true}});

    emit openSucceeded(result.filePath);
    emit openAsyncFinished(result.filePath, true);
}

void DocumentManager::setFrontMatter(const QString &block)
{
    m_frontMatter = block;
}

void DocumentManager::rebindFilePath(const QString &newPath)
{
    if (m_currentFilePath == newPath || newPath.isEmpty())
        return;
    // The note has already moved on disk. A save still running was handed the
    // old path and would recreate the file there, leaving a duplicate beside
    // the renamed note. Stop it before adopting the new path.
    cancelPendingWrites();
    m_currentFilePath = newPath;
    emit currentFilePathChanged();
}

bool DocumentManager::restoreBody(const QString &markdown)
{
    if (!m_model || !m_undoStack)
        return false;

    // One compound: append the restored blocks, then remove the old ones
    // (descending, so indexes stay valid). One Ctrl+Z brings the
    // pre-restore document back.
    const int oldCount = m_model->count();
    QList<DocumentSerializer::BlockData> blocks = m_serializer->parse(markdown);
    if (blocks.isEmpty()) {
        DocumentSerializer::BlockData empty;
        blocks.append(empty);
    }

    m_undoStack->beginMacro(QStringLiteral("Restore from backup"));
    int at = oldCount;
    for (const DocumentSerializer::BlockData &block : blocks) {
        Block::State state;
        state.type = block.type;
        state.content = block.content;
        state.indentLevel = block.indentLevel;
        state.checked = block.checked;
        state.language = block.language;
        state.calloutTitle = block.calloutTitle;
        state.attributes = block.attributes;
        m_undoStack->push(std::make_unique<InsertBlockCommand>(m_model, at, state));
        ++at;
    }
    for (int i = oldCount - 1; i >= 0; --i)
        m_model->removeBlock(i);
    m_undoStack->endMacro();
    return true;
}

void DocumentManager::setJournalPath(const QString &path)
{
    if (m_journalPath == path)
        return;
    // A pending write for the previous note must not fire at the new
    // path; the previous journal (if any) is removed by the save that
    // accompanies every note switch.
    m_journalTimer.stop();
    ++m_asyncJournalGeneration;
    m_lastJournalHash = 0;
    m_lastJournalChars = -1;
    m_pendingJournalHash = 0;
    m_pendingJournalChars = -1;
    m_journalPath = path;
    emit journalPathChanged();
}

void DocumentManager::setJournalDebounceMs(int ms)
{
    m_journalTimer.setInterval(qMax(0, ms));
}

void DocumentManager::setAsyncPersistenceDelayMsForTests(int ms)
{
    m_asyncPersistenceDelayMs = qMax(0, ms);
}

void DocumentManager::onDocumentChangedForJournal()
{
    ++m_documentRevision;
    if (m_journalPath.isEmpty())
        return;
    m_journalTimer.start(); // restart: 2 s after the LAST change
}

void DocumentManager::writeJournal()
{
    PerfLog::ScopedTimer perf(QStringLiteral("note.journal_write"),
                              QVariantMap{{QStringLiteral("path"), m_journalPath}});
    if (m_journalPath.isEmpty() || !isDirty() || !m_model)
        return;
    QVariantMap context = persistenceContext(m_journalPath);

    QString fileText;
    {
        PerfLog::ScopedTimer split(
            QStringLiteral("note.journal_write.serialize"), context);
        fileText = currentFileText();
        split.addContext(QStringLiteral("chars"), fileText.size());
    }
    perf.addContext(QStringLiteral("blocks"), m_model->count());
    perf.addContext(QStringLiteral("chars"), fileText.size());

    quint64 hash = 0;
    const bool couldMatchLast = m_lastJournalChars == fileText.size();
    const bool couldMatchPending = m_pendingJournalChars == fileText.size();
    if (couldMatchLast || couldMatchPending) {
        hash = textHash(fileText);
        if ((couldMatchLast && m_lastJournalHash == hash)
            || (couldMatchPending && m_pendingJournalHash == hash)) {
            perf.addContext(QStringLiteral("skipped"), true);
            return;
        }
    }

    if (m_asyncJournalWatcher.isRunning()) {
        m_journalRequestedWhileRunning = true;
        return;
    }

    const int undoIndex = m_undoStack ? m_undoStack->index() : -1;
    const quint64 generation = ++m_asyncJournalGeneration;
    m_pendingJournalHash = hash;
    m_pendingJournalChars = fileText.size();
    m_journalRequestedWhileRunning = false;
    m_asyncJournalWatcher.setFuture(QtConcurrent::run(
        &DocumentManager::writeSnapshotToFile,
        QStringLiteral("note.journal_write"),
        m_journalPath,
        fileText,
        context,
        generation,
        hash,
        m_documentRevision,
        undoIndex,
        m_asyncPersistenceDelayMs,
        // The journal has a fixed control path that note renames never move,
        // so a path change is no reason to abandon it.
        WriteCancellationPtr()));
}

void DocumentManager::removeJournal()
{
    m_journalTimer.stop();
    ++m_asyncJournalGeneration;
    m_lastJournalHash = 0;
    m_lastJournalChars = -1;
    m_pendingJournalHash = 0;
    m_pendingJournalChars = -1;
    if (m_asyncJournalWatcher.isRunning())
        m_asyncJournalWatcher.waitForFinished();
    if (!m_journalPath.isEmpty())
        QFile::remove(m_journalPath);
}

void DocumentManager::invalidateAsyncOpen()
{
    ++m_asyncOpenGeneration;
    setAsyncOpenInProgress(false);
}

void DocumentManager::setAsyncOpenInProgress(bool inProgress)
{
    if (m_asyncOpenInProgress == inProgress)
        return;
    m_asyncOpenInProgress = inProgress;
    emit openInProgressChanged();
}

// One-time backup before the first diverging overwrite: when a load found
// that saving would rewrite the file (the parse-time normalizations are not
// undoable — the normalized state is the document's baseline), the first save
// to the SAME path copies the on-disk file to <filename>.bak first. The full
// name is appended (note.md → note.md.bak) so the backup stays out of the
// folder tree and collection search. Save-as disarms without a backup — the
// original file is not being overwritten. A seatbelt, not a versioning
// system: an existing .bak is presumed to hold an earlier
// (closer-to-original) form and is never overwritten.
void DocumentManager::maybeBackupBeforeDivergingWrite(const QString &filePath)
{
    if (!m_loadDiverged)
        return;
    m_loadDiverged = false;   // one backup per load, ever
    if (filePath != m_currentFilePath)
        return;               // save-as: original stays untouched
    const QString backupPath = filePath + QStringLiteral(".bak");
    if (QFile::exists(backupPath) || !QFile::exists(filePath))
        return;
    QFile::copy(filePath, backupPath);
}

QString DocumentManager::currentFileText() const
{
    QString body = m_serializer->serialize(m_model);
    if (m_frontMatter.isEmpty())
        return body;

    QString text;
    text.reserve(m_frontMatter.size() + body.size());
    text.append(m_frontMatter);
    text.append(body);
    return text;
}

void DocumentManager::onAutoSaveTimer()
{
    // Only auto-save if we have a file path and there are unsaved changes
    if (!m_currentFilePath.isEmpty() && isDirty()) {
        saveToFileAsync(m_currentFilePath, SaveKind::AutoSave);
    }
}

void DocumentManager::onUndoStackCleanChanged()
{
    emit isDirtyChanged();
}

void DocumentManager::onAsyncSaveFinished()
{
    const PersistenceWriteResult result = m_asyncSaveWatcher.result();
    if (result.generation != m_asyncSaveGeneration)
        return;

    recordPersistenceWriteSplits(result);

    if (result.cancelled) {
        // Deliberately abandoned because the note moved: nothing failed and
        // there is nothing to tell the user. The document stays dirty, so the
        // next save writes it to wherever it lives now.
        return;
    }

    if (!result.committed) {
        emit saveFailed(result.error);
        return;
    }

    const bool snapshotStillCurrent =
        result.path == m_currentFilePath
        && result.documentRevision == m_documentRevision
        && (!m_undoStack || m_undoStack->index() == result.undoIndex);
    if (snapshotStillCurrent) {
        removeJournal();
        if (m_undoStack)
            m_undoStack->setClean();
        emit saveSucceeded(result.path);
        emit isDirtyChanged();
        setLastSavedAt(QDateTime::currentDateTime());
    }

    if (m_autosaveRequestedWhileRunning && isDirty()) {
        m_autosaveRequestedWhileRunning = false;
        QMetaObject::invokeMethod(this, "onAutoSaveTimer",
                                  Qt::QueuedConnection);
    }
}

void DocumentManager::onAsyncJournalFinished()
{
    const PersistenceWriteResult result = m_asyncJournalWatcher.result();
    if (result.generation != m_asyncJournalGeneration)
        return;

    recordPersistenceWriteSplits(result);
    m_pendingJournalHash = 0;
    m_pendingJournalChars = -1;
    if (result.committed) {
        m_lastJournalHash = result.contentHash;
        m_lastJournalChars = result.contentChars;
    }

    if (m_journalRequestedWhileRunning && isDirty()) {
        m_journalRequestedWhileRunning = false;
        QMetaObject::invokeMethod(this, "writeJournal",
                                  Qt::QueuedConnection);
    }
}

QVariantMap DocumentManager::persistenceContext(const QString &filePath) const
{
    QVariantMap context{{QStringLiteral("path"), filePath}};
    if (m_model)
        context.insert(QStringLiteral("blocks"), m_model->count());
    return context;
}

quint64 DocumentManager::textHash(const QString &text)
{
    return qHash(text, 0x9e3779b9U);
}

void DocumentManager::recordPersistenceWriteSplits(
    const PersistenceWriteResult &result)
{
    QVariantMap context = result.context;
    context.insert(QStringLiteral("chars"), result.contentChars);
    if (result.delayMs > 0)
        context.insert(QStringLiteral("delayMs"), result.delayMs);

    QVariantMap openContext = context;
    openContext.insert(QStringLiteral("ok"), result.opened);
    PerfLog::instance().record(result.operation + QStringLiteral(".open"),
                               result.openMs,
                               openContext);

    if (!result.opened)
        return;

    PerfLog::instance().record(result.operation + QStringLiteral(".write"),
                               result.writeMs,
                               context);
    PerfLog::instance().record(result.operation + QStringLiteral(".flush"),
                               result.flushMs,
                               context);

    QVariantMap commitContext = context;
    commitContext.insert(QStringLiteral("ok"), result.committed);
    PerfLog::instance().record(result.operation + QStringLiteral(".commit"),
                               result.commitMs,
                               commitContext);
}

void DocumentManager::waitForPendingPersistence()
{
    if (m_asyncSaveWatcher.isRunning())
        m_asyncSaveWatcher.waitForFinished();
    if (m_asyncJournalWatcher.isRunning())
        m_asyncJournalWatcher.waitForFinished();
}
