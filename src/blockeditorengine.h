// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
#ifndef BLOCKEDITORENGINE_H
#define BLOCKEDITORENGINE_H

#include <QObject>
#include <QPointer>
#include <QString>
#include <QList>
#include <QHash>
#include <QVariantMap>
#include <QFont>
#include <QTextDocument>
#include <QQuickTextDocument>
#include <QQmlParserStatus>
#include <QtQml/qqmlregistration.h>

#include "markdownformatter.h"
#include "theme.h"
#include "documentoutline.h"
// Full include (not a forward declaration): moc requires pointer Q_PROPERTY
// types to be complete when it registers their meta types.
#include "notecollection.h"

class MarkdownHighlighter;

// The hybrid-editing engine. One instance
// per block delegate, attached to the delegate TextArea's QTextDocument via
// the `document` property. Owns the relationship between three
// representations:
//
//   storage markdown   "This is **bold** here"   (BlockModel content — truth)
//   display text       "This is bold here"       (markers stripped)
//   display + reveal   "This is **bold** here"   (cursor span's markers shown)
//
// The document normally holds the display text; when the cursor touches a
// span — or a selection touches one or more spans (§2.2.4) — the engine
// inserts those spans' markers with a QTextCursor and removes them when the
// cursor/selection leaves. A QSyntaxHighlighter styles span content in both
// states and mutes revealed markers. The recognized inline types are the
// rows of the span-type registry (markdownformatter.cpp); ***bolditalic***
// is its own span type. Spans nest (except inside inline code, whose
// content is verbatim): nested content combines its ancestors' styles, and
// the reveal unit is the top-level span — all markers in its subtree show
// and hide together (features.md §2.2.7).
//
// Reveal rules (features.md §2.2):
//  - Collapsed cursor: the span whose markdown range [start, end] contains
//    the cursor position, INCLUSIVE. The inclusive edges give the spec's
//    boundary behaviors for free: backspacing at a rendered span's edge
//    first reveals it (§2.2.4), and a span whose closing marker was just
//    typed stays revealed until the cursor moves on (§2.2.5, example 2).
//  - Selection: every span whose markdown range touches the selection.
//  - No reveal while cursorActive is false (block unfocused).
//
// Sync rules:
//  - model -> engine: setMarkdown() (QML binding on delegate.content);
//    updates the document with a minimal prefix/suffix diff under the
//    internal-edit guard, which keeps the cursor stable across undo/redo
//    and load rebuilds.
//  - user -> model: document edits NOT made under the guard are mapped back
//    into storage markdown and emitted via markdownEdited(); the delegate
//    forwards that to BlockModel. The engine never talks to the model.
//  - Reveal transitions change only marker characters and are never user
//    edits: they emit nothing and are not undoable (document undo is off;
//    undo/redo lives in the model-level UndoStack).
//  - Reveal evaluation is deferred to a clean stack: the triggers arrive
//    from inside QQuickTextEdit's signal dispatch, where mutating the
//    document makes QQuickTextEdit re-assert stale cached text.
class BlockEditorEngine : public QObject, public QQmlParserStatus
{
    Q_OBJECT
    QML_ELEMENT
    Q_INTERFACES(QQmlParserStatus)
    Q_PROPERTY(QQuickTextDocument *document READ document WRITE setDocument NOTIFY documentChanged)
    Q_PROPERTY(QString markdown READ markdown WRITE setMarkdown NOTIFY markdownChanged)
    Q_PROPERTY(int cursorPosition READ cursorPosition WRITE setCursorPosition NOTIFY cursorPositionChanged)
    Q_PROPERTY(int selectionStart READ selectionStart WRITE setSelectionStart NOTIFY selectionChanged)
    Q_PROPERTY(int selectionEnd READ selectionEnd WRITE setSelectionEnd NOTIFY selectionChanged)
    Q_PROPERTY(bool cursorActive READ cursorActive WRITE setCursorActive NOTIFY cursorActiveChanged)
    // Verbatim mode (code blocks): no inline parsing, no reveal — the
    // document text IS the markdown and every mapping is the identity.
    Q_PROPERTY(bool verbatim READ verbatim WRITE setVerbatim NOTIFY verbatimChanged)
    // Code-block language for syntax highlighting. Only consulted in verbatim
    // mode; an empty or unrecognized value paints no code colors (a plain
    // monospace code block). A change rehighlights the attached document.
    Q_PROPERTY(QString codeLanguage READ codeLanguage WRITE setCodeLanguage NOTIFY codeLanguageChanged)
    // Search matches to tint: a list of {"start", "length", "current"} in
    // display coordinates, bound by the delegate from
    // documentSearch.matchesForBlock(). Rendering maps them through the
    // current reveal state per highlight pass, so reveal transitions move the
    // tint with the text. Verbatim engines paint them too (code blocks skip
    // span styling only).
    Q_PROPERTY(QVariantList searchMatches READ searchMatches WRITE setSearchMatches NOTIFY searchMatchesChanged)
    // Theme tokens for the highlighter. Optional: with no theme set the
    // engine styles with its built-in (light) constants, which keeps every
    // theme-unaware test running unchanged. A theme change rehighlights the
    // attached document.
    Q_PROPERTY(Theme *theme READ theme WRITE setTheme NOTIFY themeChanged)
    // Internal-link resolver: the DocumentOutline, set from QML so the
    // highlighter can render a `[text](#slug)` link whose slug matches no
    // heading in the muted "unresolved" style. Optional — with none set every
    // internal link renders as an ordinary link. A change to the outline's
    // slug set rehighlights (coalesced, off the keystroke path).
    Q_PROPERTY(DocumentOutline *linkResolver READ linkResolver WRITE setLinkResolver NOTIFY linkResolverChanged)
    // Wiki-link resolver: the NoteCollection, set from QML so the highlighter
    // can render a [[target]] whose target matches no note in the muted
    // "unresolved" style. Optional — with none set every wiki-link renders as
    // an ordinary link. A collection revision bump rehighlights (coalesced,
    // off the keystroke path).
    Q_PROPERTY(NoteCollection *wikiResolver READ wikiResolver WRITE setWikiResolver NOTIFY wikiResolverChanged)
    // Typography (features.md §10.2): the proportional line height applied as
    // block format (TextEdit has no lineHeight property), and the monospace
    // family inline `code` spans render with. Both are plain values so the
    // engine stays decoupled from the Typography settings object.
    Q_PROPERTY(qreal lineHeight READ lineHeight WRITE setLineHeight NOTIFY lineHeightChanged)
    Q_PROPERTY(QString monoFontFamily READ monoFontFamily WRITE setMonoFontFamily NOTIFY monoFontFamilyChanged)
    Q_PROPERTY(int contentFontPixelSize READ contentFontPixelSize WRITE setContentFontPixelSize NOTIFY contentFontChanged)
    // The optically matched math pixel size for the current content font
    // (MathRenderer::opticalMathPixelSize): inline reservation, overlay
    // metrics, and the QML overlay images must all use this same size.
    Q_PROPERTY(int mathFontPixelSize READ mathFontPixelSize NOTIFY contentFontChanged)
    Q_PROPERTY(QString contentFontFamily READ contentFontFamily WRITE setContentFontFamily NOTIFY contentFontChanged)
    Q_PROPERTY(int contentFontWeight READ contentFontWeight WRITE setContentFontWeight NOTIFY contentFontChanged)

public:
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

    explicit BlockEditorEngine(QObject *parent = nullptr);
    ~BlockEditorEngine() override;

    void classBegin() override;
    void componentComplete() override;

    QQuickTextDocument *document() const;
    void setDocument(QQuickTextDocument *document);

    QString markdown() const;
    void setMarkdown(const QString &markdown);

    int cursorPosition() const;
    void setCursorPosition(int position);

    int selectionStart() const;
    void setSelectionStart(int position);
    int selectionEnd() const;
    void setSelectionEnd(int position);

    bool cursorActive() const;
    void setCursorActive(bool active);

    bool verbatim() const;
    void setVerbatim(bool verbatim);

    QString codeLanguage() const { return m_codeLanguage; }
    void setCodeLanguage(const QString &language);

    QVariantList searchMatches() const;
    void setSearchMatches(const QVariantList &matches);

    Theme *theme() const { return m_theme; }
    void setTheme(Theme *theme);

    DocumentOutline *linkResolver() const { return m_linkResolver; }
    void setLinkResolver(DocumentOutline *resolver);

    NoteCollection *wikiResolver() const { return m_wikiResolver; }
    void setWikiResolver(NoteCollection *resolver);

    qreal lineHeight() const { return m_lineHeight; }
    void setLineHeight(qreal height);
    QString monoFontFamily() const { return m_monoFontFamily; }
    void setMonoFontFamily(const QString &family);
    int contentFontPixelSize() const { return m_contentFontPixelSize; }
    void setContentFontPixelSize(int size);
    QString contentFontFamily() const { return m_contentFontFamily; }
    void setContentFontFamily(const QString &family);
    int contentFontWeight() const { return m_contentFontWeight; }
    void setContentFontWeight(int weight);
    int mathFontPixelSize() const;

    // Position mapping between the current document text and storage
    // markdown, honoring the current reveal state. Used by QML for
    // split/merge/formatting operations that address model content.
    Q_INVOKABLE int toMarkdownPosition(int documentPosition) const;
    Q_INVOKABLE int toDocumentPosition(int markdownPosition) const;

    // Copy-as-markdown: the markdown equivalent
    // of a document-coordinate selection in the current reveal state.
    Q_INVOKABLE QString markdownForRange(int docStart, int docEnd) const;
    // Cut: the markdown left after removing what markdownForRange captured
    // for the same selection, plus the markdown cursor position. Returns
    // {"markdown": string, "cursor": int}.
    Q_INVOKABLE QVariantMap cutRange(int docStart, int docEnd) const;
    // Markdown with formatting stripped (paste-plain, Ctrl+Shift+V).
    Q_INVOKABLE QString stripFormatting(const QString &markdown) const;

    // URL of the deepest link or autolink span at a document position in
    // the current reveal state, or an empty string (features.md §2.4
    // click / Ctrl+Click to open).
    // Combined SpanFormat flags of the span chain at this document
    // position, both span ends inclusive (the reveal rule) — the
    // toolbar and formatting bar reflect the caret's state through
    // this. 0 in verbatim mode and outside any span.
    Q_INVOKABLE int formatFlagsAtDocumentPosition(int docPos) const;

    Q_INVOKABLE QString linkAtDocumentPosition(int docPos) const;

    // Inline-math boxes to overlay: for each HIDDEN math
    // span in the current reveal state, {tex, docStart, docEnd, width, height,
    // baseline, depth, valid, error}. Document ranges are in document
    // coordinates; metrics are MicroTeX logical pixels at the content font
    // size. The delegate positions an image://math Image over the
    // renderer-width transparent TeX box, repositioning on relayout and
    // reveal. A revealed math span is omitted — its $…$ source shows and is
    // editable, like inline code. Empty in verbatim mode.
    Q_INVOKABLE QVariantList inlineMathBoxes() const;

    // The inline math span containing a document position, both content
    // edges inclusive — the backslash command-menu trigger gate. Returns
    // {"found": bool, "mdStart"/"mdEnd": the span's markdown range including
    // the $ markers, "contentStart"/"contentEnd": the markdown content range,
    // "docContentStart"/"docContentEnd": that content range in document
    // coordinates for the current reveal state, "tex": the content}.
    // {"found": false} in verbatim mode or outside any math span.
    Q_INVOKABLE QVariantMap mathSpanRangeAt(int docPos) const;

    // The $ auto-pair gate: true when a $ typed at this document position
    // should insert its closing $ too. False when the block holds an
    // unmatched unescaped $ left of the caret, the previous character escapes
    // it (\$), the caret sits inside an inline code span, or a letter, digit,
    // or $ follows the caret. Always false in verbatim mode. ignoreFollowing
    // skips the following-character rule — the selection-wrap path, where the
    // selection itself is what follows the caret.
    Q_INVOKABLE bool shouldAutoPairDollar(int docPos,
                                          bool ignoreFollowing = false) const;

    // Link span under a (collapsed) cursor for the Ctrl+K dialog:
    // {"found": bool, "start"/"end": markdown range, "text": link text,
    //  "url": target, "removable": true for [text](url) links (an
    //  autolink's text IS its URL — nothing to unlink)}.
    Q_INVOKABLE QVariantMap linkSpanAtCursor(int docPos) const;

    // Currently revealed spans (indexes into parseSpans order, ascending).
    // revealedSpan() is the single-cursor convenience: first revealed
    // index or -1. Exposed for tests.
    QList<int> revealedSpans() const { return m_revealedSpans; }
    int revealedSpan() const { return m_revealedSpans.isEmpty() ? -1 : m_revealedSpans.first(); }

    // ---- Pure state functions (the "transition tables"; all unit-testable
    // without a GUI). revealedSpans are indexes into
    // MarkdownFormatter::parseSpans(markdown) order. The int overloads are
    // single-span conveniences (-1 = none). ----

    // Markdown with all top-level span markers stripped.
    static QString displayText(const QString &markdown);
    // What the document must contain for a given reveal state.
    static QString documentText(const QString &markdown, const QList<int> &revealedSpans);
    static QString documentText(const QString &markdown, int revealedSpan);
    // Document position -> markdown position. At a hidden span's left edge
    // maps inside the span (after the opening marker); at the right edge
    // maps after the closing marker — matching where a QTextCursor lands
    // when the markers are inserted at the cursor (see spike notes).
    static int documentToMarkdown(const QString &markdown, const QList<int> &revealedSpans, int docPos);
    static int documentToMarkdown(const QString &markdown, int revealedSpan, int docPos);
    // Markdown position -> document position (marker interiors clamp to
    // the nearest content edge when the span is hidden).
    static int markdownToDocument(const QString &markdown, const QList<int> &revealedSpans, int mdPos);
    static int markdownToDocument(const QString &markdown, int revealedSpan, int mdPos);
    // The span to reveal for a collapsed cursor at mdPos (inclusive), or -1.
    static int spanToReveal(const QString &markdown, int mdPos);
    // All spans touched by the markdown range [mdStart, mdEnd] (inclusive).
    static QList<int> spansToRevealForRange(const QString &markdown, int mdStart, int mdEnd);
    // Character formats for the whole document in a given state.
    static QList<FormatRange> formatRangesForState(const QString &markdown, const QList<int> &revealedSpans);
    static QList<FormatRange> formatRangesForState(const QString &markdown, int revealedSpan);
    // Map a document edit (pos/removed/inserted on documentText(markdown,
    // revealedSpans)) back into storage markdown. Deleting all of a hidden
    // span's content deletes its markers too; partial deletions keep them.
    static EditResult applyDocumentEdit(const QString &markdown, const QList<int> &revealedSpans,
                                        int pos, int removedLen, const QString &insertedText);
    static EditResult applyDocumentEdit(const QString &markdown, int revealedSpan,
                                        int pos, int removedLen, const QString &insertedText);
    // Markdown equivalent of the document range [docStart, docEnd).
    // Marker characters inside the range are ignored; every span whose
    // CONTENT intersects the range contributes its markers wrapped around
    // the selected content — so selecting exactly a bold word (rendered or
    // revealed) copies "**word**", never a bare or doubled fragment.
    static QString markdownForRange(const QString &markdown, const QList<int> &revealedSpans,
                                    int docStart, int docEnd);
    // Document-coordinate ranges the highlighter paints for the given
    // display-coordinate search matches in the given reveal state. Every
    // matched character maps display → markdown (no reveals) → document
    // (current reveals); the range runs from the first matched character to
    // the last, so a revealed span's markers between matched characters tint
    // with them. Out-of-range matches (stale against a fresher document)
    // clamp or drop instead of mispainting. Verbatim mode is the identity.
    static QList<HighlightRange> searchHighlightRanges(const QString &markdown,
                                                       const QList<int> &revealedSpans,
                                                       const QList<HighlightRange> &displayMatches,
                                                       bool verbatim);
    // Pure forms of the math-entry gates above, on explicit markdown and
    // a markdown position (unit-testable without a document).
    static QVariantMap mathSpanRangeIn(const QString &markdown, int mdPos);
    static bool shouldAutoPairDollarIn(const QString &markdown, int mdPos,
                                       bool ignoreFollowing = false);
    // Inverse of markdownForRange for cut: removes the captured markdown
    // (whole span including markers when its content is fully selected;
    // content characters only when partially selected — the remaining
    // fragment keeps its formatting). Unlike plain deletion, which per
    // §2.2.7 leaves empty markers ("****") for the format-then-type
    // workflow, cut+paste must round-trip. mdEditEnd is the markdown
    // cursor position after the removal.
    static EditResult cutRangeResult(const QString &markdown, const QList<int> &revealedSpans,
                                     int docStart, int docEnd);

    // Test hook: attach directly to a bare QTextDocument (no QML layer).
    void attachDocument(QTextDocument *doc);
    QTextDocument *textDocument() const { return m_doc; }
    int rebuildCountForTesting() const { return m_rebuildCountForTesting; }
    int rehighlightCountForTesting() const { return m_rehighlightCountForTesting; }

signals:
    void documentChanged();
    void markdownChanged();
    void cursorPositionChanged();
    void selectionChanged();
    void cursorActiveChanged();
    void verbatimChanged();
    void codeLanguageChanged();
    void searchMatchesChanged();
    void themeChanged();
    void linkResolverChanged();
    void wikiResolverChanged();
    void lineHeightChanged();
    void monoFontFamilyChanged();
    void contentFontChanged();
    // Emitted only for user edits of the document — never for engine or
    // model-driven rebuilds or reveal transitions. The delegate forwards
    // this to the model.
    void markdownEdited(const QString &markdown);

private:
    void onContentsChange(int position, int charsRemoved, int charsAdded);
    // The document text the current state calls for (markdown itself in
    // verbatim mode).
    QString stateText() const;
    void requestRebuild();
    void requestRehighlight();
    void flushPendingRenderUpdate();
    void rebuildDocument(bool runRehighlight = true);
    void applyMinimalDiff(const QString &expected);
    void scheduleRevealUpdate();
    void updateRevealState();
    void transitionTo(const QList<int> &revealedSpans);
    void rehighlight();
    void applyLineHeight();
    void applyVerticalAlignmentFormats();
    QVariantMap cachedMathMetrics(const QString &tex, int textSizePx) const;
    int effectiveContentFontPixelSize() const;
    QFont contentFontForFlags(quint32 flags) const;
    QVariantMap mathReservationMetrics(const QString &tex, quint32 flags) const;

    QPointer<QQuickTextDocument> m_quickDocument;
    QPointer<QTextDocument> m_doc;
    MarkdownHighlighter *m_highlighter = nullptr;
    QString m_markdown;
    QList<int> m_revealedSpans; // sorted ascending
    int m_cursorPos = 0;
    int m_selStart = 0;
    int m_selEnd = 0;
    bool m_cursorActive = false;
    bool m_verbatim = false;
    QString m_codeLanguage;
    QPointer<Theme> m_theme;
    QPointer<DocumentOutline> m_linkResolver;
    QPointer<NoteCollection> m_wikiResolver;
    qreal m_lineHeight = 1.0;
    QString m_monoFontFamily = QStringLiteral("monospace");
    int m_contentFontPixelSize = 15;
    QString m_contentFontFamily;
    int m_contentFontWeight = QFont::Normal;
    bool m_hadVerticalFormats = false;
    QVariantList m_searchMatchesVariant;
    QList<HighlightRange> m_searchMatches;
    mutable QHash<QString, QVariantMap> m_mathMetricsCache;
    // Single guard flag: set around every programmatic
    // document edit so the engine can tell its own edits from the user's.
    bool m_internalEdit = false;
    bool m_revealUpdateQueued = false;
    bool m_componentComplete = true;
    bool m_pendingRebuild = false;
    bool m_pendingRehighlight = false;
    int m_rebuildCountForTesting = 0;
    int m_rehighlightCountForTesting = 0;

    friend class MarkdownHighlighter;
};

#endif // BLOCKEDITORENGINE_H
