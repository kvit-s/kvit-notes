// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
#ifndef NOTEFILEIO_H
#define NOTEFILEIO_H

#include <QByteArray>
#include <QString>

// The read and write primitives the collection's persistence layer is built
// on. Writes go through QSaveFile, so a note, a sidecar or a backup is either
// the old content or the new one and never a truncated file. Reads report
// failure through `ok` rather than by returning an empty string, because an
// empty note and an unreadable note have to be told apart.
namespace NoteFileIo {

QString readTextFile(const QString &path, bool *ok = nullptr);
QByteArray readFileBytes(const QString &path, bool *ok = nullptr);
bool writeTextFileAtomic(const QString &path, const QString &content);
bool writeFileBytesAtomic(const QString &path, const QByteArray &content);

// The <root>/.kvit control directory holds two kinds of thing, and telling
// them apart matters: rebuildable caches (the note index, embed metadata) and
// irreplaceable state that exists in no Markdown file (trash, backups, the
// crash-recovery journals, collection.json's manual order and tag colours,
// templates). Only the caches belong here. Everything disposable lives under
// <root>/.kvit/cache, so a "clear cache" action, a sync exclusion, or a
// backup tool has one subtree it can drop without taking state with it.
QString vaultCacheDir(const QString &rootPath);

// Create the cache directory and mark it with a CACHEDIR.TAG, the standard
// signature borg, restic and other backup tools read to skip a directory
// without the user configuring anything. Idempotent; safe to call on open.
void ensureVaultCacheDir(const QString &rootPath);

// Remove the caches from their pre-split locations directly under
// <root>/.kvit. They are rebuildable, so this loses nothing; it only stops a
// stale copy lingering beside the state after the move to <root>/.kvit/cache.
void removeLegacyVaultCache(const QString &rootPath);

} // namespace NoteFileIo

#endif // NOTEFILEIO_H
