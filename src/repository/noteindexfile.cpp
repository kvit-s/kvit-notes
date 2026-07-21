// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
#include "noteindexfile.h"

#include "notefileio.h"
#include "notefrontmatter.h"
#include "perflog.h"

#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStringList>
#include <QVariantMap>

namespace {

const QString indexFileName = QStringLiteral("index.json");
const QString mdSuffix = QStringLiteral(".md");

QJsonArray stringListToJson(const QStringList &values)
{
    QJsonArray array;
    for (const QString &value : values)
        array.append(value);
    return array;
}

QStringList stringListFromJson(const QJsonArray &array)
{
    QStringList values;
    values.reserve(array.size());
    for (const QJsonValue &value : array)
        values.append(value.toString());
    return values;
}

} // namespace

void NoteIndexFile::setRootPath(const QString &rootPath)
{
    m_rootPath = rootPath;
    if (!rootPath.isEmpty()) {
        // The index lives under .kvit/cache now. Establish the tagged cache
        // directory and drop any copy left at the pre-split .kvit/index.json,
        // which is rebuilt from this scan anyway.
        NoteFileIo::ensureVaultCacheDir(rootPath);
        NoteFileIo::removeLegacyVaultCache(rootPath);
    }
}

bool NoteIndexFile::writeBytes(const QString &path, const QByteArray &bytes)
{
    return NoteFileIo::writeFileBytesAtomic(path, bytes);
}

QString NoteIndexFile::path() const
{
    if (m_rootPath.isEmpty())
        return QString();
    return NoteFileIo::vaultCacheDir(m_rootPath)
        + QLatin1Char('/') + indexFileName;
}

QHash<QString, NoteEntry> NoteIndexFile::load(bool *ok) const
{
    if (ok)
        *ok = false;

    QHash<QString, NoteEntry> notes;
    const QString filePath = path();
    if (filePath.isEmpty() || !QFileInfo::exists(filePath))
        return notes;

    bool readOk = false;
    const QByteArray bytes = NoteFileIo::readFileBytes(filePath, &readOk);
    if (!readOk)
        return notes;

    QJsonParseError error;
    const QJsonDocument doc = QJsonDocument::fromJson(bytes, &error);
    if (error.error != QJsonParseError::NoError || !doc.isObject())
        return notes;

    const QJsonObject root = doc.object();
    // Only the current sidecar format is trusted; an older cache is dropped and
    // rebuilt from Markdown.
    if (root.value(QStringLiteral("version")).toInt() != 2)
        return notes;

    const QJsonArray entries = root.value(QStringLiteral("notes")).toArray();
    notes.reserve(entries.size());
    for (const QJsonValue &value : entries) {
        const QJsonObject object = value.toObject();
        const QString relPath = object.value(QStringLiteral("relPath")).toString();
        if (relPath.isEmpty()
            || !relPath.endsWith(mdSuffix, Qt::CaseInsensitive))
            continue;

        NoteEntry entry;
        entry.relPath = relPath;
        entry.folder = folderOfRelPath(relPath);
        const QString fallbackName = nameOfRelPath(relPath);
        entry.title = object.value(QStringLiteral("title")).toString(
            fallbackName.endsWith(mdSuffix, Qt::CaseInsensitive)
                ? fallbackName.left(fallbackName.size() - mdSuffix.size())
                : fallbackName);
        entry.created = dateTimeFromMs(
            qint64(object.value(QStringLiteral("createdMs")).toDouble()));
        entry.modified = dateTimeFromMs(
            qint64(object.value(QStringLiteral("modifiedMs")).toDouble()));
        entry.fileSize =
            qint64(object.value(QStringLiteral("size")).toDouble(-1));
        entry.wordCount = object.value(QStringLiteral("wordCount")).toInt();
        entry.snippet = object.value(QStringLiteral("snippet")).toString();
        // Wiki-link targets are read from the sidecar; without the body cached
        // they can no longer be re-derived here.
        entry.links = stringListFromJson(
            object.value(QStringLiteral("links")).toArray());
        entry.meta = NoteFrontMatter::parse(
            object.value(QStringLiteral("frontMatter")).toString());
        notes.insert(relPath, entry);
    }

    if (ok)
        *ok = true;
    return notes;
}

QByteArray NoteIndexFile::buildBytes(
    const QHash<QString, NoteEntry> &notes)
{
    QJsonObject root;
    // Version 2: the sidecar no longer caches full
    // bodies or per-block display text — global search reads those from the
    // SQLite index. Wiki-link targets, previously re-derived from the cached
    // body on every load, are persisted so warm startup keeps the backlink
    // graph without reading every note. An older version-1 cache is discarded.
    root.insert(QStringLiteral("version"), 2);

    QJsonArray entries;
    QStringList paths = notes.keys();
    paths.sort();
    for (const QString &relPath : paths) {
        const auto it = notes.constFind(relPath);
        if (it == notes.constEnd())
            continue;
        const NoteEntry &entry = it.value();
        QJsonObject object;
        object.insert(QStringLiteral("relPath"), entry.relPath);
        object.insert(QStringLiteral("title"), entry.title);
        object.insert(QStringLiteral("createdMs"),
                      double(dateTimeMs(entry.created)));
        object.insert(QStringLiteral("modifiedMs"),
                      double(dateTimeMs(entry.modified)));
        object.insert(QStringLiteral("size"), double(entry.fileSize));
        object.insert(QStringLiteral("wordCount"), entry.wordCount);
        object.insert(QStringLiteral("snippet"), entry.snippet);
        object.insert(QStringLiteral("links"), stringListToJson(entry.links));
        object.insert(QStringLiteral("frontMatter"),
                      NoteFrontMatter::serialize(entry.meta));
        entries.append(object);
    }
    root.insert(QStringLiteral("notes"), entries);

    return QJsonDocument(root).toJson(QJsonDocument::Compact);
}

bool NoteIndexFile::save(const QHash<QString, NoteEntry> &notes) const
{
    const QString filePath = path();
    if (filePath.isEmpty())
        return false;

    PerfLog::ScopedTimer perf(
        QStringLiteral("collection.index_save"),
        QVariantMap{{QStringLiteral("path"), filePath},
                    {QStringLiteral("notes"), notes.size()}});

    if (notes.isEmpty() && !QFileInfo::exists(filePath)) {
        perf.addContext(QStringLiteral("skipped"), true);
        return true; // nothing to write is not a failure
    }

    NoteFileIo::ensureVaultCacheDir(m_rootPath);
    const QByteArray bytes = buildBytes(notes);
    perf.addContext(QStringLiteral("bytes"), bytes.size());
    const bool ok = writeBytes(filePath, bytes);
    perf.addContext(QStringLiteral("ok"), ok);
    return ok;
}
