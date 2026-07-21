// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
#ifndef IMAGEASSETS_H
#define IMAGEASSETS_H

#include <QObject>
#include <QString>
#include <QVariantMap>


// Image and local-media block support.
//
// Storage: an image or media block keeps its markdown expression verbatim in
// the block's content — ![alt|width](path "caption") — exactly as the table
// and kanban container types keep their markdown. The delegate parses it
// with the pure functions here and rewrites it (resize → width, caption edit)
// through the model as one undo step; serialization is then the identity, and
// round-trip fidelity is inherited. Alt text, width, caption, resize,
// lightbox, placeholder and path resolution all work off that one stored
// expression, uniformly across the wave-2 container types.
//
// A paragraph becomes an Image/Media block only when its ENTIRE content is one
// image expression: `![alt](x.png)` mid-prose stays literal. The category
// (image vs local media) is decided by the file extension.
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
    // trailing text fails — an image block is a whole line).
    static Parsed parseLine(const QString &line);

    // True for an http(s) URL that names no recognized image or media
    // *file* — a web page or a video host, which renders as an embed
    // preview card rather than as an image. The block model asks this to
    // decide a delegate, so it lives beside kindForExtension rather than on
    // the embed service, which knows about the network and a cache.
    Q_INVOKABLE static bool isEmbedUrl(const QString &url);

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

    // Ingestion — saving a pasted, dropped or chosen file into the vault's
    // assets directory — is AssetStore (repository/assetstore.h). It writes,
    // and writing into the vault belongs to the repository; everything here
    // is a pure transform of an expression's text.

    // ---- QML wrappers ----

    // {valid, alt, path, caption, width, kind:"image"|"media"|"none"}.
    Q_INVOKABLE QVariantMap parse(const QString &content) const;
    Q_INVOKABLE QString build(const QString &path, const QString &alt,
                              const QString &caption, int width) const;
    Q_INVOKABLE QString resolve(const QString &stored, const QString &noteDir,
                                const QString &collectionRoot) const;
    Q_INVOKABLE QString kindOf(const QString &path) const;
};

#endif // IMAGEASSETS_H
