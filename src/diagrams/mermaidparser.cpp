// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
#include "mermaidparser.h"
#include "mermaidlexer.h"
#include "mermaidsequence.h"
#include "mermaidclass.h"
#include "mermaidstate.h"
#include "mermaider.h"

#include <QStringList>

namespace Mermaid {

void parseStyleDeclarations(ClassDef &def, const QString &styles)
{
    const QStringList decls = styles.split(u',', Qt::SkipEmptyParts);
    for (const QString &declRaw : decls) {
        const QString decl = declRaw.trimmed();
        const int colon = decl.indexOf(u':');
        if (colon < 0)
            continue;
        const QString key = decl.left(colon).trimmed().toLower();
        const QString val = decl.mid(colon + 1).trimmed();
        if (key == QLatin1String("fill")) {
            QColor c(val);
            if (c.isValid()) { def.fill = c; def.hasFill = true; }
        } else if (key == QLatin1String("stroke")) {
            QColor c(val);
            if (c.isValid()) { def.stroke = c; def.hasStroke = true; }
        } else if (key == QLatin1String("stroke-width")) {
            QString num = val;
            num.remove(QLatin1String("px"));
            bool ok = false;
            const double w = num.trimmed().toDouble(&ok);
            if (ok) def.strokeWidth = w;
        } else if (key == QLatin1String("stroke-dasharray")) {
            def.dashed = !val.isEmpty();
        } else if (key == QLatin1String("font-weight")) {
            def.bold = val.contains(QLatin1String("bold"));
        }
    }
}

namespace {

// Strip a leading YAML frontmatter block (`---` ... `---`) before lexing so its
// `---` fences are never mis-read as edges. Only the allowlisted `title` is
// used; other keys are ignored (diagrams-prd.md §8.2).
QString stripFrontmatter(const QString &src, QString *title, int *bodyOffset)
{
    *bodyOffset = 0;
    const QStringList lines = src.split(u'\n');
    if (lines.isEmpty() || lines.first().trimmed() != QLatin1String("---"))
        return src;
    int close = -1;
    for (int i = 1; i < lines.size(); ++i) {
        if (lines.at(i).trimmed() == QLatin1String("---")) {
            close = i;
            break;
        }
    }
    if (close < 0)
        return src;   // unterminated: leave it to the lexer
    if (title) {
        for (int i = 1; i < close; ++i) {
            const QString l = lines.at(i).trimmed();
            if (l.startsWith(QLatin1String("title:")))
                *title = l.mid(6).trimmed();
        }
    }
    // The body is a literal suffix of the source, so §20 spans recorded
    // against it shift by one constant.
    int offset = 0;
    for (int i = 0; i <= close; ++i)
        offset += lines.at(i).size() + 1;
    *bodyOffset = offset;
    return src.mid(offset);
}

Direction directionFromWord(const QString &w, bool *ok)
{
    *ok = true;
    if (w == QLatin1String("TB") || w == QLatin1String("TD")) return Direction::TB;
    if (w == QLatin1String("BT")) return Direction::BT;
    if (w == QLatin1String("LR")) return Direction::LR;
    if (w == QLatin1String("RL")) return Direction::RL;
    // flow.jison header direction symbols (flowDb setDirection mapping).
    if (w == QLatin1String(">")) return Direction::LR;
    if (w == QLatin1String("<")) return Direction::RL;
    if (w == QLatin1String("^")) return Direction::BT;
    if (w == QLatin1String("v")) return Direction::TB;
    if (w == QLatin1String("BR")) return Direction::TB;
    *ok = false;
    return Direction::TB;
}

// Map a header keyword to a family. `flowchart-elk` and `swimlane-beta` are
// flow-grammar headers selecting an alternate layout engine upstream; Kvit
// parses them as flowcharts and warns that its own layered layout is used.
DiagramType familyFromHeader(const QString &word)
{
    if (word == QLatin1String("flowchart") || word == QLatin1String("graph")
        || word == QLatin1String("flowchart-elk")
        || word == QLatin1String("swimlane-beta"))
        return DiagramType::Flowchart;
    if (word.startsWith(QLatin1String("sequenceDiagram"), Qt::CaseInsensitive))
        return DiagramType::Sequence;
    if (word.startsWith(QLatin1String("classDiagram")))
        return DiagramType::Class;
    if (word.startsWith(QLatin1String("stateDiagram")))
        return DiagramType::State;
    if (word.startsWith(QLatin1String("erDiagram")))
        return DiagramType::Er;
    return DiagramType::Unsupported;
}

// Map an `@{ shape: name }` shape name (the v11 extended-shape registry) onto
// Kvit's shape set. Unknown names keep Rect; the caller warns.
NodeShape shapeFromName(const QString &raw, bool *known)
{
    const QString n = raw.trimmed().toLower();
    *known = true;
    if (n == QLatin1String("rect") || n == QLatin1String("rectangle")
        || n == QLatin1String("proc") || n == QLatin1String("process")
        || n == QLatin1String("sq") || n == QLatin1String("square"))
        return NodeShape::Rect;
    if (n == QLatin1String("rounded") || n == QLatin1String("event"))
        return NodeShape::RoundRect;
    if (n == QLatin1String("stadium") || n == QLatin1String("pill")
        || n == QLatin1String("terminal") || n == QLatin1String("start")
        || n == QLatin1String("stop"))
        return NodeShape::Stadium;
    if (n == QLatin1String("subroutine") || n == QLatin1String("subproc")
        || n == QLatin1String("fr-rect") || n == QLatin1String("framed-rectangle")
        || n == QLatin1String("subprocess"))
        return NodeShape::Subroutine;
    if (n == QLatin1String("cyl") || n == QLatin1String("cylinder")
        || n == QLatin1String("db") || n == QLatin1String("database"))
        return NodeShape::Cylinder;
    if (n == QLatin1String("circle") || n == QLatin1String("circ"))
        return NodeShape::Circle;
    if (n == QLatin1String("dbl-circ") || n == QLatin1String("double-circle"))
        return NodeShape::DoubleCircle;
    if (n == QLatin1String("diam") || n == QLatin1String("diamond")
        || n == QLatin1String("decision") || n == QLatin1String("question"))
        return NodeShape::Rhombus;
    if (n == QLatin1String("hex") || n == QLatin1String("hexagon")
        || n == QLatin1String("prepare"))
        return NodeShape::Hexagon;
    if (n == QLatin1String("lean-r") || n == QLatin1String("lean-right")
        || n == QLatin1String("in-out"))
        return NodeShape::Parallelogram;
    if (n == QLatin1String("lean-l") || n == QLatin1String("lean-left")
        || n == QLatin1String("out-in"))
        return NodeShape::ParallelogramAlt;
    if (n == QLatin1String("trap-b") || n == QLatin1String("trapezoid-bottom")
        || n == QLatin1String("trap") || n == QLatin1String("trapezoid")
        || n == QLatin1String("priority"))
        return NodeShape::Trapezoid;
    if (n == QLatin1String("trap-t") || n == QLatin1String("trapezoid-top")
        || n == QLatin1String("inv-trapezoid") || n == QLatin1String("manual"))
        return NodeShape::TrapezoidAlt;
    if (n == QLatin1String("odd") || n == QLatin1String("flag"))
        return NodeShape::Odd;
    if (n == QLatin1String("ellipse"))
        return NodeShape::Ellipse;
    *known = false;
    return NodeShape::Rect;
}


// Scan the body for `%% mermaid-flow:pos …` arrangement lines (§20.3). The
// first recognized line wins; later ones are ignored with a diagnostic.
// Individually malformed entries are skipped; the plugin's optional
// `,width,height` suffix parses and is dropped (node size always derives from
// label metrics and shape).
void parsePosLines(const QString &body, int baseOffset, ParseResult &result)
{
    FlowchartAst &ast = result.flowchart;
    static const QLatin1String kPrefix("mermaid-flow:pos");
    int lineStart = 0;
    int lineNo = 0;
    while (lineStart <= body.size()) {
        ++lineNo;
        int lineEnd = body.indexOf(u'\n', lineStart);
        if (lineEnd < 0)
            lineEnd = body.size();
        const QString line = body.mid(lineStart, lineEnd - lineStart);
        const QString trimmed = line.trimmed();
        if (trimmed.startsWith(QLatin1String("%%"))
            && trimmed.mid(2).trimmed().startsWith(kPrefix)) {
            if (ast.hasPosLine) {
                result.diagnostics.append(
                    { lineNo, 1,
                      QStringLiteral("Only one `%% mermaid-flow:pos` line is "
                                     "recognized; this one is ignored"),
                      Diagnostic::Warning });
            } else {
                ast.hasPosLine = true;
                ast.posLineSpan = { baseOffset + lineStart,
                                    int(line.size()) };
                const QString entries = trimmed.mid(2).trimmed()
                                            .mid(kPrefix.size()).trimmed();
                const QStringList parts =
                    entries.split(u' ', Qt::SkipEmptyParts);
                for (const QString &part : parts) {
                    const int eq = part.indexOf(u'=');
                    if (eq <= 0)
                        continue;   // malformed entry: skipped
                    const QStringList nums =
                        part.mid(eq + 1).split(u',', Qt::SkipEmptyParts);
                    if (nums.size() != 2 && nums.size() != 4)
                        continue;
                    bool okX = false, okY = false;
                    const double x = nums.at(0).toDouble(&okX);
                    const double y = nums.at(1).toDouble(&okY);
                    if (!okX || !okY)
                        continue;
                    // A `,width,height` suffix parses; the dimensions are
                    // ignored and dropped on the next arrangement write.
                    PosEntry entry;
                    entry.id = part.left(eq);
                    entry.x = x;
                    entry.y = y;
                    ast.posEntries.append(entry);
                }
            }
        }
        lineStart = lineEnd + 1;
        if (lineEnd == body.size())
            break;
    }
}

// A stateful pass over one flowchart's statements.
class FlowParser
{
public:
    FlowParser(ParseResult &result, int baseOffset)
        : m_r(result), m_ast(result.flowchart), m_base(baseOffset) {}

    void run(const QList<QList<Token>> &statements, int headerStatement,
             const QString &accTitle);

private:
    void parseStatement(const QList<Token> &st);
    void parseChain(const QList<Token> &st, int start);
    QStringList parseGroup(const QList<Token> &st, int &idx);
    int ensureNode(const QString &id, const Token &at);
    void applyShapeData(int nodeIndex, const Token &data);
    void diag(int line, int col, const QString &msg,
              Diagnostic::Severity sev = Diagnostic::Error);

    ParseResult &m_r;
    FlowchartAst &m_ast;
    QList<int> m_subgraphStack;   // indices into m_ast.subgraphs
    int m_base = 0;               // offset of the parsed body in the source
    SourceSpan m_stmtSpan;        // span of the statement being parsed
    int m_edgeCount = 0;
    bool m_nodeCapWarned = false;
    bool m_edgeCapWarned = false;
};

void FlowParser::diag(int line, int col, const QString &msg,
                      Diagnostic::Severity sev)
{
    Diagnostic d;
    d.line = line;
    d.column = col;
    d.message = msg;
    d.severity = sev;
    m_r.diagnostics.append(d);
}

int FlowParser::ensureNode(const QString &id, const Token &at)
{
    int index = m_ast.indexOfNode(id);
    if (index < 0) {
        if (m_ast.nodes.size() >= kMaxNodes) {
            if (!m_nodeCapWarned) {
                diag(at.line, at.column,
                     QStringLiteral("Too many nodes (limit %1); extra nodes are "
                                    "not rendered").arg(kMaxNodes));
                m_nodeCapWarned = true;
            }
            return -1;
        }
        Node n;
        n.id = id;
        n.label = id;
        n.order = m_ast.nodes.size();
        m_ast.nodes.append(n);
        index = m_ast.nodes.size() - 1;
    }
    // §20 source mapping: every reference span, the first one as the
    // declaration. The id may sit inside a larger token (`A:::hot`, `A,B`).
    {
        const int within = at.text.indexOf(id);
        if (within >= 0 && !id.isEmpty()) {
            const SourceSpan span{ m_base + at.offset + within,
                                   int(id.size()) };
            if (!m_ast.nodes[index].idSpan.valid())
                m_ast.nodes[index].idSpan = span;
            m_ast.nodes[index].refSpans.append(span);
        }
    }
    // Record subgraph membership on every reference made while a subgraph is
    // open, so a node defined earlier and merely listed inside still belongs.
    if (!m_subgraphStack.isEmpty()) {
        QStringList &members = m_ast.subgraphs[m_subgraphStack.last()].nodeIds;
        if (!members.contains(id))
            members.append(id);
    }
    return index;
}

// Apply an `@{ key: value, ... }` shape-data block (flow.jison shapeData).
// `shape` and `label` are honoured; every other key is ignored with a warning
// (diagrams-prd.md §9 restricted syntax: configuration is allowlisted).
void FlowParser::applyShapeData(int nodeIndex, const Token &data)
{
    if (nodeIndex < 0)
        return;
    // Split into entries at commas and newlines outside quotes.
    QStringList entries;
    QString cur;
    bool inQuote = false;
    for (const QChar ch : data.text) {
        if (ch == u'"') {
            inQuote = !inQuote;
            cur += ch;
        } else if (!inQuote && (ch == u',' || ch == u'\n')) {
            entries << cur;
            cur.clear();
        } else {
            cur += ch;
        }
    }
    entries << cur;
    for (const QString &entryRaw : entries) {
        const QString entry = entryRaw.trimmed();
        if (entry.isEmpty())
            continue;
        const int colon = entry.indexOf(u':');
        if (colon < 0) {
            diag(data.line, data.column,
                 QStringLiteral("Malformed shape data entry: %1").arg(entry),
                 Diagnostic::Warning);
            continue;
        }
        const QString key = entry.left(colon).trimmed().toLower();
        QString val = entry.mid(colon + 1).trimmed();
        if (val.size() >= 2 && val.startsWith(u'"') && val.endsWith(u'"'))
            val = val.mid(1, val.size() - 2);
        if (key == QLatin1String("shape")) {
            bool known = false;
            const NodeShape sh = shapeFromName(val, &known);
            if (!known)
                diag(data.line, data.column,
                     QStringLiteral("Unknown shape \"%1\"; drawn as a rectangle")
                         .arg(val), Diagnostic::Warning);
            m_ast.nodes[nodeIndex].shape = sh;
        } else if (key == QLatin1String("label")) {
            m_ast.nodes[nodeIndex].label = val.left(kMaxLabelChars);
        } else {
            diag(data.line, data.column,
                 QStringLiteral("Shape data key \"%1\" is ignored in Kvit")
                     .arg(key), Diagnostic::Warning);
        }
    }
}

QStringList FlowParser::parseGroup(const QList<Token> &st, int &idx)
{
    QStringList ids;
    while (idx < st.size()) {
        const Token &t = st.at(idx);
        if (t.kind != Token::Word)
            break;
        // Split a trailing `:::class` off the id.
        QString word = t.text;
        QString cls;
        const int trip = word.indexOf(QLatin1String(":::"));
        if (trip >= 0) {
            cls = word.mid(trip + 3);
            word = word.left(trip);
        }
        ++idx;
        const int ni = ensureNode(word, t);
        // An optional shape/label immediately after the id.
        if (idx < st.size() && st.at(idx).kind == Token::Shape) {
            const Token &sh = st.at(idx);
            if (ni >= 0) {
                QString label = sh.text;
                NodeShape shape = sh.shape;
                if (label.size() > kMaxLabelChars) {
                    diag(sh.line, sh.column,
                         QStringLiteral("Label exceeds %1 characters")
                             .arg(kMaxLabelChars));
                    label = label.left(kMaxLabelChars);
                }
                // The legacy `[|prop:value|text]` vertex-with-props form: the
                // properties are ignored with a warning, the text is kept.
                if (shape == NodeShape::Rect && label.startsWith(u'|')) {
                    const int second = label.indexOf(u'|', 1);
                    const int colon = label.indexOf(u':');
                    if (second > 0 && colon > 0 && colon < second) {
                        diag(sh.line, sh.column,
                             QStringLiteral("Node properties (`[|prop:value|…]`) "
                                            "are ignored in Kvit"),
                             Diagnostic::Warning);
                        label = label.mid(second + 1).trimmed();
                    }
                }
                m_ast.nodes[ni].shape = shape;
                m_ast.nodes[ni].label = label;
                m_ast.nodes[ni].shapeSpan = { m_base + sh.offset, sh.length };
                if (sh.labelOffset >= 0)
                    m_ast.nodes[ni].labelSpan = { m_base + sh.labelOffset,
                                                  sh.labelLength };
            }
            ++idx;
        }
        // A `:::class` written as a separate token after the shape.
        if (idx < st.size() && st.at(idx).kind == Token::Word
            && st.at(idx).text.startsWith(QLatin1String(":::"))) {
            cls = st.at(idx).text.mid(3);
            ++idx;
        }
        // An `@{ shape: ..., label: ... }` shape-data block after the vertex.
        if (idx < st.size() && st.at(idx).kind == Token::ShapeData) {
            applyShapeData(ni, st.at(idx));
            ++idx;
        }
        if (ni >= 0 && !cls.isEmpty() && !m_ast.nodes[ni].classes.contains(cls))
            m_ast.nodes[ni].classes.append(cls);
        if (ni >= 0)
            ids.append(word);
        if (idx < st.size() && st.at(idx).kind == Token::Amp) {
            ++idx;
            continue;
        }
        break;
    }
    return ids;
}

void FlowParser::parseChain(const QList<Token> &st, int start)
{
    int idx = start;
    QStringList left = parseGroup(st, idx);
    if (left.isEmpty()) {
        if (idx < st.size())
            diag(st.at(idx).line, st.at(idx).column,
                 QStringLiteral("Expected a node"));
        return;
    }
    while (idx < st.size()) {
        // An optional `e1@` edge id (flow.jison LINK_ID) before the link.
        QString edgeId;
        const int beforeId = idx;
        if (st.at(idx).kind == Token::Word && st.at(idx).text.size() > 1
            && st.at(idx).text.endsWith(u'@')
            && idx + 1 < st.size() && st.at(idx + 1).kind == Token::Edge) {
            edgeId = st.at(idx).text.chopped(1);
            ++idx;
        }
        if (st.at(idx).kind != Token::Edge) {
            idx = beforeId;
            break;
        }
        const Token edge = st.at(idx);
        ++idx;
        QString label = edge.edgeLabel;
        // A pipe label `-->|text|` after the edge.
        SourceSpan pipeSpan;
        if (idx < st.size() && st.at(idx).kind == Token::Pipe) {
            const int pipeStart = st.at(idx).offset;
            ++idx;
            QStringList parts;
            while (idx < st.size() && st.at(idx).kind != Token::Pipe) {
                if (st.at(idx).kind == Token::Word)
                    parts << st.at(idx).text;
                else if (st.at(idx).kind == Token::Shape)
                    parts << st.at(idx).text;
                ++idx;
            }
            if (idx < st.size() && st.at(idx).kind == Token::Pipe) {
                pipeSpan = { m_base + pipeStart,
                             st.at(idx).offset + 1 - pipeStart };
                ++idx;   // closing pipe
            }
            QString joined = parts.join(u' ');
            if (joined.size() >= 2 && joined.startsWith(u'"') && joined.endsWith(u'"'))
                joined = joined.mid(1, joined.size() - 2);
            label = joined;
        }
        QStringList right = parseGroup(st, idx);
        if (right.isEmpty()) {
            diag(edge.line, edge.column,
                 QStringLiteral("Edge has no target node"));
            break;
        }
        for (const QString &f : left) {
            for (const QString &t : right) {
                if (m_edgeCount >= kMaxEdges) {
                    if (!m_edgeCapWarned) {
                        diag(edge.line, edge.column,
                             QStringLiteral("Too many edges (limit %1)")
                                 .arg(kMaxEdges));
                        m_edgeCapWarned = true;
                    }
                    return;
                }
                Edge e;
                e.from = f;
                e.to = t;
                e.label = label;
                e.id = edgeId;
                e.opSpan = { m_base + edge.offset, edge.length };
                e.pipeSpan = pipeSpan;
                e.stmtSpan = m_stmtSpan;
                e.stroke = edge.stroke;
                e.arrowStart = edge.arrowStart;
                e.arrowEnd = edge.arrowEnd;
                e.invisible = edge.invisible;
                e.minLen = edge.minLen;
                e.order = m_edgeCount++;
                m_ast.edges.append(e);
            }
        }
        left = right;
    }
}

void FlowParser::parseStatement(const QList<Token> &st)
{
    if (st.isEmpty())
        return;
    const Token &head = st.first();
    if (head.kind == Token::Word) {
        const QString kw = head.text;
        if (kw == QLatin1String("subgraph")) {
            if (m_subgraphStack.size() >= kMaxDepth) {
                diag(head.line, head.column,
                     QStringLiteral("Subgraph nesting too deep (limit %1)")
                         .arg(kMaxDepth));
                return;
            }
            Subgraph sg;
            int idx = 1;
            // `subgraph My Long Title` — textNoTags may span words; the first
            // word doubles as the stable id, the joined words as the title.
            QStringList words;
            while (idx < st.size() && st.at(idx).kind == Token::Word) {
                words << st.at(idx).text;
                ++idx;
            }
            if (!words.isEmpty()) {
                sg.id = words.first();
                QString title = words.join(u' ');
                if (title.size() >= 2 && title.startsWith(u'"')
                    && title.endsWith(u'"'))
                    title = title.mid(1, title.size() - 2);
                sg.title = title;
            }
            if (idx < st.size() && st.at(idx).kind == Token::Shape) {
                sg.title = st.at(idx).text;
                ++idx;
            }
            m_ast.subgraphs.append(sg);
            m_subgraphStack.append(m_ast.subgraphs.size() - 1);
            return;
        }
        if (kw == QLatin1String("end")) {
            if (m_subgraphStack.isEmpty())
                diag(head.line, head.column,
                     QStringLiteral("`end` without a matching `subgraph`"));
            else
                m_subgraphStack.removeLast();
            return;
        }
        if (kw == QLatin1String("direction")) {
            if (st.size() >= 2 && st.at(1).kind == Token::Word) {
                bool ok = false;
                const Direction d = directionFromWord(st.at(1).text, &ok);
                if (ok) {
                    if (!m_subgraphStack.isEmpty()) {
                        m_ast.subgraphs[m_subgraphStack.last()].direction = d;
                        m_ast.subgraphs[m_subgraphStack.last()].hasDirection = true;
                    } else {
                        m_ast.direction = d;
                    }
                }
            }
            return;
        }
        if (kw == QLatin1String("classDef")) {
            // `classDef a,b styles` defines every comma-separated name.
            if (st.size() >= 2 && st.at(1).kind == Token::Word) {
                QStringList rest;
                for (int i = 2; i < st.size(); ++i)
                    if (st.at(i).kind == Token::Word)
                        rest << st.at(i).text;
                const QStringList names =
                    st.at(1).text.split(u',', Qt::SkipEmptyParts);
                for (const QString &name : names) {
                    ClassDef def = m_ast.classDefs.value(name);
                    parseStyleDeclarations(def, rest.join(u' '));
                    m_ast.classDefs.insert(name, def);
                }
            }
            return;
        }
        if (kw == QLatin1String("class")) {
            // `class A,B,C className`
            if (st.size() >= 3 && st.at(1).kind == Token::Word
                && st.last().kind == Token::Word) {
                const QString cls = st.last().text;
                const QStringList targets =
                    st.at(1).text.split(u',', Qt::SkipEmptyParts);
                for (const QString &id : targets) {
                    const int ni = ensureNode(id, st.at(1));
                    if (ni >= 0 && !m_ast.nodes[ni].classes.contains(cls))
                        m_ast.nodes[ni].classes.append(cls);
                }
            }
            return;
        }
        if (kw == QLatin1String("style")) {
            // `style A fill:#f9f,...` — a per-node override folded into a
            // synthetic class so the scene applies it uniformly.
            if (st.size() >= 3 && st.at(1).kind == Token::Word) {
                const QString id = st.at(1).text;
                QStringList rest;
                for (int i = 2; i < st.size(); ++i)
                    if (st.at(i).kind == Token::Word)
                        rest << st.at(i).text;
                const QString synth = QStringLiteral("__style_") + id;
                ClassDef def = m_ast.classDefs.value(synth);
                parseStyleDeclarations(def, rest.join(u' '));
                m_ast.classDefs.insert(synth, def);
                const int ni = ensureNode(id, st.at(1));
                if (ni >= 0 && !m_ast.nodes[ni].classes.contains(synth))
                    m_ast.nodes[ni].classes.append(synth);
            }
            return;
        }
        if (kw == QLatin1String("click") || kw == QLatin1String("linkStyle")) {
            diag(head.line, head.column,
                 QStringLiteral("`%1` is ignored in Kvit (interactivity and "
                                "link styling are not supported)").arg(kw),
                 Diagnostic::Warning);
            return;
        }
        if (kw.startsWith(QLatin1String("accTitle"))
            || kw.startsWith(QLatin1String("accDescr"))) {
            QStringList rest;
            // The keyword token may carry a `:text` tail if written `accTitle:x`.
            const int colon = kw.indexOf(u':');
            if (colon >= 0 && colon + 1 < kw.size())
                rest << kw.mid(colon + 1);
            for (int i = 1; i < st.size(); ++i)
                if (st.at(i).kind == Token::Word || st.at(i).kind == Token::Shape)
                    rest << st.at(i).text;
            const QString text = rest.join(u' ').trimmed();
            if (kw.startsWith(QLatin1String("accTitle")))
                m_ast.accTitle = text;
            else
                m_ast.accDescr = text;
            return;
        }
    }
    // Otherwise a node/edge chain.
    parseChain(st, 0);
}

void FlowParser::run(const QList<QList<Token>> &statements, int headerStatement,
                     const QString &accTitle)
{
    if (!accTitle.isEmpty())
        m_ast.accTitle = accTitle;
    for (int s = 0; s < statements.size(); ++s) {
        if (s == headerStatement)
            continue;
        const QList<Token> &st = statements.at(s);
        if (!st.isEmpty())
            m_stmtSpan = { m_base + st.first().offset,
                           st.last().offset + st.last().length
                               - st.first().offset };
        parseStatement(st);
    }
}

} // namespace

ParseResult MermaidParser::parse(const QString &source) const
{
    ParseResult result;

    QString frontTitle;
    int bodyOffset = 0;
    QString body = stripFrontmatter(source, &frontTitle, &bodyOffset);
    if (body.size() > kMaxSourceChars) {
        result.diagnostics.append(
            { 1, 1,
              QStringLiteral("Diagram source exceeds %1 KiB")
                  .arg(kMaxSourceChars / 1024), Diagnostic::Error });
        return result;
    }

    // Find the header keyword on the first substantive line BEFORE any
    // family-specific lexing — the families tokenize very differently.
    QString headerWord;
    int headerLine = 1;
    int headerColumn = 1;
    {
        const QStringList lines = body.split(u'\n');
        for (int i = 0; i < lines.size() && headerWord.isEmpty(); ++i) {
            QString l = lines.at(i);
            const int cut = l.indexOf(QLatin1String("%%"));
            if (cut >= 0)
                l = l.left(cut);
            const QString t = l.trimmed();
            if (t.isEmpty())
                continue;
            int j = 0;
            while (j < t.size() && !t.at(j).isSpace() && t.at(j) != u';')
                ++j;
            headerWord = t.left(j);
            headerLine = i + 1;
            headerColumn = l.indexOf(t.at(0)) + 1;
        }
    }

    if (headerWord.isEmpty()) {
        result.diagnostics.append(
            { 1, 1,
              QStringLiteral("Empty diagram — expected a diagram type "
                             "declaration such as `flowchart`"),
              Diagnostic::Error });
        return result;
    }

    result.type = familyFromHeader(headerWord);
    result.familyName = headerWord;

    if (result.type == DiagramType::Sequence) {
        result.supported = true;
        parseSequence(body, bodyOffset, result);
        if (result.sequence.title.isEmpty())
            result.sequence.title = frontTitle;
        if (result.sequence.participants.isEmpty())
            result.diagnostics.append(
                { headerLine, headerColumn,
                  QStringLiteral("Sequence diagram has no participants"),
                  Diagnostic::Warning });
        return result;
    }

    if (result.type == DiagramType::Class) {
        result.supported = true;
        parseClassDiagram(body, bodyOffset, result);
        if (result.classDiagram.title.isEmpty())
            result.classDiagram.title = frontTitle;
        if (result.classDiagram.classes.isEmpty())
            result.diagnostics.append(
                { headerLine, headerColumn,
                  QStringLiteral("Class diagram has no classes"),
                  Diagnostic::Warning });
        return result;
    }

    if (result.type == DiagramType::State) {
        result.supported = true;
        parseStateDiagram(body, bodyOffset, result);
        if (result.stateDiagram.title.isEmpty())
            result.stateDiagram.title = frontTitle;
        if (result.stateDiagram.states.isEmpty())
            result.diagnostics.append(
                { headerLine, headerColumn,
                  QStringLiteral("State diagram has no states"),
                  Diagnostic::Warning });
        return result;
    }

    if (result.type == DiagramType::Er) {
        result.supported = true;
        parseErDiagram(body, bodyOffset, result);
        if (result.er.title.isEmpty())
            result.er.title = frontTitle;
        if (result.er.entities.isEmpty())
            result.diagnostics.append(
                { headerLine, headerColumn,
                  QStringLiteral("ER diagram has no entities"),
                  Diagnostic::Warning });
        return result;
    }

    if (result.type != DiagramType::Flowchart) {
        result.supported = false;
        result.diagnostics.append(
            { headerLine, headerColumn,
              QStringLiteral("Unsupported Mermaid diagram type in this Kvit "
                             "version: %1").arg(headerWord), Diagnostic::Error });
        return result;
    }

    MermaidLexer lexer(body);
    // Group the flat token stream into statements at Sep boundaries.
    QList<QList<Token>> statements;
    QList<Token> current;
    for (const Token &t : lexer.tokens()) {
        if (t.kind == Token::Sep) {
            if (!current.isEmpty()) {
                statements.append(current);
                current.clear();
            }
        } else {
            current.append(t);
        }
    }
    if (!current.isEmpty())
        statements.append(current);

    // Locate the header statement in the token stream.
    int headerStatement = -1;
    for (int s = 0; s < statements.size(); ++s) {
        if (!statements.at(s).isEmpty()
            && statements.at(s).first().kind == Token::Word) {
            headerStatement = s;
            break;
        }
    }
    if (headerStatement < 0) {
        result.diagnostics.append(
            { 1, 1,
              QStringLiteral("Empty diagram — expected a diagram type "
                             "declaration such as `flowchart`"),
              Diagnostic::Error });
        return result;
    }

    // Flowchart: read the header direction, then the statements.
    result.supported = true;
    if (headerWord != QLatin1String("flowchart")
        && headerWord != QLatin1String("graph")) {
        const Token &h = statements.at(headerStatement).first();
        result.diagnostics.append(
            { h.line, h.column,
              QStringLiteral("`%1` selects a layout engine Kvit does not ship; "
                             "rendered with the standard layered layout")
                  .arg(headerWord), Diagnostic::Warning });
    }
    const QList<Token> &header = statements.at(headerStatement);
    if (header.size() >= 2 && header.at(1).kind == Token::Word) {
        bool ok = false;
        const Direction d = directionFromWord(header.at(1).text, &ok);
        if (ok)
            result.flowchart.direction = d;
    }

    FlowParser fp(result, bodyOffset);
    fp.run(statements, headerStatement, frontTitle);
    parsePosLines(body, bodyOffset, result);

    if (result.flowchart.nodes.isEmpty()) {
        result.diagnostics.append(
            { 1, 1,
              QStringLiteral("Flowchart has no nodes"), Diagnostic::Warning });
    }
    return result;
}

} // namespace Mermaid
