// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
#include "notebackupstore.h"

#include "notefileio.h"
#include "notefrontmatter.h"
#include "perflog.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
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

void writeBackupSnapshot(const QString &dirPath,
                         const QString &target,
                         QByteArray bytes)
{
    PerfLog::ScopedTimer perf(
        QStringLiteral("collection.backup_before_overwrite.write"),
        QVariantMap{{QStringLiteral("path"), target},
                    {QStringLiteral("bytes"), bytes.size()},
                    {QStringLiteral("async"), true}});

    QDir().mkpath(dirPath);
    const bool copied = NoteFileIo::writeFileBytesAtomic(target, bytes);
    perf.addContext(QStringLiteral("copied"), copied);
    if (!copied)
        return;

    QDir dir(dirPath);
    QStringList existing = dir.entryList({QStringLiteral("*.md")},
                                         QDir::Files, QDir::Name);
    while (existing.size() > backupKeep)
        QFile::remove(dirPath + QLatin1Char('/') + existing.takeFirst());
}
} // namespace

void NoteBackupStore::setRootPath(const QString &rootPath)
{
    m_rootPath = rootPath;
}

void NoteBackupStore::setClockForTesting(std::function<QDateTime()> clock)
{
    m_clock = std::move(clock);
}

QString NoteBackupStore::dirFor(const QString &relPath) const
{
    return m_rootPath + QLatin1Char('/') + kvitDirName + QLatin1Char('/')
        + backupsDirName + QLatin1Char('/') + relPath;
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
    QDir dir(dirPath);
    const QDateTime now = m_clock ? m_clock() : QDateTime::currentDateTime();

    // Rotation floor: at most one backup per window, whatever the
    // auto-save cadence.
    QStringList existing = dir.entryList({QStringLiteral("*.md")},
                                         QDir::Files, QDir::Name);
    if (!existing.isEmpty()) {
        const QString newest = existing.last();
        const QDateTime newestStamp = QDateTime::fromString(
            newest.left(newest.size() - 3), backupStampFormat);
        if (newestStamp.isValid()
            && newestStamp.secsTo(now) < backupFloorSecs) {
            perf.addContext(QStringLiteral("skipped"), true);
            perf.addContext(QStringLiteral("reason"),
                            QStringLiteral("rotation_floor"));
            return;
        }
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

    QtConcurrent::run(writeBackupSnapshot, dirPath, target, std::move(bytes));
}

QVariantList NoteBackupStore::listFor(
    const QString &relPath,
    const std::function<QString(const QString &)> &previewOfBody) const
{
    QVariantList listing;
    const QString dirPath = dirFor(relPath);
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
    const QString path = dirFor(relPath) + QLatin1Char('/') + fileName;
    bool ok = false;
    const QString text = NoteFileIo::readTextFile(path, &ok);
    return ok ? NoteFrontMatter::split(text).body : QString();
}
