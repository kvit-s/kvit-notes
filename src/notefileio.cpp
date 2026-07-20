// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
#include "notefileio.h"

#include <QDir>
#include <QFile>
#include <QSaveFile>
#include <QTextStream>

namespace NoteFileIo {

namespace {
const QString kCacheRel = QStringLiteral(".kvit/cache");
// The first line is the exact magic CACHEDIR.TAG requires; tools match on it.
const QByteArray kCacheTag =
    "Signature: 8a477f597d28d172789f06886806bc55\n"
    "# This file marks <vault>/.kvit/cache as a Kvit Notes cache directory.\n"
    "# Its contents are rebuilt on demand and are safe to delete. The\n"
    "# irreplaceable state lives in .kvit itself, not here.\n"
    "# See https://bford.info/cachedir/\n";
} // namespace

QString vaultCacheDir(const QString &rootPath)
{
    if (rootPath.isEmpty())
        return QString();
    return QDir(rootPath).filePath(kCacheRel);
}

void ensureVaultCacheDir(const QString &rootPath)
{
    const QString dir = vaultCacheDir(rootPath);
    if (dir.isEmpty())
        return;
    QDir().mkpath(dir);
    const QString tag = QDir(dir).filePath(QStringLiteral("CACHEDIR.TAG"));
    if (!QFile::exists(tag))
        writeFileBytesAtomic(tag, kCacheTag);
}

void removeLegacyVaultCache(const QString &rootPath)
{
    if (rootPath.isEmpty())
        return;
    QDir kvit(QDir(rootPath).filePath(QStringLiteral(".kvit")));
    QFile::remove(kvit.filePath(QStringLiteral("index.json")));
    QDir(kvit.filePath(QStringLiteral("embedcache"))).removeRecursively();
}

QString readTextFile(const QString &path, bool *ok)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        if (ok)
            *ok = false;
        return QString();
    }
    QTextStream stream(&file);
    stream.setEncoding(QStringConverter::Utf8);
    if (ok)
        *ok = true;
    return stream.readAll();
}

QByteArray readFileBytes(const QString &path, bool *ok)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        if (ok)
            *ok = false;
        return QByteArray();
    }
    if (ok)
        *ok = true;
    return file.readAll();
}

bool writeTextFileAtomic(const QString &path, const QString &content)
{
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text))
        return false;
    QTextStream stream(&file);
    stream.setEncoding(QStringConverter::Utf8);
    stream << content;
    stream.flush();
    // A stream that ran out of space stops writing and records it here rather
    // than reporting it from the insertion operator. Committing without this
    // check renames a truncated file over the target and returns success, so
    // the caller tells the user their note was saved while most of it is
    // gone. writeFileBytesAtomic below has always checked its write; this is
    // the same guarantee for the text path.
    if (stream.status() != QTextStream::Ok) {
        file.cancelWriting();
        return false;
    }
    return file.commit();
}

bool writeFileBytesAtomic(const QString &path, const QByteArray &content)
{
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly))
        return false;
    if (file.write(content) != content.size())
        return false;
    return file.commit();
}

} // namespace NoteFileIo
