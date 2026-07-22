// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
#include "blockeditorengine.h"
#include "codelanguages.h"
#include "markdownformatter.h"
#include "mathrenderer.h"
#include "theme.h"
#include "documentoutline.h"
#include "notecollection.h"
#include "perflog.h"

#include <QQuickTextDocument>
#include <QSyntaxHighlighter>
#include <QFontMetricsF>
#include <QTextBlock>
#include <QTextCharFormat>
#include <QTextCursor>
#include <QTextDocument>
#include <QTextLayout>
#include <QDebug>
#include <QtMath>
#include <algorithm>

namespace {

// Inline-style fallbacks (features.md §2.1, §2.2): the light theme's
// values, used by engines with no theme set (unit tests, and any
// context that never installs one). With a theme set, the highlighter
// reads the equivalent tokens from it.
// The search tints are blue, not yellow, so a match inside a
// ==highlight== span stays distinguishable; the current match is the
// strong one the eye lands on.
const QColor kMarkerColor("#b8b8b8");          // muted markdown markers
const QColor kHighlightBackground("#fdf3a9");  // ==highlight== tint
const QColor kCodeBackground("#f0f0ee");       // `code` background
const QColor kLinkColor("#2970c8");            // link text accent
const QColor kUnresolvedLinkColor("#9a9a9a");  // muted: #slug with no heading
const QColor kSearchMatchBackground("#b5dcff");
const QColor kSearchCurrentBackground("#ffb454");
// Code-highlight fallbacks (light theme's values) for engines with no
// theme set — mirrors the inline fallbacks above.
const QColor kCodeKeyword("#a626a4");
const QColor kCodeType("#4078f2");
const QColor kCodeString("#50a14f");
const QColor kCodeComment("#a0a1a7");
const QColor kCodeNumber("#986801");

QList<int> singleSpanList(int revealedSpan)
{
    if (revealedSpan < 0)
        return {};
    return {revealedSpan};
}

QString mathMetricsCacheKey(const QString &tex, int textSizePx)
{
    return QString::number(textSizePx) + QChar(0x1f) + tex;
}

QVariantMap mathMetricsMap(const MathRenderer::Metrics &metrics)
{
    return QVariantMap{
        {QStringLiteral("width"), metrics.width},
        {QStringLiteral("height"), metrics.height},
        {QStringLiteral("baseline"), metrics.baseline},
        {QStringLiteral("ascent"), metrics.ascent},
        {QStringLiteral("descent"), metrics.descent},
        {QStringLiteral("depth"), metrics.depth},
        {QStringLiteral("valid"), metrics.valid},
        {QStringLiteral("error"), metrics.error},
    };
}

int inlineMathVerticalPaddingPx(int textSizePx)
{
    return qMax(2, qCeil(qMax(1, textSizePx) * 0.12));
}

int fontPixelSizeForLineHeight(const QFont &baseFont, int basePixelSize,
                               qreal targetHeight)
{
    int pixelSize = qMax(1, basePixelSize);
    QFont probe(baseFont);
    probe.setPixelSize(pixelSize);
    while (QFontMetricsF(probe).height() < targetHeight && pixelSize < 256) {
        probe.setPixelSize(++pixelSize);
    }
    return pixelSize;
}

} // namespace

// Styles the document from the engine's state: hidden span content keeps
// its bold/italic format; a revealed span additionally shows its markers in
// the muted color. QSyntaxHighlighter works per text block, so the engine's
// document-coordinate ranges are offset into block-local positions.
class MarkdownHighlighter : public QSyntaxHighlighter
{
public:
    explicit MarkdownHighlighter(QTextDocument *doc, BlockEditorEngine *engine)
        : QSyntaxHighlighter(doc)
        , m_engine(engine)
    {
    }

protected:
    void highlightBlock(const QString &text) override
    {
        const int blockPos = currentBlock().position();
        if (m_engine->m_verbatim)
            applyCodeFormats(text);
        else
            applySpanFormats(text, blockPos);
        applySearchOverlay(text, blockPos);
    }

private:
    // Code-block syntax highlighting. Runs only in verbatim mode. Multi-line
    // constructs (block comments, triple-quoted strings) ride
    // QSyntaxHighlighter's block-state mechanism: the previous line's
    // carry-state feeds this line's scan, and this line's end-state is
    // published so a change re-runs the following block. An empty or
    // unrecognized language paints nothing — a plain monospace code block.
    void applyCodeFormats(const QString &text)
    {
        const QString &lang = m_engine->m_codeLanguage;
        if (lang.isEmpty() || !CodeLanguages::isSupported(lang)) {
            setCurrentBlockState(-1);
            return;
        }
        const int startState = qMax(0, previousBlockState());
        const CodeLanguages::LineResult res =
            CodeLanguages::highlightLine(lang, text, startState);
        setCurrentBlockState(res.endState);

        const Theme *theme = m_engine->m_theme;
        for (const CodeLanguages::Span &span : res.spans) {
            QColor color;
            switch (span.token) {
            case CodeLanguages::Token::Keyword:
                color = theme ? theme->codeKeyword() : kCodeKeyword; break;
            case CodeLanguages::Token::Type:
                color = theme ? theme->codeType() : kCodeType; break;
            case CodeLanguages::Token::String:
                color = theme ? theme->codeString() : kCodeString; break;
            case CodeLanguages::Token::Comment:
                color = theme ? theme->codeComment() : kCodeComment; break;
            case CodeLanguages::Token::Number:
                color = theme ? theme->codeNumber() : kCodeNumber; break;
            case CodeLanguages::Token::Plain:
                continue;
            }
            QTextCharFormat format;
            format.setForeground(color);
            setFormat(span.start, span.length, format);
        }
    }

    void applySpanFormats(const QString &text, int blockPos)
    {
        const auto ranges = InlineMarkdown::formatRangesForState(
            m_engine->m_markdown, m_engine->m_revealedSpans);
        for (const auto &range : ranges) {
            const int localStart = range.start - blockPos;
            if (localStart + range.length <= 0 || localStart >= text.length())
                continue;

            const Theme *theme = m_engine->m_theme;
            QTextCharFormat format;
            if (range.kind & BlockEditorEngine::FormatRange::Marker)
                format.setForeground(theme ? theme->marker() : kMarkerColor);
            if (range.kind & BlockEditorEngine::FormatRange::Bold)
                format.setFontWeight(QFont::Bold);
            if (range.kind & BlockEditorEngine::FormatRange::Italic)
                format.setFontItalic(true);
            if (range.kind & BlockEditorEngine::FormatRange::Strike)
                format.setFontStrikeOut(true);
            if (range.kind & BlockEditorEngine::FormatRange::Underline)
                format.setFontUnderline(true);
            if (range.kind & BlockEditorEngine::FormatRange::Code) {
                format.setFontFamilies({m_engine->m_monoFontFamily});
                format.setBackground(theme ? theme->inlineCodeBackground()
                                           : kCodeBackground);
            }
            if (range.kind & BlockEditorEngine::FormatRange::Highlight)
                format.setBackground(theme ? theme->highlightBackground()
                                           : kHighlightBackground);
            if (range.kind & BlockEditorEngine::FormatRange::Link) {
                // An internal link ([text](#slug)) whose slug matches no
                // heading renders muted — the recoverable "unresolved" state.
                // Needs a resolver; without one, links are ordinary. Only
                // #-prefixed targets are ever checked, so an external URL
                // never pays the lookup.
                bool unresolved = false;
                if (m_engine->m_linkResolver
                    && range.url.startsWith(QLatin1Char('#'))) {
                    unresolved = !m_engine->m_linkResolver->hasSlug(
                        range.url.mid(1));
                }
                // A [[wiki-link]] whose target matches no note renders muted
                // the same way (§3.1). The bare-anchor form [[#heading]]
                // resolves against the open document's outline; a foreign
                // note's heading is never checked here — the note existing
                // is the resolution bar.
                if (range.kind & BlockEditorEngine::FormatRange::WikiLink) {
                    const QString spec =
                        range.url.mid(int(qstrlen("kvit-note:")));
                    const int hash = spec.indexOf(QLatin1Char('#'));
                    const QString target =
                        (hash >= 0 ? spec.left(hash) : spec).trimmed();
                    if (target.isEmpty()) {
                        if (m_engine->m_linkResolver && hash >= 0) {
                            unresolved = !m_engine->m_linkResolver->hasSlug(
                                DocumentOutline::baseSlug(spec.mid(hash + 1)));
                        }
                    } else if (m_engine->m_wikiResolver) {
                        unresolved = m_engine->m_wikiResolver
                                         ->resolveWikiTarget(target).isEmpty();
                    }
                }
                if (unresolved)
                    format.setForeground(theme ? theme->textMuted()
                                               : kUnresolvedLinkColor);
                else
                    format.setForeground(theme ? theme->link() : kLinkColor);
                format.setFontUnderline(true);
            }
            // Text color: the explicit color wins over any
            // inherited foreground (link accent included). An invalid value
            // is left unpainted rather than defaulting to black.
            if (range.kind & BlockEditorEngine::FormatRange::Color) {
                const QColor c(range.color);
                if (c.isValid())
                    format.setForeground(c);
            }
            // Hidden inline math: render the TeX content
            // transparently and stretch/compress it to the renderer-owned
            // logical width. The delegate overlays the rendered equation at
            // that box. A revealed math span has had its Math flag stripped in
            // formatRangesForState, so its source stays visible/editable.
            if (range.kind & BlockEditorEngine::FormatRange::Math) {
                format.setForeground(Qt::transparent);
                applyMathReservation(format, range, text, localStart);
            }
            // Raised/lowered smaller text (§2.1). The vertical SHIFT
            // comes from the character formats the engine applies in
            // applyVerticalAlignmentFormats() — Qt Quick's glyph
            // pipeline ignores layout-format alignment. This layer
            // still sets it so the font shrink follows the same range
            // list as every other span style.
            if (range.kind & BlockEditorEngine::FormatRange::Superscript)
                format.setVerticalAlignment(
                    QTextCharFormat::AlignSuperScript);
            if (range.kind & BlockEditorEngine::FormatRange::Subscript)
                format.setVerticalAlignment(QTextCharFormat::AlignSubScript);
            setFormat(localStart, range.length, format);
        }
    }

    qreal cachedMathWidth(const QString &tex, int textSizePx)
    {
        const QVariantMap metrics =
            m_engine->cachedMathMetrics(tex, textSizePx);
        return metrics.value(QStringLiteral("valid")).toBool()
            ? metrics.value(QStringLiteral("width")).toReal() : 0;
    }

    void applyMathReservation(QTextCharFormat &format,
                              const BlockEditorEngine::FormatRange &range,
                              const QString &text, int localStart)
    {
        if (localStart < 0 || localStart + range.length > text.length())
            return;
        const QString tex = text.mid(localStart, range.length);
        if (tex.trimmed().isEmpty())
            return;

        const int textSizePx = m_engine->mathFontPixelSize();
        const qreal renderedWidth = cachedMathWidth(tex, textSizePx);
        if (renderedWidth <= 0)
            return;

        const QVariantMap reservation =
            m_engine->mathReservationMetrics(tex, range.kind);
        if (!reservation.value(QStringLiteral("reservationValid")).toBool())
            return;

        QFont reservationFont = m_engine->contentFontForFlags(range.kind);
        reservationFont.setPixelSize(
            reservation.value(QStringLiteral("reservationFontPixelSize"))
                .toInt());
        const QFontMetricsF sourceMetrics(reservationFont);
        const qreal sourceWidth = sourceMetrics.horizontalAdvance(tex);
        if (sourceWidth <= 0)
            return;

        // Widen or narrow the transparent source text to exactly the width
        // of the rendered image, using an additive per-character advance.
        //
        // QFont::setStretch() looks like the natural tool and was used here
        // originally, but Qt documents it as either matching a condensed or
        // expanded face OR applying a transform, without specifying which,
        // and it guarantees exactness in neither case. That held on Linux
        // (measured within 1.3 px of target from 6% to 148%) and failed on
        // macOS, where `x^2` reserved 13.45 px against a rendered 16.00 and
        // `\frac{a}{b}` reserved 1.47 against 12.00. Letter spacing is
        // applied by Qt's own layout rather than negotiated with the font
        // engine, and measured more accurate on Linux too (within 0.13 px
        // across the same cases).
        //
        // The spacing lands after every character including the last, so the
        // total advance is sourceWidth + count * delta; solve for delta.
        const int count = tex.size();
        if (count <= 0)
            return;
        reservationFont.setLetterSpacing(QFont::AbsoluteSpacing,
                                         (renderedWidth - sourceWidth) / count);
        format.setFont(reservationFont,
                       QTextCharFormat::FontPropertiesSpecifiedOnly);
    }

    // Search-match overlay. Backgrounds are MERGED into the already-set
    // formats character by character — setFormat over a whole range would
    // replace them and strip the bold/italic/link styling under the tint.
    // Runs in verbatim mode too: code blocks skip span styling only.
    void applySearchOverlay(const QString &text, int blockPos)
    {
        if (m_engine->m_searchMatches.isEmpty())
            return;
        const auto ranges = InlineMarkdown::searchHighlightRanges(
            m_engine->m_markdown, m_engine->m_revealedSpans,
            m_engine->m_searchMatches, m_engine->m_verbatim);
        for (const auto &range : ranges) {
            const int localStart = qMax(0, range.start - blockPos);
            const int localEnd = qMin<int>(text.length(),
                                           range.start + range.length - blockPos);
            const Theme *theme = m_engine->m_theme;
            const QColor tint =
                range.current
                    ? (theme ? theme->searchCurrentBackground()
                             : kSearchCurrentBackground)
                    : (theme ? theme->searchMatchBackground()
                             : kSearchMatchBackground);
            for (int i = localStart; i < localEnd; ++i) {
                QTextCharFormat merged = format(i);
                merged.setBackground(tint);
                setFormat(i, 1, merged);
            }
        }
    }

    BlockEditorEngine *m_engine;
};

BlockEditorEngine::BlockEditorEngine(QObject *parent)
    : QObject(parent)
{
}

BlockEditorEngine::~BlockEditorEngine()
{
    // The highlighter is parented to the QTextDocument, not to the engine, and
    // holds a raw back-pointer into it. A TextArea's document routinely
    // outlives the engine bound to it, so leaving the highlighter behind would
    // let the next edit run a highlight pass through freed memory. m_doc is a
    // QPointer: when it is null the document went first and took the
    // highlighter down with it as its child.
    if (m_doc) {
        m_doc->disconnect(this);
        delete m_highlighter;
    }
    m_highlighter = nullptr;
}

void BlockEditorEngine::classBegin()
{
    m_componentComplete = false;
    m_pendingRebuild = false;
    m_pendingRehighlight = false;
}

void BlockEditorEngine::componentComplete()
{
    m_componentComplete = true;
    flushPendingRenderUpdate();
}

QQuickTextDocument *BlockEditorEngine::document() const
{
    return m_quickDocument;
}

void BlockEditorEngine::setDocument(QQuickTextDocument *document)
{
    if (m_quickDocument == document)
        return;
    m_quickDocument = document;
    attachDocument(document ? document->textDocument() : nullptr);
    emit documentChanged();
}

void BlockEditorEngine::attachDocument(QTextDocument *doc)
{
    if (m_doc == doc)
        return;

    if (m_doc) {
        m_doc->disconnect(this);
        delete m_highlighter;
        m_highlighter = nullptr;
    }

    m_doc = doc;
    m_revealedSpans.clear();
    if (!m_doc)
        return;

    // Force creation of the document layout: QTextDocument only emits
    // contentsChange (and runs syntax highlighters) once a layout exists.
    // TextArea documents always have one; bare documents in unit tests
    // do not.
    (void)m_doc->documentLayout();

    // Undo/redo lives in the model-level UndoStack; marker and rebuild
    // edits must never become undoable document actions.
    m_doc->setUndoRedoEnabled(false);

    m_highlighter = new MarkdownHighlighter(m_doc, this);

    connect(m_doc, &QTextDocument::contentsChange,
            this, &BlockEditorEngine::onContentsChange);
    // The highlighter is the document's child, so it dies with the document.
    // Drop the raw pointer at the same moment, or a later rehighlight() would
    // follow it into freed memory.
    connect(m_doc, &QObject::destroyed, this, [this]() {
        m_highlighter = nullptr;
    });

    requestRebuild();
}

QString BlockEditorEngine::markdown() const
{
    return m_markdown;
}

void BlockEditorEngine::setMarkdown(const QString &markdown)
{
    if (m_markdown == markdown) {
        // Still make sure the document agrees (first attach, pooling).
        if (m_doc && m_doc->toPlainText() != stateText())
            requestRebuild();
        return;
    }
    m_markdown = markdown;
    emit markdownChanged();
    requestRebuild();
}

bool BlockEditorEngine::verbatim() const
{
    return m_verbatim;
}

void BlockEditorEngine::setVerbatim(bool verbatim)
{
    if (m_verbatim == verbatim)
        return;
    m_verbatim = verbatim;
    emit verbatimChanged();
    if (m_doc)
        requestRebuild();
}

void BlockEditorEngine::setCodeLanguage(const QString &language)
{
    if (m_codeLanguage == language)
        return;
    m_codeLanguage = language;
    emit codeLanguageChanged();
    // Only the code highlighter's colors depend on the language; a rehighlight
    // pass repaints them with no document mutation, so it is never an undo step.
    if (m_verbatim)
        requestRehighlight();
}

QVariantList BlockEditorEngine::searchMatches() const
{
    return m_searchMatchesVariant;
}

void BlockEditorEngine::setSearchMatches(const QVariantList &matches)
{
    QList<HighlightRange> parsed;
    for (const QVariant &v : matches) {
        const QVariantMap map = v.toMap();
        HighlightRange range;
        range.start = map.value(QStringLiteral("start")).toInt();
        range.length = map.value(QStringLiteral("length")).toInt();
        range.current = map.value(QStringLiteral("current")).toBool();
        if (range.length > 0)
            parsed.append(range);
    }
    m_searchMatchesVariant = matches;
    if (parsed == m_searchMatches)
        return;
    m_searchMatches = parsed;
    emit searchMatchesChanged();
    requestRehighlight();
}

void BlockEditorEngine::setTheme(Theme *theme)
{
    if (m_theme == theme)
        return;
    if (m_theme)
        disconnect(m_theme, nullptr, this, nullptr);
    m_theme = theme;
    if (m_theme) {
        // A theme switch restyles in place — one rehighlight pass, no
        // document mutation, so it can never be an undo step. Engines
        // of pooled-away delegates rehighlight on reattach instead
        // (setDocument installs a fresh highlighter pass).
        connect(m_theme, &Theme::themeChanged,
                this, &BlockEditorEngine::requestRehighlight);
    }
    emit themeChanged();
    requestRehighlight();
}

void BlockEditorEngine::setLinkResolver(DocumentOutline *resolver)
{
    if (m_linkResolver == resolver)
        return;
    if (m_linkResolver)
        disconnect(m_linkResolver, nullptr, this, nullptr);
    m_linkResolver = resolver;
    if (m_linkResolver) {
        // A change to the set of heading slugs can flip a link between
        // resolved and unresolved; restyle in place. The outline emits this
        // coalesced (off the keystroke path), so a heading edit does not
        // rehighlight every block per keystroke.
        connect(m_linkResolver, &DocumentOutline::slugsChanged,
                this, &BlockEditorEngine::requestRehighlight);
    }
    emit linkResolverChanged();
    requestRehighlight();
}

void BlockEditorEngine::setWikiResolver(NoteCollection *resolver)
{
    if (m_wikiResolver == resolver)
        return;
    if (m_wikiResolver)
        disconnect(m_wikiResolver, nullptr, this, nullptr);
    m_wikiResolver = resolver;
    if (m_wikiResolver) {
        // Notes appearing/disappearing can flip a [[wiki-link]] between
        // resolved and unresolved; restyle in place. Revision bumps are
        // already coalesced by the collection, so this never lands on the
        // keystroke path.
        connect(m_wikiResolver, &NoteCollection::revisionChanged,
                this, &BlockEditorEngine::requestRehighlight);
    }
    emit wikiResolverChanged();
    requestRehighlight();
}

void BlockEditorEngine::setLineHeight(qreal height)
{
    if (qFuzzyCompare(m_lineHeight, height))
        return;
    m_lineHeight = height;
    emit lineHeightChanged();
    if (m_componentComplete || (m_doc && !m_doc->isEmpty()))
        applyLineHeight();
    else
        requestRebuild();
}

void BlockEditorEngine::setMonoFontFamily(const QString &family)
{
    const QString value =
        family.isEmpty() ? QStringLiteral("monospace") : family;
    if (m_monoFontFamily == value)
        return;
    m_monoFontFamily = value;
    emit monoFontFamilyChanged();
    requestRehighlight();
}

void BlockEditorEngine::setContentFontPixelSize(int size)
{
    const int value = size > 0 ? size : 15;
    if (m_contentFontPixelSize == value)
        return;
    m_contentFontPixelSize = value;
    m_mathMetricsCache.clear();
    m_mathMetricsOrder.clear();
    m_mathSegmentsValid = false;
    emit contentFontChanged();
    requestRehighlight();
}

void BlockEditorEngine::setContentFontFamily(const QString &family)
{
    if (m_contentFontFamily == family)
        return;
    m_contentFontFamily = family;
    emit contentFontChanged();
    requestRehighlight();
}

void BlockEditorEngine::setContentFontWeight(int weight)
{
    const int value = qBound(1, weight, 1000);
    if (m_contentFontWeight == value)
        return;
    m_contentFontWeight = value;
    emit contentFontChanged();
    requestRehighlight();
}

QVariantMap BlockEditorEngine::cachedMathMetrics(const QString &tex,
                                                 int textSizePx) const
{
    const int size = textSizePx > 0 ? textSizePx : 15;
    const QString key = mathMetricsCacheKey(tex, size);
    const auto it = m_mathMetricsCache.constFind(key);
    if (it != m_mathMetricsCache.constEnd())
        return it.value();

    const QVariantMap metrics =
        mathMetricsMap(MathRenderer::measure(tex, size));
    m_mathMetricsCache.insert(key, metrics);
    m_mathMetricsOrder.append(key);
    while (m_mathMetricsOrder.size() > MaxMathMetricsEntries) {
        m_mathMetricsCache.remove(m_mathMetricsOrder.takeFirst());
    }
    return metrics;
}

int BlockEditorEngine::mathFontPixelSize() const
{
    return MathRenderer::opticalMathPixelSize(contentFontForFlags(0));
}

int BlockEditorEngine::effectiveContentFontPixelSize() const
{
    if (m_contentFontPixelSize > 0)
        return m_contentFontPixelSize;
    if (m_doc) {
        const QFont font = m_doc->defaultFont();
        if (font.pixelSize() > 0)
            return font.pixelSize();
        if (font.pointSizeF() > 0)
            return qRound(font.pointSizeF() * 96.0 / 72.0);
    }
    return 15;
}

QFont BlockEditorEngine::contentFontForFlags(quint32 flags) const
{
    QFont font = m_doc ? m_doc->defaultFont() : QFont();
    if (!m_contentFontFamily.isEmpty())
        font.setFamily(m_contentFontFamily);
    font.setPixelSize(effectiveContentFontPixelSize());
    const int weight = (flags & FormatRange::Bold)
        ? int(QFont::Bold)
        : qBound(1, m_contentFontWeight, 1000);
    font.setWeight(static_cast<QFont::Weight>(weight));
    font.setItalic(flags & FormatRange::Italic);
    return font;
}

QVariantMap BlockEditorEngine::mathReservationMetrics(const QString &tex,
                                                       quint32 flags) const
{
    const int textSizePx = effectiveContentFontPixelSize();
    const int mathSizePx = mathFontPixelSize();
    const QVariantMap metrics = cachedMathMetrics(tex, mathSizePx);
    if (!metrics.value(QStringLiteral("valid")).toBool()) {
        return {
            {QStringLiteral("reservationValid"), false},
            {QStringLiteral("reservationError"),
             metrics.value(QStringLiteral("error")).toString()},
        };
    }

    QFont baseFont = contentFontForFlags(flags);
    const QFontMetricsF baseMetrics(baseFont);
    const int verticalPadding = inlineMathVerticalPaddingPx(mathSizePx);
    const qreal targetHeight =
        metrics.value(QStringLiteral("height")).toReal() + 2 * verticalPadding;
    const int reservationPixelSize =
        targetHeight > baseMetrics.height()
            ? fontPixelSizeForLineHeight(baseFont, textSizePx, targetHeight)
            : textSizePx;

    QFont reservationFont(baseFont);
    reservationFont.setPixelSize(reservationPixelSize);
    const QFontMetricsF reservationMetrics(reservationFont);
    return {
        {QStringLiteral("reservationValid"), true},
        {QStringLiteral("reservationFontPixelSize"), reservationPixelSize},
        {QStringLiteral("reservationHeight"), reservationMetrics.height()},
        {QStringLiteral("reservationAscent"), reservationMetrics.ascent()},
        {QStringLiteral("reservationDescent"), reservationMetrics.descent()},
        {QStringLiteral("reservationTargetHeight"), targetHeight},
        {QStringLiteral("inlineVerticalPadding"), verticalPadding},
    };
}

// TextEdit has no line-height property, so the multiplier lands as
// proportional block format on the whole document — an internal edit
// like reveal transitions, never an undo step. Re-applied after every
// minimal-diff rebuild; text blocks created by user edits inherit the
// previous block's format, so the typing path needs no hook.
void BlockEditorEngine::applyLineHeight()
{
    if (!m_doc)
        return;
    m_internalEdit = true;
    QTextCursor tc(m_doc);
    tc.select(QTextCursor::Document);
    QTextBlockFormat format;
    format.setLineHeight(m_lineHeight * 100.0,
                         QTextBlockFormat::ProportionalHeight);
    tc.mergeBlockFormat(format);
    m_internalEdit = false;
}

QString BlockEditorEngine::stateText() const
{
    return m_verbatim ? m_markdown : InlineMarkdown::documentText(m_markdown, m_revealedSpans);
}

void BlockEditorEngine::requestRebuild()
{
    if (!m_componentComplete) {
        if (m_doc && !m_markdown.isEmpty()) {
            rebuildDocument(false);
            m_pendingRebuild = false;
            m_pendingRehighlight = true;
            return;
        }
        m_pendingRebuild = true;
        m_pendingRehighlight = false;
        return;
    }
    rebuildDocument();
}

void BlockEditorEngine::requestRehighlight()
{
    if (!m_componentComplete) {
        if (!m_pendingRebuild)
            m_pendingRehighlight = true;
        return;
    }
    rehighlight();
}

void BlockEditorEngine::flushPendingRenderUpdate()
{
    if (m_pendingRebuild) {
        m_pendingRebuild = false;
        m_pendingRehighlight = false;
        rebuildDocument();
        return;
    }
    if (m_pendingRehighlight) {
        m_pendingRehighlight = false;
        rehighlight();
    }
}

int BlockEditorEngine::cursorPosition() const
{
    return m_cursorPos;
}

void BlockEditorEngine::setCursorPosition(int position)
{
    if (m_cursorPos != position) {
        m_cursorPos = position;
        emit cursorPositionChanged();
    }
    // During internal edits the cursor moves as a side effect of marker or
    // rebuild edits; the state update at the end of those operations
    // re-evaluates reveal against the final cursor value.
    if (m_internalEdit)
        return;
    scheduleRevealUpdate();
}

int BlockEditorEngine::selectionStart() const
{
    return m_selStart;
}

void BlockEditorEngine::setSelectionStart(int position)
{
    if (m_selStart != position) {
        m_selStart = position;
        emit selectionChanged();
    }
    if (m_internalEdit)
        return;
    scheduleRevealUpdate();
}

int BlockEditorEngine::selectionEnd() const
{
    return m_selEnd;
}

void BlockEditorEngine::setSelectionEnd(int position)
{
    if (m_selEnd != position) {
        m_selEnd = position;
        emit selectionChanged();
    }
    if (m_internalEdit)
        return;
    scheduleRevealUpdate();
}

bool BlockEditorEngine::cursorActive() const
{
    return m_cursorActive;
}

void BlockEditorEngine::setCursorActive(bool active)
{
    if (m_cursorActive == active)
        return;
    m_cursorActive = active;
    emit cursorActiveChanged();
    if (!m_internalEdit)
        scheduleRevealUpdate();
}

int BlockEditorEngine::toMarkdownPosition(int documentPosition) const
{
    if (m_verbatim)
        return qBound(0, documentPosition, int(m_markdown.length()));
    return InlineMarkdown::documentToMarkdown(m_markdown, m_revealedSpans, documentPosition);
}

int BlockEditorEngine::toDocumentPosition(int markdownPosition) const
{
    if (m_verbatim)
        return qBound(0, markdownPosition, int(m_markdown.length()));
    return InlineMarkdown::markdownToDocument(m_markdown, m_revealedSpans, markdownPosition);
}

QString BlockEditorEngine::markdownForRange(int docStart, int docEnd) const
{
    if (m_verbatim) {
        const int lo = qBound(0, docStart, int(m_markdown.length()));
        const int hi = qBound(lo, docEnd, int(m_markdown.length()));
        return m_markdown.mid(lo, hi - lo);
    }
    return InlineMarkdown::markdownForRange(m_markdown, m_revealedSpans, docStart, docEnd);
}

QVariantMap BlockEditorEngine::cutRange(int docStart, int docEnd) const
{
    if (m_verbatim) {
        const int lo = qBound(0, docStart, int(m_markdown.length()));
        const int hi = qBound(lo, docEnd, int(m_markdown.length()));
        return {
            {QStringLiteral("markdown"), m_markdown.left(lo) + m_markdown.mid(hi)},
            {QStringLiteral("cursor"), lo},
        };
    }
    const EditResult result = InlineMarkdown::cutRangeResult(m_markdown, m_revealedSpans, docStart, docEnd);
    return {
        {QStringLiteral("markdown"), result.markdown},
        {QStringLiteral("cursor"), result.mdEditEnd},
    };
}

QString BlockEditorEngine::stripFormatting(const QString &markdown) const
{
    if (m_verbatim)
        return markdown;  // code content is literal; nothing to strip
    return InlineMarkdown::displayText(markdown);
}

QVariantMap BlockEditorEngine::linkSpanAtCursor(int docPos) const
{
    if (m_verbatim)
        return {{QStringLiteral("found"), false}};
    const int mdPos = InlineMarkdown::documentToMarkdown(m_markdown, m_revealedSpans, docPos);
    return InlineMarkdown::linkSpanIn(m_markdown, mdPos);
}

QVariantMap BlockEditorEngine::mathSpanRangeAt(int docPos) const
{
    if (m_verbatim)
        return {{QStringLiteral("found"), false}};
    const int mdPos = InlineMarkdown::documentToMarkdown(m_markdown, m_revealedSpans, docPos);
    QVariantMap map = InlineMarkdown::mathSpanRangeIn(m_markdown, mdPos);
    if (map.value(QStringLiteral("found")).toBool()) {
        map.insert(QStringLiteral("docContentStart"),
                   InlineMarkdown::markdownToDocument(m_markdown, m_revealedSpans,
                                      map.value(QStringLiteral("contentStart"))
                                          .toInt()));
        map.insert(QStringLiteral("docContentEnd"),
                   InlineMarkdown::markdownToDocument(m_markdown, m_revealedSpans,
                                      map.value(QStringLiteral("contentEnd"))
                                          .toInt()));
    }
    return map;
}

bool BlockEditorEngine::shouldAutoPairDollar(int docPos,
                                             bool ignoreFollowing) const
{
    if (m_verbatim)
        return false;
    const int mdPos = InlineMarkdown::documentToMarkdown(m_markdown, m_revealedSpans, docPos);
    return InlineMarkdown::shouldAutoPairDollarIn(m_markdown, mdPos, ignoreFollowing);
}

QVariantList BlockEditorEngine::inlineMathBoxes() const
{
    QVariantList out;
    if (m_verbatim)
        return out;

    // Everything except the line positions depends only on the markdown and
    // the reveal state. The overlay asks for this list again on every
    // relayout and every caret move, so re-scanning the markdown and
    // re-merging the metric maps each time was the bulk of the cost of
    // having math in a paragraph at all.
    if (!m_mathSegmentsValid || m_mathSegmentsMarkdown != m_markdown
        || m_mathSegmentsRevealed != m_revealedSpans) {
        m_mathSegmentsCache = buildInlineMathSegments();
        m_mathSegmentsMarkdown = m_markdown;
        m_mathSegmentsRevealed = m_revealedSpans;
        m_mathSegmentsValid = true;
    }

    out = m_mathSegmentsCache;
    if (!m_doc)
        return out;

    // The line each span sits on has to come from the live layout: a width
    // change rewraps the text without changing a character of it.
    for (int i = 0; i < out.size(); ++i) {
        QVariantMap box = out.at(i).toMap();
        const int docStart = box.value(QStringLiteral("docStart")).toInt();
        const QTextBlock block = m_doc->findBlock(docStart);
        if (block.isValid()) {
            if (QTextLayout *layout = block.layout()) {
                const QTextLine line =
                    layout->lineForTextPosition(docStart - block.position());
                if (line.isValid()) {
                    box.insert(QStringLiteral("lineStart"),
                               block.position() + line.textStart());
                    box.insert(QStringLiteral("lineEnd"),
                               block.position() + line.textStart()
                                   + line.textLength());
                }
            }
        }
        out[i] = box;
    }
    return out;
}

// The markdown-derived half of a box list: which spans are hidden, what they
// render as, and what was reserved for them.
QVariantList BlockEditorEngine::buildInlineMathSegments() const
{
    QVariantList out;
    for (const InlineMarkdown::MathSegment &seg :
         InlineMarkdown::hiddenMathSegments(m_markdown, m_revealedSpans)) {
        QVariantMap box{
            {QStringLiteral("tex"), seg.tex},
            {QStringLiteral("docStart"), seg.docStart},
            {QStringLiteral("docEnd"), seg.docEnd},
        };
        // No line positions here: those come from the live layout, which
        // this result outlives, and inlineMathBoxes() fills them in.
        const QVariantMap metrics =
            cachedMathMetrics(seg.tex, mathFontPixelSize());
        for (auto it = metrics.constBegin(); it != metrics.constEnd(); ++it)
            box.insert(it.key(), it.value());
        const QVariantMap reservation =
            mathReservationMetrics(seg.tex, seg.flags);
        for (auto it = reservation.constBegin(); it != reservation.constEnd(); ++it)
            box.insert(it.key(), it.value());
        out.append(box);
    }
    return out;
}

int BlockEditorEngine::formatFlagsAtDocumentPosition(int docPos) const
{
    if (m_verbatim)
        return 0;
    return int(InlineMarkdown::formatFlagsAt(m_markdown, m_revealedSpans, docPos));
}

QString BlockEditorEngine::linkAtDocumentPosition(int docPos) const
{
    if (m_verbatim)
        return QString();
    return InlineMarkdown::linkAt(m_markdown, m_revealedSpans, docPos);
}

void BlockEditorEngine::rebuildDocument(bool runRehighlight)
{
    if (!m_doc)
        return;
    ++m_rebuildCountForTesting;

    // Model-driven rebuilds start from the fully-rendered state; the next
    // (scheduled) evaluation re-reveals whatever the cursor/selection is
    // in. The minimal diff keeps the cursor stable across undo/redo and
    // load rebuilds (risk table: "idempotent and cursor-preserving").
    m_revealedSpans.clear();
    applyMinimalDiff(stateText());
    if (runRehighlight)
        rehighlight();
    scheduleRevealUpdate();
}

// Replace only the differing middle of the document (common prefix/suffix
// preserved), under the internal-edit guard. Cursors outside the changed
// middle keep their positions via Qt's automatic adjustment.
void BlockEditorEngine::applyMinimalDiff(const QString &expected)
{
    const QString actual = m_doc->toPlainText();
    if (actual == expected)
        return;

    int prefix = 0;
    const int maxCommon = qMin(actual.length(), expected.length());
    while (prefix < maxCommon && actual.at(prefix) == expected.at(prefix))
        ++prefix;
    int suffix = 0;
    while (suffix < maxCommon - prefix
           && actual.at(actual.length() - 1 - suffix) == expected.at(expected.length() - 1 - suffix))
        ++suffix;

    m_internalEdit = true;
    QTextCursor tc(m_doc);
    tc.setPosition(prefix);
    tc.setPosition(int(actual.length()) - suffix, QTextCursor::KeepAnchor);
    tc.insertText(expected.mid(prefix, expected.length() - suffix - prefix));
    m_internalEdit = false;

    if (!qFuzzyCompare(m_lineHeight, 1.0))
        applyLineHeight();
}

// Reveal transitions mutate the document, and the triggers (cursor moves,
// focus changes, edits) arrive synchronously from inside QQuickTextEdit's
// own event processing. Editing the document there makes QQuickTextEdit
// re-assert its stale cached text as a full setText afterwards, corrupting
// the edit mapping (observed as a rm==add full-document replace). So state
// evaluation is always deferred to a clean stack.
void BlockEditorEngine::scheduleRevealUpdate()
{
    if (m_revealUpdateQueued)
        return;
    m_revealUpdateQueued = true;
    QMetaObject::invokeMethod(this, [this]() {
        m_revealUpdateQueued = false;
        updateRevealState();
    }, Qt::QueuedConnection);
}

void BlockEditorEngine::updateRevealState()
{
    if (!m_doc)
        return;

    // Marker edits auto-adjust the cursor, which re-enters the setters;
    // the loop settles because transitionTo() leaves cursor and selection
    // at the same markdown positions they started from.
    for (int i = 0; i < 3; ++i) {
        QList<int> desired;
        if (m_cursorActive && !m_verbatim) {
            if (m_selStart != m_selEnd) {
                // Selection: reveal every span it touches (§2.2.4).
                const int selLo = qMin(m_selStart, m_selEnd);
                const int selHi = qMax(m_selStart, m_selEnd);
                const int mdLo = InlineMarkdown::documentToMarkdown(m_markdown, m_revealedSpans, selLo);
                const int mdHi = InlineMarkdown::documentToMarkdown(m_markdown, m_revealedSpans, selHi);
                desired = InlineMarkdown::spansToRevealForRange(m_markdown, mdLo, mdHi);
            } else {
                const int mdPos = InlineMarkdown::documentToMarkdown(m_markdown, m_revealedSpans, m_cursorPos);
                desired = singleSpanList(InlineMarkdown::spanToReveal(m_markdown, mdPos));
            }
        }
        if (desired == m_revealedSpans)
            return;
        transitionTo(desired);
    }
}

void BlockEditorEngine::transitionTo(const QList<int> &revealedSpans)
{
    const int spanCount = InlineMarkdown::spanCount(m_markdown);

    m_internalEdit = true;

    // Hide spans that should no longer be revealed. Marker document
    // positions are computed against the current state before any edit of
    // that span, then removed right to left so earlier positions stay
    // valid. The cursor can only be here while it is outside a hidden
    // span's inclusive markdown range, so the removals never straddle it.
    QList<int> current = m_revealedSpans;
    for (int idx = current.size() - 1; idx >= 0; --idx) {
        const int k = current.at(idx);
        if (revealedSpans.contains(k) || k >= spanCount)
            continue;
        const auto markers = InlineMarkdown::subtreeMarkers(m_markdown, k);

        QList<QPair<int, int>> ranges; // docStart, length
        for (const auto &m : markers) {
            ranges.append({InlineMarkdown::markdownToDocument(m_markdown, current, m.first),
                           int(m.second.length())});
        }
        QTextCursor tc(m_doc);
        for (int i = ranges.size() - 1; i >= 0; --i) {
            tc.setPosition(ranges.at(i).first);
            tc.setPosition(ranges.at(i).first + ranges.at(i).second,
                           QTextCursor::KeepAnchor);
            tc.removeSelectedText();
        }
        current.removeAt(idx);
    }

    // Reveal newly desired spans: insertion points are computed in the
    // pre-reveal state, then inserted right to left (closing markers
    // first), which also keeps a cursor at the content's left edge in
    // front of the opening marker until that marker lands.
    for (int k : revealedSpans) {
        if (current.contains(k) || k < 0 || k >= spanCount)
            continue;
        const auto markers = InlineMarkdown::subtreeMarkers(m_markdown, k);

        QList<QPair<int, QString>> inserts; // docPos, marker text
        for (const auto &m : markers) {
            inserts.append({InlineMarkdown::markdownToDocument(m_markdown, current, m.first),
                            m.second});
        }
        QTextCursor tc(m_doc);
        for (int i = inserts.size() - 1; i >= 0; --i) {
            tc.setPosition(inserts.at(i).first);
            tc.insertText(inserts.at(i).second);
        }
        current.append(k);
        std::sort(current.begin(), current.end());
    }

    m_revealedSpans = current;
    m_internalEdit = false;
    rehighlight();
}

void BlockEditorEngine::rehighlight()
{
    if (m_highlighter) {
        ++m_rehighlightCountForTesting;
        m_internalEdit = true;
        m_highlighter->rehighlight();
        applyVerticalAlignmentFormats();
        m_internalEdit = false;
    }
}

// Sup/sub raising. The highlighter's layout formats shrink the font but Qt
// Quick's glyph pipeline ignores their vertical alignment; FRAGMENT character
// formats (the rich-text path) do render raised/lowered. So the alignment
// lands as real character formats under the internal-edit guard — re-applied
// on every rehighlight, which already runs after rebuilds, reveal
// transitions, and theme changes. The clearing pass keeps stale alignment
// from surviving span edits.
void BlockEditorEngine::applyVerticalAlignmentFormats()
{
    if (!m_doc || m_verbatim)
        return;

    const auto ranges = InlineMarkdown::formatRangesForState(m_markdown, m_revealedSpans);
    bool hasAny = false;
    for (const auto &range : ranges) {
        if (range.kind & (FormatRange::Superscript | FormatRange::Subscript))
            hasAny = true;
    }
    if (!hasAny && !m_hadVerticalFormats)
        return;

    QTextCursor tc(m_doc);
    tc.select(QTextCursor::Document);
    QTextCharFormat normal;
    normal.setVerticalAlignment(QTextCharFormat::AlignNormal);
    tc.mergeCharFormat(normal);

    for (const auto &range : ranges) {
        const bool sup = range.kind & FormatRange::Superscript;
        const bool sub = range.kind & FormatRange::Subscript;
        if (!sup && !sub)
            continue;
        tc.setPosition(range.start);
        tc.setPosition(range.start + range.length, QTextCursor::KeepAnchor);
        QTextCharFormat vertical;
        vertical.setVerticalAlignment(sup ? QTextCharFormat::AlignSuperScript
                                          : QTextCharFormat::AlignSubScript);
        tc.mergeCharFormat(vertical);
    }
    m_hadVerticalFormats = hasAny;
}

void BlockEditorEngine::onContentsChange(int position, int charsRemoved, int charsAdded)
{
    if (m_internalEdit)
        return;
    // Format-only passes report no added/removed characters.
    if (charsRemoved == 0 && charsAdded == 0)
        return;

    // Idempotent re-assert: QQuickTextEdit occasionally rewrites the whole
    // document with the text it already contains (rm == add, same string).
    // If the document still matches the engine state, nothing was edited.
    if (m_doc->toPlainText() == stateText())
        return;

    PerfLog::ScopedTimer perf(
        QStringLiteral("keystroke"),
        QVariantMap{
            {QStringLiteral("markdownChars"), m_markdown.size()},
            {QStringLiteral("position"), position},
            {QStringLiteral("removed"), charsRemoved},
            {QStringLiteral("added"), charsAdded},
            {QStringLiteral("verbatim"), m_verbatim},
        },
        PerfLog::Verbose,
        16.0);

    if (m_verbatim) {
        // The document text IS the markdown; no reveal state to repair.
        m_markdown = m_doc->toPlainText();
        emit markdownChanged();
        emit markdownEdited(m_markdown);
        return;
    }

    const QString inserted = m_doc->toPlainText().mid(position, charsAdded);
    const EditResult result = InlineMarkdown::applyDocumentEdit(m_markdown, m_revealedSpans,
                                                position, charsRemoved, inserted);
    m_markdown = result.markdown;

    // Adopt the state the document is already in if it is consistent with
    // the new markdown; only repair the document when it is not.
    const QString actual = m_doc->toPlainText();
    const int spanCount = InlineMarkdown::spanCount(m_markdown);
    QList<QList<int>> candidates;
    candidates << singleSpanList(InlineMarkdown::spanToReveal(m_markdown, result.mdEditEnd));
    candidates << QList<int>{};
    for (int i = 0; i < spanCount; ++i)
        candidates << QList<int>{i};

    bool adopted = false;
    for (const auto &candidate : candidates) {
        if (InlineMarkdown::documentText(m_markdown, candidate) == actual) {
            m_revealedSpans = candidate;
            adopted = true;
            break;
        }
    }

    if (!adopted) {
        // No single-span state matches (e.g. an edit replaced a selection
        // that had several spans revealed). Repair to the fully-rendered
        // state with a minimal diff; the scheduled evaluation then
        // re-reveals per cursor/selection.
        m_revealedSpans.clear();
        applyMinimalDiff(InlineMarkdown::documentText(m_markdown, m_revealedSpans));
    }
    rehighlight();

    emit markdownChanged();
    emit markdownEdited(m_markdown);

    // The cursor property update usually arrives after this signal; the
    // deferred evaluation sees the final cursor value.
    scheduleRevealUpdate();
}
