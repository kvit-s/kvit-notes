// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
#ifndef INLINEMARKDOWN_H
#define INLINEMARKDOWN_H

#include <QList>
#include <QString>
#include <QVariantMap>

#include "markdownformatter.h"

// The hybrid-editing transition tables: every mapping between the three
// representations a block's inline text has, as pure functions of
// (markdown, revealed spans).
//
//   storage markdown   "This is **bold** here"   (BlockModel content — truth)
//   display text       "This is bold here"       (markers stripped)
//   display + reveal   "This is **bold** here"   (revealed spans' markers shown)
//
// BlockEditorEngine drives a live QTextDocument with these; the document
// model, selection, in-document search, statistics, export and the
// collection-wide search all need the same mappings without a document, a
// theme or a QML engine. Keeping them here is what lets those callers stay
// below the presentation layer.
//
// "Revealed spans" are indexes into MarkdownFormatter::parseSpans(markdown)
// order, ascending. The reveal unit is the top-level span: a revealed span
// shows every marker in its subtree and a hidden one hides them all
// (features.md §2.2.7). The int overloads are single-span conveniences,
// where -1 means nothing is revealed.
namespace InlineMarkdown {

// One character-format run, in document coordinates. `kind` is a
// combination of SpanFormat flags (markdownformatter.h) — nested spans
// combine their ancestors' content flags into one flat range list. The
// names are mirrored here so rendering code and tests read naturally.
struct FormatRange {
    enum Kind : quint32 {
        Marker     = SpanFormat::Marker,
        Bold       = SpanFormat::Bold,
        Italic     = SpanFormat::Italic,
        BoldItalic = SpanFormat::BoldItalic,
        Strike     = SpanFormat::Strike,
        Underline  = SpanFormat::Underline,
        Code       = SpanFormat::Code,
        Highlight  = SpanFormat::Highlight,
        Link       = SpanFormat::Link,
        Superscript = SpanFormat::Superscript,
        Subscript   = SpanFormat::Subscript,
        Color       = SpanFormat::Color,
        Math        = SpanFormat::Math,
        WikiLink    = SpanFormat::WikiLink,
    };
    int start = 0;
    int length = 0;
    quint32 kind = Marker;
    // The color-span value for a Color range (empty otherwise). Per
    // instance, so it travels with the range rather than mapping from a
    // fixed token.
    QString color;
    // The link target for a Link range (empty otherwise). Per instance,
    // like color; the highlighter reads it to render a `#slug` internal
    // link muted when its slug resolves to no heading.
    QString url;

    bool operator==(const FormatRange &other) const
    {
        return start == other.start && length == other.length
               && kind == other.kind && color == other.color
               && url == other.url;
    }
};

// Result of mapping a document edit back into storage markdown.
struct EditResult {
    QString markdown;  // new storage markdown
    int mdEditEnd = 0; // markdown position just after the inserted text
};

// One search-match highlight range: the input of searchHighlightRanges is
// DocumentSearch's display coordinates, its output the document
// coordinates of the current reveal state that the highlighter paints.
struct HighlightRange {
    int start = 0;
    int length = 0;
    bool current = false; // the distinctly tinted current match

    bool operator==(const HighlightRange &other) const
    {
        return start == other.start && length == other.length
               && current == other.current;
    }
};

// A hidden inline-math span's source and where its transparent reservation
// sits in the document. The delegate overlays a rendered equation on it;
// the measurement itself needs a font and therefore stays with the engine.
struct MathSegment {
    QString tex;
    int docStart = 0;
    int docEnd = 0;
    quint32 flags = 0;
};

// Markdown with all top-level span markers stripped.
QString displayText(const QString &markdown);
// What the document must contain for a given reveal state.
QString documentText(const QString &markdown, const QList<int> &revealedSpans);
QString documentText(const QString &markdown, int revealedSpan);
// Document position -> markdown position. At a hidden span's left edge
// maps inside the span (after the opening marker); at the right edge
// maps after the closing marker — matching where a QTextCursor lands
// when the markers are inserted at the cursor.
int documentToMarkdown(const QString &markdown, const QList<int> &revealedSpans,
                       int docPos);
int documentToMarkdown(const QString &markdown, int revealedSpan, int docPos);
// Markdown position -> document position (marker interiors clamp to
// the nearest content edge when the span is hidden).
int markdownToDocument(const QString &markdown, const QList<int> &revealedSpans,
                       int mdPos);
int markdownToDocument(const QString &markdown, int revealedSpan, int mdPos);
// The span to reveal for a collapsed cursor at mdPos (inclusive), or -1.
int spanToReveal(const QString &markdown, int mdPos);
// All spans touched by the markdown range [mdStart, mdEnd] (inclusive).
QList<int> spansToRevealForRange(const QString &markdown, int mdStart, int mdEnd);
// How many top-level spans the markdown parses into.
int spanCount(const QString &markdown);
// Character formats for the whole document in a given state.
QList<FormatRange> formatRangesForState(const QString &markdown,
                                        const QList<int> &revealedSpans);
QList<FormatRange> formatRangesForState(const QString &markdown, int revealedSpan);
// Map a document edit (pos/removed/inserted on documentText(markdown,
// revealedSpans)) back into storage markdown. Deleting all of a hidden
// span's content deletes its markers too; partial deletions keep them.
EditResult applyDocumentEdit(const QString &markdown,
                             const QList<int> &revealedSpans,
                             int pos, int removedLen,
                             const QString &insertedText);
EditResult applyDocumentEdit(const QString &markdown, int revealedSpan,
                             int pos, int removedLen,
                             const QString &insertedText);
// Markdown equivalent of the document range [docStart, docEnd).
// Marker characters inside the range are ignored; every span whose
// CONTENT intersects the range contributes its markers wrapped around
// the selected content — so selecting exactly a bold word (rendered or
// revealed) copies "**word**", never a bare or doubled fragment.
QString markdownForRange(const QString &markdown, const QList<int> &revealedSpans,
                         int docStart, int docEnd);
// Inverse of markdownForRange for cut: removes the captured markdown
// (whole span including markers when its content is fully selected;
// content characters only when partially selected — the remaining
// fragment keeps its formatting). Unlike plain deletion, which per
// §2.2.7 leaves empty markers ("****") for the format-then-type
// workflow, cut+paste must round-trip. mdEditEnd is the markdown
// cursor position after the removal.
EditResult cutRangeResult(const QString &markdown, const QList<int> &revealedSpans,
                          int docStart, int docEnd);
// Document-coordinate ranges the highlighter paints for the given
// display-coordinate search matches in the given reveal state. Every
// matched character maps display → markdown (no reveals) → document
// (current reveals); the range runs from the first matched character to
// the last, so a revealed span's markers between matched characters tint
// with them. Out-of-range matches (stale against a fresher document)
// clamp or drop instead of mispainting. Verbatim mode is the identity.
QList<HighlightRange> searchHighlightRanges(const QString &markdown,
                                            const QList<int> &revealedSpans,
                                            const QList<HighlightRange> &displayMatches,
                                            bool verbatim);
// The inline math span containing a markdown position, both content
// edges inclusive — the backslash command-menu trigger gate. Returns
// {"found": bool, "mdStart"/"mdEnd": the span's markdown range including
// the $ markers, "contentStart"/"contentEnd": the markdown content range,
// "tex": the content}. {"found": false} outside any math span.
QVariantMap mathSpanRangeIn(const QString &markdown, int mdPos);
// The $ auto-pair gate: true when a $ typed at this markdown position
// should insert its closing $ too. False when the block holds an
// unmatched unescaped $ left of the caret, the previous character escapes
// it (\$), the caret sits inside an inline code span, or a letter, digit,
// or $ follows the caret. ignoreFollowing skips the following-character
// rule — the selection-wrap path, where the selection itself is what
// follows the caret.
bool shouldAutoPairDollarIn(const QString &markdown, int mdPos,
                            bool ignoreFollowing = false);
// Link span under a (collapsed) markdown position for the Ctrl+K dialog:
// {"found": bool, "start"/"end": markdown range, "text": link text,
//  "url": target, "removable": true for [text](url) links (an
//  autolink's text IS its URL — nothing to unlink)}.
QVariantMap linkSpanIn(const QString &markdown, int mdPos);
// Combined SpanFormat flags of the span chain at a document position,
// both span ends inclusive (the reveal rule) — the toolbar and formatting
// bar reflect the caret's state through this. 0 outside any span.
quint32 formatFlagsAt(const QString &markdown, const QList<int> &revealedSpans,
                      int docPos);
// URL of the deepest link or autolink span at a document position in the
// given reveal state, or an empty string (features.md §2.4 click /
// Ctrl+Click to open).
QString linkAt(const QString &markdown, const QList<int> &revealedSpans,
               int docPos);
// Every HIDDEN math span in the given reveal state, in document order. A
// revealed math span is omitted — its $…$ source shows and is editable,
// like inline code.
QList<MathSegment> hiddenMathSegments(const QString &markdown,
                                      const QList<int> &revealedSpans);
// All markers of a span's subtree in ascending markdown order, as
// (markdown position, marker text). The reveal transition inserts and
// removes exactly these.
QList<QPair<int, QString>> subtreeMarkers(const QString &markdown, int spanIndex);

} // namespace InlineMarkdown

#endif // INLINEMARKDOWN_H
