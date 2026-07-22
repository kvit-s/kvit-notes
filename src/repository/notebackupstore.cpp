// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
#include "notebackupstore.h"

#include "notefileio.h"
#include "notefrontmatter.h"
#include "perflog.h"
#include "vaultpaths.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMutex>
#include <QMutexLocker>
#include <QSharedPointer>
#include <QVariantMap>
#include <QtConcurrent/QtConcurrent>

#include <utility>

namespace {
const QString kvitDirName = QStringLiteral(".kvit");
const QString backupsDirName = QStringLiteral("backups");
const QString mdSuffix = QStringLiteral(".md");
const int backupFloorSecs = 10 * 60;
const int backupKeep = 10;
const QString backupStampFormat = QStringLiteral("yyyyMMdd-HHmmss");

// One mutex per backup directory, so two snapshots of the same note never
// commit or prune concurrently. Pruning reads the directory listing and
// deletes the oldest entries in it; two workers doing that at once can each
// see the other's file and between them delete more than the rotation limit
// asks for, or delete the snapshot the other has just committed.
QMutex g_directoryMutexesGuard;
QHash<QString, QSharedPointer<QMutex>> g_directoryMutexes;

QSharedPointer<QMutex> mutexForDirectory(const QString &dirPath)
{
    QMutexLocker locker(&g_directoryMutexesGuard);
    auto it = g_directoryMutexes.find(dirPath);
    if (it == g_directoryMutexes.end())
        it = g_directoryMutexes.insert(dirPath, QSharedPointer<QMutex>::create());
    return it.value();
}

void writeBackupSnapshot(const QString &dirPath,
                         const QString &target,
                         QByteArray bytes)
{
    PerfLog::ScopedTimer perf(
        QStringLiteral("collection.backup_before_overwrite.write"),
        QVariantMap{{QStringLiteral("path"), target},
                    {QStringLiteral("bytes"), bytes.size()},
                    {QStringLiteral("async"), true}});

    const QSharedPointer<QMutex> serialize = mutexForDirectory(dirPath);
    QMutexLocker locker(serialize.data());

    QDir().mkpath(dirPath);
    const bool copied = NoteFileIo::writeFileBytesAtomic(target, bytes);
    perf.addContext(QStringLiteral("copied"), copied);
    if (!copied)
        return;

    // Pruning happens after the commit and under the same lock, so the count
    // it enforces is the one that exists on disk at that moment.
    QDir dir(dirPath);
    QStringList existing = dir.entryList({QStringLiteral("*.md")},
                                         QDir::Files, QDir::Name);
    // A removal that fails leaves the directory over the cap, and dropping
    // the name from the list regardless would report a rotation that did not
    // happen. The count is what survives on disk, and a failure is carried
    // into the perf record rather than passing silently.
    int kept = existing.size();
    int failed = 0;
    while (kept > backupKeep && !existing.isEmpty()) {
        if (QFile::remove(dirPath + QLatin1Char('/') + existing.takeFirst()))
            --kept;
        else
            ++failed;
    }
    perf.addContext(QStringLiteral("kept"), kept);
    if (failed > 0)
        perf.addContext(QStringLiteral("unremovable"), failed);
}
} // namespace

void NoteBackupStore::setRootPath(const QString &rootPath)
{
    m_rootPath = rootPath;
    // The reservations belong to the vault being left.
    m_scheduled.clear();
}

void NoteBackupStore::setClockForTesting(std::function<QDateTime()> clock)
{
    m_clock = std::move(clock);
}

void NoteBackupStore::setSnapshotWriterForTesting(
    std::function<void(const QString &, const QString &, const QByteArray &)>
        writer)
{
    m_writer = std::move(writer);
}

QString NoteBackupStore::dirFor(const QString &relPath) const
{
    // The backup tree mirrors note paths, so `relPath` decides directories
    // here and has to be a plain relative path; and .kvit/backups has to be
    // the vault's own, since rotation deletes the oldest files it finds.
    if (!VaultPaths::isPlainRelativePath(relPath))
        return QString();
    const QString base = VaultPaths::ownedDir(
        m_rootPath, kvitDirName + QLatin1Char('/') + backupsDirName);
    if (base.isEmpty())
        return QString();
    return base + QLatin1Char('/') + relPath;
}

void NoteBackupStore::backupBeforeOverwrite(const QString &relPath,
                                            const QString &absPath)
{
    PerfLog::ScopedTimer perf(
        QStringLiteral("collection.backup_before_overwrite"),
        QVariantMap{{QStringLiteral("path"), absPath}});

    if (relPath.isEmpty() || !QFileInfo::exists(absPath)) {
        perf.addContext(QStringLiteral("skipped"), true);
        return;
    }

    const QString dirPath = dirFor(relPath);
    if (dirPath.isEmpty()) {
        perf.addContext(QStringLiteral("skipped"), true);
        perf.addContext(QStringLiteral("reason"), QStringLiteral("containment"));
        return;
    }
    QDir dir(dirPath);
    const QDateTime now = m_clock ? m_clock() : QDateTime::currentDateTime();

    // Rotation floor: at most one backup per window, whatever the
    // auto-save cadence. The newest snapshot is whichever is later of what is
    // on disk and what has been scheduled but not yet written -- the second
    // is what a burst of saves produces, and consulting only the first made
    // every save in the burst believe it was the first one.
    QDateTime newestStamp = m_scheduled.value(relPath);
    QStringList existing = dir.entryList({QStringLiteral("*.md")},
                                         QDir::Files, QDir::Name);
    if (!existing.isEmpty()) {
        const QString newest = existing.last();
        const QDateTime onDisk = QDateTime::fromString(
            newest.left(newest.size() - 3), backupStampFormat);
        if (onDisk.isValid() && (!newestStamp.isValid() || onDisk > newestStamp))
            newestStamp = onDisk;
    }
    if (newestStamp.isValid() && newestStamp.secsTo(now) < backupFloorSecs) {
        perf.addContext(QStringLiteral("skipped"), true);
        perf.addContext(QStringLiteral("reason"),
                        QStringLiteral("rotation_floor"));
        return;
    }

    QString target = dirPath + QLatin1Char('/')
        + now.toString(backupStampFormat) + mdSuffix;
    if (QFileInfo::exists(target)) {
        perf.addContext(QStringLiteral("skipped"), true);
        perf.addContext(QStringLiteral("reason"),
                        QStringLiteral("duplicate_stamp"));
        return; // same-second duplicate: the window already has its copy
    }

    bool ok = false;
    QByteArray bytes = NoteFileIo::readFileBytes(absPath, &ok);
    perf.addContext(QStringLiteral("copied"), ok);
    perf.addContext(QStringLiteral("bytes"), bytes.size());
    perf.addContext(QStringLiteral("async"), ok);
    if (!ok)
        return;

    // The window is reserved here, where the decision is made, and not in the
    // worker: a second save arriving before the worker runs has to see it.
    m_scheduled.insert(relPath, now);
    if (m_writer) {
        m_writer(dirPath, target, bytes);
        return;
    }
    QtConcurrent::run(writeBackupSnapshot, dirPath, target, std::move(bytes));
}

QVariantList NoteBackupStore::listFor(
    const QString &relPath,
    const std::function<QString(const QString &)> &previewOfBody) const
{
    QVariantList listing;
    const QString dirPath = dirFor(relPath);
    if (dirPath.isEmpty())
        return listing;
    const QStringList files = QDir(dirPath).entryList(
        {QStringLiteral("*.md")}, QDir::Files, QDir::Name | QDir::Reversed);
    for (const QString &fileName : files) {
        const QDateTime stamp = QDateTime::fromString(
            fileName.left(fileName.size() - 3), backupStampFormat);
        bool ok = false;
        const QString text =
            NoteFileIo::readTextFile(dirPath + QLatin1Char('/') + fileName, &ok);
        if (!ok)
            continue;
        const NoteFrontMatter::Split split = NoteFrontMatter::split(text);
        listing.append(QVariantMap{
            {QStringLiteral("fileName"), fileName},
            {QStringLiteral("timestamp"), stamp},
            {QStringLiteral("preview"), previewOfBody(split.body)},
        });
    }
    return listing;
}

QString NoteBackupStore::bodyOf(const QString &relPath,
                                const QString &fileName) const
{
    if (fileName.contains(QLatin1Char('/')))
        return QString();
    const QString dirPath = dirFor(relPath);
    if (dirPath.isEmpty())
        return QString();
    const QString path = dirPath + QLatin1Char('/') + fileName;
    bool ok = false;
    const QString text = NoteFileIo::readTextFile(path, &ok);
    return ok ? NoteFrontMatter::split(text).body : QString();
}
