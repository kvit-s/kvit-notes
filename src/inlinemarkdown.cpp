// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
#include "inlinemarkdown.h"

#include <algorithm>

namespace InlineMarkdown {
namespace {

QList<FormattedSpan> spansFor(const QString &markdown)
{
    const MarkdownFormatter formatter;
    return formatter.parseSpans(markdown);
}

QList<int> singleSpanList(int revealedSpan)
{
    if (revealedSpan < 0)
        return {};
    return {revealedSpan};
}

// The single traversal every state function is built on: ordered segments
// mapping document ranges to markdown ranges for a given reveal state.
// Segments with docLen > 0 are 1:1 (their document text equals their
// markdown slice); HiddenMarker segments are the marker characters of a
// hidden span — present in the markdown, zero-width in the document.
// Nested spans recurse: the reveal unit is the top-level span, so a revealed
// span shows every marker in its subtree and a hidden one hides them all. A
// trailing Plain segment is always present (possibly zero-length) so
// end-of-text positions extrapolate from it.
struct Seg {
    enum Kind {
        Plain,         // text outside any span
        Content,       // a span's direct content (between child spans)
        VisibleMarker, // marker of a revealed span (1:1)
        HiddenMarker,  // marker of a hidden span (docLen == 0)
    };
    int docStart = 0;
    int docLen = 0;
    int mdStart = 0;
    int mdLen = 0;
    Kind kind = Plain;
    int spanIdx = -1;   // top-level span index, -1 for Plain
    quint32 flags = 0;  // char-format flags (ancestor flags combined)
    int ownerIdx = -1;  // owning node in the flat span tree, -1 for Plain
    QString color;      // nearest enclosing color-span value
    QString url;        // nearest enclosing link target
};

// Pre-order flattening of the span tree, for owner chains and coverage.
struct FlatSpan {
    const FormattedSpan *span;
    int parent; // flat index, -1 for a top-level span
    int topIdx; // index of the containing top-level span
};

void emitSpanSegs(const FormattedSpan &span, bool revealed, quint32 inherited,
                  int topIdx, int parentFlat, QList<FlatSpan> &flat,
                  int &doc, QList<Seg> &segs, const QString &inheritedColor,
                  const QString &inheritedUrl)
{
    const int self = flat.size();
    flat.append({&span, parentFlat, topIdx});
    const quint32 flags = inherited | span.formatFlags;
    // The nearest enclosing color-span value styles this span's content; a
    // nested color span overrides it for its own content (innermost wins).
    const QString color = (span.formatFlags & SpanFormat::Color)
        ? span.color : inheritedColor;
    // Same for a link's target: content inside a link carries its url so the
    // highlighter can distinguish a resolved from an unresolved #slug link.
    const QString url = (span.formatFlags & SpanFormat::Link)
        ? span.url : inheritedUrl;

    segs.append({doc, revealed ? span.openLen : 0, span.start, span.openLen,
                 revealed ? Seg::VisibleMarker : Seg::HiddenMarker,
                 topIdx, SpanFormat::Marker, self, QString(), QString()});
    doc += revealed ? span.openLen : 0;

    int md = span.start + span.openLen;
    for (const FormattedSpan &child : span.children) {
        if (child.start > md) {
            segs.append({doc, child.start - md, md, child.start - md,
                         Seg::Content, topIdx, flags, self, color, url});
            doc += child.start - md;
        }
        emitSpanSegs(child, revealed, flags, topIdx, self, flat, doc, segs,
                     color, url);
        md = child.end;
    }
    const int contentEnd = span.end - span.closeLen;
    if (contentEnd > md) {
        segs.append({doc, contentEnd - md, md, contentEnd - md,
                     Seg::Content, topIdx, flags, self, color, url});
        doc += contentEnd - md;
    }

    segs.append({doc, revealed ? span.closeLen : 0, contentEnd, span.closeLen,
                 revealed ? Seg::VisibleMarker : Seg::HiddenMarker,
                 topIdx, SpanFormat::Marker, self, QString(), QString()});
    doc += revealed ? span.closeLen : 0;
}

QList<Seg> segmentsFor(const QList<FormattedSpan> &spans, const QString &markdown,
                       const QList<int> &revealedSpans,
                       QList<FlatSpan> *flatOut = nullptr)
{
    QList<Seg> segs;
    QList<FlatSpan> flat;
    int doc = 0;
    int md = 0;
    for (int i = 0; i < spans.size(); ++i) {
        const auto &span = spans.at(i);
        if (span.start > md) {
            segs.append({doc, span.start - md, md, span.start - md,
                         Seg::Plain, -1, 0, -1});
            doc += span.start - md;
        }
        emitSpanSegs(span, revealedSpans.contains(i), 0, i, -1, flat, doc, segs,
                     QString(), QString());
        md = span.end;
    }
    segs.append({doc, int(markdown.length()) - md, md, int(markdown.length()) - md,
                 Seg::Plain, -1, 0, -1});
    if (flatOut)
        *flatOut = flat;
    return segs;
}

QList<Seg> segmentsFor(const QString &markdown, const QList<int> &revealedSpans)
{
    return segmentsFor(spansFor(markdown), markdown, revealedSpans);
}

// Deepest span carrying a URL whose inclusive markdown range contains
// mdPos — the span Ctrl+K edits.
const FormattedSpan *deepestLinkAt(const QList<FormattedSpan> &spans, int mdPos)
{
    const FormattedSpan *found = nullptr;
    for (const FormattedSpan &span : spans) {
        if (mdPos < span.start || mdPos > span.end)
            continue;
        // Wiki-links are excluded: the Ctrl+K dialog edits [text](url)
        // spans, and rewriting a [[wiki-link]] into that form would mangle
        // it. Ctrl+K inside one inserts a fresh link instead.
        if (span.type == QLatin1String("wikilink"))
            continue;
        if (!span.url.isEmpty() || span.type == QLatin1String("link"))
            found = &span;
        if (const FormattedSpan *inner = deepestLinkAt(span.children, mdPos))
            found = inner;
    }
    return found;
}

// The math span whose content range contains mdPos, both edges inclusive
// (a caret right after the opening $ or right before the closing $ counts
// as inside), searching nested spans depth-first. Pointers reference the
// caller-held span list.
const FormattedSpan *mathSpanAt(const QList<FormattedSpan> &spans, int mdPos)
{
    for (const FormattedSpan &span : spans) {
        if (span.type == QLatin1String("math")
            && mdPos >= span.start + span.openLen
            && mdPos <= span.end - span.closeLen) {
            return &span;
        }
        if (const FormattedSpan *child = mathSpanAt(span.children, mdPos))
            return child;
    }
    return nullptr;
}

// Whether mdPos sits inside (content edges inclusive) any span carrying
// the given content flags — the inline-code check of the auto-pair gate.
bool insideSpanWithFlags(const QList<FormattedSpan> &spans, int mdPos,
                         quint32 flags)
{
    for (const FormattedSpan &span : spans) {
        if ((span.formatFlags & flags)
            && mdPos >= span.start + span.openLen
            && mdPos <= span.end - span.closeLen) {
            return true;
        }
        if (insideSpanWithFlags(span.children, mdPos, flags))
            return true;
    }
    return false;
}

// Whether the character at `pos` is escaped by a backslash run before it
// (odd run length), matching the span parser's isEscapedAt semantics.
bool escapedAt(const QString &text, int pos)
{
    int backslashes = 0;
    for (int i = pos - 1; i >= 0 && text.at(i) == QLatin1Char('\\'); --i)
        ++backslashes;
    return (backslashes % 2) == 1;
}

// All markers of a span's subtree in ascending markdown order — the
// reveal unit is the top-level span, so its children's markers show and
// hide together with its own (features.md §2.2.7).
void collectSubtreeMarkers(const FormattedSpan &span,
                           QList<QPair<int, QString>> &markers)
{
    markers.append({span.start, span.openMarker()});
    for (const FormattedSpan &child : span.children)
        collectSubtreeMarkers(child, markers);
    markers.append({span.end - span.closeLen, span.closeMarker()});
}

} // namespace

QString displayText(const QString &markdown)
{
    return documentText(markdown, QList<int>{});
}

QString documentText(const QString &markdown, int revealedSpan)
{
    return documentText(markdown, singleSpanList(revealedSpan));
}

QString documentText(const QString &markdown, const QList<int> &revealedSpans)
{
    QString out;
    out.reserve(markdown.size());
    for (const Seg &seg : segmentsFor(markdown, revealedSpans)) {
        if (seg.docLen > 0)
            out += markdown.mid(seg.mdStart, seg.docLen);
    }
    return out;
}

int documentToMarkdown(const QString &markdown, int revealedSpan, int docPos)
{
    return documentToMarkdown(markdown, singleSpanList(revealedSpan), docPos);
}

int documentToMarkdown(const QString &markdown, const QList<int> &revealedSpans,
                       int docPos)
{
    const auto segs = segmentsFor(markdown, revealedSpans);
    for (const Seg &seg : segs) {
        if (seg.docLen > 0 && docPos < seg.docStart + seg.docLen)
            return seg.mdStart + (docPos - seg.docStart);
    }
    // Past the end: extrapolate from the trailing segment. A hidden span's
    // left content edge maps inside the span (after the opening marker) and
    // the right edge maps after the closing marker because the zero-width
    // marker segments never contain a position — matching where a
    // QTextCursor lands when the markers are inserted at the cursor.
    const Seg &last = segs.last();
    return last.mdStart + last.mdLen + (docPos - (last.docStart + last.docLen));
}

int markdownToDocument(const QString &markdown, int revealedSpan, int mdPos)
{
    return markdownToDocument(markdown, singleSpanList(revealedSpan), mdPos);
}

int markdownToDocument(const QString &markdown, const QList<int> &revealedSpans,
                       int mdPos)
{
    const auto segs = segmentsFor(markdown, revealedSpans);
    for (const Seg &seg : segs) {
        if (seg.mdLen > 0 && mdPos < seg.mdStart + seg.mdLen) {
            // Hidden-marker interiors clamp to the nearest content edge.
            if (seg.docLen == 0)
                return seg.docStart;
            return seg.docStart + (mdPos - seg.mdStart);
        }
    }
    const Seg &last = segs.last();
    return last.docStart + last.docLen + (mdPos - (last.mdStart + last.mdLen));
}

int spanToReveal(const QString &markdown, int mdPos)
{
    const auto spans = spansFor(markdown);
    for (int i = 0; i < spans.size(); ++i) {
        if (mdPos >= spans.at(i).start && mdPos <= spans.at(i).end)
            return i;
    }
    return -1;
}

QList<int> spansToRevealForRange(const QString &markdown, int mdStart, int mdEnd)
{
    const auto spans = spansFor(markdown);
    QList<int> touched;
    for (int i = 0; i < spans.size(); ++i) {
        if (spans.at(i).start <= mdEnd && spans.at(i).end >= mdStart)
            touched.append(i);
    }
    return touched;
}

int spanCount(const QString &markdown)
{
    return int(spansFor(markdown).size());
}

QList<FormatRange> formatRangesForState(const QString &markdown, int revealedSpan)
{
    return formatRangesForState(markdown, singleSpanList(revealedSpan));
}

QList<FormatRange> formatRangesForState(const QString &markdown,
                                        const QList<int> &revealedSpans)
{
    QList<FormatRange> ranges;
    for (const Seg &seg : segmentsFor(markdown, revealedSpans)) {
        if (seg.docLen <= 0 || seg.kind == Seg::Plain)
            continue;
        quint32 flags = seg.flags;
        // A REVEALED math span shows its $…$ source (editable, like inline
        // code), so its content is not rendered transparently — strip Math.
        // A HIDDEN math span keeps Math: the highlighter renders its content
        // invisibly at renderer-measured width, and the delegate overlays the
        // equation.
        if ((flags & SpanFormat::Math) && seg.spanIdx >= 0
            && revealedSpans.contains(seg.spanIdx))
            flags &= ~SpanFormat::Math;
        ranges.append({seg.docStart, seg.docLen, flags, seg.color, seg.url});
    }
    return ranges;
}

EditResult applyDocumentEdit(const QString &markdown, int revealedSpan,
                             int pos, int removedLen, const QString &insertedText)
{
    return applyDocumentEdit(markdown, singleSpanList(revealedSpan), pos,
                             removedLen, insertedText);
}

QString markdownForRange(const QString &markdown, const QList<int> &revealedSpans,
                         int docStart, int docEnd)
{
    const auto spans = spansFor(markdown);
    QList<FlatSpan> flat;
    const auto segs = segmentsFor(spans, markdown, revealedSpans, &flat);

    // Coverage of each top-level span's visible content (its own and its
    // descendants'). Marker characters in the selection never count —
    // the reconstruction supplies markers exactly once.
    QList<int> total(spans.size(), 0);
    QList<int> covered(spans.size(), 0);
    for (const Seg &seg : segs) {
        if (seg.kind != Seg::Content || seg.spanIdx < 0)
            continue;
        const int a = qMax(docStart, seg.docStart);
        const int b = qMin(docEnd, seg.docStart + seg.docLen);
        total[seg.spanIdx] += seg.docLen;
        covered[seg.spanIdx] += qMax(0, b - a);
    }

    // A fully covered top-level span contributes its raw markdown
    // verbatim — byte-faithful, nested markers included. A partially
    // covered one is rebuilt from its covered pieces as self-contained
    // fragments: every owner-chain change closes all open markers and
    // reopens the piece's full chain, so each fragment parses on its own
    // (a shared-prefix reconstruction like "**o *i***" would hit the
    // scanner's first-fit corner cases and render differently).
    QString out;
    QList<int> current; // open chain for fragment reconstruction
    int lastWholeTop = -1;
    auto chainOf = [&](int node) {
        QList<int> chain;
        for (int k = node; k != -1; k = flat.at(k).parent)
            chain.prepend(k);
        return chain;
    };
    auto syncChain = [&](const QList<int> &target) {
        if (current == target)
            return;
        for (int i = current.size() - 1; i >= 0; --i)
            out += flat.at(current.at(i)).span->closeMarker();
        for (int i = 0; i < target.size(); ++i)
            out += flat.at(target.at(i)).span->openMarker();
        current = target;
    };

    for (const Seg &seg : segs) {
        if (seg.docLen <= 0 || (seg.kind != Seg::Plain && seg.kind != Seg::Content))
            continue;
        const int a = qMax(docStart, seg.docStart);
        const int b = qMin(docEnd, seg.docStart + seg.docLen);
        if (a >= b)
            continue;
        if (seg.kind == Seg::Plain) {
            syncChain({});
            out += markdown.mid(seg.mdStart + (a - seg.docStart), b - a);
            continue;
        }
        if (covered.at(seg.spanIdx) == total.at(seg.spanIdx)) {
            syncChain({});
            if (lastWholeTop != seg.spanIdx) {
                out += spans.at(seg.spanIdx).rawText;
                lastWholeTop = seg.spanIdx;
            }
            continue;
        }
        syncChain(chainOf(seg.ownerIdx));
        out += markdown.mid(seg.mdStart + (a - seg.docStart), b - a);
    }
    syncChain({});
    return out;
}

EditResult cutRangeResult(const QString &markdown, const QList<int> &revealedSpans,
                          int docStart, int docEnd)
{
    const auto spans = spansFor(markdown);
    QList<FlatSpan> flat;
    const auto segs = segmentsFor(spans, markdown, revealedSpans, &flat);

    // Mirror of markdownForRange: plain slices remove 1:1; a span node
    // whose entire visible content is covered — highest such ancestor
    // wins — is removed whole (markers included), partial coverage
    // removes content characters only; marker characters in the selection
    // are ignored. Unlike applyDocumentEdit, this applies to revealed
    // spans too: cut removes exactly what copy captured.
    QList<int> total(flat.size(), 0);
    QList<int> covered(flat.size(), 0);
    for (const Seg &seg : segs) {
        if (seg.kind != Seg::Content || seg.ownerIdx < 0)
            continue;
        const int a = qMax(docStart, seg.docStart);
        const int b = qMin(docEnd, seg.docStart + seg.docLen);
        const int cov = qMax(0, b - a);
        for (int k = seg.ownerIdx; k != -1; k = flat.at(k).parent) {
            total[k] += seg.docLen;
            covered[k] += cov;
        }
    }
    auto highestFullyCovered = [&](int node) {
        int best = -1;
        for (int k = node; k != -1; k = flat.at(k).parent) {
            if (total.at(k) > 0 && covered.at(k) == total.at(k))
                best = k;
        }
        return best;
    };

    QList<QPair<int, int>> removals;
    for (const Seg &seg : segs) {
        if (seg.docLen <= 0 || (seg.kind != Seg::Plain && seg.kind != Seg::Content))
            continue;
        const int a = qMax(docStart, seg.docStart);
        const int b = qMin(docEnd, seg.docStart + seg.docLen);
        if (a >= b)
            continue;
        if (seg.kind == Seg::Content) {
            const int h = highestFullyCovered(seg.ownerIdx);
            if (h != -1) {
                const QPair<int, int> whole{flat.at(h).span->start,
                                            flat.at(h).span->end};
                if (removals.isEmpty() || removals.last() != whole)
                    removals.append(whole);
                continue;
            }
        }
        removals.append({seg.mdStart + (a - seg.docStart),
                         seg.mdStart + (b - seg.docStart)});
    }

    int mdCursor;
    if (!removals.isEmpty()) {
        mdCursor = removals.first().first;
    } else {
        mdCursor = documentToMarkdown(markdown, revealedSpans, docStart);
    }

    QString out = markdown;
    for (int i = removals.size() - 1; i >= 0; --i)
        out.remove(removals.at(i).first, removals.at(i).second - removals.at(i).first);

    return {out, mdCursor};
}

EditResult applyDocumentEdit(const QString &markdown,
                             const QList<int> &revealedSpans,
                             int pos, int removedLen, const QString &insertedText)
{
    const auto spans = spansFor(markdown);
    QList<FlatSpan> flat;
    const auto segs = segmentsFor(spans, markdown, revealedSpans, &flat);
    const int removeEnd = pos + removedLen;

    // Visible-content coverage per hidden span node: a node whose entire
    // visible content (its own and its descendants') is removed loses its
    // whole markdown range, markers included — and the highest fully
    // covered ancestor wins, so emptying a nested span empties outward.
    // A revealed span's characters (markers too) remove 1:1 instead.
    QList<int> total(flat.size(), 0);
    QList<int> covered(flat.size(), 0);
    for (const Seg &seg : segs) {
        if (seg.kind != Seg::Content || seg.ownerIdx < 0
            || revealedSpans.contains(seg.spanIdx))
            continue;
        const int a = qMax(pos, seg.docStart);
        const int b = qMin(removeEnd, seg.docStart + seg.docLen);
        const int cov = qMax(0, b - a);
        for (int k = seg.ownerIdx; k != -1; k = flat.at(k).parent) {
            total[k] += seg.docLen;
            covered[k] += cov;
        }
    }
    auto highestFullyCovered = [&](int node) {
        int best = -1;
        for (int k = node; k != -1; k = flat.at(k).parent) {
            if (total.at(k) > 0 && covered.at(k) == total.at(k))
                best = k;
        }
        return best;
    };

    // Map the removed document range onto markdown ranges.
    QList<QPair<int, int>> removals;
    for (const Seg &seg : segs) {
        if (seg.docLen <= 0)
            continue;
        const int a = qMax(pos, seg.docStart);
        const int b = qMin(removeEnd, seg.docStart + seg.docLen);
        if (a >= b)
            continue;
        if (seg.kind == Seg::Content && !revealedSpans.contains(seg.spanIdx)) {
            const int h = highestFullyCovered(seg.ownerIdx);
            if (h != -1) {
                const QPair<int, int> whole{flat.at(h).span->start,
                                            flat.at(h).span->end};
                if (removals.isEmpty() || removals.last() != whole)
                    removals.append(whole);
                continue;
            }
        }
        removals.append({seg.mdStart + (a - seg.docStart),
                         seg.mdStart + (b - seg.docStart)});
    }

    int mdInsertPos;
    if (!removals.isEmpty()) {
        mdInsertPos = removals.first().first;
    } else {
        mdInsertPos = documentToMarkdown(markdown, revealedSpans, pos);
    }

    QString md = markdown;
    for (int i = removals.size() - 1; i >= 0; --i)
        md.remove(removals.at(i).first, removals.at(i).second - removals.at(i).first);
    md.insert(mdInsertPos, insertedText);

    return {md, mdInsertPos + int(insertedText.length())};
}

QList<HighlightRange> searchHighlightRanges(const QString &markdown,
                                            const QList<int> &revealedSpans,
                                            const QList<HighlightRange> &displayMatches,
                                            bool verbatim)
{
    QList<HighlightRange> out;
    if (displayMatches.isEmpty())
        return out;

    // Display length bounds stale matches: a keystroke can shorten the
    // text before the queued search recompute delivers fresh ranges.
    const int displayLength = verbatim
        ? static_cast<int>(markdown.length())
        : static_cast<int>(documentText(markdown, QList<int>()).length());

    for (const HighlightRange &match : displayMatches) {
        const int start = qBound(0, match.start, displayLength);
        const int end = qBound(0, match.start + match.length, displayLength);
        if (end <= start)
            continue;
        if (verbatim) {
            out.append({start, end - start, match.current});
            continue;
        }
        // Per matched character: display → markdown (no reveals) →
        // document (current reveals). The last character maps as a
        // position-of-char so the range covers exactly the matched
        // characters — plus any markers a reveal put between them.
        const int mdStart = documentToMarkdown(markdown, QList<int>(), start);
        const int mdLast = documentToMarkdown(markdown, QList<int>(), end - 1);
        const int docStart = markdownToDocument(markdown, revealedSpans, mdStart);
        const int docEnd = markdownToDocument(markdown, revealedSpans, mdLast) + 1;
        if (docEnd > docStart)
            out.append({docStart, docEnd - docStart, match.current});
    }
    return out;
}

QVariantMap mathSpanRangeIn(const QString &markdown, int mdPos)
{
    QVariantMap map{{QStringLiteral("found"), false}};
    const auto spans = spansFor(markdown);
    const FormattedSpan *span = mathSpanAt(spans, mdPos);
    if (!span)
        return map;
    map.insert(QStringLiteral("found"), true);
    map.insert(QStringLiteral("mdStart"), span->start);
    map.insert(QStringLiteral("mdEnd"), span->end);
    map.insert(QStringLiteral("contentStart"), span->start + span->openLen);
    map.insert(QStringLiteral("contentEnd"), span->end - span->closeLen);
    map.insert(QStringLiteral("tex"),
               markdown.mid(span->start + span->openLen,
                            span->contentLength()));
    return map;
}

bool shouldAutoPairDollarIn(const QString &markdown, int mdPos,
                            bool ignoreFollowing)
{
    mdPos = qBound(0, mdPos, markdown.size());
    // Escaped: the caret follows an unescaped backslash — this $ is the
    // literal \$.
    if (escapedAt(markdown, mdPos))
        return false;
    // Unmatched $ left of the caret: this keystroke closes that span
    // rather than opening a new one. Escaped dollars are prose.
    int dollars = 0;
    for (int i = 0; i < mdPos; ++i) {
        if (markdown.at(i) == QLatin1Char('$') && !escapedAt(markdown, i))
            ++dollars;
    }
    if ((dollars % 2) == 1)
        return false;
    // Inside an inline code span, $ is always literal.
    const auto spans = spansFor(markdown);
    if (insideSpanWithFlags(spans, mdPos, SpanFormat::Code))
        return false;
    // A letter, digit, or $ right of the caret: typing a dollar in front
    // of existing text means a price, not a formula. The selection-wrap
    // path skips this — the selection is what follows.
    if (!ignoreFollowing && mdPos < markdown.size()) {
        const QChar next = markdown.at(mdPos);
        if (next.isLetterOrNumber() || next == QLatin1Char('$'))
            return false;
    }
    return true;
}

QVariantMap linkSpanIn(const QString &markdown, int mdPos)
{
    const auto spans = spansFor(markdown);
    const FormattedSpan *span = deepestLinkAt(spans, mdPos);
    QVariantMap map;
    map.insert(QStringLiteral("found"), span != nullptr);
    if (span) {
        map.insert(QStringLiteral("start"), span->start);
        map.insert(QStringLiteral("end"), span->end);
        map.insert(QStringLiteral("text"),
                   markdown.mid(span->start + span->openLen, span->contentLength()));
        map.insert(QStringLiteral("url"), span->url);
        map.insert(QStringLiteral("removable"), span->type == QLatin1String("link"));
    }
    return map;
}

quint32 formatFlagsAt(const QString &markdown, const QList<int> &revealedSpans,
                      int docPos)
{
    const auto spans = spansFor(markdown);
    QList<FlatSpan> flat;
    const auto segs = segmentsFor(spans, markdown, revealedSpans, &flat);
    // Both segment ends inclusive, matching the reveal rule: with the
    // caret at a span edge the toolbar shows the span's state, exactly
    // when typing there would extend the span. A boundary position
    // touches two segments (plain|span or span|span) — the flags of
    // everything touched combine, as the reveal does.
    quint32 flags = 0;
    for (const Seg &seg : segs) {
        if (seg.docLen <= 0 || docPos < seg.docStart
            || docPos > seg.docStart + seg.docLen)
            continue;
        for (int k = seg.ownerIdx; k != -1; k = flat.at(k).parent)
            flags |= flat.at(k).span->formatFlags;
    }
    return flags & ~quint32(SpanFormat::Marker);
}

QString linkAt(const QString &markdown, const QList<int> &revealedSpans,
               int docPos)
{
    const auto spans = spansFor(markdown);
    QList<FlatSpan> flat;
    const auto segs = segmentsFor(spans, markdown, revealedSpans, &flat);
    for (const Seg &seg : segs) {
        if (seg.docLen <= 0 || docPos < seg.docStart
            || docPos >= seg.docStart + seg.docLen)
            continue;
        // Deepest span in the owner chain that carries a URL — covers
        // link text, a revealed link's markers, and autolinks.
        for (int k = seg.ownerIdx; k != -1; k = flat.at(k).parent) {
            if (!flat.at(k).span->url.isEmpty())
                return flat.at(k).span->url;
        }
        return QString();
    }
    return QString();
}

QList<MathSegment> hiddenMathSegments(const QString &markdown,
                                      const QList<int> &revealedSpans)
{
    QList<MathSegment> out;
    for (const Seg &seg : segmentsFor(markdown, revealedSpans)) {
        if (seg.kind != Seg::Content || seg.docLen <= 0)
            continue;
        if (!(seg.flags & SpanFormat::Math))
            continue;
        // Only HIDDEN math spans overlay an image; a revealed one shows its
        // editable $…$ source, so skip it (its top-level span is revealed).
        if (seg.spanIdx >= 0 && revealedSpans.contains(seg.spanIdx))
            continue;
        out.append({markdown.mid(seg.mdStart, seg.mdLen), seg.docStart,
                    seg.docStart + seg.docLen, seg.flags});
    }
    return out;
}

QList<QPair<int, QString>> subtreeMarkers(const QString &markdown, int spanIndex)
{
    QList<QPair<int, QString>> markers;
    const auto spans = spansFor(markdown);
    if (spanIndex < 0 || spanIndex >= spans.size())
        return markers;
    collectSubtreeMarkers(spans.at(spanIndex), markers);
    return markers;
}

} // namespace InlineMarkdown
