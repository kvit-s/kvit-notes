// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
#include "notefileio.h"

#include "vaultpaths.h"

#include <QDir>
#include <QFile>
#include <QSaveFile>
#include <QTextStream>

namespace NoteFileIo {

namespace {
const QString kCacheRel = QStringLiteral(".kvit/cache");
const QString kKvitRel = QStringLiteral(".kvit");
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
    // "" when .kvit or .kvit/cache is a link rather than a directory the
    // vault owns. Every caller already treats an empty directory as "no
    // cache available", which is the right answer for a cache that would
    // otherwise be written outside the vault.
    return VaultPaths::ownedDir(rootPath, kCacheRel);
}

void ensureVaultCacheDir(const QString &rootPath)
{
    const QString dir = VaultPaths::ensureOwnedDir(rootPath, kCacheRel);
    if (dir.isEmpty())
        return;
    const QString tag = QDir(dir).filePath(QStringLiteral("CACHEDIR.TAG"));
    if (!QFile::exists(tag))
        writeFileBytesAtomic(tag, kCacheTag);
}

void removeLegacyVaultCache(const QString &rootPath)
{
    // The most dangerous line in the module: it deletes a file and then
    // removes a directory tree, unprompted, as a side effect of attaching the
    // index store to a root. Through a symlinked .kvit that is a recursive
    // delete of a directory the user never opened, so the containment test
    // comes first and a refusal deletes nothing at all.
    const QString kvit = VaultPaths::ownedDir(rootPath, kKvitRel);
    if (kvit.isEmpty())
        return;
    QFile::remove(QDir(kvit).filePath(QStringLiteral("index.json")));
    const QString embedCache =
        VaultPaths::ownedDir(rootPath, kKvitRel + QLatin1String("/embedcache"));
    if (!embedCache.isEmpty())
        QDir(embedCache).removeRecursively();
}

// Opening a file successfully says nothing about reading it. A failing disk,
// a network mount that drops, a removable device pulled mid-read: each of
// those returns short from read() with an error recorded on the device, and
// readAll() hands back the bytes it managed to get. Callers here overwrite
// the file they just read, or delete a recovery journal once they believe
// they hold its contents, so a partial read reported as a whole one destroys
// the rest of the note. Success is therefore claimed only after the device
// reports no error and, where the size was known in advance, after every byte
// of it arrived.
QString readTextFile(const QString &path, bool *ok, qint64 maxBytes)
{
    if (ok)
        *ok = false;
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        return QString();
    if (maxBytes > 0 && file.size() > maxBytes)
        return QString();
    QTextStream stream(&file);
    stream.setEncoding(QStringConverter::Utf8);
    const QString text = stream.readAll();
    // The decoded length cannot be compared against the file size, so the
    // text reader relies on the device's own error state and on having
    // actually reached the end of the file.
    if (file.error() != QFileDevice::NoError || stream.status() != QTextStream::Ok
        || !file.atEnd()) {
        return QString();
    }
    if (ok)
        *ok = true;
    return text;
}

QByteArray readFileBytes(const QString &path, bool *ok, qint64 maxBytes)
{
    if (ok)
        *ok = false;
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return QByteArray();
    if (maxBytes > 0 && file.size() > maxBytes)
        return QByteArray();
    // Recorded before the read: a file that shrinks underneath us is exactly
    // the case the count is meant to catch. Zero means "not known" — the
    // shape /proc entries and pipes report — and only a device error can
    // speak for those.
    const qint64 expected = file.size();
    const QByteArray content = file.readAll();
    if (file.error() != QFileDevice::NoError)
        return QByteArray();
    if (expected > 0 && content.size() != expected)
        return QByteArray();
    if (ok)
        *ok = true;
    return content;
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
