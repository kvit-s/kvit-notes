// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
#include "markdownformatter.h"
#include "wikilinkscanner.h"
#include <QRegularExpression>
#include <algorithm>

// ---- The span-type registry ----
//
// Every inline type is a data row; the parser is a single left-to-right
// scan that tries the rows in table order at each position (longest marker
// first within a delimiter family) and takes the first complete match.
// Adding a symmetric type is one row; only a genuinely new *shape* of
// syntax adds a matcher.

// ---- Bounding the parse ----
//
// A block's text is untrusted: it arrives by import, paste or sync. Two shapes
// used to cost far more than their size suggested. A run of N backticks made
// every position in the run rescan the whole remainder for a closing run of
// its own length, which is quadratic; and nested color spans made each level
// rescan and then recursively reparse almost the entire suffix, which measured
// about thirty seconds at depth 1,600 (roughly 50 KiB) and risked exhausting
// the stack besides.
//
// Three things hold the cost down. Backtick runs are now scanned once per run
// instead of once per position (CodeRunScan). Nesting stops at
// kMaxSpanDepth, so content deeper than that stays plain text rather than
// being parsed again per level. And every scanning loop spends from one work
// budget shared by the whole parse, including its nested levels; once that is
// gone the remaining matchers decline, which leaves the rest of the text
// literal — a degradation, but a bounded one, and only reachable by input far
// past anything a person writes.
//
// The budget is generous on purpose. Ordinary parsing is close to linear, and
// the quadratic term only matters for large inputs, so a constant floor plus a
// per-character allowance keeps every realistic block far inside it.
struct MarkdownParseState {
    int depth = 0;
    qint64 steps = 0;
    qint64 budget = 0;

    bool exhausted() const { return steps > budget; }
    // Charge `n` units of scanning work; true while the budget holds.
    bool spend(qint64 n = 1)
    {
        steps += n;
        return steps <= budget;
    }
};

namespace {

constexpr int kMaxSpanDepth = 24;
constexpr qint64 kParseStepFloor = 2'000'000;
constexpr qint64 kParseStepsPerChar = 512;

qint64 parseBudgetFor(const QString &markdown)
{
    return kParseStepFloor + kParseStepsPerChar * markdown.length();
}

enum MatcherKind {
    DelimiterPair,    // symmetric marker, e.g. ** ... **
    LinkMatcher,      // [text](url) — asymmetric markers
    AutolinkMatcher,  // bare http(s) URL — zero-length markers
    ColorMatcher,     // <span style="color:VALUE">…</span>
    MathMatcher,      // $…$ inline math with Pandoc adjacency
    CodeMatcher,      // `code`, or a longer backtick run closed by its equal
    EscapeMatcher,    // \X literal punctuation
    WikiLinkMatcher,  // [[target#heading|alias]] note references
};

// A character is escaped when preceded by an odd run of backslashes: "\*"
// escapes the star; in "\\*" the second backslash is itself escaped, so the
// star is an ordinary marker.
bool isEscapedAt(const QString &md, int pos)
{
    int n = 0;
    for (int i = pos - 1; i >= 0 && md.at(i) == QLatin1Char('\\'); --i)
        ++n;
    return (n & 1) != 0;
}

struct SpanTypeDef {
    const char *name;
    MatcherKind matcher;
    const char *openMarker;   // DelimiterPair: also the closing marker
    bool verbatimContent;     // inline code: nothing parses inside
    bool wordBoundary;        // "_" family: no intra-word open/close
    quint32 formatFlags;      // SpanFormat flags of the content
    // Pandoc's sup/sub rule: content may not contain spaces, so prose
    // like "either ~5 or ~3" never subscripts across words.
    bool noSpaceContent = false;
};

const SpanTypeDef kSpanTypes[] = {
    // The "*" family rows come first: they are the canonical markers that
    // formatting commands emit; the "_" variants (features.md §2.3) parse
    // to the same type names and flags but only at word boundaries.
    {"bolditalic", DelimiterPair, "***", false, false, SpanFormat::BoldItalic},
    {"bold",       DelimiterPair, "**",  false, false, SpanFormat::Bold},
    {"italic",     DelimiterPair, "*",   false, false, SpanFormat::Italic},
    {"bolditalic", DelimiterPair, "___", false, true,  SpanFormat::BoldItalic},
    {"bold",       DelimiterPair, "__",  false, true,  SpanFormat::Bold},
    {"italic",     DelimiterPair, "_",   false, true,  SpanFormat::Italic},
    // ~~ per features.md §2.3; == and ++ are the markdown-it/Obsidian
    // conventions.
    {"strike",     DelimiterPair, "~~",  false, false, SpanFormat::Strike},
    {"highlight",  DelimiterPair, "==",  false, false, SpanFormat::Highlight},
    {"underline",  DelimiterPair, "++",  false, false, SpanFormat::Underline},
    // ^sup^ / ~sub~ (Pandoc's extension syntax). "~" sits after "~~":
    // the scan tries longer markers of a family first, exactly like
    // "***"/"**"/"*". Space-free content only, so tildes and carets in
    // prose stay literal.
    {"superscript", DelimiterPair, "^",  false, false, SpanFormat::Superscript, true},
    {"subscript",   DelimiterPair, "~",  false, false, SpanFormat::Subscript,   true},
    {"code",       CodeMatcher,     "`",   true,  false, SpanFormat::Code},
    // Inline math $x^2$ (features.md §1.2.15). Verbatim TeX content; the
    // Pandoc adjacency rule (in matchTypeAt) keeps prose dollars like "$5
    // and $6" literal. Only "$" starts this type, so its position among
    // the rows does not affect any other marker.
    {"math",       MathMatcher,     "$", true,  false, SpanFormat::Math},
    // Backslash escapes: "\X" is a one-char span whose opening marker is
    // the backslash — concealment hides it exactly like other markers, so
    // "\*" displays as "*". Verbatim, zero format flags: the content is
    // plain text that no command targets.
    {"escape",     EscapeMatcher,   "\\", true, false, 0},
    // Text color (features.md §2.1), the one recognized inline-HTML form.
    // Parameterized like a link (the value rides on the opening marker),
    // content nests other spans, closing marker fixed. Anything failing the
    // exact grammar stays literal text.
    {"color",      ColorMatcher,    "",  false, false, SpanFormat::Color},
    // [[wiki-links]]. MUST sit before the "link" row: both start at '[' and
    // rows are tried in table order, so the scan sees "[[" before
    // LinkMatcher's "[". Verbatim content — the target is a note name, never
    // inline markup — and the concealed display shows the alias when one is
    // present.
    {"wikilink",   WikiLinkMatcher, "[[", true, false,
     SpanFormat::Link | SpanFormat::WikiLink},
    // Links (features.md §2.4). An autolink's content is its URL —
    // verbatim, so URL characters never parse as inline markers.
    {"link",       LinkMatcher,     "",  false, false, SpanFormat::Link},
    {"autolink",   AutolinkMatcher, "",  true,  false, SpanFormat::Link},
};

// The color-span grammar's character classes. ASCII, deliberately: a CSS
// named color and a hex triplet are both ASCII, and so is the whitespace the
// HTML attribute syntax allows around a value.
bool isColorSpace(QChar c)
{
    return c == u' ' || c == u'\t' || c == u'\n' || c == u'\r' || c == u'\f'
        || c == u'\v';
}

bool isHexDigit(QChar c)
{
    return (c >= u'0' && c <= u'9') || (c >= u'a' && c <= u'f')
        || (c >= u'A' && c <= u'F');
}

bool isAsciiLetter(QChar c)
{
    return (c >= u'a' && c <= u'z') || (c >= u'A' && c <= u'Z');
}

// Recognize a color-span opening marker at `pos`:
//   <span style="color:VALUE">  (double- or single-quoted, one property)
// VALUE is #rgb, #rrggbb, or a run of ASCII letters (a CSS named color),
// with optional whitespace around the value. Returns the marker length and
// the trimmed value on success, or -1 — the exact-grammar rule that keeps
// every near-miss (extra attributes, other properties, other tags) literal.
int matchColorOpen(const QString &md, int pos, QString *valueOut = nullptr)
{
    // Read by hand rather than with a regular expression. A color span's
    // balancing scan asks this question at every character of its content and
    // at every nesting level, so the cost of one call is multiplied by the
    // whole input: the prefix test below answers no for almost every position
    // in a single comparison, and the rest of the grammar is a short walk.
    // The regular expression it replaces cost tens of microseconds per call
    // once the subject string was large.
    static const QLatin1String kPrefix("<span style=");
    static const QLatin1String kProperty("color:");
    const QStringView view(md);
    if (!view.mid(pos).startsWith(kPrefix))
        return -1;
    int i = pos + kPrefix.size();
    if (i >= md.size())
        return -1;
    const QChar quote = md.at(i);          // the closing quote must match
    if (quote != u'"' && quote != u'\'')
        return -1;
    ++i;
    if (!view.mid(i).startsWith(kProperty))
        return -1;
    i += kProperty.size();
    while (i < md.size() && isColorSpace(md.at(i)))
        ++i;

    // VALUE is #rgb, #rrggbb, or a run of ASCII letters (a CSS named color).
    const int valueStart = i;
    if (i < md.size() && md.at(i) == u'#') {
        ++i;
        int digits = 0;
        while (i < md.size() && isHexDigit(md.at(i))) {
            ++i;
            ++digits;
        }
        if (digits != 3 && digits != 6)
            return -1;
    } else {
        while (i < md.size() && isAsciiLetter(md.at(i)))
            ++i;
        if (i == valueStart)
            return -1;
    }
    const int valueEnd = i;

    while (i < md.size() && isColorSpace(md.at(i)))
        ++i;
    if (i >= md.size() || md.at(i) != quote)
        return -1;
    ++i;
    if (i >= md.size() || md.at(i) != u'>')
        return -1;
    ++i;
    if (valueOut)
        *valueOut = md.mid(valueStart, valueEnd - valueStart);
    return i - pos;
}

const SpanTypeDef *findTypeDef(const QString &name)
{
    for (const SpanTypeDef &def : kSpanTypes) {
        if (name == QLatin1String(def.name))
            return &def;
    }
    return nullptr;
}

// Does the registry contain a delimiter row of the same marker character
// with length strictly greater / exactly two? Drives the family rules
// below, so a family without a longer sibling (e.g. "~~" with no "~~~")
// never inherits the "*" family's deference behavior.
bool familyHasLongerThan(QChar c, int n)
{
    for (const SpanTypeDef &def : kSpanTypes) {
        if (def.matcher != DelimiterPair)
            continue;
        const QLatin1String m(def.openMarker);
        if (m.size() > n && m.front() == c)
            return true;
    }
    return false;
}

bool familyHasDouble(QChar c)
{
    for (const SpanTypeDef &def : kSpanTypes) {
        if (def.matcher != DelimiterPair)
            continue;
        const QLatin1String m(def.openMarker);
        if (m.size() == 2 && m.front() == c)
            return true;
    }
    return false;
}

// Match a symmetric delimiter span opening at `pos`. Returns the span end
// (index just past the closing marker) or -1. The rules generalize the
// earlier hand-written "*" branches, parameterized by marker char/length:
//  - opening defers to a longer sibling type (the run continues and the
//    registry has a longer marker of this char) — the longer row already
//    had its chance at this position and failed;
//  - content must be non-empty;
//  - a closing candidate immediately preceded by the marker char is the
//    tail of a longer run and is skipped;
//  - a length-1 closing candidate that starts the double marker is only
//    taken when no later single marker could close us past that pair
//    ("*a **b** c*" keeps the whole italic).
int matchDelimiter(const QString &md, int pos, const QString &marker, bool wordBoundary,
                   MarkdownParseState &state, bool noSpaceContent = false)
{
    const QChar c = marker.at(0);
    const int n = marker.length();

    if (pos + n > md.length())
        return -1;
    for (int i = 0; i < n; ++i) {
        if (md.at(pos + i) != c)
            return -1;
    }

    // "_" family flanking rule: an opening flanked by a word character on
    // the outside never opens a span, so snake_case stays literal. An
    // opening preceded by the marker char is mid-run — after a longer
    // sibling failed to match, the leftover run chars are literal too
    // ("__a__b" must not yield a sneaky "_a_" at position 1).
    if (wordBoundary && pos > 0
        && (md.at(pos - 1).isLetterOrNumber() || md.at(pos - 1) == c))
        return -1;

    if (n == 3 && !wordBoundary) {
        // Longest family marker: search skips escaped candidates (fix 5);
        // empty content rejects.
        int from = pos + 3;
        while (true) {
            const int closePos = md.indexOf(marker, from);
            if (!state.spend((closePos < 0 ? md.length() : closePos) - from + 1))
                return -1;
            if (closePos == -1)
                return -1;
            if (isEscapedAt(md, closePos)) {
                from = closePos + 1;
                continue;
            }
            return closePos > pos + 3 ? closePos + 3 : -1;
        }
    }

    if (n < 3 && pos + n < md.length() && md.at(pos + n) == c && familyHasLongerThan(c, n))
        return -1; // part of a longer (unclosed) run; defer

    // Space-free content (sup/sub): the close must land before the
    // first whitespace, or the span does not exist at all — no later
    // candidate can help, its content would contain the space too.
    int contentLimit = md.length();
    if (noSpaceContent) {
        for (int i = pos + n; i < md.length(); ++i) {
            if (!state.spend())
                return -1;
            if (md.at(i).isSpace()) {
                contentLimit = i;
                break;
            }
        }
    }

    int searchStart = pos + n;
    while (searchStart < md.length()) {
        const int cand = md.indexOf(marker, searchStart);
        if (!state.spend((cand < 0 ? md.length() : cand) - searchStart + 1))
            return -1;
        if (cand == -1 || cand > contentLimit)
            return -1;
        if (cand > pos + n) {
            if (isEscapedAt(md, cand)) {
                searchStart = cand + 1; // an escaped closer is literal (fix 5)
                continue;
            }
            if (md.at(cand - 1) == c) {
                searchStart = cand + 1; // tail of a longer run
                continue;
            }
            if (wordBoundary && cand + n < md.length()
                && md.at(cand + n).isLetterOrNumber()) {
                searchStart = cand + 1; // intra-word close: keep looking
                continue;
            }
            if (n == 1 && familyHasDouble(c)
                && cand + 1 < md.length() && md.at(cand + 1) == c) {
                // Candidate starts a double marker: if that pair closes and
                // a single marker follows later, skip past the pair.
                const int dblClose = md.indexOf(QString(2, c), cand + 2);
                if (dblClose != -1 && md.indexOf(c, dblClose + 2) != -1) {
                    searchStart = dblClose + 2;
                    continue;
                }
            }
            return cand + n;
        }
        searchStart = cand + 1;
    }
    return -1;
}

// One backtick run's closing candidates, computed once for the whole run.
//
// Every position inside a run of L backticks opens a candidate code span, of
// length L, L-1, … 1, and all of them start looking for their closing run at
// the same place: the first character after the run. So the search region is
// identical for the whole run, and one pass over it answers for every position
// at once — where the first run of each length is, or that there is none.
// Scanning per position instead made a run of N backticks cost O(N^2).
struct CodeRunScan {
    int runStart = -1;          // [runStart, runEnd) is the run this describes
    int runEnd = -1;
    QList<int> firstClose;      // by open length: first closing run, or -1

    bool covers(int pos) const { return pos >= runStart && pos < runEnd; }

    void build(const QString &md, int pos, MarkdownParseState &state)
    {
        int start = pos;
        while (start > 0 && md.at(start - 1) == QLatin1Char('`'))
            --start;
        int end = pos;
        while (end < md.length() && md.at(end) == QLatin1Char('`'))
            ++end;
        runStart = start;
        runEnd = end;
        const int longest = end - start;
        firstClose.assign(longest + 1, -1);
        state.spend(longest);
        int i = end;
        while (i < md.length()) {
            if (md.at(i) != QLatin1Char('`')) {
                ++i;
                if (!state.spend())
                    return;
                continue;
            }
            int run = 0;
            while (i + run < md.length() && md.at(i + run) == QLatin1Char('`'))
                ++run;
            if (run <= longest && firstClose[run] < 0)
                firstClose[run] = i;
            i += run;
            if (!state.spend(run))
                return;
        }
    }
};

// Match one registry row at `pos`; fills `span` (start/end/type/markers/
// flags) and returns true on a complete match.
bool matchTypeAt(const SpanTypeDef &def, const QString &md, int pos,
                 FormattedSpan &span, MarkdownParseState &state,
                 CodeRunScan &codeRuns)
{
    // Past the budget nothing matches, so the remaining text stays literal and
    // the outer scan walks out in linear time.
    if (state.exhausted())
        return false;

    // Fix 5: a delimiter preceded by a backslash never opens a span — the
    // old \$-only rule generalized to the whole registry. The escape row
    // itself passes (an even backslash run before "\X" leaves that
    // backslash unescaped).
    if (pos > 0 && isEscapedAt(md, pos))
        return false;

    switch (def.matcher) {
    case DelimiterPair: {
        const QString marker = QLatin1String(def.openMarker);
        const int end = matchDelimiter(md, pos, marker, def.wordBoundary,
                                       state, def.noSpaceContent);
        if (end < 0)
            return false;
        span.openLen = marker.length();
        span.closeLen = marker.length();
        span.end = end;
        break;
    }
    case CodeMatcher: {
        // `code`, or a longer backtick run closed by a run of exactly the
        // same length. CommonMark sizes the delimiter to the content so a
        // span may contain backticks at all; a one-length-only reader would
        // mis-split every such span it met.
        if (md.at(pos) != QLatin1Char('`'))
            return false;
        // The closing candidates for every position in this run, scanned once
        // (see CodeRunScan) and reused as the outer parse walks through it.
        if (!codeRuns.covers(pos))
            codeRuns.build(md, pos, state);
        const int openLen = codeRuns.runEnd - pos;
        const int closeAt = openLen < codeRuns.firstClose.size()
                                ? codeRuns.firstClose.at(openLen) : -1;
        if (closeAt < 0 || closeAt == pos + openLen)
            return false;   // unclosed, or an empty span
        span.openLen = openLen;
        span.closeLen = openLen;
        span.end = closeAt + openLen;
        break;
    }
    case LinkMatcher: {
        // [text](url): text non-empty with no brackets (an invisible or
        // ambiguous span must not exist), url with no spaces or parens.
        if (md.at(pos) != QLatin1Char('['))
            return false;
        const int closeBracket = md.indexOf(QLatin1String("]("), pos + 1);
        if (!state.spend((closeBracket < 0 ? md.length() : closeBracket) - pos))
            return false;
        if (closeBracket <= pos + 1)
            return false;
        const QString text = md.mid(pos + 1, closeBracket - pos - 1);
        if (text.contains(QLatin1Char('[')) || text.contains(QLatin1Char(']'))
            || text.contains(QLatin1Char('\n')))
            return false;
        const int closeParen = md.indexOf(QLatin1Char(')'), closeBracket + 2);
        if (!state.spend((closeParen < 0 ? md.length() : closeParen)
                         - closeBracket))
            return false;
        if (closeParen == -1)
            return false;
        const QString url = md.mid(closeBracket + 2, closeParen - closeBracket - 2);
        if (url.contains(QLatin1Char(' ')) || url.contains(QLatin1Char('\n'))
            || url.contains(QLatin1Char('(')))
            return false;
        span.openLen = 1;
        span.closeLen = closeParen - closeBracket + 1; // "](url)"
        span.end = closeParen + 1;
        span.url = url;
        break;
    }
    case ColorMatcher: {
        // <span style="color:VALUE">CONTENT</span>. The opening marker must
        // pass the exact grammar; CONTENT nests other spans (including nested
        // color spans), so the matching </span> is found by balancing color-
        // span openings against closes.
        QString value;
        const int openLen = matchColorOpen(md, pos, &value);
        if (openLen < 0)
            return false;
        static const QString kClose = QStringLiteral("</span>");
        int depth = 0;
        int i = pos + openLen;
        int closeAt = -1;
        while (i < md.length()) {
            if (!state.spend())
                return false;
            const int nestOpen = matchColorOpen(md, i);
            if (nestOpen > 0) {
                ++depth;
                i += nestOpen;
                continue;
            }
            if (QStringView(md).mid(i).startsWith(kClose)) {
                if (depth == 0) {
                    closeAt = i;
                    break;
                }
                --depth;
                i += kClose.length();
                continue;
            }
            ++i;
        }
        if (closeAt < 0)
            return false;         // unclosed: stays literal text
        if (closeAt == pos + openLen)
            return false;         // empty content: an invisible span cannot exist
        span.openLen = openLen;
        span.closeLen = kClose.length();
        span.end = closeAt + kClose.length();
        span.color = value;
        break;
    }
    case MathMatcher: {
        // Inline math $…$ with Pandoc's adjacency rule: the opening $ is
        // immediately followed by a non-space (and is not the $$ of a math
        // block), the closing $ is immediately preceded by a non-space and
        // not followed by a digit, and the content is a single non-empty
        // line. The first $ after the opener decides — if it fails the close
        // test the opener stays literal, so "$5 and $6" is prose.
        if (md.at(pos) != QLatin1Char('$'))
            return false;
        const int contentStart = pos + 1;
        if (contentStart >= md.length())
            return false;
        const QChar afterOpen = md.at(contentStart);
        if (afterOpen.isSpace() || afterOpen == QLatin1Char('$'))
            return false;                        // no space after $; $$ is block
        int close = -1;
        for (int i = contentStart; i < md.length(); ++i) {
            if (!state.spend())
                return false;
            const QChar c = md.at(i);
            if (c == QLatin1Char('\n'))
                return false;                    // inline math is single-line
            if (c == QLatin1Char('$')) {
                if (isEscapedAt(md, i))
                    continue;                    // \$ inside TeX is literal (fix 5)
                const bool spaceBefore = md.at(i - 1).isSpace();
                const bool digitAfter = i + 1 < md.length()
                                        && md.at(i + 1).isDigit();
                if (!spaceBefore && !digitAfter && i > contentStart)
                    close = i;
                break;                           // first unescaped $ decides
            }
        }
        if (close < 0)
            return false;
        span.openLen = 1;
        span.closeLen = 1;
        span.end = close + 1;
        break;
    }
    case WikiLinkMatcher: {
        // [[target]], [[target|alias]], [[target#heading]],
        // [[target#heading|alias]]. The target may contain spaces but not
        // []|# or newlines; [[#heading]] is the same-note anchor form. The
        // url carries the raw target with a scheme-like prefix so downstream
        // link handling can tell wiki from web links. With an alias the
        // opening marker swallows "target|", so concealment shows only the
        // alias text.
        WikiLinkScanner::Occurrence occurrence;
        if (!WikiLinkScanner::matchAt(md, pos, &occurrence))
            return false;
        span.openLen = occurrence.aliasStart >= 0
            ? occurrence.targetLength + 3 : 2; // "[[" (+ "target#h|")
        span.closeLen = 2;
        span.end = occurrence.start + occurrence.length;
        span.url = QStringLiteral("kvit-note:") + occurrence.rawTarget;
        break;
    }
    case EscapeMatcher: {
        // "\X" for X in the escapable set: registry markers plus the block
        // prefixes whose escaped form should display cleanly. "\\" consumes
        // both characters, so the second backslash cannot escape what
        // follows. The markdown source keeps the backslash (round-trip
        // fidelity); only the display and HTML export conceal it.
        if (md.at(pos) != QLatin1Char('\\') || pos + 1 >= md.length())
            return false;
        static const QString kEscapable =
            QStringLiteral("*_~^=+`[]\\$#->|");
        if (!kEscapable.contains(md.at(pos + 1)))
            return false;
        span.openLen = 1;
        span.closeLen = 0;
        span.end = pos + 2;
        break;
    }
    case AutolinkMatcher: {
        // A bare http(s) URL at a word boundary, running to whitespace or
        // a closing bracket; trailing sentence punctuation stays text.
        if (pos > 0 && (md.at(pos - 1).isLetterOrNumber()
                        || md.at(pos - 1) == QLatin1Char('/')))
            return false;
        const QStringView rest = QStringView(md).mid(pos);
        int schemeLen = 0;
        if (rest.startsWith(QLatin1String("https://")))
            schemeLen = 8;
        else if (rest.startsWith(QLatin1String("http://")))
            schemeLen = 7;
        else
            return false;
        int end = pos + schemeLen;
        while (end < md.length() && !md.at(end).isSpace()
               && md.at(end) != QLatin1Char(')') && md.at(end) != QLatin1Char(']')) {
            ++end;
            if (!state.spend())
                return false;
        }
        while (end > pos + schemeLen
               && QLatin1String(".,;:!?").contains(md.at(end - 1)))
            --end;
        if (end == pos + schemeLen)
            return false; // scheme alone is not a link
        span.openLen = 0;
        span.closeLen = 0;
        span.end = end;
        span.url = md.mid(pos, end - pos);
        break;
    }
    }
    span.start = pos;
    span.type = QLatin1String(def.name);
    span.rawText = md.mid(pos, span.end - pos);
    span.formatFlags = def.formatFlags;
    return true;
}

// Marker characters of a span and all its descendants — the difference
// between its markdown length and its rendered (display) length.
int markerCharCount(const FormattedSpan &span)
{
    int count = span.openLen + span.closeLen;
    for (const FormattedSpan &child : span.children)
        count += markerCharCount(child);
    return count;
}

// Children are parsed on the content substring (local coordinates) and
// shifted into the parent's absolute markdown/display coordinates.
void shiftSpan(FormattedSpan &span, int mdShift, int displayShift)
{
    span.start += mdShift;
    span.end += mdShift;
    span.displayStart += displayShift;
    span.displayEnd += displayShift;
    for (FormattedSpan &child : span.children)
        shiftSpan(child, mdShift, displayShift);
}

} // namespace

MarkdownFormatter::MarkdownFormatter(QObject *parent)
    : QObject(parent)
{
}

QString MarkdownFormatter::escapeHtml(const QString &text) const
{
    QString escaped = text;
    escaped.replace("&", "&amp;");
    escaped.replace("<", "&lt;");
    escaped.replace(">", "&gt;");
    escaped.replace("\"", "&quot;");
    return escaped;
}

QString MarkdownFormatter::convertSpanToHtml(const QString &text, const QString &type) const
{
    QString escaped = escapeHtml(text);
    if (type == "bold") {
        return "<b>" + escaped + "</b>";
    } else if (type == "italic") {
        return "<i>" + escaped + "</i>";
    } else if (type == "bolditalic") {
        return "<b><i>" + escaped + "</i></b>";
    }
    return escaped;
}

QString MarkdownFormatter::extractInnerText(const QString &rawText, const QString &type) const
{
    int markerLen = getMarkerLength(type);
    if (rawText.length() < markerLen * 2) {
        return rawText;
    }
    return rawText.mid(markerLen, rawText.length() - markerLen * 2);
}

// Marker length/string by type name. Meaningful for the symmetric types —
// the delimiter pairs, and inline code, whose default marker is one backtick
// even though an individual span may use a longer run. A link's markers are
// per-instance; use FormattedSpan's openLen/closeLen/openMarker/closeMarker
// for those, and for a code span whose run is longer than the default.
bool markerIsSymmetric(const SpanTypeDef *def)
{
    return def && (def->matcher == DelimiterPair || def->matcher == CodeMatcher);
}

int MarkdownFormatter::getMarkerLength(const QString &type) const
{
    const SpanTypeDef *def = findTypeDef(type);
    return markerIsSymmetric(def) ? int(qstrlen(def->openMarker)) : 0;
}

QString MarkdownFormatter::getMarkerString(const QString &type) const
{
    const SpanTypeDef *def = findTypeDef(type);
    return markerIsSymmetric(def) ? QLatin1String(def->openMarker) : QString();
}

QList<FormattedSpan> MarkdownFormatter::parseSpans(const QString &markdown) const
{
    MarkdownParseState state;
    state.budget = parseBudgetFor(markdown);
    return parseSpans(markdown, state);
}

QList<FormattedSpan> MarkdownFormatter::parseSpans(const QString &markdown,
                                                   MarkdownParseState &state) const
{
    QList<FormattedSpan> spans;

    if (markdown.isEmpty()) {
        return spans;
    }

    int pos = 0;
    int displayOffset = 0; // Marker chars skipped so far in display coords
    CodeRunScan codeRuns;  // valid for this string only, so it lives here

    while (pos < markdown.length()) {
        bool matched = false;
        for (const SpanTypeDef &def : kSpanTypes) {
            FormattedSpan span;
            if (!matchTypeAt(def, markdown, pos, span, state, codeRuns))
                continue;

            span.displayStart = pos - displayOffset;

            // Nested spans: the content parses recursively — except inside
            // inline code, whose content is verbatim. Display text strips
            // markers at every level. Past kMaxSpanDepth the content is left
            // as plain text: markup that deep is not something anyone wrote,
            // and each further level costs another scan of the whole content.
            if (!def.verbatimContent && span.contentLength() > 0
                && state.depth < kMaxSpanDepth) {
                const QString content =
                    markdown.mid(pos + span.openLen, span.contentLength());
                ++state.depth;
                span.children = parseSpans(content, state);
                --state.depth;
                for (FormattedSpan &child : span.children)
                    shiftSpan(child, span.start + span.openLen, span.displayStart);
            }

            const int markerChars = markerCharCount(span);
            span.displayEnd =
                span.displayStart + (span.end - span.start) - markerChars;

            displayOffset += markerChars;
            pos = span.end;
            spans.append(span);
            matched = true;
            break;
        }
        if (!matched) {
            pos++;
            state.spend();
        }
    }

    return spans;
}

// One span's HTML, with its content rendered from the parsed child tree.
//
// The parser already knows what is inside a span: children carries it, in
// absolute markdown coordinates. Reading each container's content back out of
// the raw text instead — which is what this used to do everywhere except bold
// and italic — meant the nested markup was escaped as literal characters, so
// `**[docs](https://x)**` came out as bold text reading "[docs](https://x)"
// rather than a bold link. Rendering from the tree makes every combination
// work by construction, and there is one place that decides what a type looks
// like.
QString MarkdownFormatter::renderSpanHtml(const QString &markdown,
                                          const FormattedSpan &span) const
{
    const int contentStart = span.start + span.openLen;
    const int contentEnd = span.end - span.closeLen;
    // Verbatim types (inline code, math, escapes, wiki links, autolinks) have
    // no children, so this is exactly their escaped raw content.
    const QString inner =
        renderRangeHtml(markdown, span.children, contentStart, contentEnd);

    if (span.type == QLatin1String("bolditalic"))
        return QStringLiteral("<b><i>") + inner + QStringLiteral("</i></b>");
    if (span.type == QLatin1String("bold"))
        return QStringLiteral("<b>") + inner + QStringLiteral("</b>");
    if (span.type == QLatin1String("italic"))
        return QStringLiteral("<i>") + inner + QStringLiteral("</i>");
    if (span.type == QLatin1String("code"))
        return QStringLiteral("<code>") + inner + QStringLiteral("</code>");
    if (span.type == QLatin1String("link")
        || span.type == QLatin1String("autolink")) {
        return QStringLiteral("<a href=\"") + escapeHtml(span.url)
             + QStringLiteral("\">") + inner + QStringLiteral("</a>");
    }
    if (span.type == QLatin1String("color")) {
        return QStringLiteral("<span style=\"color:") + escapeHtml(span.color)
             + QStringLiteral("\">") + inner + QStringLiteral("</span>");
    }
    // "escape" and "wikilink" conceal their markers and show the content: "\*"
    // displays as "*", and a wiki link shows the alias when it has one (which
    // the opening marker already swallowed) or else the target.
    // Every remaining type shows its text unstyled rather than being dropped —
    // table cells render through this path, and the fix-1 repair puts inline
    // code spans in cells.
    return inner;
}

// The HTML for markdown[from, to): each span rendered by type, the plain gaps
// between them escaped.
QString MarkdownFormatter::renderRangeHtml(const QString &markdown,
                                           const QList<FormattedSpan> &spans,
                                           int from, int to) const
{
    QString html;
    int pos = from;
    for (const FormattedSpan &span : spans) {
        if (span.start < pos || span.end > to)
            continue;   // defensive: a span outside the range it belongs to
        if (pos < span.start)
            html += escapeHtml(markdown.mid(pos, span.start - pos));
        html += renderSpanHtml(markdown, span);
        pos = span.end;
    }
    if (pos < to)
        html += escapeHtml(markdown.mid(pos, to - pos));
    return html;
}

QString MarkdownFormatter::toHtml(const QString &markdown) const
{
    if (markdown.isEmpty()) {
        return QString();
    }

    const QList<FormattedSpan> spans = parseSpans(markdown);

    if (spans.isEmpty()) {
        return escapeHtml(markdown);
    }

    return renderRangeHtml(markdown, spans, 0, markdown.length());
}

QString MarkdownFormatter::toMarkdown(const QString &html) const
{
    QString md = html;

    // Handle Qt's full HTML document structure
    // Extract body content if it's a full HTML document
    static QRegularExpression bodyRe("<body[^>]*>(.*)</body>",
        QRegularExpression::DotMatchesEverythingOption);
    QRegularExpressionMatch bodyMatch = bodyRe.match(md);
    if (bodyMatch.hasMatch()) {
        md = bodyMatch.captured(1);
    }

    // Remove paragraph tags, keeping content
    // Handle empty paragraphs
    static QRegularExpression emptyParaRe("<p[^>]*-qt-paragraph-type:empty[^>]*>.*?</p>");
    md.replace(emptyParaRe, "");

    // Handle normal paragraphs
    static QRegularExpression paraRe("<p[^>]*>(.*?)</p>",
        QRegularExpression::DotMatchesEverythingOption);
    md.replace(paraRe, "\\1");

    // Remove line breaks that Qt adds
    md.replace("<br />", "");
    md.replace("<br/>", "");
    md.replace("<br>", "");

    // Replace HTML tags with Markdown equivalents
    static QRegularExpression boldItalicRe1("<b><i>(.*?)</i></b>");
    static QRegularExpression boldItalicRe2("<i><b>(.*?)</b></i>");
    static QRegularExpression boldRe("<b>(.*?)</b>");
    static QRegularExpression italicRe("<i>(.*?)</i>");

    md.replace(boldItalicRe1, "***\\1***");
    md.replace(boldItalicRe2, "***\\1***");
    md.replace(boldRe, "**\\1**");
    md.replace(italicRe, "*\\1*");

    // Remove any remaining HTML tags
    static QRegularExpression anyTagRe("<[^>]*>");
    md.replace(anyTagRe, "");

    // Unescape HTML entities
    md.replace("&lt;", "<");
    md.replace("&gt;", ">");
    md.replace("&amp;", "&");
    md.replace("&quot;", "\"");
    md.replace("&nbsp;", " ");

    // Trim whitespace
    md = md.trimmed();

    return md;
}

QVariantList MarkdownFormatter::getFormattedSpans(const QString &markdown) const
{
    QVariantList result;
    QList<FormattedSpan> spans = parseSpans(markdown);

    for (const FormattedSpan &span : spans) {
        result.append(span.toVariantMap());
    }

    return result;
}

bool MarkdownFormatter::isInsideFormattedRegion(const QString &markdown, int cursorPos) const
{
    QList<FormattedSpan> spans = parseSpans(markdown);

    for (const FormattedSpan &span : spans) {
        if (cursorPos >= span.start && cursorPos <= span.end) {
            return true;
        }
    }

    return false;
}

QVariantMap MarkdownFormatter::getSpanAtCursor(const QString &markdown, int cursorPos) const
{
    QList<FormattedSpan> spans = parseSpans(markdown);

    for (const FormattedSpan &span : spans) {
        if (cursorPos >= span.start && cursorPos <= span.end) {
            return span.toVariantMap();
        }
    }

    return QVariantMap();
}

int MarkdownFormatter::markdownToDisplayPosition(const QString &markdown, int mdPos) const
{
    QList<FormattedSpan> spans = parseSpans(markdown);

    if (spans.isEmpty()) {
        return mdPos;
    }

    int offset = 0; // Total marker characters before mdPos

    for (const FormattedSpan &span : spans) {
        if (mdPos < span.start) {
            // Position is before this span
            break;
        } else if (mdPos <= span.end) {
            // Position is inside this span (including markers)
            int markerLen = getMarkerLength(span.type);

            if (mdPos < span.start + markerLen) {
                // Inside opening marker - map to display start
                return span.displayStart;
            } else if (mdPos > span.end - markerLen) {
                // Inside or after closing marker - map to display end
                return span.displayEnd;
            } else {
                // Inside the content
                int contentOffset = mdPos - (span.start + markerLen);
                return span.displayStart + contentOffset;
            }
        } else {
            // Position is after this span
            offset += getMarkerLength(span.type) * 2;
        }
    }

    return mdPos - offset;
}

int MarkdownFormatter::displayToMarkdownPosition(const QString &markdown, int displayPos) const
{
    QList<FormattedSpan> spans = parseSpans(markdown);

    if (spans.isEmpty()) {
        return displayPos;
    }

    int offset = 0; // Total marker characters to add

    for (const FormattedSpan &span : spans) {
        if (displayPos < span.displayStart) {
            // Position is before this span
            break;
        } else if (displayPos < span.displayEnd) {
            // Position is inside this span's displayed content
            int markerLen = getMarkerLength(span.type);
            int contentOffset = displayPos - span.displayStart;
            return span.start + markerLen + contentOffset;
        } else {
            // Position is after this span
            offset += getMarkerLength(span.type) * 2;
        }
    }

    return displayPos + offset;
}

namespace {

// The deepest span containing [selStart, selEnd] whose format flags
// overlap `flags` — the span a remove/toggle command operates on.
const FormattedSpan *deepestSpanWithFlags(const QList<FormattedSpan> &spans,
                                          int selStart, int selEnd, quint32 flags)
{
    const FormattedSpan *found = nullptr;
    for (const FormattedSpan &span : spans) {
        if (selStart < span.start || selEnd > span.end)
            continue;
        if (span.formatFlags & flags)
            found = &span;
        if (const FormattedSpan *inner =
                deepestSpanWithFlags(span.children, selStart, selEnd, flags))
            found = inner;
    }
    return found;
}

// First registry row whose content flags are exactly `flags` — the
// canonical type a partially-unformatted span converts to.
const SpanTypeDef *findTypeDefByFlags(quint32 flags)
{
    for (const SpanTypeDef &def : kSpanTypes) {
        if (def.matcher == DelimiterPair && def.formatFlags == flags)
            return &def;
    }
    return nullptr;
}

} // namespace

QString MarkdownFormatter::applySpanType(const QString &text, int selectionStart,
                                         int selectionEnd, const QString &type) const
{
    const SpanTypeDef *def = findTypeDef(type);
    if (!markerIsSymmetric(def))
        return text;
    const QString selected =
        text.mid(selectionStart, selectionEnd - selectionStart);
    QString marker = QLatin1String(def->openMarker);
    if (def->matcher == CodeMatcher) {
        // Size the delimiter to the content: a selection containing backticks
        // needs a longer run than any inside it, or it would close early.
        int longest = 0;
        int run = 0;
        for (const QChar c : selected) {
            run = (c == QLatin1Char('`')) ? run + 1 : 0;
            longest = qMax(longest, run);
        }
        marker = QString(longest + 1, QLatin1Char('`'));
    }
    // A collapsed cursor gets an empty marker pair (format-then-type,
    // features.md §2.2.7).
    return text.left(selectionStart) + marker + selected
           + marker + text.mid(selectionEnd);
}

QString MarkdownFormatter::removeSpanType(const QString &text, int selectionStart,
                                          int selectionEnd, const QString &type) const
{
    const SpanTypeDef *def = findTypeDef(type);
    if (!def)
        return text;
    // Hold the parsed list in a local: deepestSpanWithFlags returns a pointer
    // into it, used below, so the temporary must not be destroyed at the ';'.
    const QList<FormattedSpan> spans = parseSpans(text);
    const FormattedSpan *span = deepestSpanWithFlags(spans,
                                                     selectionStart, selectionEnd,
                                                     def->formatFlags);
    if (!span)
        return text;

    QString inner = text.mid(span->start + span->openLen, span->contentLength());
    const quint32 remaining = span->formatFlags & ~def->formatFlags;
    if (remaining) {
        // Composite span loses one aspect: ***x*** minus bold is *x*.
        if (const SpanTypeDef *wrap = findTypeDefByFlags(remaining)) {
            const QString marker = QLatin1String(wrap->openMarker);
            inner = marker + inner + marker;
        }
    }
    return text.left(span->start) + inner + text.mid(span->end);
}

QString MarkdownFormatter::toggleSpanType(const QString &text, int selectionStart,
                                          int selectionEnd, const QString &type) const
{
    const SpanTypeDef *def = findTypeDef(type);
    if (!def)
        return text;
    if (deepestSpanWithFlags(parseSpans(text), selectionStart, selectionEnd,
                             def->formatFlags))
        return removeSpanType(text, selectionStart, selectionEnd, type);
    return applySpanType(text, selectionStart, selectionEnd, type);
}

QString MarkdownFormatter::applyColor(const QString &text, int selectionStart,
                                      int selectionEnd, const QString &colorValue) const
{
    if (colorValue.isEmpty())
        return text;
    const QString openMarker =
        QStringLiteral("<span style=\"color:") + colorValue + QStringLiteral("\">");
    const QString closeMarker = QStringLiteral("</span>");

    // Re-coloring an existing color span whose content is exactly the
    // selection rewrites its value in place; a sub-selection
    // wraps (and nests, innermost winning). The parsed list is held in a
    // local so the returned pointer into it does not dangle.
    const QList<FormattedSpan> spans = parseSpans(text);
    const FormattedSpan *span = deepestSpanWithFlags(
        spans, selectionStart, selectionEnd, SpanFormat::Color);
    if (span) {
        const int contentStart = span->start + span->openLen;
        const int contentEnd = span->end - span->closeLen;
        // Rewrite in place when the selection covers the whole content —
        // exactly, or with an end mapped past the closing </span> (the
        // asymmetric edge-mapping a revealed span produces, mirroring links).
        // A strict sub-selection wraps instead, nesting a new color.
        if (selectionStart <= contentStart && selectionEnd >= contentEnd)
            return text.left(span->start) + openMarker + text.mid(contentStart);
    }
    return text.left(selectionStart) + openMarker
           + text.mid(selectionStart, selectionEnd - selectionStart)
           + closeMarker + text.mid(selectionEnd);
}

QString MarkdownFormatter::removeColor(const QString &text, int selectionStart,
                                       int selectionEnd) const
{
    const QList<FormattedSpan> spans = parseSpans(text);
    const FormattedSpan *span = deepestSpanWithFlags(
        spans, selectionStart, selectionEnd, SpanFormat::Color);
    if (!span)
        return text;
    const QString inner = text.mid(span->start + span->openLen, span->contentLength());
    return text.left(span->start) + inner + text.mid(span->end);
}

QVariantMap MarkdownFormatter::colorSpanAt(const QString &text, int selectionStart,
                                           int selectionEnd) const
{
    const QList<FormattedSpan> spans = parseSpans(text);
    const FormattedSpan *span = deepestSpanWithFlags(
        spans, selectionStart, selectionEnd, SpanFormat::Color);
    QVariantMap m;
    m.insert(QStringLiteral("found"), span != nullptr);
    if (span) {
        m.insert(QStringLiteral("color"), span->color);
        m.insert(QStringLiteral("start"), span->start);
        m.insert(QStringLiteral("end"), span->end);
        m.insert(QStringLiteral("contentStart"), span->start + span->openLen);
        m.insert(QStringLiteral("contentEnd"), span->end - span->closeLen);
    }
    return m;
}

QString MarkdownFormatter::applyBold(const QString &text, int selectionStart, int selectionEnd) const
{
    return applySpanType(text, selectionStart, selectionEnd, QStringLiteral("bold"));
}

QString MarkdownFormatter::applyItalic(const QString &text, int selectionStart, int selectionEnd) const
{
    return applySpanType(text, selectionStart, selectionEnd, QStringLiteral("italic"));
}

QString MarkdownFormatter::removeBold(const QString &text, int selectionStart, int selectionEnd) const
{
    return removeSpanType(text, selectionStart, selectionEnd, QStringLiteral("bold"));
}

QString MarkdownFormatter::removeItalic(const QString &text, int selectionStart, int selectionEnd) const
{
    return removeSpanType(text, selectionStart, selectionEnd, QStringLiteral("italic"));
}

QString MarkdownFormatter::toggleBold(const QString &text, int selectionStart, int selectionEnd) const
{
    return toggleSpanType(text, selectionStart, selectionEnd, QStringLiteral("bold"));
}

QString MarkdownFormatter::toggleItalic(const QString &text, int selectionStart, int selectionEnd) const
{
    return toggleSpanType(text, selectionStart, selectionEnd, QStringLiteral("italic"));
}
