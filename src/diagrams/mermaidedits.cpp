// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
#include "mermaidedits.h"
#include "mermaidlexer.h"
#include "mermaidparser.h"

#include <QRegularExpression>

#include <algorithm>

namespace Mermaid {
namespace Edits {

namespace {

Result fail(const QString &why)
{
    Result r;
    r.error = why;
    return r;
}

Result okResult(const QString &source)
{
    Result r;
    r.ok = true;
    r.source = source;
    return r;
}

// ---- shared gesture context (§20.2: spans from the AST, never regex) ----

struct Stmt {
    SourceSpan span;       // token range, absolute in `source`
    QString firstWord;
};

struct Ctx {
    bool ok = false;
    QString error;
    QString source;
    ParseResult pr;
    QList<Stmt> stmts;     // in source order, header included
    int errorsBefore = 0;
};

int frontmatterOffset(const QString &src)
{
    const QStringList lines = src.split(u'\n');
    if (lines.isEmpty() || lines.first().trimmed() != QLatin1String("---"))
        return 0;
    for (int i = 1; i < lines.size(); ++i) {
        if (lines.at(i).trimmed() == QLatin1String("---")) {
            int offset = 0;
            for (int j = 0; j <= i; ++j)
                offset += lines.at(j).size() + 1;
            return offset;
        }
    }
    return 0;
}

int errorCount(const ParseResult &pr)
{
    int n = 0;
    for (const Diagnostic &d : pr.diagnostics)
        if (d.severity == Diagnostic::Error)
            ++n;
    return n;
}

Ctx makeCtx(const QString &source)
{
    Ctx ctx;
    ctx.source = source;
    MermaidParser parser;
    ctx.pr = parser.parse(source);
    if (ctx.pr.type != DiagramType::Flowchart || !ctx.pr.supported) {
        ctx.error = QStringLiteral("On-diagram editing applies to flowcharts "
                                   "only");
        return ctx;
    }
    ctx.errorsBefore = errorCount(ctx.pr);

    const int base = frontmatterOffset(source);
    MermaidLexer lexer(source.mid(base));
    QList<Token> current;
    auto flush = [&]() {
        if (current.isEmpty())
            return;
        Stmt s;
        s.span = { base + current.first().offset,
                   current.last().offset + current.last().length
                       - current.first().offset };
        if (current.first().kind == Token::Word)
            s.firstWord = current.first().text;
        ctx.stmts.append(s);
        current.clear();
    };
    for (const Token &t : lexer.tokens()) {
        if (t.kind == Token::Sep)
            flush();
        else
            current.append(t);
    }
    flush();
    ctx.ok = true;
    return ctx;
}

// §20.2: after any gesture the source must reparse without new diagnostics.
Result postChecked(const Ctx &ctx, const QString &newSource,
                   const QString &newId = QString())
{
    MermaidParser parser;
    const ParseResult after = parser.parse(newSource);
    if (after.type != DiagramType::Flowchart
        || errorCount(after) > ctx.errorsBefore)
        return fail(QStringLiteral("The edit would leave the source invalid; "
                                   "refused"));
    Result r = okResult(newSource);
    r.newId = newId;
    return r;
}

const Node *findNode(const Ctx &ctx, const QString &id)
{
    const int i = ctx.pr.flowchart.indexOfNode(id);
    return i >= 0 ? &ctx.pr.flowchart.nodes.at(i) : nullptr;
}

bool stmtMentionsNode(const Stmt &s, const Node &n)
{
    for (const SourceSpan &ref : n.refSpans)
        if (ref.start >= s.span.start && ref.end() <= s.span.end())
            return true;
    return false;
}

// The whole-line removal range for a statement, taking its trailing newline.
// When another statement shares the same lines (`;`-separated), only the
// statement's own span plus one adjacent separator is removed.
QPair<int, int> removalRange(const Ctx &ctx, int stmtIndex)
{
    const Stmt &s = ctx.stmts.at(stmtIndex);
    int lineStart = s.span.start;
    while (lineStart > 0 && ctx.source.at(lineStart - 1) != u'\n')
        --lineStart;
    int lineEnd = s.span.end();
    while (lineEnd < ctx.source.size() && ctx.source.at(lineEnd) != u'\n')
        ++lineEnd;
    if (lineEnd < ctx.source.size())
        ++lineEnd;   // the newline goes with the line
    bool shared = false;
    for (int i = 0; i < ctx.stmts.size(); ++i) {
        if (i == stmtIndex)
            continue;
        const Stmt &o = ctx.stmts.at(i);
        if (o.span.end() > lineStart && o.span.start < lineEnd)
            shared = true;
    }
    if (!shared)
        return { lineStart, lineEnd };
    // Shared line: remove the span and one adjacent `;` separator.
    int start = s.span.start;
    int end = s.span.end();
    int probe = end;
    while (probe < ctx.source.size() && ctx.source.at(probe) == u' ')
        ++probe;
    if (probe < ctx.source.size() && ctx.source.at(probe) == u';') {
        end = probe + 1;
    } else {
        probe = start - 1;
        while (probe > 0 && ctx.source.at(probe) == u' ')
            --probe;
        if (probe >= 0 && ctx.source.at(probe) == u';')
            start = probe;
    }
    return { start, end };
}

QString removeRanges(const QString &source, QList<QPair<int, int>> ranges)
{
    std::sort(ranges.begin(), ranges.end(),
              [](const QPair<int, int> &a, const QPair<int, int> &b) {
                  return a.first > b.first;
              });
    QString out = source;
    int lastStart = out.size() + 1;
    for (const auto &range : ranges) {
        if (range.second > lastStart)
            continue;   // overlapping range already removed
        out.remove(range.first, range.second - range.first);
        lastStart = range.first;
    }
    return out;
}

// The leading whitespace of the line holding `offset`.
QString indentAt(const QString &source, int offset)
{
    int lineStart = offset;
    while (lineStart > 0 && source.at(lineStart - 1) != u'\n')
        --lineStart;
    QString indent;
    for (int i = lineStart; i < source.size(); ++i) {
        const QChar c = source.at(i);
        if (c == u' ' || c == u'\t')
            indent += c;
        else
            break;
    }
    return indent;
}

// Insert `statementText` as a new line after the statement at `stmtIndex`
// (before the §20.3 pos line, which is a comment and never a statement).
QString insertStatementAfter(const Ctx &ctx, int stmtIndex,
                             const QString &statementText)
{
    const Stmt &s = ctx.stmts.at(stmtIndex);
    int lineEnd = s.span.end();
    while (lineEnd < ctx.source.size() && ctx.source.at(lineEnd) != u'\n')
        ++lineEnd;
    const QString indent = indentAt(ctx.source, s.span.start);
    QString out = ctx.source;
    const QString insertion =
        QStringLiteral("\n") + indent + statementText;
    out.insert(lineEnd, insertion);
    return out;
}

// The bracket delimiters for each shape (the flow.jison vertex forms).
QPair<QString, QString> shapeDelimiters(NodeShape shape)
{
    switch (shape) {
    case NodeShape::Rect: return { "[", "]" };
    case NodeShape::RoundRect: return { "(", ")" };
    case NodeShape::Stadium: return { "([", "])" };
    case NodeShape::Subroutine: return { "[[", "]]" };
    case NodeShape::Cylinder: return { "[(", ")]" };
    case NodeShape::Circle: return { "((", "))" };
    case NodeShape::DoubleCircle: return { "(((", ")))" };
    case NodeShape::Ellipse: return { "(-", "-)" };
    case NodeShape::Rhombus: return { "{", "}" };
    case NodeShape::Hexagon: return { "{{", "}}" };
    case NodeShape::Parallelogram: return { "[/", "/]" };
    case NodeShape::ParallelogramAlt: return { "[\\", "\\]" };
    case NodeShape::Trapezoid: return { "[/", "\\]" };
    case NodeShape::TrapezoidAlt: return { "[\\", "/]" };
    case NodeShape::Odd: return { ">", "]" };
    }
    return { "[", "]" };
}

// Quote a label only when the raw text requires it (§20.4).
QString renderLabel(const QString &text, bool *needsRefusal)
{
    *needsRefusal = false;
    if (text.contains(u'"')) {
        *needsRefusal = true;   // flow.jison STR cannot hold a double quote
        return QString();
    }
    static const QRegularExpression unsafe(
        QStringLiteral("[\\[\\](){}<>|;&`]"));
    if (text.isEmpty() || text != text.trimmed()
        || unsafe.match(text).hasMatch())
        return QStringLiteral("\"%1\"").arg(text);
    return text;
}

// Rebuild an arrow token with a different stroke, preserving arrowheads,
// extra length, and any inline label.
QString edgeOpText(const Edge &e, EdgeStroke stroke)
{
    QString runChar = QStringLiteral("-");
    if (stroke == EdgeStroke::Thick)
        runChar = QStringLiteral("=");
    const int extra = qMax(0, e.minLen - 1);
    QString op;
    if (stroke == EdgeStroke::Dotted) {
        // `-.->` with extra dots for length; open form `-.-`.
        op = QStringLiteral("-") + QString(1 + extra, u'.')
             + QStringLiteral("-");
    } else {
        op = QString(2 + extra, runChar.at(0));
    }
    if (!e.label.isEmpty() && !e.pipeSpan.valid()) {
        // Inline label: `-- text -->` / `-. text .-` / `== text ==>` forms.
        QString head;
        if (stroke == EdgeStroke::Dotted)
            head = QStringLiteral(".-") + (e.arrowEnd ? QStringLiteral(">")
                                                      : QString());
        else
            head = QString(2, runChar.at(0))
                   + (e.arrowEnd ? QStringLiteral(">") : QString());
        QString tail = stroke == EdgeStroke::Dotted
            ? QStringLiteral("-.") : QString(2, runChar.at(0));
        if (e.arrowStart)
            tail.prepend(u'<');
        return tail + QStringLiteral(" ") + e.label + QStringLiteral(" ")
               + head;
    }
    if (e.arrowEnd)
        op += QStringLiteral(">");
    if (e.arrowStart)
        op.prepend(u'<');
    return op;
}

// The full source text of a node reference within a statement: the id plus an
// immediately attached bracket construct / `:::class` suffix. Falls back to
// the bare id when the reference sits inside an `&` group.
QString refTextInStatement(const Ctx &ctx, const Node &n, const Stmt &s,
                           int stopAt)
{
    for (const SourceSpan &ref : n.refSpans) {
        if (ref.start < s.span.start || ref.end() > s.span.end())
            continue;
        int end = ref.end();
        const int limit = stopAt > 0 ? qMin(stopAt, s.span.end())
                                     : s.span.end();
        // Extend through directly attached construct text until whitespace.
        while (end < limit && !ctx.source.at(end).isSpace())
            ++end;
        // Bracketed labels may contain spaces: extend through a bracket
        // construct that starts immediately at `end` boundary.
        const QString segment = ctx.source.mid(ref.start, end - ref.start);
        if (segment.contains(u'&'))
            return n.id;
        // If a bracket opened but did not close (label with spaces), extend
        // to the closing bracket.
        auto balance = [&](const QString &sgmt) {
            int open = 0;
            for (const QChar c : sgmt) {
                if (c == u'[' || c == u'(' || c == u'{')
                    ++open;
                else if (c == u']' || c == u')' || c == u'}')
                    --open;
            }
            return open;
        };
        QString text = segment;
        int extendedEnd = end;
        while (balance(text) > 0 && extendedEnd < limit) {
            ++extendedEnd;
            text = ctx.source.mid(ref.start, extendedEnd - ref.start);
        }
        if (balance(text) != 0 || text.contains(u'&'))
            return n.id;
        return text;
    }
    return n.id;
}

// The canonical §20.3 pos line for the given positions. Snapshot
// serialization is deterministic: caller order (source order) is preserved,
// coordinates are rounded integers, and the plugin's entry pattern
// `id=x,y` is emitted without dimension suffixes.
QString posLineFor(const QList<QPair<QString, QPointF>> &positions)
{
    QString line = QStringLiteral("%% mermaid-flow:pos");
    for (const auto &pos : positions) {
        const QString &id = pos.first;
        if (id.isEmpty() || id.contains(u'=') || id.contains(u' ')
            || id.contains(u'\t'))
            continue;   // an id the plugin grammar cannot round-trip
        line += QStringLiteral(" %1=%2,%3")
                    .arg(id)
                    .arg(qRound(pos.second.x()))
                    .arg(qRound(pos.second.y()));
    }
    return line;
}

} // namespace

Result writeArrangement(const QString &source,
                        const QList<QPair<QString, QPointF>> &positions)
{
    MermaidParser parser;
    const ParseResult pr = parser.parse(source);
    if (pr.type != DiagramType::Flowchart || !pr.supported)
        return fail(QStringLiteral("Manual arrangement applies to flowcharts "
                                   "only"));
    if (positions.isEmpty())
        return fail(QStringLiteral("No node positions to write"));

    const QString line = posLineFor(positions);
    QString out = source;
    if (pr.flowchart.hasPosLine && pr.flowchart.posLineSpan.valid()) {
        // Replace only the pos line; every other byte is untouched (§20.2).
        out.replace(pr.flowchart.posLineSpan.start,
                    pr.flowchart.posLineSpan.length, line);
    } else if (out.endsWith(u'\n')) {
        out += line + QStringLiteral("\n");
    } else if (out.isEmpty()) {
        out = line;
    } else {
        out += QStringLiteral("\n") + line;
    }
    return okResult(out);
}

// ---- §20.4 semantic gestures ----

Result setNodeLabel(const QString &source, const QString &nodeId,
                    const QString &newLabel)
{
    Ctx ctx = makeCtx(source);
    if (!ctx.ok)
        return fail(ctx.error);
    const Node *n = findNode(ctx, nodeId);
    if (!n)
        return fail(QStringLiteral("Unknown node: %1").arg(nodeId));
    bool refuse = false;
    const QString rendered = renderLabel(newLabel, &refuse);
    if (refuse)
        return fail(QStringLiteral("Labels cannot contain a double quote"));
    QString out = source;
    if (n->labelSpan.valid()) {
        out.replace(n->labelSpan.start, n->labelSpan.length, rendered);
    } else if (n->idSpan.valid()) {
        out.insert(n->idSpan.end(), QStringLiteral("[") + rendered
                                        + QStringLiteral("]"));
    } else {
        return fail(QStringLiteral("The node has no editable declaration"));
    }
    return postChecked(ctx, out);
}

Result setNodeShape(const QString &source, const QString &nodeId,
                    NodeShape shape)
{
    Ctx ctx = makeCtx(source);
    if (!ctx.ok)
        return fail(ctx.error);
    const Node *n = findNode(ctx, nodeId);
    if (!n)
        return fail(QStringLiteral("Unknown node: %1").arg(nodeId));
    const auto delims = shapeDelimiters(shape);
    QString out = source;
    if (n->shapeSpan.valid() && n->labelSpan.valid()) {
        const QString rawLabel =
            source.mid(n->labelSpan.start, n->labelSpan.length);
        out.replace(n->shapeSpan.start, n->shapeSpan.length,
                    delims.first + rawLabel + delims.second);
    } else if (n->idSpan.valid()) {
        if (shape == NodeShape::Rect)
            return okResult(source);   // already the default: a no-op
        out.insert(n->idSpan.end(),
                   delims.first + n->label + delims.second);
    } else {
        return fail(QStringLiteral("The node has no editable declaration"));
    }
    return postChecked(ctx, out);
}

Result renameNode(const QString &source, const QString &oldId,
                  const QString &newId)
{
    Ctx ctx = makeCtx(source);
    if (!ctx.ok)
        return fail(ctx.error);
    const Node *n = findNode(ctx, oldId);
    if (!n)
        return fail(QStringLiteral("Unknown node: %1").arg(oldId));
    static const QRegularExpression valid(
        QStringLiteral("^[A-Za-z0-9_][A-Za-z0-9_-]*$"));
    if (!valid.match(newId).hasMatch())
        return fail(QStringLiteral("Node ids use letters, digits, `_`, "
                                   "and `-`"));
    static const QStringList kReserved{
        "subgraph", "end", "direction", "classDef", "class", "style",
        "click", "linkStyle", "flowchart", "graph",
    };
    if (kReserved.contains(newId))
        return fail(QStringLiteral("`%1` is a reserved word").arg(newId));
    if (findNode(ctx, newId))
        return fail(QStringLiteral("A node named %1 already exists")
                        .arg(newId));
    // Replace every AST-known reference span, last first (§20.4).
    QList<SourceSpan> refs = n->refSpans;
    std::sort(refs.begin(), refs.end(),
              [](const SourceSpan &a, const SourceSpan &b) {
                  return a.start > b.start;
              });
    QString out = source;
    int lastStart = out.size() + 1;
    for (const SourceSpan &ref : refs) {
        if (ref.end() > lastStart)
            continue;   // duplicate span (e.g. recorded twice)
        out.replace(ref.start, ref.length, newId);
        lastStart = ref.start;
    }
    return postChecked(ctx, out);
}

Result deleteNode(const QString &source, const QString &nodeId)
{
    Ctx ctx = makeCtx(source);
    if (!ctx.ok)
        return fail(ctx.error);
    const Node *n = findNode(ctx, nodeId);
    if (!n)
        return fail(QStringLiteral("Unknown node: %1").arg(nodeId));

    QList<QPair<int, int>> removals;
    for (int i = 0; i < ctx.stmts.size(); ++i) {
        const Stmt &s = ctx.stmts.at(i);
        if (!stmtMentionsNode(s, *n))
            continue;
        const QString kw = s.firstWord;
        if (kw == QLatin1String("class") || kw == QLatin1String("style")) {
            // §20.2: refused rather than applied approximately when the
            // statement styles other nodes too.
            bool others = false;
            for (const Node &other : ctx.pr.flowchart.nodes) {
                if (other.id == n->id)
                    continue;
                if (stmtMentionsNode(s, other))
                    others = true;
            }
            if (others)
                return fail(QStringLiteral("%1 is styled together with other "
                                           "nodes; edit the source instead")
                                .arg(nodeId));
            removals.append(removalRange(ctx, i));
            continue;
        }
        if (kw == QLatin1String("subgraph"))
            return fail(QStringLiteral("%1 names a subgraph; edit the source "
                                       "instead").arg(nodeId));
        if (kw == QLatin1String("classDef") || kw == QLatin1String("click")
            || kw == QLatin1String("linkStyle"))
            return fail(QStringLiteral("%1 is referenced from retained "
                                       "syntax; edit the source instead")
                            .arg(nodeId));
        // A declaration or edge chain statement: removed whole (§20.4).
        removals.append(removalRange(ctx, i));
    }
    if (removals.isEmpty())
        return fail(QStringLiteral("No statements declare %1").arg(nodeId));
    const QString out = removeRanges(source, removals);
    return postChecked(ctx, out);
}

Result deleteEdge(const QString &source, int edgeIndex)
{
    Ctx ctx = makeCtx(source);
    if (!ctx.ok)
        return fail(ctx.error);
    const QList<Edge> &edges = ctx.pr.flowchart.edges;
    if (edgeIndex < 0 || edgeIndex >= edges.size())
        return fail(QStringLiteral("Unknown edge"));
    const Edge &victim = edges.at(edgeIndex);
    if (!victim.stmtSpan.valid())
        return fail(QStringLiteral("The edge has no locatable statement"));

    // Chain mates: edges sharing the statement.
    QList<int> mates;
    for (int i = 0; i < edges.size(); ++i)
        if (edges.at(i).stmtSpan.start == victim.stmtSpan.start
            && edges.at(i).stmtSpan.length == victim.stmtSpan.length)
            mates.append(i);

    int stmtIndex = -1;
    for (int i = 0; i < ctx.stmts.size(); ++i)
        if (ctx.stmts.at(i).span.start == victim.stmtSpan.start)
            stmtIndex = i;
    if (stmtIndex < 0)
        return fail(QStringLiteral("The edge statement cannot be located"));

    if (mates.size() == 1) {
        const QString out =
            removeRanges(source, { removalRange(ctx, stmtIndex) });
        return postChecked(ctx, out);
    }

    // §20.4: a chained statement is split so unaffected links survive.
    const Stmt &s = ctx.stmts.at(stmtIndex);
    const QString indent = indentAt(source, s.span.start);
    QStringList lines;
    for (const int i : mates) {
        if (i == edgeIndex)
            continue;
        const Edge &e = edges.at(i);
        const Node *from = findNode(ctx, e.from);
        const Node *to = findNode(ctx, e.to);
        if (!from || !to)
            return fail(QStringLiteral("The chain cannot be split safely"));
        const QString fromText =
            refTextInStatement(ctx, *from, s, e.opSpan.start);
        // The to-ref stops at the next edge's operator in the statement.
        int stopAt = -1;
        for (const int j : mates) {
            if (edges.at(j).opSpan.start > e.opSpan.start
                && (stopAt < 0 || edges.at(j).opSpan.start < stopAt))
                stopAt = edges.at(j).opSpan.start;
        }
        const QString toText = refTextInStatement(ctx, *to, s, stopAt);
        QString op = source.mid(e.opSpan.start, e.opSpan.length);
        if (e.pipeSpan.valid())
            op += source.mid(e.pipeSpan.start, e.pipeSpan.length);
        lines << fromText + QStringLiteral(" ") + op.trimmed()
                     + QStringLiteral(" ") + toText;
    }
    QString replacement;
    for (int i = 0; i < lines.size(); ++i) {
        if (i > 0)
            replacement += QStringLiteral("\n") + indent;
        replacement += lines.at(i);
    }
    QString out = source;
    out.replace(s.span.start, s.span.length, replacement);
    return postChecked(ctx, out);
}

Result setEdgeStroke(const QString &source, int edgeIndex, EdgeStroke stroke)
{
    Ctx ctx = makeCtx(source);
    if (!ctx.ok)
        return fail(ctx.error);
    const QList<Edge> &edges = ctx.pr.flowchart.edges;
    if (edgeIndex < 0 || edgeIndex >= edges.size())
        return fail(QStringLiteral("Unknown edge"));
    const Edge &e = edges.at(edgeIndex);
    if (!e.opSpan.valid())
        return fail(QStringLiteral("The edge has no locatable arrow"));
    if (e.invisible)
        return fail(QStringLiteral("Invisible links have no style"));
    QString out = source;
    out.replace(e.opSpan.start, e.opSpan.length, edgeOpText(e, stroke));
    return postChecked(ctx, out);
}

Result insertEdge(const QString &source, const QString &fromId,
                  const QString &toId)
{
    Ctx ctx = makeCtx(source);
    if (!ctx.ok)
        return fail(ctx.error);
    const Node *from = findNode(ctx, fromId);
    const Node *to = findNode(ctx, toId);
    if (!from || !to)
        return fail(QStringLiteral("Both ends must be existing nodes"));
    // Insert after the last statement mentioning the source node (§20.4).
    int anchor = -1;
    for (int i = 0; i < ctx.stmts.size(); ++i)
        if (stmtMentionsNode(ctx.stmts.at(i), *from))
            anchor = i;
    if (anchor < 0)
        anchor = ctx.stmts.size() - 1;
    if (anchor < 0)
        return fail(QStringLiteral("Nowhere to insert the edge"));
    const QString out = insertStatementAfter(
        ctx, anchor,
        fromId + QStringLiteral(" --> ") + toId);
    return postChecked(ctx, out);
}

Result quickAddNode(const QString &source, const QString &fromId)
{
    Ctx ctx = makeCtx(source);
    if (!ctx.ok)
        return fail(ctx.error);
    const Node *from = findNode(ctx, fromId);
    if (!from)
        return fail(QStringLiteral("Unknown node: %1").arg(fromId));
    QString newId;
    for (int k = 1; k < kMaxNodes; ++k) {
        newId = QStringLiteral("node%1").arg(k);
        if (!findNode(ctx, newId))
            break;
    }
    int anchor = -1;
    for (int i = 0; i < ctx.stmts.size(); ++i)
        if (stmtMentionsNode(ctx.stmts.at(i), *from))
            anchor = i;
    if (anchor < 0)
        return fail(QStringLiteral("Nowhere to insert the node"));
    const QString out = insertStatementAfter(
        ctx, anchor,
        fromId + QStringLiteral(" --> ") + newId
            + QStringLiteral("[New node]"));
    return postChecked(ctx, out, newId);
}

Result setNodeStyle(const QString &source, const QString &nodeId,
                    const QColor &fill, const QColor &stroke)
{
    Ctx ctx = makeCtx(source);
    if (!ctx.ok)
        return fail(ctx.error);
    const Node *n = findNode(ctx, nodeId);
    if (!n)
        return fail(QStringLiteral("Unknown node: %1").arg(nodeId));
    if (!fill.isValid() && !stroke.isValid())
        return fail(QStringLiteral("No style to apply"));

    QString decl;
    if (fill.isValid())
        decl += QStringLiteral("fill:%1").arg(fill.name());
    if (stroke.isValid())
        decl += (decl.isEmpty() ? QString() : QStringLiteral(","))
                + QStringLiteral("stroke:%1").arg(stroke.name());

    // Reuse a Kvit classDef with the same declarations; never rewrite an
    // existing one (§20.4).
    const QHash<QString, ClassDef> &defs = ctx.pr.flowchart.classDefs;
    QString className;
    for (auto it = defs.constBegin(); it != defs.constEnd(); ++it) {
        if (!it.key().startsWith(QLatin1String("kvit_style_")))
            continue;
        const ClassDef &d = it.value();
        const bool fillMatch = d.hasFill == fill.isValid()
            && (!fill.isValid() || d.fill == fill);
        const bool strokeMatch = d.hasStroke == stroke.isValid()
            && (!stroke.isValid() || d.stroke == stroke);
        if (fillMatch && strokeMatch) {
            className = it.key();
            break;
        }
    }
    QString newDefLine;
    if (className.isEmpty()) {
        for (int k = 1; k < kMaxNodes; ++k) {
            className = QStringLiteral("kvit_style_%1").arg(k);
            if (!defs.contains(className))
                break;
        }
        newDefLine = QStringLiteral("classDef %1 %2").arg(className, decl);
    }

    // Update an existing single-node `class <node> kvit_style_*` statement in
    // place, or insert a new class statement after the node's last mention.
    QString out = source;
    bool updated = false;
    for (int i = ctx.stmts.size() - 1; i >= 0 && !updated; --i) {
        const Stmt &s = ctx.stmts.at(i);
        if (s.firstWord != QLatin1String("class"))
            continue;
        const QString text = source.mid(s.span.start, s.span.length);
        static const QRegularExpression classStmt(
            QStringLiteral("^class\\s+(\\S+)\\s+(kvit_style_\\d+)$"));
        const auto m = classStmt.match(text);
        if (!m.hasMatch() || m.captured(1) != nodeId)
            continue;
        out.replace(s.span.start, s.span.length,
                    QStringLiteral("class %1 %2").arg(nodeId, className));
        updated = true;
    }
    if (!updated) {
        int anchor = -1;
        for (int i = 0; i < ctx.stmts.size(); ++i)
            if (stmtMentionsNode(ctx.stmts.at(i), *n))
                anchor = i;
        if (anchor < 0)
            return fail(QStringLiteral("Nowhere to apply the style"));
        Ctx outCtx = makeCtx(out);
        Q_UNUSED(outCtx);
        out = insertStatementAfter(ctx, anchor,
                                   QStringLiteral("class %1 %2")
                                       .arg(nodeId, className));
    }
    if (!newDefLine.isEmpty()) {
        // The classDef lands at the end of the body (before any pos line).
        Ctx defCtx = makeCtx(out);
        if (!defCtx.ok)
            return fail(defCtx.error);
        if (defCtx.pr.flowchart.hasPosLine
            && defCtx.pr.flowchart.posLineSpan.valid()) {
            int at = defCtx.pr.flowchart.posLineSpan.start;
            out.insert(at, newDefLine + QStringLiteral("\n"));
        } else if (out.endsWith(u'\n')) {
            out += newDefLine + QStringLiteral("\n");
        } else {
            out += QStringLiteral("\n") + newDefLine;
        }
    }
    return postChecked(ctx, out);
}

Result reparentNode(const QString &source, const QString &nodeId,
                    const QString &subgraphId)
{
    Ctx ctx = makeCtx(source);
    if (!ctx.ok)
        return fail(ctx.error);
    const Node *n = findNode(ctx, nodeId);
    if (!n)
        return fail(QStringLiteral("Unknown node: %1").arg(nodeId));

    // The node's standalone declaration statement: a chain statement that
    // mentions no other node. Also count how many statements mention the
    // node in total (edge chains keep subgraph membership alive, §20.2).
    int declIndex = -1;
    int mentions = 0;
    for (int i = 0; i < ctx.stmts.size(); ++i) {
        const Stmt &s = ctx.stmts.at(i);
        if (!s.firstWord.isEmpty()
            && (s.firstWord == QLatin1String("subgraph")
                || s.firstWord == QLatin1String("end")
                || s.firstWord == QLatin1String("class")
                || s.firstWord == QLatin1String("classDef")
                || s.firstWord == QLatin1String("style")
                || s.firstWord == QLatin1String("direction")))
            continue;
        if (!stmtMentionsNode(s, *n))
            continue;
        ++mentions;
        bool others = false;
        for (const Node &other : ctx.pr.flowchart.nodes)
            if (other.id != n->id && stmtMentionsNode(s, other))
                others = true;
        if (!others && declIndex < 0)
            declIndex = i;
    }
    // Membership already inside a subgraph that stems from edge statements
    // cannot be moved by relocating a declaration alone: refuse rather than
    // apply approximately (§20.2).
    bool memberOfSubgraph = false;
    for (const Subgraph &sg : ctx.pr.flowchart.subgraphs)
        if (sg.nodeIds.contains(nodeId) && sg.id != subgraphId)
            memberOfSubgraph = true;
    if (memberOfSubgraph && (declIndex < 0 || mentions > 1))
        return fail(QStringLiteral(
            "%1 is placed by edge statements inside a subgraph; edit the "
            "source instead").arg(nodeId));

    // Locate the target subgraph's `end` (or the body end for top level).
    const QString declText = declIndex >= 0
        ? source.mid(ctx.stmts.at(declIndex).span.start,
                     ctx.stmts.at(declIndex).span.length)
        : refTextInStatement(ctx, *n, Stmt{ SourceSpan{ 0, int(source.size()) },
                                            QString() }, -1);

    QList<QPair<int, int>> removals;
    if (declIndex >= 0)
        removals.append(removalRange(ctx, declIndex));

    QString out;
    if (subgraphId.isEmpty()) {
        // Out to top level: append at the body end.
        out = removeRanges(source, removals);
        if (out.endsWith(u'\n'))
            out += declText + QStringLiteral("\n");
        else
            out += QStringLiteral("\n") + declText;
    } else {
        // Into the subgraph: insert before its matching `end`.
        int depth = 0;
        int openIndex = -1;
        int endIndex = -1;
        for (int i = 0; i < ctx.stmts.size(); ++i) {
            const Stmt &s = ctx.stmts.at(i);
            if (s.firstWord == QLatin1String("subgraph")) {
                const QString text =
                    source.mid(s.span.start, s.span.length);
                if (openIndex < 0
                    && text.contains(QRegularExpression(
                        QStringLiteral("^subgraph\\s+%1(\\s|\\[|$)")
                            .arg(QRegularExpression::escape(subgraphId))))) {
                    openIndex = i;
                    depth = 0;
                } else if (openIndex >= 0) {
                    ++depth;
                }
            } else if (s.firstWord == QLatin1String("end")
                       && openIndex >= 0) {
                if (depth == 0) {
                    endIndex = i;
                    break;
                }
                --depth;
            }
        }
        if (openIndex < 0 || endIndex < 0)
            return fail(QStringLiteral("Unknown subgraph: %1")
                            .arg(subgraphId));
        const Stmt &endStmt = ctx.stmts.at(endIndex);
        int lineStart = endStmt.span.start;
        while (lineStart > 0 && source.at(lineStart - 1) != u'\n')
            --lineStart;
        const QString innerIndent =
            indentAt(source, ctx.stmts.at(openIndex).span.start)
            + QStringLiteral("  ");
        removals.append({ lineStart, lineStart });   // no-op guard
        out = source;
        out.insert(lineStart, innerIndent + declText + QStringLiteral("\n"));
        // Remove the old declaration (adjust for the insertion shift).
        if (declIndex >= 0) {
            QPair<int, int> range = removalRange(ctx, declIndex);
            const int shift = range.first >= lineStart
                ? innerIndent.size() + declText.size() + 1 : 0;
            out.remove(range.first + shift, range.second - range.first);
        }
    }
    return postChecked(ctx, out);
}

Result reorderNode(const QString &source, const QString &nodeId, int delta)
{
    Ctx ctx = makeCtx(source);
    if (!ctx.ok)
        return fail(ctx.error);
    if (ctx.pr.flowchart.hasPosLine)
        return fail(QStringLiteral("Reordering applies in auto layout only"));
    const Node *n = findNode(ctx, nodeId);
    if (!n)
        return fail(QStringLiteral("Unknown node: %1").arg(nodeId));

    // The first-encounter statement must be a standalone declaration.
    int declIndex = -1;
    for (int i = 0; i < ctx.stmts.size(); ++i) {
        if (!stmtMentionsNode(ctx.stmts.at(i), *n))
            continue;
        bool others = false;
        for (const Node &other : ctx.pr.flowchart.nodes)
            if (other.id != n->id
                && stmtMentionsNode(ctx.stmts.at(i), other))
                others = true;
        if (others)
            return fail(QStringLiteral("%1 is first declared in a chain; "
                                       "edit the source instead").arg(nodeId));
        declIndex = i;
        break;
    }
    if (declIndex < 0)
        return fail(QStringLiteral("No standalone declaration for %1")
                        .arg(nodeId));
    const int other = declIndex + (delta < 0 ? -1 : 1);
    if (other <= 0 || other >= ctx.stmts.size())
        return fail(QStringLiteral("Nothing to swap with"));
    const Stmt &a = ctx.stmts.at(qMin(declIndex, other));
    const Stmt &b = ctx.stmts.at(qMax(declIndex, other));
    const QString textA = source.mid(a.span.start, a.span.length);
    const QString textB = source.mid(b.span.start, b.span.length);
    QString out = source;
    out.replace(b.span.start, b.span.length, textA);
    out.replace(a.span.start, a.span.length, textB);
    return postChecked(ctx, out);
}

// ---- §20.4 sequence reordering (Phase 5d) ----

namespace {

// Swap two non-overlapping spans' text; the later span is replaced first so
// offsets stay valid.
QString swapSpans(const QString &source, const SourceSpan &a,
                  const SourceSpan &b)
{
    const SourceSpan &first = a.start < b.start ? a : b;
    const SourceSpan &second = a.start < b.start ? b : a;
    const QString textFirst = source.mid(first.start, first.length);
    const QString textSecond = source.mid(second.start, second.length);
    QString out = source;
    out.replace(second.start, second.length, textFirst);
    out.replace(first.start, first.length, textSecond);
    return out;
}

Result seqPostChecked(const QString &oldSource, const QString &newSource)
{
    MermaidParser parser;
    const int before = errorCount(parser.parse(oldSource));
    const ParseResult after = parser.parse(newSource);
    if (after.type != DiagramType::Sequence || errorCount(after) > before)
        return fail(QStringLiteral("The edit would leave the source invalid; "
                                   "refused"));
    return okResult(newSource);
}

} // namespace

Result moveSequenceMessage(const QString &source, int eventIndex, int delta)
{
    MermaidParser parser;
    const ParseResult pr = parser.parse(source);
    if (pr.type != DiagramType::Sequence || !pr.supported)
        return fail(QStringLiteral("Message reordering applies to sequence "
                                   "diagrams only"));
    const QList<SeqEvent> &events = pr.sequence.events;
    if (eventIndex < 0 || eventIndex >= events.size()
        || events.at(eventIndex).kind != SeqEvent::Message)
        return fail(QStringLiteral("No message selected"));
    const SeqEvent &moving = events.at(eventIndex);
    if (!moving.srcSpan.valid())
        return fail(QStringLiteral("The message cannot be located"));
    // The adjacent message in the requested direction.
    int other = -1;
    for (int i = eventIndex + (delta < 0 ? -1 : 1);
         i >= 0 && i < events.size(); i += (delta < 0 ? -1 : 1)) {
        if (events.at(i).kind == SeqEvent::Message) {
            other = i;
            break;
        }
    }
    if (other < 0)
        return fail(QStringLiteral("The message is already at the %1")
                        .arg(delta < 0 ? QStringLiteral("top")
                                       : QStringLiteral("bottom")));
    const SeqEvent &neighbour = events.at(other);
    if (!neighbour.srcSpan.valid()
        || neighbour.srcSpan.start == moving.srcSpan.start)
        return fail(QStringLiteral("The messages share a statement; edit the "
                                   "source instead"));
    return seqPostChecked(source,
                          swapSpans(source, moving.srcSpan,
                                    neighbour.srcSpan));
}

Result moveSequenceParticipant(const QString &source, const QString &id,
                               int delta)
{
    MermaidParser parser;
    const ParseResult pr = parser.parse(source);
    if (pr.type != DiagramType::Sequence || !pr.supported)
        return fail(QStringLiteral("Participant reordering applies to "
                                   "sequence diagrams only"));
    const QList<SeqParticipant> &parts = pr.sequence.participants;
    const int i = pr.sequence.indexOfParticipant(id);
    if (i < 0)
        return fail(QStringLiteral("Unknown participant: %1").arg(id));
    const int j = i + (delta < 0 ? -1 : 1);
    if (j < 0 || j >= parts.size())
        return fail(QStringLiteral("The participant is already at the edge"));
    auto isDeclaration = [&](const SeqParticipant &p) {
        if (!p.srcSpan.valid())
            return false;
        const QString text = source.mid(p.srcSpan.start, p.srcSpan.length);
        return text.startsWith(QLatin1String("participant"),
                               Qt::CaseInsensitive)
               || text.startsWith(QLatin1String("actor"),
                                  Qt::CaseInsensitive);
    };
    if (!isDeclaration(parts.at(i)) || !isDeclaration(parts.at(j)))
        return fail(QStringLiteral("Both participants need explicit "
                                   "`participant` declarations to reorder"));
    return seqPostChecked(source,
                          swapSpans(source, parts.at(i).srcSpan,
                                    parts.at(j).srcSpan));
}

Result resetArrangement(const QString &source)
{
    MermaidParser parser;
    const ParseResult pr = parser.parse(source);
    if (!pr.flowchart.hasPosLine || !pr.flowchart.posLineSpan.valid())
        return okResult(source);
    const SourceSpan span = pr.flowchart.posLineSpan;
    int start = span.start;
    int end = span.end();
    if (end < source.size() && source.at(end) == u'\n')
        ++end;   // take the line's newline with it
    else if (start > 0 && source.at(start - 1) == u'\n')
        --start; // last line without a trailing newline: drop the break before
    QString out = source;
    out.remove(start, end - start);
    return okResult(out);
}

} // namespace Edits
} // namespace Mermaid
