// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
#include "notefileio.h"

#include <QFile>
#include <QSaveFile>
#include <QTextStream>

namespace NoteFileIo {

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
