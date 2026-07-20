// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
#include "recoveryjournalstore.h"

#include "notefileio.h"

#include <QDir>
#include <QFile>
#include <QUrl>

namespace {
const QString kvitDirName = QStringLiteral(".kvit");
const QString recoveryDirName = QStringLiteral("recovery");
}

void RecoveryJournalStore::setRootPath(const QString &rootPath)
{
    m_rootPath = rootPath;
    if (m_rootPath.isEmpty())
        m_pending.clear();
}

QString RecoveryJournalStore::journalPathFor(const QString &relPath) const
{
    if (m_rootPath.isEmpty() || relPath.isEmpty())
        return QString();
    const QString dirPath = m_rootPath + QLatin1Char('/') + kvitDirName
        + QLatin1Char('/') + recoveryDirName;
    QDir().mkpath(dirPath);
    // The file name IS the relPath, percent-encoded (flat directory).
    const QString encoded = QString::fromUtf8(
        QUrl::toPercentEncoding(relPath));
    return dirPath + QLatin1Char('/') + encoded;
}

void RecoveryJournalStore::reload()
{
    m_pending.clear();
    const QDir recoveryDir(m_rootPath + QLatin1Char('/') + kvitDirName
                           + QLatin1Char('/') + recoveryDirName);
    const QStringList journals = recoveryDir.entryList(QDir::Files, QDir::Name);
    for (const QString &encoded : journals) {
        m_pending.append(QString::fromUtf8(
            QByteArray::fromPercentEncoding(encoded.toUtf8())));
    }
}

bool RecoveryJournalStore::isPending(const QString &relPath) const
{
    return m_pending.contains(relPath);
}

QString RecoveryJournalStore::readJournal(const QString &relPath, bool *ok) const
{
    return NoteFileIo::readTextFile(journalPathFor(relPath), ok);
}

void RecoveryJournalStore::resolve(const QString &relPath)
{
    QFile::remove(journalPathFor(relPath));
    m_pending.removeAll(relPath);
}

void RecoveryJournalStore::clear()
{
    m_pending.clear();
}
