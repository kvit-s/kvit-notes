// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
#include "kindgroups.h"

#include "blockkinddef.h"
#include "blockstyle.h"
#include "blocktext.h"
#include "htmlinline.h"
#include "imageassets.h"
#include "rendercontext.h"

namespace {

// ---- what the three kinds share ----
//
// All three store one markdown image expression — ![alt|width](path
// "caption") — in `content`, and nothing else. The kind is decided by the
// path: an image extension is an Image, an audio or video extension a Media,
// and an http(s) URL naming neither is an Embed.

// A media file as a link line. There is no inline player in an export: an
// HTML file opened from anywhere but the vault cannot reach a note-relative
// path, and the PDF seam draws no video at all, so both targets get the
// arrow, the link and the name.
//
// Shared because the embed kind renders it too — see EmbedKindDef::toHtml for
// why a Media block can end up here through the embed path.
QString mediaLinkHtml(const ImageAssets::Parsed &parsed)
{
    return "<p>&#9654; <a href=\"" + HtmlInline::esc(parsed.path) + "\">"
         + HtmlInline::esc(parsed.alt.isEmpty() ? parsed.path : parsed.alt)
         + "</a></p>";
}

// An image, a video or an audio file as plain text: what it is, what it is
// called and where it lives. There is no picture to write, and the markdown
// expression was only ever readable by accident.
//
// One function for both kinds, reading the stored type for the label, because
// the two differ in that one word and nothing else.
QString mediaPlainText(const Block::State &state)
{
    const ImageAssets::Parsed parsed = ImageAssets::parseLine(state.content);
    // An expression that does not parse is not silently dropped: whatever the
    // reader typed is still their content, so it goes out as prose.
    if (!parsed.valid)
        return BlockText::renderedFully(state.content);
    const QString label = state.type == Block::Media
        ? QStringLiteral("media") : QStringLiteral("image");
    QString out = QLatin1Char('[') + label;
    if (!parsed.alt.isEmpty())
        out += QStringLiteral(": ") + parsed.alt;
    out += QStringLiteral("] ") + parsed.path;
    if (!parsed.caption.isEmpty())
        out += QLatin1Char('\n')
             + BlockText::indent(BlockText::renderedFully(parsed.caption), 2);
    return out;
}

// ---- image ----
class ImageKindDef : public BlockKindDef
{
public:
    BlockKind kind() const override { return BlockKind::Image; }
    QString id() const override { return QStringLiteral("image"); }
    QString fenceLanguage() const override { return QString(); }

    // The stored expression, unchanged. The serializer has no case for an
    // image at all — it falls through to `case Block::Paragraph: default:
    // return content;` — and that identity is what makes the round trip
    // lossless, since the delegate edits the expression in place through the
    // model. Writing anything else here, an empty string most of all, would
    // erase every image in every note on the next save.
    QString serialize(const Block::State &state, int) const override
    {
        return state.content;
    }
    bool attributeTagRidesOpeningLine() const override { return false; }

    // One line, so the tag trails it and the three projections are the plain
    // rendered text — the same answer Block::ensureTextCache gives an image
    // today. It is not verbatim, so the search database's per-block verbatim
    // flag and the sidecar index's word count stay what they were.
    QString displayText(const Block::State &state) const override
    {
        // rendered, not renderedFully: this is what the word counts written
        // into the vault's sidecar index and the search database's stored
        // text are computed from, and both were computed with the marker fast
        // path. The export projections below deliberately use the other one.
        return BlockText::rendered(state.content);
    }
    QString statisticsText(const Block::State &state) const override
    {
        return displayText(state);
    }
    QString searchText(const Block::State &state) const override
    {
        return displayText(state);
    }

    bool isVerbatim() const override { return false; }
    bool holdsContent() const override { return true; }
    bool foldsLineBreaks() const override { return false; }
    QString unfoldableTail(const Block::State &) const override
    {
        return QString();
    }
    int headingLevel() const override { return 0; }
    FontRole fontRole() const override { return FontRole::Body; }
    // Block type 11, one of the six the toolbar's alignment buttons apply to.
    bool isAlignable() const override { return true; }

    QString toHtml(const Block::State &state,
                   const RenderContext &ctx) const override
    {
        const ImageAssets::Parsed parsed =
            ImageAssets::parseLine(state.content);
        const BlockStyle::Attributes attrs =
            BlockStyle::parse(state.attributes);
        // Empty when the path does not resolve or the file is over the
        // attachment budget, which the placeholder below treats as one case.
        const QString uri = ctx.services
            ? ctx.services->imageDataUri(parsed.path) : QString();

        // The stored image effects, matching ImageBlock: a corner radius
        // (12px unless the attribute names one), a drop shadow, and a border
        // in the given colour.
        QStringList imgDeclarations;
        if (BlockStyle::has(attrs, QLatin1String("rounded"))) {
            const int radius =
                BlockStyle::num(attrs, QLatin1String("rounded"), 0);
            imgDeclarations << QStringLiteral("border-radius:%1px")
                                   .arg(radius > 0 ? radius : 12);
        }
        if (BlockStyle::has(attrs, QLatin1String("shadow")))
            imgDeclarations
                << QStringLiteral("box-shadow:0 2px 10px rgba(0,0,0,0.25)");
        // A bare `border` flag means the theme's own border colour, which a
        // block kind cannot reach: the theme lives two modules up, and
        // keeping it out of here is the layering this design exists to hold.
        // The document's stylesheet names that colour once, in a rule for
        // this class, so the kind asks for the class and the renderer that
        // has the theme supplies the value.
        QString imgClass;
        if (BlockStyle::has(attrs, QLatin1String("border"))) {
            const QString custom = BlockStyle::cssColor(
                BlockStyle::str(attrs, QLatin1String("border")));
            if (custom.isEmpty())
                imgClass = QStringLiteral(" class=\"bordered\"");
            else
                imgDeclarations << QStringLiteral("border:1px solid ") + custom;
        }

        const QString img = uri.isEmpty()
            ? ("<em>[image: " + HtmlInline::esc(parsed.path) + "]</em>")
            : ("<img alt=\"" + HtmlInline::esc(parsed.alt) + "\" src=\""
               + uri + "\""
               + (parsed.width > 0
                      ? " width=\"" + QString::number(parsed.width) + "\""
                      : QString())
               + imgClass
               + BlockStyle::styleAttr(imgDeclarations) + ">");

        // An image is centred on screen unless the block says otherwise, so
        // only an explicit alignment writes a rule. One call covers an
        // explicit `align=left` as well: the exporter needed a second branch
        // for it because its own helper refused "left" outright, and
        // BlockStyle::textAlign accepts it against the kind's default, which
        // produces the same declaration.
        QStringList figureDeclarations;
        const QString align =
            BlockStyle::textAlign(attrs, QStringLiteral("center"));
        if (!align.isEmpty())
            figureDeclarations << align;

        return "<figure" + BlockStyle::styleAttr(figureDeclarations) + ">"
             + img
             + (parsed.caption.isEmpty()
                    ? QString()
                    : "<figcaption>"
                          + HtmlInline::escFlowing(parsed.caption)
                          + "</figcaption>")
             + "</figure>";
    }

    QString toPlainText(const Block::State &state,
                        const RenderContext &) const override
    {
        return mediaPlainText(state);
    }

    QList<MenuEntry> menuEntries() const override
    {
        // Media group (§4.3). Image inserts rather than converts: an empty
        // Image block has no path, so BlockMenu hands off to the file dialog
        // or the URL prompt instead of seeding starter content here.
        MenuEntry entry;
        entry.type = Block::Image;
        entry.name = QStringLiteral("Image");
        entry.description =
            QStringLiteral("Embed an image from a file or URL");
        entry.group = QStringLiteral("Media");
        entry.icon = QStringLiteral("▨");
        entry.aliases = { QStringLiteral("image"), QStringLiteral("img"),
                          QStringLiteral("picture"), QStringLiteral("photo"),
                          QStringLiteral("![") };
        return { entry };
    }

    int delegateKind() const override
    {
        return static_cast<int>(kind());
    }
    QString delegateUrl() const override
    {
        return QStringLiteral("qrc:/qml/ImageBlock.qml");
    }
};

// ---- local audio and video ----
//
// The same stored expression as an image, told apart by the file extension.
class MediaKindDef : public BlockKindDef
{
public:
    BlockKind kind() const override { return BlockKind::Media; }
    QString id() const override { return QStringLiteral("media"); }
    QString fenceLanguage() const override { return QString(); }

    // As for an image: the serializer has no case for a media block either,
    // so it falls through to the paragraph default and writes the expression
    // back unchanged.
    QString serialize(const Block::State &state, int) const override
    {
        return state.content;
    }
    bool attributeTagRidesOpeningLine() const override { return false; }

    QString displayText(const Block::State &state) const override
    {
        // rendered, not renderedFully: this is what the word counts written
        // into the vault's sidecar index and the search database's stored
        // text are computed from, and both were computed with the marker fast
        // path. The export projections below deliberately use the other one.
        return BlockText::rendered(state.content);
    }
    QString statisticsText(const Block::State &state) const override
    {
        return displayText(state);
    }
    QString searchText(const Block::State &state) const override
    {
        return displayText(state);
    }

    bool isVerbatim() const override { return false; }
    bool holdsContent() const override { return true; }
    bool foldsLineBreaks() const override { return false; }
    QString unfoldableTail(const Block::State &) const override
    {
        return QString();
    }
    int headingLevel() const override { return 0; }
    FontRole fontRole() const override { return FontRole::Body; }
    // Not in the toolbar's alignable list: a media block draws a player, and
    // the alignment attribute would have nothing to act on.
    bool isAlignable() const override { return false; }

    QString toHtml(const Block::State &state,
                   const RenderContext &) const override
    {
        return mediaLinkHtml(ImageAssets::parseLine(state.content));
    }

    QString toPlainText(const Block::State &state,
                        const RenderContext &) const override
    {
        return mediaPlainText(state);
    }

    QList<MenuEntry> menuEntries() const override
    {
        // Local audio/video: inserts like an image (file dialog); the path's
        // extension is what lands it as a Media block rather than an Image.
        MenuEntry entry;
        entry.type = Block::Media;
        entry.name = QStringLiteral("Audio / Video");
        entry.description = QStringLiteral("Play a local audio or video file");
        entry.group = QStringLiteral("Media");
        entry.icon = QStringLiteral("▷");
        entry.aliases = { QStringLiteral("audio"), QStringLiteral("video"),
                          QStringLiteral("media"), QStringLiteral("sound"),
                          QStringLiteral("movie"), QStringLiteral("mp4"),
                          QStringLiteral("mp3") };
        return { entry };
    }

    int delegateKind() const override
    {
        return static_cast<int>(kind());
    }
    QString delegateUrl() const override
    {
        return QStringLiteral("qrc:/qml/MediaBlock.qml");
    }
};

// ---- web embed ----
//
// The one kind decided by a block's CONTENT rather than by its stored type or
// a fence language: an Image or Media block whose URL names a web page or a
// video host rather than a file. Nothing new is stored for it — the note
// still holds an ![](url) expression — so an embed opened by an editor that
// does not know the kind is still an image expression and still round-trips.
class EmbedKindDef : public BlockKindDef
{
public:
    BlockKind kind() const override { return BlockKind::Embed; }
    QString id() const override { return QStringLiteral("embed"); }
    QString fenceLanguage() const override { return QString(); }

    // The stored expression, unchanged, and for the same reason as the other
    // two: the serializer has no case for the Image or Media type this block
    // is stored as, so it takes the paragraph default.
    QString serialize(const Block::State &state, int) const override
    {
        return state.content;
    }
    bool attributeTagRidesOpeningLine() const override { return false; }

    QString displayText(const Block::State &state) const override
    {
        // rendered, not renderedFully: this is what the word counts written
        // into the vault's sidecar index and the search database's stored
        // text are computed from, and both were computed with the marker fast
        // path. The export projections below deliberately use the other one.
        return BlockText::rendered(state.content);
    }
    QString statisticsText(const Block::State &state) const override
    {
        return displayText(state);
    }
    QString searchText(const Block::State &state) const override
    {
        return displayText(state);
    }

    bool isVerbatim() const override { return false; }
    bool holdsContent() const override { return true; }
    bool foldsLineBreaks() const override { return false; }
    QString unfoldableTail(const Block::State &) const override
    {
        return QString();
    }
    int headingLevel() const override { return 0; }
    FontRole fontRole() const override { return FontRole::Body; }
    bool isAlignable() const override { return false; }

    QString toHtml(const Block::State &state,
                   const RenderContext &ctx) const override
    {
        const ImageAssets::Parsed parsed =
            ImageAssets::parseLine(state.content);
        // A preserved asymmetry, carried across deliberately. The editor
        // gives the embed card to an Image OR a Media block whose URL names a
        // page (BlockModel::delegateKindForContent), but the exporter's embed
        // branch tested the Image type alone, so a Media block naming a page
        // has always EXPORTED as the media link while showing a card on
        // screen. Nothing covers that case either way, so this change is not
        // the place to decide it; the export keeps the behaviour it had.
        if (state.type == Block::Media)
            return mediaLinkHtml(parsed);
        // An <img> pointed at a page is a broken image in every viewer, so
        // the card exports as a link. The card itself reads the preview cache
        // and never fetches, which is why it lives above `domain`.
        return ctx.services
            ? ctx.services->embedCardHtml(parsed.path, parsed.alt,
                                          parsed.caption, state.attributes)
            : QString();
    }

    QString toPlainText(const Block::State &state,
                        const RenderContext &ctx) const override
    {
        // The same asymmetry as toHtml, for the same reason: the plain-text
        // builder tested the Image type alone too, so a Media block naming a
        // page writes the media line.
        if (state.type == Block::Media)
            return mediaPlainText(state);
        const ImageAssets::Parsed parsed =
            ImageAssets::parseLine(state.content);
        return ctx.services
            ? ctx.services->embedCardPlainText(parsed.path, parsed.alt,
                                               parsed.caption)
            : QString();
    }

    QList<MenuEntry> menuEntries() const override
    {
        // A preview card for a web page or video URL, stored as an ![](url)
        // image expression rather than as a type of its own.
        //
        // The type stays Block::Image and the marker stays "embed": BlockMenu
        // keys the URL-prompt insert flow on exactly that PAIR, so changing
        // either one sends the row down the file-dialog path and the reader
        // gets a file picker where the address field should be. The pair is
        // also the row's persisted recency id, so a change silently drops it
        // out of everyone's recently-used list.
        MenuEntry entry;
        entry.type = Block::Image;
        entry.name = QStringLiteral("Web Embed");
        entry.description =
            QStringLiteral("Preview card for a web page or video URL");
        entry.group = QStringLiteral("Media");
        entry.icon = QStringLiteral("◧");
        entry.aliases = { QStringLiteral("embed"), QStringLiteral("bookmark"),
                          QStringLiteral("link"), QStringLiteral("url"),
                          QStringLiteral("youtube"), QStringLiteral("web") };
        entry.defaultLanguage = QStringLiteral("embed");
        return { entry };
    }

    int delegateKind() const override
    {
        return static_cast<int>(kind());
    }
    QString delegateUrl() const override
    {
        return QStringLiteral("qrc:/qml/EmbedBlock.qml");
    }
};

} // namespace

namespace BlockKindGroups {

const QList<const BlockKindDef *> &media()
{
    static const ImageKindDef image;
    static const MediaKindDef mediaFile;
    static const EmbedKindDef embed;

    static const QList<const BlockKindDef *> kinds = {
        &image, &mediaFile, &embed,
    };
    return kinds;
}

} // namespace BlockKindGroups
