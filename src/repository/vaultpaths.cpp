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
    // A filesystem root already ends in its separator ("/", "C:/"), and
    // appending another one produces a prefix ("//", "C://") that no child
    // path starts with, so every mutation under a vault at the root of a
    // filesystem was rejected as being outside itself.
    const QString prefix = canonicalRoot.endsWith(QLatin1Char('/'))
        ? canonicalRoot
        : canonicalRoot + QLatin1Char('/');
    return canonical.startsWith(prefix);
}

namespace {

// Everything Qt can tell us about a path standing in for another one: a
// POSIX symbolic link, a Windows junction, a Windows shortcut. isSymbolicLink
// alone answers only the first of the three on Windows.
bool isLinkLike(const QFileInfo &info)
{
    return info.isSymbolicLink() || info.isJunction() || info.isShortcut();
}

} // namespace

bool ownedDirIsSound(const QString &rootPath, const QString &relDir)
{
    if (rootPath.isEmpty() || !isPlainRelativePath(relDir))
        return false;
    const QString canonicalRoot = canonicalizeMissingOk(rootPath);
    if (canonicalRoot.isEmpty())
        return false;

    // Walk down from the root so a link is caught at the component that is
    // one, rather than only when the final canonical path lands elsewhere.
    // symLinkTarget is not consulted: a link pointing back inside the vault
    // is refused too, because the point is that the repository decides where
    // its own directories are.
    QString accumulated = QDir::cleanPath(rootPath);
    const QStringList segments = relDir.split(QLatin1Char('/'));
    for (const QString &segment : segments) {
        accumulated += QLatin1Char('/') + segment;
        const QFileInfo info(accumulated);
        if (isLinkLike(info))
            return false;
        if (info.exists() && !info.isDir())
            return false;
    }
    // The components are all real, so this only differs from the textual path
    // when the root itself was reached through a link -- and then it is the
    // check that says the two agree.
    return isWithinCanonicalRoot(canonicalRoot, accumulated);
}

QString ownedDir(const QString &rootPath, const QString &relDir)
{
    if (!ownedDirIsSound(rootPath, relDir))
        return QString();
    return QDir::cleanPath(rootPath) + QLatin1Char('/') + relDir;
}

QString ensureOwnedDir(const QString &rootPath, const QString &relDir)
{
    if (!ownedDirIsSound(rootPath, relDir))
        return QString();
    const QString absDir = QDir::cleanPath(rootPath) + QLatin1Char('/') + relDir;
    if (!QDir().mkpath(absDir))
        return QString();
    // Creating it can have raced a link appearing on the way down, and mkpath
    // itself follows links, so the answer is only good once it has been asked
    // again about what now exists.
    return ownedDirIsSound(rootPath, relDir) ? absDir : QString();
}

QString ownedFile(const QString &rootPath, const QString &relDir,
                  const QString &name)
{
    if (name.isEmpty() || !isPlainRelativePath(name)
        || name.contains(QLatin1Char('/'))) {
        return QString();
    }
    const QString dir = ownedDir(rootPath, relDir);
    return dir.isEmpty() ? QString() : dir + QLatin1Char('/') + name;
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
