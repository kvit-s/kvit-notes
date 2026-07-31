// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
#include "recoveryjournalstore.h"

#include "notefileio.h"
#include "vaultpaths.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QUrl>

namespace {
const QString recoveryRelDir = QStringLiteral(".kvit/recovery");
}

void RecoveryJournalStore::setRootPath(const QString &rootPath)
{
    m_rootPath = rootPath;
    if (m_rootPath.isEmpty())
        m_pending.clear();
}

QString RecoveryJournalStore::journalPathFor(const QString &relPath) const
{
    if (m_rootPath.isEmpty() || !isValidRelativeNotePath(relPath))
        return QString();
    // A journal is the only copy of edits a crash interrupted, so it must be
    // written where this vault can find it again, not through a link into
    // some other directory.
    const QString dirPath =
        VaultPaths::ensureOwnedDir(m_rootPath, recoveryRelDir);
    if (dirPath.isEmpty())
        return QString();
    // The file name IS the relPath, percent-encoded (flat directory).
    const QString encoded = QString::fromUtf8(
        QUrl::toPercentEncoding(relPath));
    const QString filePath = dirPath + QLatin1Char('/') + encoded;
    // The directory is the repository's own, and so is every file in it. A
    // link standing where a journal belongs works in both directions: reading
    // it puts a file from outside the vault in front of the reader as their
    // own recovered note, and restoring writes that content into the vault
    // under the note name the link's own name decodes to. The journal write
    // would follow it out of the vault as well.
    const QFileInfo info(filePath);
    if (info.isSymbolicLink() || info.isJunction() || info.isShortcut())
        return QString();
    return filePath;
}

void RecoveryJournalStore::reload()
{
    m_pending.clear();
    const QString dirPath = VaultPaths::ownedDir(m_rootPath, recoveryRelDir);
    if (dirPath.isEmpty())
        return;
    const QDir recoveryDir(dirPath);
    // NoSymLinks: a link dropped in here would otherwise be listed as a
    // pending recovery, previewed from wherever it points, and restored into
    // the vault under the note name its own name decodes to.
    const QStringList journals =
        recoveryDir.entryList(QDir::Files | QDir::NoSymLinks, QDir::Name);
    for (const QString &encoded : journals) {
        const QString decoded = QString::fromUtf8(
            QByteArray::fromPercentEncoding(encoded.toUtf8()));
        const QString canonicalName = QString::fromUtf8(
            QUrl::toPercentEncoding(decoded));
        if (canonicalName != encoded || !isValidRelativeNotePath(decoded)
            || m_pending.contains(decoded))
            continue;
        m_pending.append(decoded);
    }
}

bool RecoveryJournalStore::isValidRelativeNotePath(const QString &relPath)
{
    // The shape rule is the repository's, shared with every other place a
    // persisted path arrives from outside; a journal additionally has to name
    // a note, so a decoded value that is not Markdown is not one of ours.
    return VaultPaths::isPlainRelativePath(relPath)
        && relPath.endsWith(QLatin1String(".md"), Qt::CaseInsensitive);
}

bool RecoveryJournalStore::isPending(const QString &relPath) const
{
    return m_pending.contains(relPath);
}

QString RecoveryJournalStore::readJournal(const QString &relPath, bool *ok) const
{
    return NoteFileIo::readTextFile(journalPathFor(relPath), ok);
}

bool RecoveryJournalStore::resolve(const QString &relPath)
{
    if (!isValidRelativeNotePath(relPath))
        return false;
    const QString path = journalPathFor(relPath);
    if (path.isEmpty())
        return false;
    // Confirmed absence counts as resolved: the goal is that no journal is
    // left behind, not that this call is the one that removed it.
    if (QFile::exists(path) && !QFile::remove(path))
        return false;
    if (QFile::exists(path))
        return false;
    m_pending.removeAll(relPath);
    return true;
}

void RecoveryJournalStore::clear()
{
    m_pending.clear();
}
