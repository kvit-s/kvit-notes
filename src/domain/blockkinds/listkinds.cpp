// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
#include "kindgroups.h"

#include "blockkinddef.h"
#include "blockmarkdown.h"
#include "blocktext.h"
#include "htmlinline.h"
#include "markdownformatter.h"
#include "rendercontext.h"
#include "todometa.h"

namespace {

// ---- the list family and the quote ----
//
// THE HTML CONTRACT FOR THE THREE LIST KINDS, which is the easiest thing here
// to get wrong: toHtml() returns the item's INNER html and nothing else — no
// <li>, no <ul>, no <ol>, no closing tag. The document renderer collects the
// contiguous run of items of one flavour, opens the list tags, and defers
// every closing </li> so that an item deeper than the one before it opens its
// sublist INSIDE the still-open <li>, which is where HTML wants a nested list.
// A kind that wrapped its own <li> would close it before the sublist could be
// opened, and every nested list would export flat.
//
// The quote is in this file rather than with the prose kinds because it shares
// the family's indentation handling: its indentLevel is nesting depth, written
// as repeated "> " markers, the same way a list item's is written as leading
// spaces.

// ---- bullet list ----
class BulletListKindDef : public BlockKindDef
{
public:
    BlockKind kind() const override { return BlockKind::BulletList; }
    QString id() const override { return QStringLiteral("bulletlist"); }
    QString fenceLanguage() const override { return QString(); }

    QString serialize(const Block::State &state, int) const override
    {
        return BlockMarkdown::listItemLines(
            BlockMarkdown::indentPrefix(state.indentLevel)
                + QStringLiteral("- "),
            state.content);
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
    bool holdsContent() const override { return true; }
    bool foldsLineBreaks() const override { return true; }
    QString unfoldableTail(const Block::State &) const override
    {
        return QString();
    }
    int headingLevel() const override { return 0; }
    FontRole fontRole() const override { return FontRole::Body; }
    bool isAlignable() const override { return false; }

    // The item's inner html only: see the contract at the top of this file.
    QString toHtml(const Block::State &state,
                   const RenderContext &ctx) const override
    {
        return HtmlInline::renderInline(state.content, ctx.mathJax,
                                        &ctx.sawMath);
    }

    QString toPlainText(const Block::State &state,
                        const RenderContext &) const override
    {
        return BlockMarkdown::indentPrefix(state.indentLevel)
             + QStringLiteral("- ") + BlockText::renderedFully(state.content);
    }

    QList<MenuEntry> menuEntries() const override
    {
        MenuEntry entry;
        entry.type = Block::BulletList;
        entry.name = QStringLiteral("Bulleted List");
        entry.description = QStringLiteral("Unordered list item");
        entry.group = QStringLiteral("Lists");
        entry.icon = QStringLiteral("•");
        entry.aliases = { QStringLiteral("bullet"),
                          QStringLiteral("unordered"), QStringLiteral("ul"),
                          QStringLiteral("list"), QStringLiteral("-") };
        return { entry };
    }

    int delegateKind() const override { return static_cast<int>(kind()); }
    QString delegateUrl() const override
    {
        return QStringLiteral("qrc:/qml/BulletListDelegate.qml");
    }
};

// ---- numbered list ----
//
// The only kind that reads serialize()'s `ordinal` argument, and the only one
// that reads RenderContext::ordinal. Both numbers are counted by the caller,
// which keeps one counter per indent level so that a nested list restarts at
// 1 rather than continuing the outer list's run.
class NumberedListKindDef : public BlockKindDef
{
public:
    BlockKind kind() const override { return BlockKind::NumberedList; }
    QString id() const override { return QStringLiteral("numberedlist"); }
    QString fenceLanguage() const override { return QString(); }

    QString serialize(const Block::State &state, int ordinal) const override
    {
        // qMax(1, …) because a caller that has no numbering to give passes 0,
        // and "0. " is not a list item to any markdown parser: the item would
        // load back as a paragraph.
        return BlockMarkdown::listItemLines(
            BlockMarkdown::indentPrefix(state.indentLevel)
                + QString::number(qMax(1, ordinal)) + QStringLiteral(". "),
            state.content);
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
    bool holdsContent() const override { return true; }
    bool foldsLineBreaks() const override { return true; }
    QString unfoldableTail(const Block::State &) const override
    {
        return QString();
    }
    int headingLevel() const override { return 0; }
    FontRole fontRole() const override { return FontRole::Body; }
    bool isAlignable() const override { return false; }

    // The item's inner html only: see the contract at the top of this file.
    // The number itself is never written here — the <ol> the caller opens is
    // what numbers the item, so a run that starts at an item other than the
    // first still counts from 1 in a browser.
    QString toHtml(const Block::State &state,
                   const RenderContext &ctx) const override
    {
        return HtmlInline::renderInline(state.content, ctx.mathJax,
                                        &ctx.sawMath);
    }

    QString toPlainText(const Block::State &state,
                        const RenderContext &ctx) const override
    {
        return BlockMarkdown::indentPrefix(state.indentLevel)
             + QString::number(ctx.ordinal) + QStringLiteral(". ")
             + BlockText::renderedFully(state.content);
    }

    QList<MenuEntry> menuEntries() const override
    {
        MenuEntry entry;
        entry.type = Block::NumberedList;
        entry.name = QStringLiteral("Numbered List");
        entry.description = QStringLiteral("Ordered list item");
        entry.group = QStringLiteral("Lists");
        entry.icon = QStringLiteral("1.");
        entry.aliases = { QStringLiteral("numbered"),
                          QStringLiteral("ordered"), QStringLiteral("ol"),
                          QStringLiteral("1.") };
        return { entry };
    }

    int delegateKind() const override { return static_cast<int>(kind()); }
    QString delegateUrl() const override
    {
        return QStringLiteral("qrc:/qml/NumberedListDelegate.qml");
    }
};

// ---- to-do ----
//
// A to-do's content carries a metadata tail — a 📅 due date and a priority
// arrow, stored as trailing Obsidian Tasks tokens — that the editor draws as
// chips rather than as text. The three text projections strip it and the
// markup does not, which is the behaviour in place today: the tail is written
// into the file, so an export that dropped it would lose the due date, while
// a word count that kept it would count the emoji as words and write that
// number into the vault's sidecar index.
class TodoKindDef : public BlockKindDef
{
public:
    BlockKind kind() const override { return BlockKind::Todo; }
    QString id() const override { return QStringLiteral("todo"); }
    QString fenceLanguage() const override { return QString(); }

    QString serialize(const Block::State &state, int) const override
    {
        return BlockMarkdown::listItemLines(
            BlockMarkdown::indentPrefix(state.indentLevel)
                + (state.checked ? QStringLiteral("- [x] ")
                                 : QStringLiteral("- [ ] ")),
            state.content);
    }
    bool attributeTagRidesOpeningLine() const override { return false; }

    // The metadata tail comes off BEFORE the inline markers are resolved,
    // which is the one per-kind text rule the block's text cache kept.
    QString displayText(const Block::State &state) const override
    {
        return BlockText::rendered(TodoMeta::displayText(state.content));
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
    bool foldsLineBreaks() const override { return true; }
    QString unfoldableTail(const Block::State &) const override
    {
        // Nothing: a to-do's metadata sits at the end of its line already, so
        // folding the item's wrapped lines cannot turn chrome into body text
        // the way it can in a quote.
        return QString();
    }
    int headingLevel() const override { return 0; }
    FontRole fontRole() const override { return FontRole::Body; }
    bool isAlignable() const override { return false; }

    // The item's inner html only: see the contract at the top of this file.
    // The box is an entity followed by exactly one space, and the content is
    // the stored content with its metadata tail left on, both as the exporter
    // has always written them.
    QString toHtml(const Block::State &state,
                   const RenderContext &ctx) const override
    {
        const QString box = state.checked ? QStringLiteral("&#9745; ")
                                          : QStringLiteral("&#9744; ");
        return box + HtmlInline::renderInline(state.content, ctx.mathJax,
                                              &ctx.sawMath);
    }

    QString toPlainText(const Block::State &state,
                        const RenderContext &) const override
    {
        // "[x] " and "[ ] " without the "- " the markdown form carries: this
        // is text for a reader, and the marker would be noise in front of a
        // box that already says the same thing.
        return BlockMarkdown::indentPrefix(state.indentLevel)
             + (state.checked ? QStringLiteral("[x] ")
                              : QStringLiteral("[ ] "))
             // The whole content, metadata tail included. The editor draws
             // the due date and the priority as chips beside the text, so a
             // reader sees them; the three text projections above strip them
             // because a word count and a search index should not carry an
             // emoji tail, and an export is neither of those.
             + BlockText::renderedFully(state.content);
    }

    QList<MenuEntry> menuEntries() const override
    {
        MenuEntry entry;
        entry.type = Block::Todo;
        entry.name = QStringLiteral("To-do");
        entry.description = QStringLiteral("Checkbox item");
        entry.group = QStringLiteral("Lists");
        entry.icon = QStringLiteral("☐");
        entry.aliases = { QStringLiteral("todo"), QStringLiteral("task"),
                          QStringLiteral("checkbox"), QStringLiteral("check"),
                          QStringLiteral("[]") };
        return { entry };
    }

    int delegateKind() const override { return static_cast<int>(kind()); }
    QString delegateUrl() const override
    {
        return QStringLiteral("qrc:/qml/TodoDelegate.qml");
    }
};

// ---- quote ----
class QuoteKindDef : public BlockKindDef
{
public:
    BlockKind kind() const override { return BlockKind::Quote; }
    QString id() const override { return QStringLiteral("quote"); }
    QString fenceLanguage() const override { return QString(); }

    QString serialize(const Block::State &state, int) const override
    {
        // One quote block per contiguous run at one depth; the depth is
        // indentLevel + 1, since a top-level quote already writes one marker.
        const int depth = state.indentLevel + 1;
        QString prefix;
        for (int d = 0; d < depth; ++d)
            prefix += QStringLiteral("> ");
        const QStringList lines = state.content.split(QLatin1Char('\n'));
        QStringList quoted;
        quoted.reserve(lines.size());
        // An empty content line writes the markers with NO trailing space.
        // Keeping the space would put trailing whitespace on the line, which
        // the round-trip corpus compares byte for byte: every quote holding a
        // blank line would rewrite itself on the next save.
        for (const QString &line : lines)
            quoted.append(line.isEmpty() ? prefix.trimmed() : prefix + line);
        return quoted.join(QLatin1Char('\n'));
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
    bool holdsContent() const override { return true; }
    bool foldsLineBreaks() const override { return true; }

    // The one kind with a tail a line fold has to leave alone. An attribution
    // is the last line of a multi-line quote starting with an em dash and a
    // space, and the delegate draws it as chrome below the body; folding the
    // newline in front of it would silently turn it into body text.
    //
    // The returned tail INCLUDES its leading newline, which is the convention
    // the caller depends on: it takes the body as
    // content.left(content.size() - tail.size()), folds that, and appends the
    // tail unchanged. A tail without the newline would leave that newline at
    // the end of the body, where the fold would eat it and join the
    // attribution onto the prose after all.
    QString unfoldableTail(const Block::State &state) const override
    {
        // Constructed per call, as the fold path already does: the formatter
        // holds no state, and a QObject shared across threads would be worse
        // than the allocation this avoids.
        const MarkdownFormatter formatter;
        return formatter.attributionTail(state.content);
    }

    int headingLevel() const override { return 0; }
    FontRole fontRole() const override { return FontRole::Body; }
    bool isAlignable() const override { return false; }

    QString toHtml(const Block::State &state,
                   const RenderContext &ctx) const override
    {
        // No <p> inside the blockquote: the export's stylesheet spaces the
        // blockquote itself, and a paragraph in there adds its own margins on
        // top of that. The attribution stays part of the quoted text, as the
        // markdown stores it.
        return "<blockquote>"
             + HtmlInline::renderInline(state.content, ctx.mathJax,
                                        &ctx.sawMath)
             + "</blockquote>";
    }

    QString toPlainText(const Block::State &state,
                        const RenderContext &) const override
    {
        // Every line gets the marker, including an empty one, which therefore
        // carries a trailing space. That is what the plain-text export has
        // always written, and its tests compare the whole string.
        const QStringList lines =
            BlockText::renderedFully(state.content).split(QLatin1Char('\n'));
        QStringList quoted;
        quoted.reserve(lines.size());
        for (const QString &line : lines)
            quoted.append(QStringLiteral("> ") + line);
        return quoted.join(QLatin1Char('\n'));
    }

    QList<MenuEntry> menuEntries() const override
    {
        MenuEntry entry;
        entry.type = Block::Quote;
        entry.name = QStringLiteral("Quote");
        entry.description = QStringLiteral("Block quotation");
        entry.group = QStringLiteral("Advanced");
        entry.icon = QStringLiteral("❝");
        entry.aliases = { QStringLiteral("quote"),
                          QStringLiteral("blockquote"), QStringLiteral(">") };
        return { entry };
    }

    int delegateKind() const override { return static_cast<int>(kind()); }
    QString delegateUrl() const override
    {
        return QStringLiteral("qrc:/qml/QuoteDelegate.qml");
    }
};

} // namespace

namespace BlockKindGroups {

const QList<const BlockKindDef *> &lists()
{
    static const BulletListKindDef bulletList;
    static const NumberedListKindDef numberedList;
    static const TodoKindDef todo;
    static const QuoteKindDef quote;

    static const QList<const BlockKindDef *> kinds = {
        &bulletList, &numberedList, &todo, &quote,
    };
    return kinds;
}

} // namespace BlockKindGroups
