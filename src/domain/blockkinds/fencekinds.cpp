// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
#include "kindgroups.h"

#include "blockkinddef.h"
#include "blockmarkdown.h"
#include "blocktext.h"
#include "htmlinline.h"
#include "kanbandata.h"
#include "rendercontext.h"

namespace {

// ---- what the four fence kinds share ----
//
// A task board, a table of contents, a Mermaid diagram and a collection query
// are all one stored thing: a Block::CodeBlock whose `language` holds the info
// string. Everything that follows from the STORED type is therefore the same
// for all four, and is answered once here — the markdown they write, the
// verbatim text projections, and every predicate that reads the stored type
// today. What differs between them is what the reader sees, so rendering, the
// menu row, the delegate and the identity stay pure and each kind answers them.
//
// This intermediate is file-local and covers exactly the four kinds that share
// one stored type. It is not the base-class body BlockKindDef refuses: nothing
// a kind could get WRONG by not thinking about it is defaulted here, because
// the answers below are all forced by the fence the kind is stored as.
class FenceKindDef : public BlockKindDef
{
public:
    // The same backtick fence a code block writes, with the language as the
    // info string. The language comes from the block rather than from
    // fenceLanguage(), because the file has to round-trip what it stored: this
    // is the serializer's CodeBlock case with the attribute tag left off, and
    // the caller attaches that to the opening line.
    QString serialize(const Block::State &state, int) const override
    {
        return BlockMarkdown::fencedBlock(state.content, state.language);
    }

    // The tag rides the opening fence. Appended after the closer it would stop
    // the closer from closing, and the next load would read the rest of the
    // note as this block's content.
    bool attributeTagRidesOpeningLine() const override { return true; }

    // All three projections are a code block's: the content, verbatim. The
    // fence body is source — board markdown, a heading list, diagram script, a
    // query spec — and resolving inline markers in it would rewrite the source
    // the parsers read back. searchText() being literally the content is also
    // what lets find-and-replace splice by position without corrupting it.
    QString displayText(const Block::State &state) const override
    {
        return state.content;
    }
    QString statisticsText(const Block::State &state) const override
    {
        return state.content;
    }
    QString searchText(const Block::State &state) const override
    {
        return state.content;
    }

    // Verbatim, and every predicate below follows the stored type for the same
    // reason: these blocks are CodeBlocks on disk, the word count in the
    // vault's sidecar index and the per-block verbatim flag in the search
    // database were written under that answer, and changing one changes
    // results for every vault that already exists.
    bool isVerbatim() const override { return true; }
    bool foldsLineBreaks() const override { return false; }
    QString unfoldableTail(const Block::State &) const override
    {
        return QString();
    }
    int headingLevel() const override { return 0; }
    FontRole fontRole() const override { return FontRole::Mono; }
    bool isAlignable() const override { return false; }

    // Each of the four has a delegate of its own, so none of them shares
    // another kind's number: the chooser needs a distinct value per delegate.
    int delegateKind() const override { return static_cast<int>(kind()); }
};

// ---- task board ----
//
// A `kanban` fence: columns of cards, parsed out of ordinary markdown by
// KanbanData so that the board a reader drags cards around is the same text
// any other markdown tool shows.
class KanbanKindDef : public FenceKindDef
{
public:
    BlockKind kind() const override { return BlockKind::Kanban; }
    QString id() const override { return QStringLiteral("kanban"); }
    QString fenceLanguage() const override { return QStringLiteral("kanban"); }

    QString toHtml(const Block::State &state,
                   const RenderContext &ctx) const override
    {
        const KanbanData::Board board = KanbanData::parse(state.content);
        QString out = QStringLiteral("<div class=\"kanban\">");
        for (const KanbanData::Column &col : board.columns) {
            out += "<div class=\"col\"><strong>" + HtmlInline::esc(col.name)
                 + "</strong> <span class=\"count\">"
                 + QString::number(col.cards.size()) + "</span>";
            // A card is more than its title: the board shows its labels and
            // due date as chips and its description under them, and an export
            // that kept only the title threw away most of what the reader had
            // put on the card.
            for (const KanbanData::Card &card : col.cards) {
                out += "<div class=\"card\"><div class=\"title\">"
                     + (card.done ? QStringLiteral("&#9745; ")
                                  : QStringLiteral("&#9744; "))
                     + HtmlInline::renderInline(card.title, ctx.mathJax,
                                                &ctx.sawMath)
                     + "</div>";
                if (!card.description.isEmpty())
                    out += "<div class=\"meta\">"
                         + HtmlInline::renderInline(card.description,
                                                    ctx.mathJax, &ctx.sawMath)
                         + "</div>";
                if (!card.labels.isEmpty() || !card.due.isEmpty()) {
                    out += QStringLiteral("<div class=\"meta\">");
                    for (const QString &label : card.labels)
                        out += "<span class=\"chip\">" + HtmlInline::esc(label)
                             + "</span>";
                    if (!card.due.isEmpty())
                        out += "<span class=\"chip\">&#128197; "
                             + HtmlInline::esc(card.due) + "</span>";
                    out += QStringLiteral("</div>");
                }
                out += QStringLiteral("</div>");
            }
            out += "</div>";
        }
        out += "</div>";
        return out;
    }

    QString toPlainText(const Block::State &state,
                        const RenderContext &) const override
    {
        const KanbanData::Board board = KanbanData::parse(state.content);
        QStringList out;
        // Content above the first `## ` header is ordinary prose the board
        // shows as-is. A fence with no header at all lands here in full, and
        // exported as nothing back when the board branch wrote columns only.
        for (const QString &line : board.preamble) {
            if (!line.trimmed().isEmpty())
                out << line;
        }
        for (const KanbanData::Column &column : board.columns) {
            if (!out.isEmpty())
                out << QString();
            out << column.name + QStringLiteral(" (")
                       + QString::number(column.cards.size())
                       + QLatin1Char(')');
            for (const KanbanData::Card &card : column.cards) {
                QString line = (card.done ? QStringLiteral("  [x] ")
                                          : QStringLiteral("  [ ] "))
                             + BlockText::renderedFully(card.title);
                for (const QString &label : card.labels)
                    line += QStringLiteral("  #") + label;
                if (!card.due.isEmpty())
                    line += QStringLiteral("  (due ") + card.due
                          + QLatin1Char(')');
                out << line;
                if (!card.description.isEmpty()) {
                    out << BlockText::indent(
                        BlockText::renderedFully(card.description), 6);
                }
            }
        }
        return out.join(QLatin1Char('\n'));
    }

    QList<MenuEntry> menuEntries() const override
    {
        MenuEntry entry;
        // A CodeBlock by stored type, with the fence language carried on
        // `language` so the insert rides the convertBlock(language) path.
        entry.type = Block::CodeBlock;
        entry.name = QStringLiteral("Task Board");
        entry.description = QStringLiteral("Kanban columns of draggable cards");
        entry.group = QStringLiteral("Advanced");
        entry.icon = QStringLiteral("▤");
        entry.aliases = { QStringLiteral("kanban"), QStringLiteral("board"),
                          QStringLiteral("task"), QStringLiteral("tasks"),
                          QStringLiteral("todo"), QStringLiteral("trello") };
        entry.defaultLanguage = QStringLiteral("kanban");
        // Three empty columns, so the board renders something to drop cards
        // into rather than an empty panel, and does it inside the one
        // convertBlock undo step.
        entry.seed = QStringLiteral("## To do\n## In progress\n## Done");
        return { entry };
    }

    QString delegateUrl() const override
    {
        return QStringLiteral("qrc:/qml/KanbanBlock.qml");
    }
};

// ---- table of contents ----
//
// A `toc` fence. Both projections are one call into the document renderer, and
// the block's own fence body is ignored on purpose: the editor rewrites that
// body as the reader types, so it is stale in a note nobody has opened since
// the headings changed. The answer also needs every heading in the document
// plus the whole collision-suffixed slug table, neither of which one block can
// see, so it belongs to the renderer that already computes both.
class TocKindDef : public FenceKindDef
{
public:
    BlockKind kind() const override { return BlockKind::Toc; }
    QString id() const override { return QStringLiteral("toc"); }
    QString fenceLanguage() const override { return QStringLiteral("toc"); }

    QString toHtml(const Block::State &,
                   const RenderContext &ctx) const override
    {
        return ctx.services ? ctx.services->tableOfContentsHtml() : QString();
    }

    QString toPlainText(const Block::State &,
                        const RenderContext &ctx) const override
    {
        return ctx.services ? ctx.services->tableOfContentsPlainText()
                            : QString();
    }

    QList<MenuEntry> menuEntries() const override
    {
        MenuEntry entry;
        entry.type = Block::CodeBlock;
        entry.name = QStringLiteral("Table of Contents");
        entry.description = QStringLiteral("Auto-generated list of headings");
        entry.group = QStringLiteral("Advanced");
        entry.icon = QStringLiteral("☰");
        entry.aliases = { QStringLiteral("toc"), QStringLiteral("contents"),
                          QStringLiteral("outline"), QStringLiteral("index"),
                          QStringLiteral("headings") };
        entry.defaultLanguage = QStringLiteral("toc");
        // No seed here. This one row's starter content is the open document's
        // current headings, which only the running editor knows, so BlockMenu
        // generates it from DocumentOutline at insert time and passes it to
        // convertBlock. Putting a fixed string here would overwrite that.
        return { entry };
    }

    QString delegateUrl() const override
    {
        return QStringLiteral("qrc:/qml/TocBlock.qml");
    }
};

// ---- Mermaid diagram ----
//
// A `mermaid` fence. The two targets draw it by completely different means, so
// this is the one kind whose markup depends on which export is running.
class MermaidKindDef : public FenceKindDef
{
public:
    BlockKind kind() const override { return BlockKind::Mermaid; }
    QString id() const override { return QStringLiteral("mermaid"); }
    QString fenceLanguage() const override { return QStringLiteral("mermaid"); }

    QString toHtml(const Block::State &state,
                   const RenderContext &ctx) const override
    {
        if (ctx.target == RenderContext::Browser) {
            // The browser renders the original source with Mermaid.js. A
            // collapsed <details> keeps the escaped source available after
            // Mermaid replaces the render target or errors.
            //
            // This is the only writer of ctx.sawMermaid, and it sets it only
            // here: the flag decides whether the document wrapper emits the
            // Mermaid module tag, and the PDF seam runs no JavaScript, so
            // setting it on that path would attach a network dependency to a
            // file that cannot use it.
            ctx.sawMermaid = true;
            return "<pre class=\"mermaid\">" + HtmlInline::esc(state.content)
                 + "</pre>"
                 + "<details class=\"diagram-source\"><summary>Diagram "
                   "source</summary><pre><code>"
                 + HtmlInline::esc(state.content) + "</code></pre></details>";
        }
        // PDF: rasterize a natively-supported diagram; fall back to escaped
        // source for invalid or unsupported families.
        const QString uri =
            ctx.services ? ctx.services->mermaidDataUri(state.content)
                         : QString();
        if (!uri.isEmpty())
            return "<p style=\"text-align:center\"><img alt=\"Mermaid "
                   "diagram\" src=\"" + uri + "\"></p>";
        return "<pre><code>" + HtmlInline::esc(state.content)
             + "</code></pre>";
    }

    QString toPlainText(const Block::State &state,
                        const RenderContext &) const override
    {
        // There is no picture to write and no text rendering of one, so the
        // source stays — labelled, so a reader can tell a diagram from a
        // listing rather than reading `A-->B` as prose.
        return QStringLiteral("[mermaid diagram]\n") + state.content;
    }

    QList<MenuEntry> menuEntries() const override
    {
        MenuEntry entry;
        entry.type = Block::CodeBlock;
        entry.name = QStringLiteral("Mermaid Diagram");
        entry.description =
            QStringLiteral("Flowchart and graph diagrams from Mermaid syntax");
        entry.group = QStringLiteral("Advanced");
        entry.icon = QStringLiteral("◈");
        entry.aliases = { QStringLiteral("mermaid"),
                          QStringLiteral("flowchart"), QStringLiteral("graph"),
                          QStringLiteral("flow"), QStringLiteral("diagram") };
        entry.defaultLanguage = QStringLiteral("mermaid");
        // A small flowchart, so the block draws a diagram from the moment it
        // is inserted and the reader can see what editing the source does.
        entry.seed = QStringLiteral("flowchart LR\n"
                                    "  A[Start] --> B{Decision}\n"
                                    "  B -->|yes| C[Done]\n"
                                    "  B -->|no| A");
        return { entry };
    }

    QString delegateUrl() const override
    {
        return QStringLiteral("qrc:/qml/DiagramBlock.qml");
    }
};

// ---- collection query ----
//
// A `query` fence: a live table or board over the collection's front-matter.
// Both projections delegate whole, because answering the query needs the open
// collection, which lives in a module this one may not include, and because
// the words the answer is labelled with ("Query", "%1 notes") are translated
// in the exporter's context and have to stay there to keep their translations.
class QueryKindDef : public FenceKindDef
{
public:
    BlockKind kind() const override { return BlockKind::Query; }
    QString id() const override { return QStringLiteral("query"); }
    QString fenceLanguage() const override { return QStringLiteral("query"); }

    // The spec, not the answer. This is what the vault index has always
    // matched over, and it is recorded here as the status quo rather than as a
    // considered endpoint: indexing the RESULTS instead would mean a
    // front-matter edit in one note invalidates query blocks in unrelated
    // notes, which needs a dependency graph and a reindex model. The index is
    // currently a pure function of one file's text, and this keeps it one.
    QString searchText(const Block::State &state) const override
    {
        return state.content;
    }

    QString toHtml(const Block::State &state,
                   const RenderContext &ctx) const override
    {
        return ctx.services ? ctx.services->queryHtml(state.content)
                            : QString();
    }

    QString toPlainText(const Block::State &state,
                        const RenderContext &ctx) const override
    {
        return ctx.services ? ctx.services->queryPlainText(state.content)
                            : QString();
    }

    QList<MenuEntry> menuEntries() const override
    {
        MenuEntry entry;
        entry.type = Block::CodeBlock;
        entry.name = QStringLiteral("Collection Query");
        entry.description =
            QStringLiteral("Live table or board over your notes' front-matter");
        entry.group = QStringLiteral("Advanced");
        entry.icon = QStringLiteral("⌕");
        entry.aliases = { QStringLiteral("query"), QStringLiteral("database"),
                          QStringLiteral("dataview"),
                          QStringLiteral("filter"),
                          QStringLiteral("frontmatter") };
        entry.defaultLanguage = QStringLiteral("query");
        // A starter spec with the two filters commented out, so a newly
        // inserted query lists the whole collection rather than erroring, and
        // the reader adapts the lines that are already there.
        entry.seed = QStringLiteral("# from: projects/\n"
                                    "# where: status = active\n"
                                    "view: table\n"
                                    "columns: title, tags, modified\n"
                                    "sort: modified desc");
        return { entry };
    }

    QString delegateUrl() const override
    {
        return QStringLiteral("qrc:/qml/QueryBlock.qml");
    }
};

} // namespace

namespace BlockKindGroups {

const QList<const BlockKindDef *> &fences()
{
    static const KanbanKindDef kanban;
    static const TocKindDef toc;
    static const MermaidKindDef mermaid;
    static const QueryKindDef query;

    static const QList<const BlockKindDef *> kinds = {
        &kanban, &toc, &mermaid, &query,
    };
    return kinds;
}

} // namespace BlockKindGroups
