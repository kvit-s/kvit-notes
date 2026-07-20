// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
#ifndef MERMAIDLEXER_H
#define MERMAIDLEXER_H

#include <QList>
#include <QString>

#include "mermaidast.h"

// Token stream for the Mermaid flowchart parser (diagrams-prd.md §8.1/§8.2).
// The lexer normalizes CRLF, drops `%%` comments outside labels, and turns the
// source into tokens carrying one-based line/column offsets suitable for editor
// diagnostics. Shape brackets and edge operators are recognized as whole tokens
// (with their parsed shape / stroke / arrow attributes) because a flat
// character grammar cannot express them; label text between brackets — spaces,
// punctuation, quotes and all — is captured verbatim as one token.
namespace Mermaid {

struct Token {
    enum Kind {
        Word,      // a node id or a keyword
        Shape,     // a bracketed label; `shape` and `text` are set
        ShapeData, // an `@{ ... }` block after a node id; `text` is the body
        Edge,      // a link; stroke/arrows/minLen/edgeLabel are set
        Pipe,      // `|`  (edge-label delimiter)
        Amp,       // `&`  (node list)
        Sep,       // a statement boundary (newline or `;`)
    };

    Kind kind = Word;
    QString text;              // Word: id/keyword; Shape/ShapeData: content
    int line = 1;              // one-based
    int column = 1;            // one-based
    int offset = 0;            // UTF-16 offset of the token in the source
    int length = 0;            // characters consumed by the token
    int labelOffset = -1;      // Shape: raw inner text span (between brackets)
    int labelLength = 0;

    // Shape tokens
    NodeShape shape = NodeShape::Rect;

    // Edge tokens
    EdgeStroke stroke = EdgeStroke::Solid;
    bool arrowStart = false;
    bool arrowEnd = true;
    bool invisible = false;    // `~~~` link
    int minLen = 1;
    QString edgeLabel;         // inline `-- text -->` label (pipe form is separate)
};

class MermaidLexer
{
public:
    explicit MermaidLexer(const QString &source);

    // The full token stream, including Sep tokens at statement boundaries.
    const QList<Token> &tokens() const { return m_tokens; }

private:
    void run();
    bool matchComment();
    bool matchEdge();
    bool matchShape();
    bool matchBraceBlock();
    void matchWord();

    QChar at(int i) const { return i < m_src.size() ? m_src.at(i) : QChar(); }
    int column() const { return m_pos - m_lineStart + 1; }
    void emitSep();

    QString m_src;
    QList<Token> m_tokens;
    int m_pos = 0;
    int m_line = 1;
    int m_lineStart = 0;
    bool m_inPipe = false;   // between `|`…`|` a `>` is label text, not a shape
};

} // namespace Mermaid

#endif // MERMAIDLEXER_H
