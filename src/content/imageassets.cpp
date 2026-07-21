// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
#include "imageassets.h"

#include <QClipboard>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QImage>
#include <QMimeData>
#include <QRegularExpression>
#include <QSet>
#include <QUrl>

namespace {

const QSet<QString> &imageExtensions()
{
    static const QSet<QString> exts = {
        "png", "jpg", "jpeg", "gif", "webp", "svg", "bmp"};
    return exts;
}

const QSet<QString> &mediaExtensions()
{
    static const QSet<QString> exts = {
        "mp3", "wav", "ogg", "flac", "m4a",       // audio
        "mp4", "webm", "mkv", "mov"};             // video
    return exts;
}

// The file extension of a stored path or URL, lowercased, without query or
// fragment.
QString extensionOf(const QString &path)
{
    QString p = path;
    const int q = p.indexOf('?');
    if (q >= 0)
        p = p.left(q);
    const int h = p.indexOf('#');
    if (h >= 0)
        p = p.left(h);
    return QFileInfo(p).suffix().toLower();
}

bool isRemote(const QString &stored)
{
    return stored.startsWith(QLatin1String("http://"))
        || stored.startsWith(QLatin1String("https://"));
}

} // namespace

ImageAssets::ImageAssets(QObject *parent)
    : QObject(parent)
{
}

ImageAssets::Kind ImageAssets::kindForExtension(const QString &path)
{
    const QString ext = extensionOf(path);
    if (imageExtensions().contains(ext))
        return Kind::Image;
    if (mediaExtensions().contains(ext))
        return Kind::Media;
    return Kind::None;
}

namespace {

// The alt text and the caption sit inside delimiters that also occur in
// ordinary prose: "]" ends the alt, "|" introduces the width, and '"' ends
// the caption. Backslash-escape those on write and undo it on read, so a
// field carrying one keeps its text instead of changing the expression's
// shape. The escapes are symmetric and applied to exactly one field each,
// which is what makes the round trip total.
QString escapeField(const QString &text, const QString &specials)
{
    QString out;
    out.reserve(text.size());
    for (const QChar c : text) {
        if (c == u'\\' || specials.contains(c))
            out.append(u'\\');
        out.append(c);
    }
    return out;
}

QString unescapeField(const QString &text)
{
    QString out;
    out.reserve(text.size());
    for (int i = 0; i < text.size(); ++i) {
        if (text.at(i) == u'\\' && i + 1 < text.size())
            ++i;
        out.append(text.at(i));
    }
    return out;
}

// The last "|" that is not itself escaped, or -1.
int lastUnescapedBar(const QString &text)
{
    for (int i = text.size() - 1; i >= 0; --i) {
        if (text.at(i) != u'|')
            continue;
        int slashes = 0;
        for (int j = i - 1; j >= 0 && text.at(j) == u'\\'; --j)
            ++slashes;
        if ((slashes & 1) == 0)
            return i;
    }
    return -1;
}

} // namespace

ImageAssets::Parsed ImageAssets::parseLine(const QString &line)
{
    Parsed result;
    // ![altAndWidth](path optionally followed by "caption")
    static const QRegularExpression re(
        QStringLiteral("^!\\[((?:\\\\.|[^\\]\\\\])*)\\]\\((.*)\\)$"));
    const QRegularExpressionMatch m = re.match(line);
    if (!m.hasMatch())
        return result;

    const QString altPart = m.captured(1);
    QString inner = m.captured(2);

    // Split an optional ` "caption"` title off the end of the path part.
    QString path = inner;
    QString caption;
    static const QRegularExpression capRe(
        QStringLiteral("^(.*?)\\s+\"((?:\\\\.|[^\"\\\\])*)\"$"));
    const QRegularExpressionMatch cm = capRe.match(inner);
    if (cm.hasMatch()) {
        path = cm.captured(1);
        caption = unescapeField(cm.captured(2));
    }
    if (path.isEmpty())
        return result;   // no image without a path

    // Split an optional Obsidian |width (or |WxH) suffix off the alt text.
    QString alt = altPart;
    int width = 0;
    const int bar = lastUnescapedBar(altPart);
    if (bar >= 0) {
        const QString suffix = altPart.mid(bar + 1).trimmed();
        static const QRegularExpression wRe(QStringLiteral("^(\\d+)(?:x\\d+)?$"));
        const QRegularExpressionMatch wm = wRe.match(suffix);
        if (wm.hasMatch()) {
            width = wm.captured(1).toInt();
            alt = altPart.left(bar);
        }
    }
    alt = unescapeField(alt);

    result.valid = true;
    result.alt = alt;
    result.path = path;
    result.caption = caption;
    result.width = width;
    result.kind = kindForExtension(path);
    // A lone ![](url) whose URL is an http(s) web page or video host (no media
    // extension, so kindForExtension is None) is a WEB EMBED, stored as an
    // Image block; delegateKindForContent then renders it as a preview
    // card. Keeps kindForExtension pure so isEmbedUrl (which checks for
    // None) still distinguishes it from a remote image file.
    if (result.kind == Kind::None
        && (path.startsWith(QLatin1String("http://"), Qt::CaseInsensitive)
            || path.startsWith(QLatin1String("https://"), Qt::CaseInsensitive)))
        result.kind = Kind::Image;
    return result;
}

bool ImageAssets::isEmbedUrl(const QString &url)
{
    const QString u = url.trimmed();
    if (!(u.startsWith(QLatin1String("http://"), Qt::CaseInsensitive)
          || u.startsWith(QLatin1String("https://"), Qt::CaseInsensitive)))
        return false;
    // A remote image or media *file* stays an image/media block; a web page
    // or video host (no recognized media extension) is an embed.
    return kindForExtension(u) == Kind::None;
}

QString ImageAssets::buildMarkdown(const QString &path, const QString &alt,
                                   const QString &caption, int width)
{
    QString altPart = escapeField(alt, QStringLiteral("]|"));
    if (width > 0)
        altPart += QStringLiteral("|") + QString::number(width);
    QString out = QStringLiteral("![") + altPart + QStringLiteral("](") + path;
    if (!caption.isEmpty())
        out += QStringLiteral(" \"") + escapeField(caption, QStringLiteral("\""))
             + QStringLiteral("\"");
    out += QStringLiteral(")");
    return out;
}

ImageAssets::Parsed ImageAssets::classifyLine(const QString &line)
{
    const Parsed p = parseLine(line.trimmed());
    if (p.valid && p.kind != Kind::None)
        return p;
    return Parsed{};   // not a lone image/media expression
}

QString ImageAssets::resolveSource(const QString &stored, const QString &noteDir,
                                   const QString &collectionRoot)
{
    if (stored.isEmpty())
        return QString();
    if (isRemote(stored))
        return stored;
    if (stored.startsWith(QLatin1String("data:")))
        return stored;

    // Note-relative, then root-relative, then absolute/cwd-relative — the
    // first that exists wins. QDir::absoluteFilePath returns an
    // absolute `stored` unchanged, so an absolute path is tried once.
    QStringList candidates;
    if (!noteDir.isEmpty())
        candidates << QDir(noteDir).absoluteFilePath(stored);
    if (!collectionRoot.isEmpty())
        candidates << QDir(collectionRoot).absoluteFilePath(stored);
    candidates << QFileInfo(stored).absoluteFilePath();

    for (const QString &candidate : candidates) {
        const QFileInfo fi(candidate);
        if (fi.exists() && fi.isFile())
            return QUrl::fromLocalFile(fi.absoluteFilePath()).toString();
    }
    return QString();   // unresolved → broken-path placeholder
}

// ---- Asset ingestion ----

QString ImageAssets::assetsDir(const QString &root, const QString &noteDir)
{
    const QString base = !root.isEmpty() ? root : noteDir;
    if (base.isEmpty())
        return QString();
    return QDir(base).filePath(QStringLiteral("assets"));
}

QString ImageAssets::uniqueAssetName(const QString &dir, const QString &slug,
                                     const QString &stamp, const QString &ext)
{
    const QString cleanSlug = slug.isEmpty() ? QStringLiteral("image") : slug;
    const QString baseName = cleanSlug + QLatin1Char('-') + stamp;
    QDir d(dir);
    QString name = baseName + QLatin1Char('.') + ext;
    int n = 1;
    while (d.exists(name)) {
        name = baseName + QLatin1Char('-') + QString::number(n)
             + QLatin1Char('.') + ext;
        ++n;
    }
    return name;
}

// The stored path to write into the markdown: relative to the collection root
// when one is open (so a note move never breaks it), else relative to the
// note's own folder (single-file mode).
static QString storedPathFor(const QString &absFile, const QString &root,
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

QString ImageAssets::ingestImage(const QImage &image, const QString &noteSlug,
                                 const QString &root, const QString &noteDir) const
{
    if (image.isNull())
        return QString();
    const QString dir = assetsDir(root, noteDir);
    if (dir.isEmpty())
        return QString();
    if (!QDir().mkpath(dir))
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

QString ImageAssets::ingestFile(const QString &sourcePath, const QString &noteSlug,
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
    const QString dir = assetsDir(root, noteDir);
    if (dir.isEmpty())
        return QString();
    if (!QDir().mkpath(dir))
        return QString();
    const QString stamp =
        QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-HHmmss"));
    const QString ext = src.suffix().isEmpty() ? QStringLiteral("bin")
                                               : src.suffix().toLower();
    const QString name = uniqueAssetName(dir, noteSlug, stamp, ext);
    const QString abs = QDir(dir).filePath(name);
    if (!QFile::copy(absSrc, abs))
        return QString();
    return storedPathFor(abs, root, noteDir);
}

// ---- QML wrappers ----

bool ImageAssets::clipboardHasImage() const
{
    const QClipboard *cb = QGuiApplication::clipboard();
    return cb && cb->mimeData() && cb->mimeData()->hasImage();
}

QString ImageAssets::ingestClipboardImage(const QString &noteSlug,
                                          const QString &root,
                                          const QString &noteDir) const
{
    const QClipboard *cb = QGuiApplication::clipboard();
    if (!cb)
        return QString();
    const QImage image = cb->image();
    return ingestImage(image, noteSlug, root, noteDir);
}

QString ImageAssets::ingestImageBytes(const QByteArray &bytes,
                                      const QString &noteSlug,
                                      const QString &root,
                                      const QString &noteDir) const
{
    QImage image;
    if (!image.loadFromData(bytes))
        return QString();
    return ingestImage(image, noteSlug, root, noteDir);
}

QString ImageAssets::ingestLocalFile(const QString &sourcePath,
                                     const QString &noteSlug,
                                     const QString &root,
                                     const QString &noteDir) const
{
    QString path = sourcePath;
    if (path.startsWith(QStringLiteral("file://")))
        path = QUrl(path).toLocalFile();
    return ingestFile(path, noteSlug, root, noteDir);
}

QVariantMap ImageAssets::parse(const QString &content) const
{
    const Parsed p = parseLine(content.trimmed());
    QVariantMap m;
    m.insert(QStringLiteral("valid"), p.valid);
    m.insert(QStringLiteral("alt"), p.alt);
    m.insert(QStringLiteral("path"), p.path);
    m.insert(QStringLiteral("caption"), p.caption);
    m.insert(QStringLiteral("width"), p.width);
    m.insert(QStringLiteral("kind"),
             p.kind == Kind::Image ? QStringLiteral("image")
             : p.kind == Kind::Media ? QStringLiteral("media")
             : QStringLiteral("none"));
    return m;
}

QString ImageAssets::build(const QString &path, const QString &alt,
                           const QString &caption, int width) const
{
    return buildMarkdown(path, alt, caption, width);
}

QString ImageAssets::resolve(const QString &stored, const QString &noteDir,
                             const QString &collectionRoot) const
{
    return resolveSource(stored, noteDir, collectionRoot);
}

QString ImageAssets::kindOf(const QString &path) const
{
    switch (kindForExtension(path)) {
    case Kind::Image: return QStringLiteral("image");
    case Kind::Media: return QStringLiteral("media");
    case Kind::None:  return QStringLiteral("none");
    }
    return QStringLiteral("none");
}
