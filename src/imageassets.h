// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
#ifndef IMAGEASSETS_H
#define IMAGEASSETS_H

#include <QObject>
#include <QString>
#include <QVariantMap>

class QImage;

// Image and local-media block support (phase10-plan.md decisions 4, 5, 13).
//
// Storage (a deliberate refinement of decision 4's "block fields"): an image
// or media block keeps its markdown expression verbatim in the block's
// content — ![alt|width](path "caption") — exactly as the table and kanban
// container types keep their markdown (decisions 8, 9). The delegate parses it
// with the pure functions here and rewrites it (resize → width, caption edit)
// through the model as one undo step; serialization is then the identity, and
// round-trip fidelity is inherited. Every feature of decision 4 (alt, width,
// caption, resize, lightbox, placeholder, path resolution) is delivered; only
// the internal storage location differs, and uniformly across the wave-2
// container types.
//
// A paragraph becomes an Image/Media block only when its ENTIRE content is one
// image expression (decision 4): `![alt](x.png)` mid-prose stays literal this
// wave. The category (image vs local media) is decided by the file extension.
//
// The class is both the QML context object (imageAssets) — Q_INVOKABLE
// wrappers returning QVariant-friendly types — and the home of the pure static
// functions the serializer and the unit corpus call directly.
class ImageAssets : public QObject
{
    Q_OBJECT

public:
    explicit ImageAssets(QObject *parent = nullptr);

    enum class Kind { None, Image, Media };

    struct Parsed {
        bool valid = false;
        QString alt;
        QString path;
        QString caption;
        int width = 0;   // 0 = natural width
        Kind kind = Kind::None;
    };

    // ---- Pure functions (unit-tested without a filesystem) ----

    // Parse one image expression anywhere in `line` at position 0. Returns
    // invalid if `line` is not exactly a single image expression (leading or
    // trailing text fails — decision 4's whole-line rule).
    static Parsed parseLine(const QString &line);

    // Build the canonical markdown from parts (width 0 omits the |width
    // suffix; an empty caption omits the title).
    static QString buildMarkdown(const QString &path, const QString &alt,
                                 const QString &caption, int width);

    // The block category for a stored path or URL, by extension.
    static Kind kindForExtension(const QString &path);

    // Is `line` (as the serializer sees it) exactly one image/media
    // expression? The classifier that turns a lone-image paragraph into an
    // Image/Media block. Returns the parse (valid + kind) or invalid.
    static Parsed classifyLine(const QString &line);

    // Resolve a stored path to a source usable by a QML Image/MediaPlayer:
    // an http(s) URL verbatim; otherwise the first of note-relative,
    // root-relative, or absolute that exists, as a file:// URL; or "" when
    // nothing resolves (the broken-path placeholder). noteDir is the folder
    // of the note being edited; collectionRoot is the vault root (either may
    // be empty in single-file mode).
    static QString resolveSource(const QString &stored, const QString &noteDir,
                                 const QString &collectionRoot);

    // ---- Asset ingestion (decision 5) ----
    // The assets directory for a note: <root>/assets when a collection is
    // open, else <noteDir>/assets (beside the file in single-file mode).
    static QString assetsDir(const QString &root, const QString &noteDir);

    // A unique file name in `dir`: <slug>-<stamp>.<ext>, with a numeric
    // collision suffix if that name is taken. `stamp` is passed in so callers
    // (and tests) control it; the QML wrappers use the current time.
    static QString uniqueAssetName(const QString &dir, const QString &slug,
                                   const QString &stamp, const QString &ext);

    // Save `image` under the assets directory, returning the stored path to
    // put in the markdown — relative to the collection root (or to the note
    // in single-file mode), so moving a note never breaks it. "" on failure.
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
    // QML wrappers for the ingest methods above (return the stored path).
    Q_INVOKABLE QString ingestLocalFile(const QString &sourcePath,
                                        const QString &noteSlug,
                                        const QString &root,
                                        const QString &noteDir) const;


    // {valid, alt, path, caption, width, kind:"image"|"media"|"none"}.
    Q_INVOKABLE QVariantMap parse(const QString &content) const;
    Q_INVOKABLE QString build(const QString &path, const QString &alt,
                              const QString &caption, int width) const;
    Q_INVOKABLE QString resolve(const QString &stored, const QString &noteDir,
                                const QString &collectionRoot) const;
    Q_INVOKABLE QString kindOf(const QString &path) const;
};

#endif // IMAGEASSETS_H
