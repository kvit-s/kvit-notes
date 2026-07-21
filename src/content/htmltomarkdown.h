// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
#ifndef HTMLTOMARKDOWN_H
#define HTMLTOMARKDOWN_H

#include <QObject>
#include <QString>

class QTextBlock;
class QTextFrame;

// Converts external HTML (a browser or word-processor clipboard payload) into
// Kvit's markdown, for the HTML arm of the smart-paste matrix (features.md
// §5.3 "detects and converts HTML").
//
// The parse is delegated to QTextDocument rather than done with regular
// expressions: Qt's rich-text importer already handles real-world tag soup —
// unclosed tags, attribute quoting, entities, nesting — and hands back a
// structured document. This class only walks that structure and maps it onto
// markdown, so the fragile part of HTML handling is Qt's, not ours.
//
// Deliberately NOT the same thing as MarkdownFormatter::toMarkdown(), which
// exists to reverse MarkdownFormatter::toHtml() — a small, known tag subset
// this project emits itself. That one stays as-is for the display round trip.
class HtmlToMarkdown : public QObject
{
    Q_OBJECT

public:
    explicit HtmlToMarkdown(QObject *parent = nullptr);

    // Markdown for an HTML clipboard payload. Blocks are separated by a blank
    // line, as elsewhere in the serializer. Returns an empty string when the
    // payload carries no text.
    Q_INVOKABLE QString convert(const QString &html) const;

    // Whether the payload is worth converting at all: HTML that carries no
    // element markup beyond a wrapper is better pasted as its plain text, so
    // the caller can skip the conversion and keep the source's own spacing.
    Q_INVOKABLE bool hasStructure(const QString &html) const;

private:
    // Markdown for one block's inline content: the fragment run with bold,
    // italic, strikethrough, inline code, and links applied. `suppressBold`
    // drops the bold markers where the surrounding construct already implies
    // weight (a heading prefix, a table header row).
    QString inlineMarkdown(const QTextBlock &block,
                           bool suppressBold = false) const;
    // Markdown for every block in a frame, recursing into nested frames and
    // rendering QTextTable frames as pipe tables.
    QString frameMarkdown(QTextFrame *frame) const;
    // The block-level prefix ("## ", "- ", "> ", …) plus its content.
    QString blockMarkdown(const QTextBlock &block) const;
};

#endif // HTMLTOMARKDOWN_H
