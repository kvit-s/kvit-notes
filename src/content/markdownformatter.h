// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
#ifndef MARKDOWNFORMATTER_H
#define MARKDOWNFORMATTER_H

#include <QObject>
#include <QColor>
#include <QString>
#include <QList>
#include <QVariantList>
#include <QVariantMap>

// Nesting depth and scan-work accounting for one inline parse. Defined in the
// implementation: nothing outside it needs the fields, and the public entry
// points create it themselves.
struct MarkdownParseState;

// Character-format flags a span imposes on its content. Defined here — with
// the span-type registry — because they are properties of the span types;
// BlockEditorEngine::FormatRange mirrors these values for rendering. A range
// may combine several flags (***x*** is Bold | Italic).
namespace SpanFormat {
enum Flags : quint32 {
    Marker     = 0x1,  // marker characters of a revealed span (muted color)
    Bold       = 0x2,
    Italic     = 0x4,
    BoldItalic = Bold | Italic,
    Strike     = 0x8,
    Underline  = 0x10,
    Code       = 0x20, // monospace + background (features.md §2.1)
    Highlight  = 0x40, // background tint
    Link       = 0x80, // accent color + underline; span carries a url
    // §2.1 raised/lowered smaller text; Pandoc's ^x^ / ~x~ syntax.
    Superscript = 0x100,
    Subscript   = 0x200,
    // §2.1 text color, the one recognized inline-HTML form:
    // <span style="color:VALUE">…</span>. The color VALUE is per-instance data
    // (like a link's url), carried on FormattedSpan::color and painted from
    // there, not from a fixed theme token.
    Color       = 0x400,
    // §1.2.15 inline math $x^2$: verbatim TeX content with the Pandoc adjacency
    // rule. Hidden, the content renders invisibly at renderer-measured width
    // and the delegate overlays the equation; revealed, it shows the raw $…$
    // source like inline code.
    Math        = 0x800,
    // [[wiki-link]] note references. Always set together with Link — the span
    // carries a kvit-note: url so click paths work unchanged — and lets
    // styling/resolution diverge from web links: an unresolved wiki target
    // renders muted through the engine's collection-backed resolver rather
    // than the outline's slug set.
    WikiLink    = 0x1000,
};
}

struct FormattedSpan {
    int start;         // Position in raw Markdown (start of opening marker)
    int end;           // End position in raw Markdown (after closing marker)
    int displayStart;  // Position in rendered text (without markers)
    int displayEnd;    // End position in rendered text
    QString type;      // Registry name: "bold", "italic", "bolditalic", ...
    QString rawText;   // The raw markdown including markers

    // Marker lengths are per-instance, not per-type: a link's closing
    // marker "](url)" varies with the URL, and an autolink has none.
    int openLen = 0;
    int closeLen = 0;
    quint32 formatFlags = 0;   // SpanFormat flags of the content
    QString url;               // link/autolink target, empty otherwise
    QString color;             // color-span CSS value, empty otherwise

    // Nested spans inside this span's content, absolute coordinates.
    // Empty for verbatim content (inline code).
    QList<FormattedSpan> children;

    QString openMarker() const { return rawText.left(openLen); }
    QString closeMarker() const { return rawText.right(closeLen); }
    int contentLength() const { return end - start - openLen - closeLen; }

    QVariantMap toVariantMap() const {
        return {
            {"start", start},
            {"end", end},
            {"displayStart", displayStart},
            {"displayEnd", displayEnd},
            {"type", type},
            {"rawText", rawText},
            {"openLen", openLen},
            {"closeLen", closeLen},
            {"url", url},
            {"color", color}
        };
    }
};

class MarkdownFormatter : public QObject
{
    Q_OBJECT

public:
    explicit MarkdownFormatter(QObject *parent = nullptr);

    // Convert Markdown to HTML for display
    Q_INVOKABLE QString toHtml(const QString &markdown) const;

    // toHtml for rich text that Qt itself will lay out and paint — the table
    // cell path. The one difference is inline math: a `$…$` span becomes an
    // <img> against the math image provider, so a cell that is not being
    // edited shows the equation rather than its TeX source. `mathPixelSize`
    // and `mathColor` select the cached bitmap exactly as they do for the
    // inline-math overlay, and `devicePixelRatio` keeps it sharp on a scaled
    // display. A formula that does not parse falls back to its source, which
    // is what the overlay shows for the same case.
    //
    // toHtml() itself stays TeX-only because its other callers put their HTML
    // on the clipboard, where an image://math URL means nothing outside this
    // process.
    Q_INVOKABLE QString toHtmlWithMath(const QString &markdown,
                                       int mathPixelSize,
                                       const QColor &mathColor,
                                       qreal devicePixelRatio = 1.0) const;

    // Convert HTML back to Markdown (for paste operations)
    Q_INVOKABLE QString toMarkdown(const QString &html) const;

    // Get list of formatted spans in the text
    Q_INVOKABLE QVariantList getFormattedSpans(const QString &markdown) const;

    // Check if cursor is inside a formatted region
    Q_INVOKABLE bool isInsideFormattedRegion(const QString &markdown, int cursorPos) const;

    // Get the span containing the cursor (returns empty if not in a span)
    Q_INVOKABLE QVariantMap getSpanAtCursor(const QString &markdown, int cursorPos) const;

    // Map markdown position to display position (without formatting markers)
    Q_INVOKABLE int markdownToDisplayPosition(const QString &markdown, int mdPos) const;

    // Map display position to markdown position
    Q_INVOKABLE int displayToMarkdownPosition(const QString &markdown, int displayPos) const;

    // Fold every line break in one block's markdown into a single space:
    // what turns hard-wrapped prose — an imported file wrapped at 80
    // columns, a pasted answer — back into one flowing block. A run of
    // breaks and the whitespace around it collapse together, so the join
    // never leaves a double space, and the result is trimmed. Callers
    // exclude the blocks whose newlines are content (code, fences).
    Q_INVOKABLE QString joinLines(const QString &markdown) const;

    // A quote's attribution line: the last line of a multi-line quote
    // starting with an em dash and a space, returned with its newline, and
    // empty when there is none. The delegate renders it as chrome below the
    // body and the join-lines command leaves it on its own line, so the rule
    // lives here once rather than in each of them.
    Q_INVOKABLE QString attributionTail(const QString &content) const;

    // Registry-driven formatting commands.
    // Positions are markdown coordinates. Apply wraps the selection in the
    // type's canonical markers (a collapsed cursor gets an empty marker
    // pair for the format-then-type workflow); remove restructures the
    // deepest span containing the selection whose flags overlap the
    // type's — removing bold from ***x*** leaves *x*; toggle picks one.
    Q_INVOKABLE QString applySpanType(const QString &text, int selectionStart,
                                      int selectionEnd, const QString &type) const;
    Q_INVOKABLE QString removeSpanType(const QString &text, int selectionStart,
                                       int selectionEnd, const QString &type) const;
    Q_INVOKABLE QString toggleSpanType(const QString &text, int selectionStart,
                                       int selectionEnd, const QString &type) const;

    // Text color (features.md §2.1). Positions
    // are markdown coordinates. applyColor wraps the selection in a color
    // span, or — when the selection is exactly an existing color span's
    // content — rewrites that span's value in place. removeColor unwraps the
    // color span covering the selection, keeping its content. colorSpanAt
    // reports the color span (if any) covering the selection, for the UI to
    // reflect the current color and enable "remove". valueValue must already
    // be a recognized value (#rgb, #rrggbb, or a CSS named color); the caller
    // supplies it from the palette or picker.
    Q_INVOKABLE QString applyColor(const QString &text, int selectionStart,
                                   int selectionEnd, const QString &colorValue) const;
    Q_INVOKABLE QString removeColor(const QString &text, int selectionStart,
                                    int selectionEnd) const;
    Q_INVOKABLE QVariantMap colorSpanAt(const QString &text, int selectionStart,
                                        int selectionEnd) const;

    // Bold/italic conveniences (delegate to the generic commands)
    Q_INVOKABLE QString applyBold(const QString &text, int selectionStart, int selectionEnd) const;
    Q_INVOKABLE QString applyItalic(const QString &text, int selectionStart, int selectionEnd) const;
    Q_INVOKABLE QString removeBold(const QString &text, int selectionStart, int selectionEnd) const;
    Q_INVOKABLE QString removeItalic(const QString &text, int selectionStart, int selectionEnd) const;
    Q_INVOKABLE QString toggleBold(const QString &text, int selectionStart, int selectionEnd) const;
    Q_INVOKABLE QString toggleItalic(const QString &text, int selectionStart, int selectionEnd) const;

    // Structured span access (used by BlockEditorEngine's highlighter).
    // getMarkerLength is invokable: QML uses it for the collapsed-cursor
    // offset when toggling a type.
    QList<FormattedSpan> parseSpans(const QString &markdown) const;
    Q_INVOKABLE int getMarkerLength(const QString &type) const;
    QString getMarkerString(const QString &type) const;

private:
    QString escapeHtml(const QString &text) const;
    // escapeHtml for text whose spacing is content — the inside of a code
    // span. Rich text collapses a run of spaces to one and drops the ones
    // that start a line, which flattens an indented listing into a single
    // run, so those spaces are emitted as non-breaking ones.
    QString escapeHtmlKeepingSpaces(const QString &text) const;
    QString convertSpanToHtml(const QString &text, const QString &type) const;
    QString extractInnerText(const QString &rawText, const QString &type) const;
    // The recursive worker behind parseSpans(). The state is threaded through
    // every level so nesting depth and total scanning work are properties of
    // the whole parse rather than of one substring.
    QList<FormattedSpan> parseSpans(const QString &markdown,
                                    MarkdownParseState &state) const;
    // How an inline math span renders: as its bare TeX (the clipboard path)
    // or as an image from the math provider (the table cell path).
    struct MathImages {
        bool enabled = false;
        int pixelSize = 15;
        QColor color;
        qreal devicePixelRatio = 1.0;
    };
    // The <img> for one formula, or an empty string when it does not parse —
    // the caller then keeps the TeX source visible.
    QString mathImageTag(const QString &tex, const MathImages &opt) const;
    // HTML for a span and for the plain text between spans. Containers render
    // from the parsed child tree, so a link inside bold stays a link.
    QString renderSpanHtml(const QString &markdown,
                           const FormattedSpan &span,
                           const MathImages &math) const;
    QString renderRangeHtml(const QString &markdown,
                            const QList<FormattedSpan> &spans,
                            int from, int to,
                            const MathImages &math) const;
};

#endif // MARKDOWNFORMATTER_H
