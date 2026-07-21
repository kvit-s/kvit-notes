// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
#ifndef ASSETSTORE_H
#define ASSETSTORE_H

#include <QObject>
#include <QString>

class QImage;

// Bringing an image or a media file into the vault.
//
// A pasted screenshot, a dropped file, a file chosen from a dialog: each is
// copied or saved under <root>/assets (or <noteDir>/assets in single-file
// mode) and the markdown then stores the path relative to the root, so moving
// the note never breaks the reference.
//
// It is a repository service rather than part of ImageAssets, which parses
// and builds the markdown expression itself, because this half writes into
// the vault. That is the ownership line the module split draws: the parsing
// is a pure transform every layer can use, and the write belongs to the one
// module allowed to change what is on disk. Splitting them also puts the
// containment check somewhere — the file name is built from a caller-supplied
// slug, and nothing used to stop that slug from carrying a path.
class AssetStore : public QObject
{
    Q_OBJECT

public:
    explicit AssetStore(QObject *parent = nullptr);

    // The assets directory for a note: <root>/assets when a collection is
    // open, else <noteDir>/assets (beside the file in single-file mode).
    static QString assetsDir(const QString &root, const QString &noteDir);

    // A unique file name in `dir`: <slug>-<stamp>.<ext>, with a numeric
    // collision suffix if that name is taken. `stamp` is passed in so callers
    // (and tests) control it; the QML wrappers use the current time. The slug
    // and the extension are reduced to one path segment of safe characters
    // first, so neither can steer the result out of `dir`.
    static QString uniqueAssetName(const QString &dir, const QString &slug,
                                   const QString &stamp, const QString &ext);

    // Save `image` under the assets directory, returning the stored path to
    // put in the markdown — relative to the collection root (or to the note
    // in single-file mode). "" on failure.
    QString ingestImage(const QImage &image, const QString &noteSlug,
                        const QString &root, const QString &noteDir) const;

    // Ingest a local file (drop or file dialog): a file already under the
    // collection root is linked in place (its root-relative path is returned,
    // no copy); any other file is copied into assets/. "" on failure.
    QString ingestFile(const QString &sourcePath, const QString &noteSlug,
                       const QString &root, const QString &noteDir) const;

    // ---- QML wrappers ----

    // True when the system clipboard carries an image (§5.3 paste arm).
    Q_INVOKABLE bool clipboardHasImage() const;
    // Save the clipboard image as an asset, returning the stored path or "".
    Q_INVOKABLE QString ingestClipboardImage(const QString &noteSlug,
                                             const QString &root,
                                             const QString &noteDir) const;
    // Save raw image bytes (a browser/OS drop that delivered pixels rather
    // than a file — spike b's bytes arm) as an asset. "" if not an image.
    Q_INVOKABLE QString ingestImageBytes(const QByteArray &bytes,
                                         const QString &noteSlug,
                                         const QString &root,
                                         const QString &noteDir) const;
    // QML wrapper for ingestFile (returns the stored path).
    Q_INVOKABLE QString ingestLocalFile(const QString &sourcePath,
                                        const QString &noteSlug,
                                        const QString &root,
                                        const QString &noteDir) const;
};

#endif // ASSETSTORE_H
