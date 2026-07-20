// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
#include "documentimporter.h"
#include "notecollection.h"
#include "perflog.h"

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSet>

namespace {
QString sanitizeBase(const QString &name)
{
    QString out = name.trimmed();
    out.remove(QRegularExpression(QStringLiteral("[/\\\\:*?\"<>|]")));
    return out.isEmpty() ? QStringLiteral("Imported") : out;
}
} // namespace

DocumentImporter::DocumentImporter(QObject *parent)
    : QObject(parent)
{
}

void DocumentImporter::setCollection(NoteCollection *collection)
{
    m_collection = collection;
}

bool DocumentImporter::isImportable(const QString &path)
{
    const QString lower = path.toLower();
    return lower.endsWith(QLatin1String(".md"))
        || lower.endsWith(QLatin1String(".markdown"))
        || lower.endsWith(QLatin1String(".txt"));
}

bool DocumentImporter::noteFileExists(const QString &relPath) const
{
    if (!m_collection)
        return false;
    return QFile::exists(m_collection->absolutePath(relPath));
}

QString DocumentImporter::uniqueRelPath(const QString &folder,
                                        const QString &baseName) const
{
    const QString base = sanitizeBase(baseName);
    const QString prefix = folder.isEmpty() ? QString() : folder + QLatin1Char('/');
    QString rel = prefix + base + QStringLiteral(".md");
    int n = 2;
    while (noteFileExists(rel)) {
        rel = prefix + base + QStringLiteral(" ") + QString::number(n)
            + QStringLiteral(".md");
        ++n;
    }
    return rel;
}

bool DocumentImporter::copyInto(const QString &sourcePath,
                                const QString &targetRelPath)
{
    if (!m_collection)
        return false;
    QFile in(sourcePath);
    if (!in.open(QIODevice::ReadOnly))
        return false;
    const QByteArray bytes = in.readAll();
    in.close();

    const QString destAbs = m_collection->absolutePath(targetRelPath);
    QDir().mkpath(QFileInfo(destAbs).absolutePath());
    // QSaveFile writes a temporary alongside the destination and renames it
    // into place only on commit, so a disk that fills partway through leaves
    // no half-written note for the collection to index. Every byte must land:
    // a short write is a failed import, not a smaller one.
    QSaveFile out(destAbs);
    if (!out.open(QIODevice::WriteOnly))
        return false;
    if (out.write(bytes) != bytes.size()) {
        out.cancelWriting();
        return false;
    }
    return out.commit();
}

QList<QPair<QString, QString>>
DocumentImporter::importableFilesUnder(const QString &dirPath) const
{
    QList<QPair<QString, QString>> files;
    QDir base(dirPath);
    QDirIterator it(dirPath, QDir::Files, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        const QString abs = it.next();
        if (!isImportable(abs))
            continue;
        // The relative subdirectory of this file within dirPath.
        const QString relFile = base.relativeFilePath(abs);
        const int slash = relFile.lastIndexOf(QLatin1Char('/'));
        const QString relSub = slash >= 0 ? relFile.left(slash) : QString();
        files.append({abs, relSub});
    }
    return files;
}

// ---- dry runs ----

QVariantMap DocumentImporter::dryRunFiles(const QStringList &paths,
                                          const QString &targetFolder) const
{
    int files = 0;
    int collisions = 0;
    for (const QString &p : paths) {
        if (!isImportable(p))
            continue;
        ++files;
        const QString base = QFileInfo(p).completeBaseName();
        const QString prefix = targetFolder.isEmpty()
            ? QString() : targetFolder + QLatin1Char('/');
        if (noteFileExists(prefix + base + QStringLiteral(".md")))
            ++collisions;
    }
    return QVariantMap{
        {QStringLiteral("files"), files},
        {QStringLiteral("collisions"), collisions},
        {QStringLiteral("folder"), targetFolder},
    };
}

QVariantMap DocumentImporter::dryRunFolder(const QString &dirPath,
                                           const QString &targetFolder) const
{
    const auto entries = importableFilesUnder(dirPath);
    int collisions = 0;
    QSet<QString> subdirs;
    for (const auto &e : entries) {
        if (!e.second.isEmpty())
            subdirs.insert(e.second);
        const QString base = QFileInfo(e.first).completeBaseName();
        const QString folder = targetFolder.isEmpty()
            ? e.second
            : (e.second.isEmpty() ? targetFolder
                                  : targetFolder + QLatin1Char('/') + e.second);
        const QString prefix = folder.isEmpty()
            ? QString() : folder + QLatin1Char('/');
        if (noteFileExists(prefix + base + QStringLiteral(".md")))
            ++collisions;
    }
    return QVariantMap{
        {QStringLiteral("files"), entries.size()},
        {QStringLiteral("collisions"), collisions},
        {QStringLiteral("folders"), subdirs.size()},
    };
}

// ---- imports ----

int DocumentImporter::importFiles(const QStringList &paths,
                                  const QString &targetFolder)
{
    PerfLog::ScopedTimer perf(
        QStringLiteral("import.run"),
        QVariantMap{
            {QStringLiteral("paths"), paths.size()},
            {QStringLiteral("targetFolder"), targetFolder},
        });
    if (!m_collection || !m_collection->isOpen())
        return 0;
    if (!targetFolder.isEmpty())
        QDir().mkpath(m_collection->absolutePath(targetFolder));
    int imported = 0;
    for (const QString &p : paths) {
        if (!isImportable(p))
            continue;
        const QString base = QFileInfo(p).completeBaseName();
        const QString rel = uniqueRelPath(targetFolder, base);
        if (copyInto(p, rel))
            ++imported;
    }
    if (imported > 0)
        m_collection->refresh();
    perf.addContext(QStringLiteral("imported"), imported);
    return imported;
}

int DocumentImporter::importFolder(const QString &dirPath,
                                   const QString &targetFolder)
{
    PerfLog::ScopedTimer perf(
        QStringLiteral("import.run"),
        QVariantMap{
            {QStringLiteral("dir"), dirPath},
            {QStringLiteral("targetFolder"), targetFolder},
        });
    if (!m_collection || !m_collection->isOpen())
        return 0;
    const auto entries = importableFilesUnder(dirPath);
    perf.addContext(QStringLiteral("paths"), entries.size());
    int imported = 0;
    for (const auto &e : entries) {
        // The destination folder mirrors the source subfolder under target.
        QString folder;
        if (targetFolder.isEmpty())
            folder = e.second;
        else if (e.second.isEmpty())
            folder = targetFolder;
        else
            folder = targetFolder + QLatin1Char('/') + e.second;
        if (!folder.isEmpty())
            QDir().mkpath(m_collection->absolutePath(folder));
        const QString base = QFileInfo(e.first).completeBaseName();
        const QString rel = uniqueRelPath(folder, base);
        if (copyInto(e.first, rel))
            ++imported;
    }
    if (imported > 0)
        m_collection->refresh();
    perf.addContext(QStringLiteral("imported"), imported);
    return imported;
}
