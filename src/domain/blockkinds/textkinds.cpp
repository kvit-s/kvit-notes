// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
#include "kindgroups.h"

#include "blockkinddef.h"
#include "blockmarkdown.h"
#include "blockstyle.h"
#include "blocktext.h"
#include "htmlinline.h"
#include "rendercontext.h"

namespace {

// ---- paragraph ----
//
// The default kind, and the one a block falls back to whenever a stored type
// is not recognised — which is why its text projections have to be the
// forgiving ones: a paragraph is what a corrupted block's content survives as.
class ParagraphKindDef : public BlockKindDef
{
public:
    BlockKind kind() const override { return BlockKind::Paragraph; }
    QString id() const override { return QStringLiteral("paragraph"); }
    QString fenceLanguage() const override { return QString(); }

    QString serialize(const Block::State &state, int) const override
    {
        return state.content;
    }
    bool attributeTagRidesOpeningLine() const override { return false; }

    QString displayText(const Block::State &state) const override
    {
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
    bool foldsLineBreaks() const override { return true; }
    QString unfoldableTail(const Block::State &) const override
    {
        return QString();
    }
    int headingLevel() const override { return 0; }
    FontRole fontRole() const override { return FontRole::Body; }
    bool isAlignable() const override { return true; }

    QString toHtml(const Block::State &state,
                   const RenderContext &ctx) const override
    {
        // An empty paragraph writes nothing at all. A note's blank lines are
        // its spacing, and <p></p> in an export is a stray gap in a document
        // whose spacing the stylesheet already sets.
        if (state.content.isEmpty())
            return QString();
        const BlockStyle::Attributes attrs = BlockStyle::parse(state.attributes);
        QStringList declarations;
        const QString align =
            BlockStyle::textAlign(attrs, QStringLiteral("left"));
        if (!align.isEmpty())
            declarations << align;
        const QString inner = HtmlInline::renderInline(
            state.content, ctx.mathJax, &ctx.sawMath);
        return "<p" + BlockStyle::styleAttr(declarations) + ">"
             + BlockStyle::withDropCap(inner, attrs) + "</p>";
    }

    QString toPlainText(const Block::State &state,
                        const RenderContext &) const override
    {
        // renderedFully rather than the display projection: a plain-text
        // export has always resolved every inline marker unconditionally,
        // where the block's own display text takes a fast path that leaves a
        // lone backslash escape alone. See BlockText::renderedFully.
        return BlockText::renderedFully(state.content);
    }

    QList<MenuEntry> menuEntries() const override
    {
        MenuEntry text;
        text.type = Block::Paragraph;
        text.name = QStringLiteral("Text");
        text.description = QStringLiteral("Plain paragraph");
        text.group = QStringLiteral("Basic");
        text.icon = QStringLiteral("¶");
        text.aliases = { QStringLiteral("paragraph"), QStringLiteral("plain"),
                         QStringLiteral("p") };

        // A drop cap is a paragraph with an attribute rather than a kind of
        // its own, so it is this kind's second menu row. The marker routes
        // the insert flow to an attribute write instead of a conversion.
        MenuEntry dropCap;
        dropCap.type = Block::Paragraph;
        dropCap.name = QStringLiteral("Drop Cap");
        dropCap.description =
            QStringLiteral("Enlarge this paragraph's first letter");
        dropCap.group = QStringLiteral("Advanced");
        dropCap.icon = QStringLiteral("A");
        dropCap.aliases = { QStringLiteral("dropcap"), QStringLiteral("drop cap"),
                            QStringLiteral("initial"),
                            QStringLiteral("illuminated"),
                            QStringLiteral("capital") };
        dropCap.defaultLanguage = QStringLiteral("dropcap");
        // Last in its group. The paragraph leads the menu, and its second row
        // would otherwise lead the Advanced group and take the "/d" that
        // inserts a divider.
        dropCap.order = 1;
        return { text, dropCap };
    }

    // Kind 0: the value paragraph and all four headings publish, so that
    // typing "# " at the start of a line does not destroy the delegate the
    // caret is sitting in.
    int delegateKind() const override { return 0; }
    QString delegateUrl() const override
    {
        return QStringLiteral("qrc:/qml/TextBlockDelegate.qml");
    }
};

// ---- headings ----
//
// One class, four instances. The level is the only thing that differs, and
// four classes differing in one integer would be four places to edit rather
// than one.
class HeadingKindDef : public BlockKindDef
{
public:
    HeadingKindDef(BlockKind kind, int level, Block::BlockType type,
                   FontRole role, const char *description, const char *icon,
                   QStringList aliases)
        : m_kind(kind)
        , m_level(level)
        , m_type(type)
        , m_role(role)
        , m_description(QString::fromUtf8(description))
        , m_icon(QString::fromUtf8(icon))
        , m_aliases(std::move(aliases))
    {
    }

    BlockKind kind() const override { return m_kind; }
    QString id() const override
    {
        return QStringLiteral("heading") + QString::number(m_level);
    }
    QString fenceLanguage() const override { return QString(); }

    QString serialize(const Block::State &state, int) const override
    {
        return QString(m_level, QLatin1Char('#')) + QLatin1Char(' ')
             + state.content;
    }
    bool attributeTagRidesOpeningLine() const override { return false; }

    QString displayText(const Block::State &state) const override
    {
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
    bool foldsLineBreaks() const override { return true; }
    QString unfoldableTail(const Block::State &) const override
    {
        return QString();
    }
    int headingLevel() const override { return m_level; }
    FontRole fontRole() const override { return m_role; }
    bool isAlignable() const override { return true; }

    QString toHtml(const Block::State &state,
                   const RenderContext &ctx) const override
    {
        const BlockStyle::Attributes attrs = BlockStyle::parse(state.attributes);
        QStringList declarations;
        const QString align =
            BlockStyle::textAlign(attrs, QStringLiteral("left"));
        if (!align.isEmpty())
            declarations << align;
        const QString tag = QStringLiteral("h") + QString::number(m_level);
        // The anchor comes from the document renderer, never from here:
        // slugs are collision-suffixed across the whole note, so a heading
        // that computed its own would break internal links and the table of
        // contents the moment two headings shared a title.
        return "<" + tag + " id=\"" + HtmlInline::esc(ctx.headingSlug) + "\""
             + BlockStyle::styleAttr(declarations) + ">"
             + HtmlInline::renderInline(state.content, ctx.mathJax, &ctx.sawMath)
             + "</" + tag + ">";
    }

    QString toPlainText(const Block::State &state,
                        const RenderContext &) const override
    {
        return QString(m_level, QLatin1Char('#')) + QLatin1Char(' ')
             + BlockText::renderedFully(state.content);
    }

    QList<MenuEntry> menuEntries() const override
    {
        MenuEntry entry;
        entry.type = m_type;
        entry.name = QStringLiteral("Heading ") + QString::number(m_level);
        entry.description = m_description;
        entry.group = QStringLiteral("Basic");
        entry.icon = m_icon;
        entry.aliases = m_aliases;
        return { entry };
    }

    // Kind 0, shared with the paragraph: see ParagraphKindDef.
    int delegateKind() const override { return 0; }
    // Empty because the paragraph's delegate is the one that draws these too;
    // they publish its kind, so the chooser never looks one up for them.
    QString delegateUrl() const override { return QString(); }

private:
    const BlockKind m_kind;
    const int m_level;
    const Block::BlockType m_type;
    const FontRole m_role;
    const QString m_description;
    const QString m_icon;
    const QStringList m_aliases;
};

} // namespace

namespace BlockKindGroups {

const QList<const BlockKindDef *> &text()
{
    static const ParagraphKindDef paragraph;
    static const HeadingKindDef heading1(
        BlockKind::Heading1, 1, Block::Heading1, FontRole::Heading1,
        "Largest heading, for titles", "H1",
        { QStringLiteral("h1"), QStringLiteral("heading1"),
          QStringLiteral("title"), QStringLiteral("#") });
    static const HeadingKindDef heading2(
        BlockKind::Heading2, 2, Block::Heading2, FontRole::Heading2,
        "Section heading", "H2",
        { QStringLiteral("h2"), QStringLiteral("heading2"),
          QStringLiteral("section"), QStringLiteral("##") });
    static const HeadingKindDef heading3(
        BlockKind::Heading3, 3, Block::Heading3, FontRole::Heading3,
        "Subsection heading", "H3",
        { QStringLiteral("h3"), QStringLiteral("heading3"),
          QStringLiteral("subsection"), QStringLiteral("###") });
    static const HeadingKindDef heading4(
        BlockKind::Heading4, 4, Block::Heading4, FontRole::Heading4,
        "Minor heading", "H4",
        { QStringLiteral("h4"), QStringLiteral("heading4"),
          QStringLiteral("####") });

    static const QList<const BlockKindDef *> kinds = {
        &paragraph, &heading1, &heading2, &heading3, &heading4,
    };
    return kinds;
}

} // namespace BlockKindGroups
