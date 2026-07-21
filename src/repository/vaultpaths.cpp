// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
#include "vaultpaths.h"

#include <QDir>
#include <QFileInfo>
#include <QStringList>

namespace VaultPaths {

QString canonicalizeMissingOk(const QString &path)
{
    QString head = QDir::cleanPath(path);
    QStringList tail;  // segments trimmed off, nearest-last
    forever {
        const QString canonical = QFileInfo(head).canonicalFilePath();
        if (!canonical.isEmpty()) {
            QString result = canonical;
            while (!tail.isEmpty())
                result += QLatin1Char('/') + tail.takeLast();
            return result;
        }
        const int slash = head.lastIndexOf(QLatin1Char('/'));
        if (slash <= 0)
            return QDir::cleanPath(path);  // nothing on the way exists
        tail.append(head.mid(slash + 1));
        head.truncate(slash);
    }
}

bool isWithinCanonicalRoot(const QString &canonicalRoot, const QString &absPath)
{
    if (canonicalRoot.isEmpty())
        return false;
    const QString canonical = canonicalizeMissingOk(absPath);
    if (canonical == canonicalRoot)
        return true;
    return canonical.startsWith(canonicalRoot + QLatin1Char('/'));
}

bool isPlainRelativePath(const QString &relPath)
{
    if (relPath.isEmpty() || QDir::isAbsolutePath(relPath)
        || relPath.contains(QLatin1Char('\\'))
        || QDir::cleanPath(relPath) != relPath)
        return false;
    const QStringList segments = relPath.split(QLatin1Char('/'));
    return !segments.contains(QString())
        && !segments.contains(QStringLiteral("."))
        && !segments.contains(QStringLiteral(".."));
}

} // namespace VaultPaths
