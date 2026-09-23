// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
#ifndef IMAGEASSETS_H
#define IMAGEASSETS_H

#include <QDateTime>
#include <QHash>
#include <QObject>
#include <QSize>
#include <QString>
#include <QStringList>
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

    // What the reader typed into the embed URL field, turned into a URL the
    // rest of the pipeline recognizes: a bare host ("cnn.com",
    // "localhost:8080/wiki") or a protocol-relative "//host/x" gains an
    // https:// scheme, and an http(s) URL is returned as typed. Returns an
    // empty string for text that cannot be a web address — blank, containing
    // whitespace, or carrying a scheme this card cannot fetch (mailto:,
    // file:). Without this a scheme-less host is not isRemote, so it lands as
    // an Image block and shows the broken-image placeholder instead of a
    // card. A host with no dot ("wiki", "jira") is accepted: intranet names
    // resolve, and refusing them here would be guessing.
    Q_INVOKABLE static QString normalizeEmbedUrl(const QString &input);

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
    //
    // A path starting with "/" is the form a website uses for its own root,
    // so one that names no file on this machine is also looked up under
    // siteRoot: `/images/a.png` in a Hugo vault is `<root>/static/images/
    // a.png`. siteRoot defaults to collectionRoot, the vault's own root.
    static QString resolveSource(const QString &stored, const QString &noteDir,
                                 const QString &collectionRoot,
                                 const QString &siteRoot = QString());

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
                                const QString &collectionRoot,
                                const QString &siteRoot = QString()) const;
    Q_INVOKABLE QString kindOf(const QString &path) const;

    // The size a picture has in its file, without decoding the picture.
    //
    // `resolvedSource` is what resolve() answered: a file:// URL, an http(s)
    // URL, or "". Only a local file can be measured without fetching it, so
    // everything else answers an invalid size (width -1) and the caller falls
    // back to the size the loaded image reports.
    //
    // An image block whose markdown carries no width — ![alt](pic.png) rather
    // than ![alt|600](pic.png) — draws the picture at its own width, and asks
    // the decoder for exactly that many pixels so a 12-megapixel photo shown
    // 600 px wide is not decoded at its full camera resolution. Reading the
    // displayed width back out of the loaded image would make those two
    // values depend on each other, which QML reports as a binding loop and
    // breaks by refusing to re-evaluate — after decoding the file once per
    // pass around the circle, and at a final width that depends on the
    // screen's scale factor. Measuring the file instead settles both.
    //
    // QImageReader::size() reads the file's header and stops: one open and a
    // few hundred bytes whatever the picture's resolution. An SVG answers
    // with the size its own width and height attributes give it, which is the
    // one case where the requested decode size would otherwise decide the
    // answer, since a scalable image renders at whatever size it is asked
    // for. Sizes are remembered per path and measured again when the file's
    // timestamp or length changes.
    Q_INVOKABLE QSize naturalSize(const QString &resolvedSource);

    // The name filters a file picker should offer for one kind ("image" or
    // "media"), read from the same extension sets kindForExtension uses. A
    // picker that filtered by its own list could hide a file the app would
    // have accepted, or offer one it would then classify as the other kind.
    // The trailing "All files" entry is what makes a path the lists do not
    // know still reachable.
    Q_INVOKABLE QStringList nameFilters(const QString &kind) const;

private:
    // One file already measured, with what it looked like when it was: a
    // picture replaced on disk keeps its path, so the path alone cannot say
    // whether the remembered size still describes it.
    struct MeasuredSize {
        QSize size;          // invalid when the header could not be read
        QDateTime modified;
        qint64 bytes = 0;
    };
    QHash<QString, MeasuredSize> m_measured;
};

#endif // IMAGEASSETS_H
