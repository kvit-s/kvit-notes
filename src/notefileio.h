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

} // namespace NoteFileIo

#endif // NOTEFILEIO_H
