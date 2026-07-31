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

// ---- code fence ----
//
// The one kind whose stored content is literally what the reader sees, which
// is why it is the only verbatim kind in this file and the only one drawn at
// the monospace size.
//
// A `kanban`, `toc`, `mermaid` or `query` fence is a CodeBlock by stored type
// and reaches its own def instead; this class handles every other info
// string, which is a syntax-highlighting language.
class CodeBlockKindDef : public BlockKindDef
{
public:
    BlockKind kind() const override { return BlockKind::CodeBlock; }
    QString id() const override { return QStringLiteral("codeblock"); }
    // Empty even though this kind IS a fence. The registry seeds its
    // language-to-kind table from this accessor, and a plain code fence's
    // info string is the highlighting language — `python`, `cpp` — which
    // names no kind. Returning anything here would claim one language for
    // the plain fence and route the rest nowhere.
    QString fenceLanguage() const override { return QString(); }

    QString serialize(const Block::State &state, int) const override
    {
        return BlockMarkdown::fencedBlock(state.content, state.language);
    }
    // The tag rides the opening fence. Appended after the closer it would
    // stop the closer from closing, and the rest of the note would be read
    // back as this block's content on the next load.
    bool attributeTagRidesOpeningLine() const override { return true; }

    QString displayText(const Block::State &state) const override
    {
        // Verbatim: the code IS the text. Running it through the inline
        // markdown pass would eat the `*`, `_` and backticks that a listing
        // is full of.
        return state.content;
    }
    QString statisticsText(const Block::State &state) const override
    {
        return displayText(state);
    }
    QString searchText(const Block::State &state) const override
    {
        return displayText(state);
    }

    bool isVerbatim() const override { return true; }
    bool holdsContent() const override { return true; }
    // The newlines are the code. Folding them would join a listing onto one
    // line and change what it means.
    bool foldsLineBreaks() const override { return false; }
    QString unfoldableTail(const Block::State &) const override
    {
        return QString();
    }
    int headingLevel() const override { return 0; }
    FontRole fontRole() const override { return FontRole::Mono; }
    bool isAlignable() const override { return false; }

    QString toHtml(const Block::State &state,
                   const RenderContext &ctx) const override
    {
        if (state.language == QLatin1String("diagram")
            || state.language == QLatin1String("text-diagram")
            || state.language == QLatin1String("ascii-diagram")) {
            // A character diagram: escaped preformatted text, whitespace
            // preserved, in the monospace stack the stylesheet sets on
            // pre.text-diagram. Both the browser and the PDF path share it.
            //
            // These three ids are deliberately NOT block kinds of their own.
            // A test asserts that a `diagram` fence still resolves to the
            // plain code delegate, so the branch stays inside this kind. The
            // same three strings appear in `ingestFence` in
            // documentserializer.cpp, which runs DiagramRepair over them on
            // load; adding an id in one place and not the other gives a
            // fence that is repaired but not drawn, or drawn but not
            // repaired.
            return "<pre class=\"text-diagram\"><code>"
                 + HtmlInline::esc(state.content) + "</code></pre>";
        }
        // The service returns the inner html of the <pre><code> pair — the
        // source with theme-coloured spans around its tokens — because the
        // theme lives above `domain`. With no services to ask, the escaped
        // source is what the highlighter would have produced for a language
        // it does not know.
        const QString code = ctx.services
            ? ctx.services->highlightedCodeHtml(state.language, state.content)
            : HtmlInline::esc(state.content);
        return "<pre><code>" + code + "</code></pre>";
    }

    QString toPlainText(const Block::State &state,
                        const RenderContext &) const override
    {
        // No fence markers: a `.txt` reader has no fence to close, and a
        // character diagram's source is the picture itself.
        return state.content;
    }

    QList<MenuEntry> menuEntries() const override
    {
        MenuEntry entry;
        entry.type = Block::CodeBlock;
        entry.name = QStringLiteral("Code Block");
        entry.description = QStringLiteral("Verbatim monospace code");
        entry.group = QStringLiteral("Advanced");
        entry.icon = QStringLiteral("<>");
        entry.aliases = { QStringLiteral("code"), QStringLiteral("codeblock"),
                          QStringLiteral("monospace"),
                          QStringLiteral("snippet"), QStringLiteral("```") };
        return { entry };
    }

    int delegateKind() const override { return static_cast<int>(kind()); }
    QString delegateUrl() const override
    {
        return QStringLiteral("qrc:/qml/CodeBlockDelegate.qml");
    }
};

// ---- divider ----
//
// The one kind with no text at all. Content and indent level are ignored
// everywhere below, which is why every projection answers without reading
// the state.
class DividerKindDef : public BlockKindDef
{
public:
    BlockKind kind() const override { return BlockKind::Divider; }
    QString id() const override { return QStringLiteral("divider"); }
    QString fenceLanguage() const override { return QString(); }

    QString serialize(const Block::State &, int) const override
    {
        return QStringLiteral("---");
    }
    bool attributeTagRidesOpeningLine() const override { return false; }

    QString displayText(const Block::State &state) const override
    {
        // A divider's content is empty in every block the editor writes, but
        // a hand-edited file can leave something on the line. It is rendered
        // rather than returned raw so the answer matches what every other
        // non-verbatim kind gives for the same string.
        return BlockText::rendered(state.content);
    }
    QString statisticsText(const Block::State &state) const override
    {
        return displayText(state);
    }
    QString searchText(const Block::State &) const override
    {
        // Nothing. A divider is a rule across the page; there is no text in
        // it for a search to find, and matching one would put a result in
        // front of a reader that they cannot see when they follow it.
        //
        // This is not new — the vault indexer and the note-list scan have
        // always zeroed a divider's searchable text as a special case of
        // their own, while Block::displayText did not. The two agreed only
        // because a divider the editor wrote has empty content anyway. Stated
        // here, the special case is gone from both scanners.
        return QString();
    }

    bool isVerbatim() const override { return false; }
    // "---" carries nothing back. Converting a paragraph into a divider used
    // to keep the paragraph's text in the state: the block went on rendering
    // it, the next save wrote a bare rule, and reopening the note found the
    // text gone with no record that it had ever been there.
    bool holdsContent() const override { return false; }
    bool foldsLineBreaks() const override { return false; }
    QString unfoldableTail(const Block::State &) const override
    {
        return QString();
    }
    int headingLevel() const override { return 0; }
    FontRole fontRole() const override { return FontRole::Body; }
    bool isAlignable() const override { return false; }

    QString toHtml(const Block::State &state,
                   const RenderContext &) const override
    {
        // The stored style, thickness, colour and width, matching what
        // DividerDelegate's canvas paints: a rule of the given weight and
        // dash pattern, centred at a fraction of the text column.
        // Only what the block actually states is written, as longhand over
        // the stylesheet's `hr` rule, so an unstyled divider still exports
        // as a bare "<hr>" — an exporter test compares against exactly that
        // string, and any declaration written unconditionally breaks it.
        const BlockStyle::Attributes attrs = BlockStyle::parse(state.attributes);
        const QString style = BlockStyle::str(attrs, QLatin1String("style"));
        const QString color = BlockStyle::cssColor(
            BlockStyle::str(attrs, QLatin1String("color")));
        const QString width = BlockStyle::str(attrs, QLatin1String("width"));
        QStringList declarations;
        if (BlockStyle::has(attrs, QLatin1String("thickness"))) {
            declarations << QStringLiteral("border-top-width:%1px")
                .arg(qBound(1, BlockStyle::num(attrs,
                                               QLatin1String("thickness"), 2),
                            12));
        }
        if (style == QLatin1String("dashed")
            || style == QLatin1String("dotted"))
            declarations << QStringLiteral("border-top-style:") + style;
        if (!color.isEmpty())
            declarations << QStringLiteral("border-top-color:") + color;
        bool ok = false;
        const int percent = QStringView(width).left(
            width.size() - (width.endsWith(QLatin1Char('%')) ? 1 : 0))
            .toInt(&ok);
        if (ok && percent > 0 && percent < 100) {
            declarations << QStringLiteral("width:%1%").arg(percent)
                         << QStringLiteral("margin-left:auto")
                         << QStringLiteral("margin-right:auto");
        }
        if (style == QLatin1String("decorative")) {
            // Two segments flanking a centred diamond. The flex box lays
            // them out in a browser; QTextDocument, which has no flex, still
            // draws the diamond, which is the motif.
            const QString rule = BlockStyle::styleAttr(declarations);
            return "<div class=\"hr-deco\"><hr" + rule
                 + "><span class=\"dia\""
                 + (color.isEmpty() ? QString()
                                    : " style=\"color:" + color + "\"")
                 + ">&#9670;</span><hr" + rule + "></div>";
        }
        return "<hr" + BlockStyle::styleAttr(declarations) + ">";
    }

    QString toPlainText(const Block::State &,
                        const RenderContext &) const override
    {
        return QStringLiteral("---");
    }

    QList<MenuEntry> menuEntries() const override
    {
        MenuEntry entry;
        entry.type = Block::Divider;
        entry.name = QStringLiteral("Divider");
        entry.description = QStringLiteral("Horizontal separator");
        entry.group = QStringLiteral("Advanced");
        entry.icon = QStringLiteral("—");
        entry.aliases = { QStringLiteral("divider"), QStringLiteral("hr"),
                          QStringLiteral("rule"),
                          QStringLiteral("separator"),
                          QStringLiteral("line"), QStringLiteral("---") };
        return { entry };
    }

    int delegateKind() const override { return static_cast<int>(kind()); }
    QString delegateUrl() const override
    {
        return QStringLiteral("qrc:/qml/DividerDelegate.qml");
    }
};

// ---- display math ----
//
// A $$ … $$ fence holding TeX. Two facts about it look contradictory and
// both are deliberate; see toPlainText() and isVerbatim() below.
class MathBlockKindDef : public BlockKindDef
{
public:
    BlockKind kind() const override { return BlockKind::MathBlock; }
    QString id() const override { return QStringLiteral("mathblock"); }
    QString fenceLanguage() const override { return QString(); }

    QString serialize(const Block::State &state, int) const override
    {
        // The canonical form is multi-line, so a hand-authored single-line
        // $$x$$ normalizes to it on the next save. Empty content writes the
        // two delimiters with nothing between them rather than a blank line.
        QString result = QStringLiteral("$$\n");
        if (!state.content.isEmpty())
            result += state.content + QLatin1Char('\n');
        result += QStringLiteral("$$");
        return result;
    }
    // The tag rides the opening $$ for the same reason as a code fence: the
    // math scanner requires a bare closing $$, and a tagged one never closes
    // the block.
    bool attributeTagRidesOpeningLine() const override { return true; }

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

    // False, even though toPlainText() below writes the TeX verbatim. The
    // two are separate questions and have always had separate answers here.
    //
    // isVerbatim() governs search and selection splicing: a replace maps a
    // match's offsets in searchText() straight back onto the content, and
    // the word count it feeds is written into the vault's sidecar index and
    // the search database's per-block verbatim flag. Math has been treated
    // as prose by both since those files were first written, so answering
    // true here would change the stored word count and the recorded flag for
    // every note in every existing vault.
    //
    // toPlainText() is an export projection with no stored state behind it,
    // and there the raw TeX is the only statement of the formula a `.txt`
    // reader gets.
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
        if (ctx.mathJax) {
            // MathJax reads the parsed DOM text, so HTML-escaping the TeX is
            // transparent to it. The single space inside each delimiter is
            // what the exporter has always written and what its tests
            // compare against.
            ctx.sawMath = true;
            return "<p class=\"math-display\">\\[ "
                 + HtmlInline::esc(state.content) + " \\]</p>";
        }
        // No JavaScript at the PDF seam, so the formula is typeset here and
        // embedded as a PNG. An empty answer means it did not typeset, and
        // the source is then the only thing left to show.
        const QString uri = ctx.services
            ? ctx.services->mathDataUri(state.content) : QString();
        if (!uri.isEmpty()) {
            return "<p style=\"text-align:center\"><img alt=\""
                 + HtmlInline::esc(state.content) + "\" src=\"" + uri
                 + "\"></p>";
        }
        return "<pre><code>" + HtmlInline::esc(state.content)
             + "</code></pre>";
    }

    QString toPlainText(const Block::State &state,
                        const RenderContext &) const override
    {
        // The TeX as written, NOT displayText(). The inline-markdown pass
        // reads `_`, `^` and `*` as span markers and eats them, which turns
        // a formula into a different formula.
        return state.content;
    }

    QList<MenuEntry> menuEntries() const override
    {
        MenuEntry entry;
        entry.type = Block::MathBlock;
        entry.name = QStringLiteral("Math Block");
        entry.description = QStringLiteral("LaTeX equation, rendered");
        entry.group = QStringLiteral("Advanced");
        entry.icon = QStringLiteral("∑");
        entry.aliases = { QStringLiteral("math"), QStringLiteral("equation"),
                          QStringLiteral("latex"), QStringLiteral("tex"),
                          QStringLiteral("formula"), QStringLiteral("$$") };
        return { entry };
    }

    int delegateKind() const override { return static_cast<int>(kind()); }
    QString delegateUrl() const override
    {
        return QStringLiteral("qrc:/qml/MathBlock.qml");
    }
};

} // namespace

namespace BlockKindGroups {

const QList<const BlockKindDef *> &code()
{
    static const CodeBlockKindDef codeBlock;
    static const DividerKindDef divider;
    static const MathBlockKindDef mathBlock;

    // The block menu lists its rows in the order the groups are
    // concatenated and, within a group, in the order of this list. Code
    // Block and Divider sit next to each other under "Advanced" and Math
    // Block comes later, so this order is the order a reader sees.
    static const QList<const BlockKindDef *> kinds = {
        &codeBlock, &divider, &mathBlock,
    };
    return kinds;
}

} // namespace BlockKindGroups
