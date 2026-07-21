// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
#ifndef NOTEBACKUPSTORE_H
#define NOTEBACKUPSTORE_H

#include <QByteArray>
#include <QDateTime>
#include <QHash>
#include <QString>
#include <QVariantList>

#include <functional>

// The <root>/.kvit/backups tree: timestamped copies of a note file taken
// just before it is overwritten, one per note under
// backups/<relPath>/<stamp>.md.
//
// Rotation is a time floor rather than a per-save copy, so an auto-save
// running every few seconds still produces at most one backup per window,
// and the directory is pruned to a fixed count. The clock is injectable
// because the rotation decision is what the tests need to drive.
class NoteBackupStore
{
public:
    // "" closes the store; listings then come back empty.
    void setRootPath(const QString &rootPath);

    // Copy the current bytes of `absPath` into the backup directory for
    // `relPath`, unless the newest existing backup is still inside the
    // rotation window or the stamp for this second is already taken. The
    // copy and the pruning happen on a pool thread; deciding whether to make
    // one does not.
    void backupBeforeOverwrite(const QString &relPath, const QString &absPath);

    // Newest first: [{fileName, timestamp, preview}]. `preview` comes from
    // `previewOfBody`, so the listing and the note list describe a body the
    // same way.
    QVariantList listFor(const QString &relPath,
                         const std::function<QString(const QString &)>
                             &previewOfBody) const;
    // The chosen backup's BODY, front-matter stripped. "" when `fileName` is
    // not a plain name in that note's backup directory, or cannot be read.
    QString bodyOf(const QString &relPath, const QString &fileName) const;

    // Test seam: rotation decisions read this clock. Null restores
    // QDateTime::currentDateTime.
    void setClockForTesting(std::function<QDateTime()> clock);

    // Test seam: what to do with a snapshot instead of writing it on a pool
    // thread. The rotation defect is about what happens while an earlier
    // snapshot has not landed yet, which is unobservable when the write
    // usually wins the race; holding the write is the only way to test the
    // decision rather than the timing. Null restores the pool write.
    void setSnapshotWriterForTesting(
        std::function<void(const QString &dirPath, const QString &target,
                           const QByteArray &bytes)> writer);

private:
    QString dirFor(const QString &relPath) const;

    QString m_rootPath;
    std::function<QDateTime()> m_clock; // null = QDateTime::currentDateTime
    std::function<void(const QString &, const QString &, const QByteArray &)>
        m_writer; // null = pool write
    // The window each note's last scheduled snapshot belongs to, recorded
    // when the decision is made rather than when the file appears. Several
    // saves in quick succession all used to look at a directory none of the
    // queued writes had reached yet, so each of them decided it was the first
    // and the rotation floor stopped meaning anything.
    QHash<QString, QDateTime> m_scheduled;
};

#endif // NOTEBACKUPSTORE_H
