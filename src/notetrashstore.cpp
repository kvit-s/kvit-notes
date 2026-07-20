// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
#include "notetrashstore.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>

namespace {
const QString kvitDirName = QStringLiteral(".kvit");
const QString trashDirName = QStringLiteral("trash");
}

void NoteTrashStore::setRootPath(const QString &rootPath)
{
    m_rootPath = rootPath;
}

QString NoteTrashStore::trashDirPath() const
{
    return m_rootPath + QLatin1Char('/') + kvitDirName + QLatin1Char('/')
        + trashDirName;
}

int NoteTrashStore::itemCount() const
{
    if (m_rootPath.isEmpty())
        return 0;
    const QDir trashDir(trashDirPath());
    if (!trashDir.exists())
        return 0;
    return int(trashDir.entryList(QDir::AllEntries | QDir::NoDotAndDotDot)
                   .size());
}

bool NoteTrashStore::empty()
{
    if (m_rootPath.isEmpty())
        return false;
    QDir trashDir(trashDirPath());
    if (!trashDir.exists()) {
        return true;
    }
    // Permanent by design (the §12.4 safety net ends here); the
    // confirmation dialog names the item count first.
    return trashDir.removeRecursively();
}

bool NoteTrashStore::moveIn(const QString &absPath, const QString &name)
{
    const QString trashDir = trashDirPath();
    if (!QDir().mkpath(trashDir))
        return false;

    const QString stamp =
        QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-HHmmss"));
    QString target = trashDir + QLatin1Char('/') + stamp + QLatin1Char('-') + name;
    int n = 2;
    while (QFileInfo::exists(target)) {
        target = trashDir + QLatin1Char('/') + stamp + QLatin1Char('-')
            + QString::number(n++) + QLatin1Char('-') + name;
    }
    return QFile::rename(absPath, target) || QDir().rename(absPath, target);
}
