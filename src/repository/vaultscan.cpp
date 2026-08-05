// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
#include "vaultscan.h"

#include "block.h"
#include "perflog.h"
#include "documentserializer.h"
#include "notefileio.h"
#include "notefrontmatter.h"
#include "noteindexfile.h"
#include "vaultpaths.h"
#include "wikilinkindex.h"

#include <QDir>
#include <QRegularExpression>

#include <functional>

namespace VaultScan {
namespace {

// The primitives this worker code shares with the rest of the repository,
// pulled in unqualified so the bodies below read as they did when they were
// members of NoteCollection.
using NoteFileIo::readTextFile;
using VaultPaths::canonicalizeMissingOk;

const QString kvitDirName = QStringLiteral(".kvit");
const QString mdSuffix = QStringLiteral(".md");

// A note file created outside the app may report no birth time, in which
// case its modification time is the best "created" the index can offer.
QDateTime fileCreatedTime(const QFileInfo &info)
{
    QDateTime birth = info.birthTime();
    return birth.isValid() ? birth : info.lastModified();
}

// Not const: a test has to be able to lower it, since writing a 32 MiB
// fixture to prove the cap works would itself be the problem the cap exists
// to prevent.
qint64 g_maxNoteBytes = 32LL * 1024 * 1024;

} // namespace

qint64 maxNoteBytes()
{
    return g_maxNoteBytes;
}

void setMaxNoteBytes(qint64 bytes)
{
    g_maxNoteBytes = bytes;
}

NoteEntry unparsedEntry(const QString &relPath, const QFileInfo &info)
{
    NoteEntry entry = placeholderEntry(relPath, info);
    // Unlike a placeholder, this is the final answer for the file as it
    // stands, so it carries the real size: the next scan compares size and
    // modification time and leaves it alone until it changes.
    entry.fileSize = info.size();
    return entry;
}

NoteEntry placeholderEntry(
    const QString &relPath,
    const QFileInfo &info)
{
    NoteEntry entry;
    entry.relPath = relPath;
    entry.folder = folderOfRelPath(relPath);
    const QString name = nameOfRelPath(relPath);
    entry.title = name.endsWith(mdSuffix, Qt::CaseInsensitive)
                      ? name.left(name.size() - mdSuffix.size())
                      : name;
    entry.modified = info.lastModified();
    // Not a complete index entry yet. If parsing fails before this placeholder
    // is replaced, the persisted sidecar must force a retry on the next launch.
    entry.fileSize = -1;
    entry.created = fileCreatedTime(info);
    return entry;
}

NoteEntry cachedEntryForPath(
    const QString &relPath,
    const NoteEntry &cached,
    const QFileInfo &info)
{
    NoteEntry entry = cached;
    entry.relPath = relPath;
    entry.folder = folderOfRelPath(relPath);
    const QString name = nameOfRelPath(relPath);
    entry.title = name.endsWith(mdSuffix, Qt::CaseInsensitive)
                      ? name.left(name.size() - mdSuffix.size())
                      : name;
    if (!entry.created.isValid())
        entry.created = fileCreatedTime(info);
    return entry;
}

NoteEntry entryFromText(
    const QString &relPath,
    const QString &fileText,
    const QFileInfo &info)
{
    NoteFrontMatter::Split split = NoteFrontMatter::split(fileText);

    NoteEntry entry;
    entry.relPath = relPath;
    entry.folder = folderOfRelPath(relPath);
    const QString name = nameOfRelPath(relPath);
    entry.title = name.endsWith(mdSuffix, Qt::CaseInsensitive)
                      ? name.left(name.size() - mdSuffix.size())
                      : name;
    entry.meta = NoteFrontMatter::parse(split.block);

    entry.modified = info.lastModified();
    entry.fileSize = info.size();
    entry.created =
        entry.meta.created.isValid() ? entry.meta.created : fileCreatedTime(info);
    BodyStats stats = analyzeBody(split.body);
    entry.wordCount = stats.wordCount;
    entry.snippet = stats.snippet;
    entry.links = WikiLinkIndex::extractLinks(split.body);
    return entry;
}

ScanListing buildScanListing(
    const ScanRequest &request)
{
    ScanListing listing;
    listing.rootPath = request.rootPath;
    listing.indexDirty = !request.indexOk && request.indexFileExists;
    listing.generation = request.generation;

    int noteCount = 0;
    const QStringList reservedNames = request.reserved.names();
    // Canonical directories already walked. Symbolic links are excluded
    // below, but a directory can be re-entered by other means (a bind mount,
    // a junction), and a scan that revisits one never finishes.
    QSet<QString> visitedDirs;
    // `inReserved` says the walk is inside a subtree the application manages
    // (reservedsubtrees.h): its directories are not the user's folders, and
    // the only files that enter the index are the ones the registration
    // nominates.
    std::function<void(const QString &, bool)> scanDir =
        [&](const QString &currentRelDir, bool inReserved) {
            // A vault the user has already left should stop being walked.
            // QtConcurrent::run cannot interrupt this, so the check is the
            // only way out before the last directory.
            if (isCancelled(request.cancel))
                return;
            const QString absDir = currentRelDir.isEmpty()
                ? request.rootPath
                : request.rootPath + QLatin1Char('/') + currentRelDir;
            const QString canonicalDir = canonicalizeMissingOk(absDir);
            if (visitedDirs.contains(canonicalDir))
                return;
            visitedDirs.insert(canonicalDir);
            QDir dir(absDir);
            const QFileInfoList entries = dir.entryInfoList(
                QDir::Dirs | QDir::Files | QDir::NoDotAndDotDot
                    | QDir::NoSymLinks,
                QDir::Name);
            for (const QFileInfo &info : entries) {
                if (isCancelled(request.cancel))
                    return;
                const QString name = info.fileName();
                const QString relPath = joinRelPath(currentRelDir, name);
                if (name.startsWith(QLatin1Char('.')))
                    continue;

                if (info.isDir()) {
                    // A folder inside a reserved subtree is the
                    // application's, not the user's, so it joins neither the
                    // folder tree nor the note counts.
                    if (!inReserved) {
                        FolderEntry folder;
                        folder.relPath = relPath;
                        folder.name = name;
                        listing.folders.append(folder);
                    }
                    scanDir(relPath, inReserved);
                    continue;
                }

                if (!name.endsWith(mdSuffix, Qt::CaseInsensitive))
                    continue;

                // Inside a reserved subtree only the nominated files enter,
                // and each carries the realm it was admitted to.
                QString realm;
                if (inReserved) {
                    realm = request.reserved.admittedLabel(relPath);
                    if (realm.isEmpty())
                        continue;
                }

                ++noteCount;
                const auto cachedIt = request.cachedNotes.constFind(relPath);
                if (cachedIt != request.cachedNotes.constEnd()
                    && cachedIt->fileSize == info.size()
                    && dateTimeMs(cachedIt->modified)
                        == dateTimeMs(info.lastModified())) {
                    NoteEntry entry =
                        cachedEntryForPath(relPath, cachedIt.value(), info);
                    entry.realm = realm;
                    listing.entries.append(entry);
                    continue;
                }

                NoteEntry entry =
                    cachedIt == request.cachedNotes.constEnd()
                        ? placeholderEntry(relPath, info)
                        : cachedEntryForPath(relPath, cachedIt.value(), info);
                entry.realm = realm;
                listing.entries.append(entry);

                IndexTask task;
                task.relPath = relPath;
                task.absPath = info.absoluteFilePath();
                task.createdFallback = fileCreatedTime(info);
                task.modified = info.lastModified();
                task.fileSize = info.size();
                task.generation = request.generation;
                task.realm = realm;
                task.requiredType = request.reserved.requiredTypeFor(relPath);
                listing.tasks.append(task);
                listing.indexDirty = true;
            }

            // The subtrees the application manages, reached by name rather
            // than through the listing above. A dot-directory is hidden, and
            // the listing deliberately does not ask for hidden entries:
            // asking would also change what an ordinary folder of the user's
            // shows, on a platform where "hidden" is a file attribute rather
            // than a leading dot.
            if (currentRelDir.isEmpty() && !inReserved) {
                for (const QString &reservedName : reservedNames) {
                    const QFileInfo subtree(request.rootPath + QLatin1Char('/')
                                            + reservedName);
                    if (subtree.exists() && subtree.isDir()
                        && !subtree.isSymLink()) {
                        scanDir(reservedName, true);
                    }
                }
            }
        };

    scanDir(QString(), false);

    if ((!request.indexOk && noteCount > 0)
        || request.cachedNotes.size() != noteCount) {
        listing.indexDirty = true;
    }
    listing.cancelled = isCancelled(request.cancel);
    return listing;
}

IndexResult parseIndexTask(
    const IndexTask &task)
{
    IndexResult result;
    result.relPath = task.relPath;
    result.generation = task.generation;

    QFileInfo info(task.absPath);
    if (maxNoteBytes() > 0 && info.size() > maxNoteBytes()) {
        result.entry = unparsedEntry(task.relPath, info);
        result.entry.realm = task.realm;
        // Nothing read the file, so the front-matter cross-check could not
        // run. A file too large to parse is not one of the application's
        // small nominated documents, so it leaves the realm rather than
        // being admitted on its path alone.
        result.rejected = !task.requiredType.isEmpty();
        result.ok = !result.rejected;
        return result;
    }

    bool ok = false;
    const QString fileText = readTextFile(task.absPath, &ok);
    if (!ok)
        return result;

    result.entry = entryFromText(task.relPath, fileText, info);
    result.entry.realm = task.realm;
    // The path said this file is the application's; its front matter is what
    // confirms it. A file that merely landed in the right place with the
    // right name is not admitted.
    if (!task.requiredType.isEmpty()
        && result.entry.meta.fieldString(QStringLiteral("kvit-type"))
               .compare(task.requiredType, Qt::CaseInsensitive) != 0) {
        result.rejected = true;
        return result;
    }
    result.ok = true;
    return result;
}

RefreshResult buildRefreshResult(
    const RefreshRequest &request)
{
    RefreshResult result;
    result.rootPath = request.rootPath;
    result.relDirs = request.relDirs;
    result.generation = request.generation;

    QSet<QString> visitedDirs;
    const QStringList reservedNames = request.reserved.names();
    std::function<void(const QString &, bool)> scanDir =
        [&](const QString &currentRelDir, bool inReserved) {
            // Between directories, and again between notes below: this walk
            // reads and parses every changed body, so an abandoned refresh
            // that ran to the end would hold a pool thread against work whose
            // result is already going to be thrown away.
            if (isCancelled(request.cancel))
                return;
            const QString absDir =
                request.rootPath + QLatin1Char('/') + currentRelDir;
            const QString canonicalDir = canonicalizeMissingOk(absDir);
            if (visitedDirs.contains(canonicalDir))
                return;
            visitedDirs.insert(canonicalDir);
            QDir dir(absDir);
            const QFileInfoList entries = dir.entryInfoList(
                QDir::Dirs | QDir::Files | QDir::NoDotAndDotDot
                    | QDir::NoSymLinks,
                QDir::Name);
            for (const QFileInfo &info : entries) {
                if (isCancelled(request.cancel))
                    return;
                const QString name = info.fileName();
                const QString relPath = joinRelPath(currentRelDir, name);
                if (name.startsWith(QLatin1Char('.')))
                    continue;

                if (info.isDir()) {
                    if (!inReserved) {
                        FolderEntry folder;
                        folder.relPath = relPath;
                        folder.name = name;
                        result.folders.append(folder);
                        result.seenFolders.insert(relPath);
                    }
                    scanDir(relPath, inReserved);
                    continue;
                }

                if (!name.endsWith(mdSuffix, Qt::CaseInsensitive))
                    continue;

                QString realm;
                if (inReserved) {
                    realm = request.reserved.admittedLabel(relPath);
                    // Not nominated: it stays out of the index, and out of
                    // `seenNotes`, so a file that has just stopped being
                    // nominated is removed rather than left behind.
                    if (realm.isEmpty())
                        continue;
                }

                const QString requiredType =
                    request.reserved.requiredTypeFor(relPath);

                const auto current = request.currentNotes.constFind(relPath);
                if (current != request.currentNotes.constEnd()
                    && current->fileSize == info.size()
                    && dateTimeMs(current->modified)
                        == dateTimeMs(info.lastModified())) {
                    result.seenNotes.insert(relPath);
                    continue;
                }

                if (maxNoteBytes() > 0 && info.size() > maxNoteBytes()) {
                    // As in parseIndexTask: unread means unconfirmed, and an
                    // unconfirmed file is not admitted to a realm.
                    if (!requiredType.isEmpty())
                        continue;
                    result.seenNotes.insert(relPath);
                    result.entries.append(unparsedEntry(relPath, info));
                    continue;
                }

                bool ok = false;
                const QString fileText = readTextFile(info.absoluteFilePath(), &ok);
                if (!ok) {
                    // Unreadable now, but it exists: leaving it in `seenNotes`
                    // keeps the entry it already has rather than dropping a
                    // note because one read failed.
                    result.seenNotes.insert(relPath);
                    continue;
                }

                NoteEntry entry = entryFromText(
                    relPath, fileText, QFileInfo(info.absoluteFilePath()));
                entry.realm = realm;
                if (!requiredType.isEmpty()
                    && entry.meta.fieldString(QStringLiteral("kvit-type"))
                           .compare(requiredType, Qt::CaseInsensitive) != 0) {
                    continue;
                }
                result.seenNotes.insert(relPath);
                result.entries.append(entry);
            }

            // The reserved subtrees, by name: see the note in
            // buildScanListing on why they are not in the listing above.
            if (currentRelDir.isEmpty() && !inReserved) {
                for (const QString &reservedName : reservedNames) {
                    const QFileInfo subtree(request.rootPath + QLatin1Char('/')
                                            + reservedName);
                    if (subtree.exists() && subtree.isDir()
                        && !subtree.isSymLink()) {
                        scanDir(reservedName, true);
                    }
                }
            }
        };

    for (const QString &relDir : request.relDirs) {
        const QString absDir = request.rootPath + QLatin1Char('/') + relDir;
        const QFileInfo dirInfo(absDir);
        if (!dirInfo.exists() || !dirInfo.isDir()) {
            result.missingDirs.append(relDir);
            continue;
        }

        // A re-read can start anywhere the watcher reported a change,
        // including inside a reserved subtree, so which rules apply is asked
        // of the path rather than assumed from the walk.
        const bool inReserved = request.reserved.isReservedDir(relDir);
        if (!relDir.isEmpty() && !inReserved) {
            FolderEntry folder;
            folder.relPath = relDir;
            folder.name = nameOfRelPath(relDir);
            result.folders.append(folder);
            result.seenFolders.insert(relDir);
        }
        scanDir(relDir, inReserved);
    }

    result.cancelled = isCancelled(request.cancel);
    return result;
}

IndexResult parseSavedNoteTask(
    const SavedNoteTask &task)
{
    IndexResult result;
    result.relPath = task.relPath;
    result.generation = task.generation;

    QString fileText = task.fileText;
    if (fileText.isNull()) {
        bool ok = false;
        fileText = readTextFile(task.absPath, &ok);
        if (!ok)
            return result;
    }

    PerfLog::ScopedTimer split(
        QStringLiteral("collection.note_saved.index"),
        QVariantMap{{QStringLiteral("path"), task.absPath},
                    {QStringLiteral("relPath"), task.relPath},
                    {QStringLiteral("fromMemory"), !task.fileText.isNull()}});

    const QFileInfo info(task.absPath);
    NoteEntry entry = entryFromText(task.relPath, fileText, info);
    entry.modified = task.modified;
    entry.fileSize = task.fileSize;
    entry.created = entry.meta.created.isValid()
        ? entry.meta.created : task.createdFallback;

    result.entry = std::move(entry);
    result.ok = true;
    return result;
}


// Runs on a pool thread against the snapshot in `request`, never against live
// collection state. Serializing and writing are NoteIndexFile's; the
// generation stamp and the result record are this class's, because the handler
// that receives them has to know whether the vault it was saving for is still
// the vault that is open.
IndexSaveResult writeIndexFileSnapshot(const IndexSaveRequest &request)
{
    IndexSaveResult result;
    result.path = request.path;
    result.notes = request.notes.size();
    result.generation = request.generation;

    PerfLog::ScopedTimer perf(
        QStringLiteral("collection.index_save"),
        QVariantMap{{QStringLiteral("path"), request.path},
                    {QStringLiteral("notes"), request.notes.size()},
                    {QStringLiteral("async"), true}});

    const QByteArray bytes = NoteIndexFile::buildBytes(request.notes);
    result.bytes = bytes.size();
    perf.addContext(QStringLiteral("bytes"), result.bytes);
    result.ok = NoteIndexFile::writeBytes(request.path, bytes);
    perf.addContext(QStringLiteral("ok"), result.ok);
    return result;
}


// One parse serves the index: per-block display text (what the user sees
// and what search matches over), word count, and the list snippet.
BodyStats analyzeBody(const QString &markdownBody)
{
    static const int snippetLength = 120;
    DocumentSerializer serializer;

    BodyStats stats;
    const QList<DocumentSerializer::BlockData> blocks =
        serializer.parse(markdownBody);
    for (const DocumentSerializer::BlockData &block : blocks) {
        if (block.type == Block::Divider)
            continue;
        const Block cachedBlock(block.type, block.content);
        const QString text = cachedBlock.displayText();
        stats.wordCount += cachedBlock.wordCount();
        if (stats.snippet.size() < snippetLength && !block.content.isEmpty()) {
            if (!stats.snippet.isEmpty())
                stats.snippet += QLatin1Char(' ');
            stats.snippet += text.simplified();
        }
    }
    stats.snippet = stats.snippet.left(snippetLength);
    return stats;
}

} // namespace VaultScan
