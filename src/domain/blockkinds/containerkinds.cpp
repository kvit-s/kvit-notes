// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
#include "kindgroups.h"

#include "blockkinddef.h"
#include "blockstyle.h"
#include "blocktext.h"
#include "htmlinline.h"
#include "rendercontext.h"
#include "tabledata.h"

namespace {

// ---- callout ----
//
// An aside with a type, an optional title and a fold state, written as a
// block quote the parser recognises by its `[!type]` opener. Three of the
// State fields mean something different here than they do anywhere else:
// `language` is the callout TYPE (info, warning, tip), `checked` is the FOLD
// state, and `calloutTitle` is the heading. Nothing else uses calloutTitle at
// all.
class CalloutKindDef : public BlockKindDef
{
public:
    BlockKind kind() const override { return BlockKind::Callout; }
    QString id() const override { return QStringLiteral("callout"); }
    QString fenceLanguage() const override { return QString(); }

    QString serialize(const Block::State &state, int) const override
    {
        // "> [!type][-] Title" header, then "> " body lines. Folded writes
        // the '-' marker; expanded writes none, so a hand-authored '+'
        // normalizes to no marker — a documented normalization.
        QString header = QStringLiteral("> [!") + state.language
                       + QStringLiteral("]");
        if (state.checked)
            header += QLatin1Char('-');
        if (!state.calloutTitle.isEmpty())
            header += QLatin1Char(' ') + state.calloutTitle;
        QStringList out;
        out << header;
        if (!state.content.isEmpty()) {
            // An empty body line is written as ">" with no space after it.
            // "> " would put trailing whitespace into the reader's file on
            // every save of a callout that contains a blank line.
            for (const QString &line : state.content.split(QLatin1Char('\n')))
                out << (line.isEmpty() ? QStringLiteral(">")
                                       : QStringLiteral("> ") + line);
        }
        return out.join(QLatin1Char('\n'));
    }
    // The tag rides the header line, where it stays clear of the body: the
    // last line of a callout is a "> " body line, and a tag trailing it would
    // land inside the reader's text.
    bool attributeTagRidesOpeningLine() const override { return true; }

    // The title is chrome the fold control draws, so none of the three
    // projections include it; they answer with the body alone, exactly as the
    // one cached string did before the kinds owned this.
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
    bool isAlignable() const override { return false; }

    QString toHtml(const Block::State &state,
                   const RenderContext &ctx) const override
    {
        // With no title the raw type id stands in as the heading, which is
        // what the delegate shows: an untitled warning reads as "warning".
        const QString heading = state.calloutTitle.isEmpty()
            ? state.language : state.calloutTitle;
        const BlockStyle::Attributes attrs = BlockStyle::parse(state.attributes);
        // A callout's `color` attribute overrides the type's accent, which
        // the delegate applies to the left bar and the header.
        const QString accent =
            BlockStyle::cssColor(BlockStyle::str(attrs, QLatin1String("color")));
        const QString panelStyle = accent.isEmpty()
            ? QString() : " style=\"border-left-color:" + accent + "\"";
        const QString titleStyle = accent.isEmpty()
            ? QString() : " style=\"color:" + accent + "\"";
        // The heading is escaped rather than rendered as inline markdown. A
        // title is a short label the editor never draws markers in, and the
        // exporter's assertions compare the markup byte for byte, so running
        // it through renderInline would change output the tests pin.
        return "<div class=\"callout\"" + panelStyle
             + "><div class=\"title\"" + titleStyle + ">"
             + HtmlInline::esc(heading) + "</div>"
             + HtmlInline::renderInline(state.content, ctx.mathJax, &ctx.sawMath)
             + "</div>";
    }

    QString toPlainText(const Block::State &state,
                        const RenderContext &) const override
    {
        // The type and title on one line, the body indented under it, so the
        // aside still reads as an aside rather than merging into the prose
        // above it.
        QString head = QLatin1Char('[') + state.language.toUpper()
                     + QLatin1Char(']');
        if (!state.calloutTitle.isEmpty())
            head += QLatin1Char(' ')
                  + BlockText::renderedFully(state.calloutTitle);
        if (state.content.isEmpty())
            return head;
        return head + QLatin1Char('\n')
             + BlockText::indent(BlockText::renderedFully(state.content), 2);
    }

    QList<MenuEntry> menuEntries() const override
    {
        // Two rows, one kind. Both insert a Callout and differ only in the
        // seeded `language`: a type for the first, the "toggle" marker for
        // the second, which is a Callout the delegate draws collapsed.
        MenuEntry callout;
        callout.type = Block::Callout;
        callout.name = QStringLiteral("Callout");
        callout.description =
            QStringLiteral("Highlighted info/warning/tip box");
        callout.group = QStringLiteral("Advanced");
        callout.icon = QStringLiteral("!");
        callout.aliases = { QStringLiteral("callout"),
                            QStringLiteral("admonition"),
                            QStringLiteral("note"), QStringLiteral("info"),
                            QStringLiteral("warning"), QStringLiteral("[!") };
        callout.defaultLanguage = QStringLiteral("info");

        MenuEntry toggle;
        toggle.type = Block::Callout;
        toggle.name = QStringLiteral("Toggle");
        toggle.description = QStringLiteral("Collapsible section");
        toggle.group = QStringLiteral("Advanced");
        toggle.icon = QStringLiteral("▸");
        toggle.aliases = { QStringLiteral("toggle"),
                           QStringLiteral("collapse"), QStringLiteral("fold"),
                           QStringLiteral("details") };
        toggle.defaultLanguage = QStringLiteral("toggle");
        return { callout, toggle };
    }

    int delegateKind() const override { return static_cast<int>(kind()); }
    QString delegateUrl() const override
    {
        return QStringLiteral("qrc:/qml/CalloutBlock.qml");
    }
};

// ---- table ----
//
// A pipe table, whose content IS the grid's markdown: a table does not
// decompose into per-block fields the way a heading or a to-do does, so
// TableData parses the content into cells and writes it back out.
class TableKindDef : public BlockKindDef
{
public:
    BlockKind kind() const override { return BlockKind::Table; }
    QString id() const override { return QStringLiteral("table"); }
    QString fenceLanguage() const override { return QString(); }

    QString serialize(const Block::State &state, int) const override
    {
        // The one kind that rewrites its own content on save. Parsing and
        // re-serializing canonicalizes the markdown, so a hand-authored
        // ragged or padded table squares up on the first save while a
        // Kvit-written one round-trips byte for byte. Returning the stored
        // content untouched instead would leave every delegate edit writing
        // whatever spacing the cell editor last produced.
        return TableData::serialize(TableData::parse(state.content));
    }
    // The tag rides the header row, where it stays out of the cell data: a
    // tag on the last data row would be read back as part of that row's last
    // cell.
    bool attributeTagRidesOpeningLine() const override { return true; }

    // The pipes and the delimiter row are markdown the reader never sees, but
    // stripping them here would change the word count already written into
    // every vault's sidecar index and the text the search database matches
    // over. The projections stay what they have always been: the raw grid
    // markdown with its inline markers resolved.
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
    // A cell's stored line break is content the table draws, and the rows are
    // separated by newlines that are the grid's structure, so there is
    // nothing here a line fold could sensibly remove.
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
        const TableData::Table tbl = TableData::parse(state.content);
        const BlockStyle::Attributes attrs = BlockStyle::parse(state.attributes);
        // A cell's line breaks reach here as newlines (TableData reads the
        // stored <br> back). HTML collapses those into spaces, so they are
        // re-stated as markup — otherwise a listing folded into a row exports
        // as one long line. The replacement after renderInline is not
        // redundant with escFlowing: a verbatim span, inline code, is escaped
        // without break conversion, so its newlines are still literal at this
        // point.
        const auto cellHtml = [&](const QString &cell) {
            return HtmlInline::renderInline(cell, ctx.mathJax, &ctx.sawMath)
                .replace(QLatin1Char('\n'), QLatin1String("<br>"));
        };
        QString html = QStringLiteral("<table>");
        // Column widths the reader dragged out, stored as a comma-separated
        // pixel list with 0 meaning "measure this one". A <colgroup> is the
        // only way to state them once rather than per cell, and both browsers
        // and QTextDocument honour it.
        const QStringList storedCols =
            BlockStyle::str(attrs, QLatin1String("cols"))
                .split(QLatin1Char(','), Qt::SkipEmptyParts);
        if (!storedCols.isEmpty()) {
            QString group;
            bool anyWidth = false;
            for (const QString &raw : storedCols) {
                bool ok = false;
                const int width = raw.trimmed().toInt(&ok);
                if (ok && width > 0) {
                    anyWidth = true;
                    group += "<col style=\"width:" + QString::number(width)
                           + "px\">";
                } else {
                    group += QStringLiteral("<col>");
                }
            }
            if (anyWidth)
                html += "<colgroup>" + group + "</colgroup>";
        }
        if (!tbl.headers.isEmpty()) {
            html += QStringLiteral("<tr>");
            for (const QString &h : tbl.headers)
                html += "<th>" + cellHtml(h) + "</th>";
            html += QStringLiteral("</tr>");
        }
        for (const QStringList &row : tbl.rows) {
            html += QStringLiteral("<tr>");
            for (const QString &cell : row)
                html += "<td>" + cellHtml(cell) + "</td>";
            html += QStringLiteral("</tr>");
        }
        html += QStringLiteral("</table>");
        return html;
    }

    QString toPlainText(const Block::State &state,
                        const RenderContext &) const override
    {
        // A column-aligned text table rather than the pipe markdown: the
        // grid is what the reader sees, and the pipes are syntax.
        const TableData::Table table = TableData::parse(state.content);
        QStringList headers;
        headers.reserve(table.headers.size());
        for (const QString &header : table.headers)
            headers << BlockText::renderedFully(header);
        QList<QStringList> rows;
        rows.reserve(table.rows.size());
        for (const QStringList &row : table.rows) {
            QStringList cells;
            cells.reserve(row.size());
            for (const QString &cell : row)
                cells << BlockText::renderedFully(cell);
            rows << cells;
        }
        return BlockText::alignedTable(headers, rows);
    }

    QList<MenuEntry> menuEntries() const override
    {
        MenuEntry entry;
        entry.type = Block::Table;
        entry.name = QStringLiteral("Table");
        entry.description = QStringLiteral("Grid with rows and columns");
        entry.group = QStringLiteral("Advanced");
        entry.icon = QStringLiteral("▦");
        entry.aliases = { QStringLiteral("table"), QStringLiteral("grid"),
                          QStringLiteral("spreadsheet") };
        // No seed: the insert flow opens the grid picker and builds the
        // starter markdown from the size the reader chooses.
        return { entry };
    }

    int delegateKind() const override { return static_cast<int>(kind()); }
    QString delegateUrl() const override
    {
        return QStringLiteral("qrc:/qml/TableBlock.qml");
    }
};

} // namespace

namespace BlockKindGroups {

const QList<const BlockKindDef *> &containers()
{
    static const CalloutKindDef callout;
    static const TableKindDef table;

    static const QList<const BlockKindDef *> kinds = {
        &callout, &table,
    };
    return kinds;
}

} // namespace BlockKindGroups
