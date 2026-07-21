// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
#include "assetstore.h"

#include "vaultpaths.h"

#include <QClipboard>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QImage>
#include <QMimeData>
#include <QUrl>

namespace {

// One path segment of safe characters. The slug is a note title the caller
// passed through, and the extension comes off a dropped file's name; either
// could otherwise carry a separator or dot segments and put the asset
// somewhere else entirely.
QString safeSegment(const QString &value, const QString &fallback)
{
    QString out;
    out.reserve(value.size());
    for (const QChar c : value) {
        if (c.isLetterOrNumber() || c == QLatin1Char('-')
            || c == QLatin1Char('_') || c == QLatin1Char(' '))
            out.append(c);
        else if (!out.isEmpty() && !out.endsWith(QLatin1Char('-')))
            out.append(QLatin1Char('-'));
    }
    out = out.trimmed();
    while (out.endsWith(QLatin1Char('-')))
        out.chop(1);
    return out.isEmpty() ? fallback : out;
}

// The stored path to write into the markdown: relative to the collection root
// when one is open (so a note move never breaks it), else relative to the
// note's own folder (single-file mode).
QString storedPathFor(const QString &absFile, const QString &root,
                      const QString &noteDir)
{
    const QString base = !root.isEmpty() ? root : noteDir;
    if (base.isEmpty())
        return absFile;
    const QString rel = QDir(base).relativeFilePath(absFile);
    // If the file is outside `base`, relativeFilePath yields ../ segments;
    // fall back to the absolute path so the source still resolves.
    return rel.startsWith(QStringLiteral("..")) ? absFile : rel;
}

} // namespace

AssetStore::AssetStore(QObject *parent) : QObject(parent) {}

QString AssetStore::assetsDir(const QString &root, const QString &noteDir)
{
    const QString base = !root.isEmpty() ? root : noteDir;
    if (base.isEmpty())
        return QString();
    // `assets` is the repository's directory in the same sense .kvit is: it
    // creates files there under names it chooses. A link standing in its
    // place would write every pasted screenshot and every dropped file into
    // whatever directory the link names, so it is refused, and the caller
    // reports the ingest as failed rather than silently storing the asset
    // somewhere the vault does not own.
    return VaultPaths::ownedDir(base, QStringLiteral("assets"));
}

QString AssetStore::uniqueAssetName(const QString &dir, const QString &slug,
                                    const QString &stamp, const QString &ext)
{
    const QString cleanSlug = safeSegment(slug, QStringLiteral("image"));
    const QString cleanExt = safeSegment(ext, QStringLiteral("bin"));
    const QString baseName = cleanSlug + QLatin1Char('-')
        + safeSegment(stamp, QStringLiteral("0"));
    QDir d(dir);
    QString name = baseName + QLatin1Char('.') + cleanExt;
    int n = 1;
    while (d.exists(name)) {
        name = baseName + QLatin1Char('-') + QString::number(n)
             + QLatin1Char('.') + cleanExt;
        ++n;
    }
    return name;
}

QString AssetStore::ingestImage(const QImage &image, const QString &noteSlug,
                                const QString &root, const QString &noteDir) const
{
    if (image.isNull())
        return QString();
    const QString base = !root.isEmpty() ? root : noteDir;
    const QString dir =
        VaultPaths::ensureOwnedDir(base, QStringLiteral("assets"));
    if (dir.isEmpty())
        return QString();
    const QString stamp =
        QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-HHmmss"));
    const QString name = uniqueAssetName(dir, noteSlug, stamp,
                                         QStringLiteral("png"));
    const QString abs = QDir(dir).filePath(name);
    if (!image.save(abs, "PNG"))
        return QString();
    return storedPathFor(abs, root, noteDir);
}

QString AssetStore::ingestFile(const QString &sourcePath, const QString &noteSlug,
                               const QString &root, const QString &noteDir) const
{
    const QFileInfo src(sourcePath);
    if (!src.exists() || !src.isFile())
        return QString();
    const QString absSrc = src.absoluteFilePath();

    // Link in place when the file already lives under the collection root:
    // no copy, just store its root-relative path.
    if (!root.isEmpty()) {
        const QString rootAbs = QDir(root).absolutePath();
        if (absSrc.startsWith(rootAbs + QLatin1Char('/')))
            return storedPathFor(absSrc, root, noteDir);
    }

    // Otherwise copy into assets/.
    const QString base = !root.isEmpty() ? root : noteDir;
    const QString dir =
        VaultPaths::ensureOwnedDir(base, QStringLiteral("assets"));
    if (dir.isEmpty())
        return QString();
    const QString stamp =
        QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-HHmmss"));
    const QString name = uniqueAssetName(dir, noteSlug, stamp, src.suffix());
    const QString abs = QDir(dir).filePath(name);
    if (!QFile::copy(absSrc, abs))
        return QString();
    return storedPathFor(abs, root, noteDir);
}

// ---- QML wrappers ----

bool AssetStore::clipboardHasImage() const
{
    const QClipboard *cb = QGuiApplication::clipboard();
    return cb && cb->mimeData() && cb->mimeData()->hasImage();
}

QString AssetStore::ingestClipboardImage(const QString &noteSlug,
                                         const QString &root,
                                         const QString &noteDir) const
{
    const QClipboard *cb = QGuiApplication::clipboard();
    if (!cb)
        return QString();
    const QImage image = cb->image();
    return ingestImage(image, noteSlug, root, noteDir);
}

QString AssetStore::ingestImageBytes(const QByteArray &bytes,
                                     const QString &noteSlug,
                                     const QString &root,
                                     const QString &noteDir) const
{
    QImage image;
    if (!image.loadFromData(bytes))
        return QString();
    return ingestImage(image, noteSlug, root, noteDir);
}

QString AssetStore::ingestLocalFile(const QString &sourcePath,
                                    const QString &noteSlug,
                                    const QString &root,
                                    const QString &noteDir) const
{
    QString path = sourcePath;
    if (path.startsWith(QStringLiteral("file://")))
        path = QUrl(path).toLocalFile();
    return ingestFile(path, noteSlug, root, noteDir);
}
