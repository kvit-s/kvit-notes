// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
#include "mermaidlexer.h"

namespace Mermaid {

namespace {

// Opening shape brackets, longest first so `([` beats `(`. The same opener may
// appear with two closers (the flow.jison trapezoid family): entries are tried
// in order and an unmatched closer falls through to the next entry.
struct ShapeBracket {
    const char *open;
    const char *close;
    NodeShape shape;
};
const ShapeBracket kBrackets[] = {
    { "(((", ")))", NodeShape::DoubleCircle },
    { "([", "])", NodeShape::Stadium },
    { "[[", "]]", NodeShape::Subroutine },
    { "[(", ")]", NodeShape::Cylinder },
    { "((", "))", NodeShape::Circle },
    { "{{", "}}", NodeShape::Hexagon },
    { "(-", "-)", NodeShape::Ellipse },
    { "[/", "/]", NodeShape::Parallelogram },
    { "[/", "\\]", NodeShape::Trapezoid },
    { "[\\", "\\]", NodeShape::ParallelogramAlt },
    { "[\\", "/]", NodeShape::TrapezoidAlt },
    { "[", "]", NodeShape::Rect },
    { "(", ")", NodeShape::RoundRect },
    { "{", "}", NodeShape::Rhombus },
    { ">", "]", NodeShape::Odd },
};

} // namespace

MermaidLexer::MermaidLexer(const QString &source)
{
    // The source is kept verbatim so token offsets map onto the stored fence
    // for write-back; `\r` is treated as a newline character in run()
    // rather than rewritten, which normalizes CRLF logically (§8.2) without
    // shifting offsets.
    m_src = source;
    run();
}

void MermaidLexer::emitSep()
{
    m_inPipe = false;   // a statement boundary closes any pipe-label context
    // Collapse runs of separators/blank lines to a single boundary.
    if (!m_tokens.isEmpty() && m_tokens.last().kind == Token::Sep)
        return;
    Token t;
    t.kind = Token::Sep;
    t.line = m_line;
    t.column = column();
    t.offset = m_pos;
    m_tokens.append(t);
}

void MermaidLexer::run()
{
    while (m_pos < m_src.size()) {
        const QChar c = m_src.at(m_pos);
        if (c == u'\n') {
            emitSep();
            ++m_pos;
            ++m_line;
            m_lineStart = m_pos;
            continue;
        }
        if (c == u'\r') {
            // CRLF: the `\n` that follows handles the line break. A lone CR
            // (classic Mac) acts as the newline itself.
            if (at(m_pos + 1) != u'\n') {
                emitSep();
                ++m_line;
                m_lineStart = m_pos + 1;
            }
            ++m_pos;
            continue;
        }
        if (c == u' ' || c == u'\t') {
            ++m_pos;
            continue;
        }
        if (c == u';') {
            emitSep();
            ++m_pos;
            continue;
        }
        if (matchComment())
            continue;
        if (matchBraceBlock())
            continue;
        if (c == u'|') {
            Token t; t.kind = Token::Pipe; t.line = m_line; t.column = column();
            t.offset = m_pos; t.length = 1;
            m_tokens.append(t);
            m_inPipe = !m_inPipe;
            ++m_pos;
            continue;
        }
        if (c == u'&') {
            Token t; t.kind = Token::Amp; t.line = m_line; t.column = column();
            t.offset = m_pos; t.length = 1;
            m_tokens.append(t);
            ++m_pos;
            continue;
        }
        if (matchEdge())
            continue;
        if (matchShape())
            continue;
        matchWord();
    }
    emitSep();
}

bool MermaidLexer::matchComment()
{
    if (at(m_pos) != u'%' || at(m_pos + 1) != u'%')
        return false;
    // A `%%` comment (including a single-line `%%{init}%%` directive) runs to
    // end of line; the parser allowlists directives separately if needed.
    while (m_pos < m_src.size() && m_src.at(m_pos) != u'\n')
        ++m_pos;
    return true;
}

bool MermaidLexer::matchShape()
{
    for (const ShapeBracket &b : kBrackets) {
        const QString open = QString::fromLatin1(b.open);
        if (m_src.mid(m_pos, open.size()) != open)
            continue;
        // The odd shape `A>text]` exists only immediately after a node id
        // (flow.jison: `idString TAGEND text SQE` with no SPACE token), so a
        // bare `>` elsewhere — pipe labels, `graph >` headers — stays a word.
        if (b.shape == NodeShape::Odd) {
            if (m_inPipe || m_pos == 0 || m_tokens.isEmpty()
                || m_tokens.last().kind != Token::Word
                || m_src.at(m_pos - 1).isSpace())
                continue;
        }
        const QString close = QString::fromLatin1(b.close);
        const int startCol = column();
        const int contentStart = m_pos + open.size();
        // Find the matching close, honouring a quoted label so a `]` inside
        // "..." does not close the shape early.
        int i = contentStart;
        bool inQuote = false;
        int found = -1;
        while (i < m_src.size()) {
            const QChar ch = m_src.at(i);
            if (ch == u'"') {
                inQuote = !inQuote;
                ++i;
                continue;
            }
            if (ch == u'\n')
                break;   // a shape does not span lines in this subset
            if (!inQuote && m_src.mid(i, close.size()) == close) {
                found = i;
                break;
            }
            ++i;
        }
        if (found < 0)
            continue;   // this closer is absent: try the alternate bracket pair
        QString label = m_src.mid(contentStart, found - contentStart);
        // Strip surrounding quotes; label text is plain Unicode, never HTML.
        label = label.trimmed();
        if (label.size() >= 2 && label.startsWith(u'"') && label.endsWith(u'"'))
            label = label.mid(1, label.size() - 2);
        // A markdown string `"`text`"` (flow.jison md_string) leaves backticks
        // after the quote strip; the text is treated as plain Unicode.
        if (label.size() >= 2 && label.startsWith(u'`') && label.endsWith(u'`'))
            label = label.mid(1, label.size() - 2).trimmed();
        Token t;
        t.kind = Token::Shape;
        t.shape = b.shape;
        t.text = label;
        t.line = m_line;
        t.column = startCol;
        t.offset = m_pos;
        t.length = found + close.size() - m_pos;
        t.labelOffset = contentStart;
        t.labelLength = found - contentStart;
        m_tokens.append(t);
        m_pos = found + close.size();
        return true;
    }
    return false;
}

// Capture an `@{ ... }` shape-data block after a node id (`A@{ shape: circle }`,
// flow.jison shapeData) or a multiline `accDescr { ... }` body. Both may span
// lines and contain quoted strings holding `}`. Emitted as ShapeData (node
// data) or Shape (accDescr text); anything else starting with `{` falls through
// to the rhombus bracket handling.
bool MermaidLexer::matchBraceBlock()
{
    if (at(m_pos) != u'{' || m_tokens.isEmpty())
        return false;
    const Token &prev = m_tokens.last();
    if (prev.kind != Token::Word)
        return false;
    const bool shapeData = prev.text.endsWith(u'@') && prev.text.size() > 1;
    const bool accBlock = prev.text.startsWith(QLatin1String("accDescr"));
    if (!shapeData && !accBlock)
        return false;
    const int startCol = column();
    const int startLine = m_line;
    int line = m_line;
    int lineStart = m_lineStart;
    int i = m_pos + 1;
    bool inQuote = false;
    int found = -1;
    while (i < m_src.size()) {
        const QChar ch = m_src.at(i);
        if (ch == u'"') {
            inQuote = !inQuote;
        } else if (ch == u'\n') {
            ++line;
            lineStart = i + 1;
        } else if (!inQuote && ch == u'}') {
            found = i;
            break;
        }
        ++i;
    }
    if (found < 0)
        return false;   // unterminated: ordinary handling recovers
    Token t;
    t.kind = shapeData ? Token::ShapeData : Token::Shape;
    t.text = m_src.mid(m_pos + 1, found - m_pos - 1);
    t.line = startLine;
    t.column = startCol;
    t.offset = shapeData ? m_pos - 1 : m_pos;   // shape data spans the `@`
    t.length = found + 1 - t.offset;
    if (shapeData) {
        m_tokens.last().text.chop(1);   // drop the `@` from the node id
        m_tokens.last().length -= 1;
    }
    m_tokens.append(t);
    m_pos = found + 1;
    m_line = line;
    m_lineStart = lineStart;
    return true;
}

bool MermaidLexer::matchEdge()
{
    const QChar c = m_src.at(m_pos);
    // `~~~` is the invisible link: it ranks like an edge but draws nothing.
    if (c == u'~') {
        int i = m_pos;
        while (i < m_src.size() && m_src.at(i) == u'~')
            ++i;
        const int count = i - m_pos;
        if (count < 3)
            return false;
        Token t;
        t.kind = Token::Edge;
        t.line = m_line;
        t.column = column();
        t.offset = m_pos;
        t.length = count;
        t.invisible = true;
        t.arrowEnd = false;
        t.minLen = qMax(1, count - 2);
        m_tokens.append(t);
        m_pos = i;
        return true;
    }
    if (c != u'<' && c != u'-' && c != u'=' && c != u'.' && c != u'o' && c != u'x')
        return false;

    const int start = m_pos;
    const int startCol = column();
    int i = m_pos;
    bool arrowStart = false;

    // A start arrowhead: `<`, or `o`/`x` immediately before a link body.
    if (m_src.at(i) == u'<') { arrowStart = true; ++i; }
    else if ((m_src.at(i) == u'o' || m_src.at(i) == u'x')
             && (at(i + 1) == u'-' || at(i + 1) == u'=')) { arrowStart = true; ++i; }

    // The link body must open with a link stroke char.
    if (at(i) != u'-' && at(i) != u'=' && at(i) != u'.')
        return false;

    // Consume the whole link (dashes/equals/dots and arrowheads), capturing an
    // inline `-- text -->` label when the closing half carries an arrowhead.
    const int bodyStart = i;
    auto isLink = [](QChar ch) {
        return ch == u'-' || ch == u'=' || ch == u'.';
    };
    while (i < m_src.size() && isLink(m_src.at(i)))
        ++i;
    int firstRunEnd = i;

    QString inlineLabel;
    bool arrowEnd = false;
    // Optional inline label: whitespace, text, whitespace, a second link run
    // that ends in an arrowhead.
    int save = i;
    if (i < m_src.size() && (m_src.at(i) == u' ' || m_src.at(i) == u'\t')) {
        int j = i;
        while (j < m_src.size() && (m_src.at(j) == u' ' || m_src.at(j) == u'\t'))
            ++j;
        const int textStart = j;
        // The label text runs (spaces and all) up to the second link run.
        while (j < m_src.size() && m_src.at(j) != u'\n' && !isLink(m_src.at(j)))
            ++j;
        const int textEnd = j;
        int run2 = textEnd;
        while (run2 < m_src.size() && isLink(m_src.at(run2)))
            ++run2;
        const bool hasHead = run2 < m_src.size()
            && (m_src.at(run2) == u'>' || m_src.at(run2) == u'o'
                || m_src.at(run2) == u'x');
        const QString text = m_src.mid(textStart, textEnd - textStart).trimmed();
        if (run2 > textEnd && hasHead && !text.isEmpty()) {
            inlineLabel = text;
            i = run2 + 1;
            arrowEnd = true;
            firstRunEnd = run2;   // dashes for stroke/len come from the whole op
        } else {
            i = save;   // not an inline label; fall through to plain-head handling
        }
    }

    if (!arrowEnd) {
        // Plain end arrowhead directly after the run: `-->`, `--x`, `--o`.
        if (i < m_src.size()
            && (m_src.at(i) == u'>' || m_src.at(i) == u'o' || m_src.at(i) == u'x')) {
            arrowEnd = true;
            ++i;
        }
    }

    // Classify stroke and rank span from the link characters consumed.
    const QString body = m_src.mid(bodyStart, firstRunEnd - bodyStart);
    EdgeStroke stroke = EdgeStroke::Solid;
    if (body.contains(u'.'))
        stroke = EdgeStroke::Dotted;
    else if (body.contains(u'='))
        stroke = EdgeStroke::Thick;
    int dashCount = 0;
    for (QChar ch : body)
        if (ch == u'-' || ch == u'=')
            ++dashCount;
    const int minLen = qMax(1, dashCount - 1);

    // Reject a bare single stroke char that is not actually a link (e.g. a stray
    // '-' in a word): require at least two link chars, or an arrowhead.
    if (dashCount < 2 && !arrowEnd && !(stroke == EdgeStroke::Dotted)) {
        m_pos = start;
        return false;
    }

    Token t;
    t.kind = Token::Edge;
    t.line = m_line;
    t.column = startCol;
    t.offset = start;
    t.length = i - start;
    t.stroke = stroke;
    t.arrowStart = arrowStart;
    t.arrowEnd = arrowEnd;
    t.minLen = minLen;
    t.edgeLabel = inlineLabel;
    m_tokens.append(t);
    m_pos = i;
    return true;
}

void MermaidLexer::matchWord()
{
    const int startCol = column();
    const int start = m_pos;
    while (m_pos < m_src.size()) {
        const QChar ch = m_src.at(m_pos);
        if (ch == u'\n' || ch == u' ' || ch == u'\t' || ch == u';' || ch == u'|'
            || ch == u'&')
            break;
        // Stop at a shape bracket or an edge start so the next token is clean.
        if (ch == u'[' || ch == u'(' || ch == u'{' || ch == u']' || ch == u')'
            || ch == u'}')
            break;
        // A hyphen flanked by word chars is internal (`stroke-width`,
        // `my-node`); one that starts a link (`-->`, `-.->`) ends the word.
        if (ch == u'-') {
            if (m_pos > start && at(m_pos + 1).isLetterOrNumber()) {
                ++m_pos;
                continue;
            }
            break;
        }
        if (ch == u'=' || ch == u'<')
            break;
        if (ch == u'>' && !m_inPipe)
            break;   // may open an odd shape (`A>text]`); label text keeps it
        if (ch == u'~' && at(m_pos + 1) == u'~')
            break;   // start of a `~~~` invisible link
        if (ch == u'.' && (at(m_pos + 1) == u'-'))
            break;   // start of a `-.->`-style link written as `.->`
        ++m_pos;
    }
    if (m_pos == start) {
        // No progress (a lone delimiter matchEdge/shape rejected): consume one
        // char so the lexer always advances. Lone `<`/`>` are kept as words —
        // they are valid header direction symbols (`graph <` = RL, `graph >` =
        // LR in flow.jison).
        const QChar ch = m_src.at(m_pos);
        ++m_pos;
        if (ch == u'<' || ch == u'>') {
            Token t;
            t.kind = Token::Word;
            t.text = ch;
            t.line = m_line;
            t.column = startCol;
            t.offset = m_pos - 1;
            t.length = 1;
            m_tokens.append(t);
        }
        return;
    }
    Token t;
    t.kind = Token::Word;
    t.text = m_src.mid(start, m_pos - start);
    t.line = m_line;
    t.column = startCol;
    t.offset = start;
    t.length = m_pos - start;
    m_tokens.append(t);
}

} // namespace Mermaid
