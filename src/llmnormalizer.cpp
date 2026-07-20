// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
#include "llmnormalizer.h"

#include <QStringList>

// The normalizer runs at the top of DocumentSerializer::parse, so both
// routes LLM text takes into a document — file open and paste — pass
// through it. It is three linear, fence-aware passes over the lines:
//
//   A. table-embedded code fences collapse to an inline code span (fix 1);
//   B. \[ ... \] display math becomes a $$ fence (fix 3, multi-line part);
//   C. per-line rewrites — \(...\) to $...$, Unicode spaces inside math,
//      HTML entities outside verbatim regions (fixes 3, 4, 7).
//
// The reference implementation is the chat frontend's preprocessing
// (~/chat/frontend/src/components/ReplyDisplay.vue, preprocessTableCodeBlocks
// and preprocessLatex), adapted to a block model that stores markdown.

namespace {

// Multi-line constructs give up after this many lines of lookahead, keeping
// the scan linear on pathological input (plan, "Guard" section). The bound
// is a normalizer invariant regardless of file size.
constexpr int kLookaheadLines = 200;

// ---- fence recognition (mirrors documentserializer.cpp) ----

struct Fence {
    QChar ch = QLatin1Char('`');
    int len = 0;
};

// A fence opener on a trimmed line (indented fences are fences too, fix 2):
// three or more backticks or tildes (fix 8), optionally an info string. A
// backtick info string may not contain backticks; a tilde one may
// (CommonMark).
bool fenceOpen(const QString &line, Fence *fence)
{
    const QString t = line.trimmed();
    if (t.isEmpty())
        return false;
    const QChar c = t.at(0);
    if (c != QLatin1Char('`') && c != QLatin1Char('~'))
        return false;
    int n = 0;
    while (n < t.size() && t.at(n) == c)
        ++n;
    if (n < 3)
        return false;
    if (c == QLatin1Char('`') && t.mid(n).contains(QLatin1Char('`')))
        return false;
    if (fence) {
        fence->ch = c;
        fence->len = n;
    }
    return true;
}

// The closing fence: same character, at least as long, no info string.
bool fenceClose(const QString &line, const Fence &fence)
{
    const QString t = line.trimmed();
    if (t.size() < fence.len)
        return false;
    for (const QChar &c : t) {
        if (c != fence.ch)
            return false;
    }
    return true;
}

bool isSingleLineMathBlock(const QString &trimmed)
{
    return trimmed.size() > 4 && trimmed.startsWith(QLatin1String("$$"))
        && trimmed.endsWith(QLatin1String("$$"));
}

QString stripTrailingWhitespace(QString s)
{
    int e = s.size();
    while (e > 0 && s.at(e - 1).isSpace())
        --e;
    s.truncate(e);
    return s;
}

// ---- fix 4: Unicode spaces in math content ----

bool isMathUnicodeSpace(QChar c)
{
    return c == QChar(0x202F)     // narrow no-break space
        || c == QChar(0x00A0)     // no-break space
        || c == QChar(0x2009)     // thin space
        || c == QChar(0x200A);    // hair space
}

QString normalizeMathSpaces(QString s)
{
    for (int i = 0; i < s.size(); ++i) {
        if (isMathUnicodeSpace(s.at(i)))
            s[i] = QLatin1Char(' ');
    }
    return s;
}

// ---- inline code spans within one line ----

// Backtick code spans, CommonMark-style: a run of N backticks closes at the
// next run of exactly N on the same line. Rewrites never reach inside these
// ranges (markers included).
QList<QPair<int, int>> inlineCodeRanges(const QString &line)
{
    QList<QPair<int, int>> ranges;
    int i = 0;
    while (i < line.size()) {
        if (line.at(i) != QLatin1Char('`')) {
            ++i;
            continue;
        }
        const int open = i;
        int n = 0;
        while (i < line.size() && line.at(i) == QLatin1Char('`')) {
            ++n;
            ++i;
        }
        int j = i;
        int closeStart = -1;
        while (j < line.size()) {
            if (line.at(j) != QLatin1Char('`')) {
                ++j;
                continue;
            }
            const int runStart = j;
            int m = 0;
            while (j < line.size() && line.at(j) == QLatin1Char('`')) {
                ++m;
                ++j;
            }
            if (m == n) {
                closeStart = runStart;
                break;
            }
        }
        if (closeStart >= 0) {
            ranges.append({open, closeStart + n});
            i = closeStart + n;
        }
        // An unclosed run is literal backticks; the scan continues after it.
    }
    return ranges;
}

// ---- fix 7: HTML entities ----

// One recognized entity starting at s[pos] (an '&'): the named set and
// numeric &#NNN; / &#xHHH; forms. Returns the source length and fills
// *decoded, or 0 for anything else (which stays literal).
int matchEntity(const QString &s, int pos, QString *decoded)
{
    if (pos >= s.size() || s.at(pos) != QLatin1Char('&'))
        return 0;
    int semi = -1;
    const int limit = qMin(s.size(), pos + 11);  // longest form: &#x10FFFF;
    for (int i = pos + 1; i < limit; ++i) {
        if (s.at(i) == QLatin1Char(';')) {
            semi = i;
            break;
        }
    }
    if (semi < 0)
        return 0;
    const QString body = s.mid(pos + 1, semi - pos - 1);
    if (body.isEmpty())
        return 0;
    QString out;
    if (body == QLatin1String("amp"))
        out = QStringLiteral("&");
    else if (body == QLatin1String("lt"))
        out = QStringLiteral("<");
    else if (body == QLatin1String("gt"))
        out = QStringLiteral(">");
    else if (body == QLatin1String("quot"))
        out = QStringLiteral("\"");
    else if (body == QLatin1String("apos"))
        out = QStringLiteral("'");
    else if (body == QLatin1String("nbsp"))
        out = QStringLiteral(" ");
    else if (body.startsWith(QLatin1Char('#'))) {
        bool ok = false;
        uint code = 0;
        if (body.size() > 2
            && (body.at(1) == QLatin1Char('x') || body.at(1) == QLatin1Char('X')))
            code = body.mid(2).toUInt(&ok, 16);
        else if (body.size() > 1)
            code = body.mid(1).toUInt(&ok, 10);
        // Zero, surrogate halves, and out-of-range references stay literal.
        if (!ok || code == 0 || code > 0x10FFFF
            || (code >= 0xD800 && code <= 0xDFFF))
            return 0;
        if (QChar::requiresSurrogates(code)) {
            out += QChar(QChar::highSurrogate(code));
            out += QChar(QChar::lowSurrogate(code));
        } else {
            out += QChar(code);
        }
    } else {
        return 0;
    }
    *decoded = out;
    return semi - pos + 1;
}

// True when the text at pos reads as the tail of a recognized entity — the
// part after an '&' ("lt;", "amp;", "#38;", ...).
bool entityTailAt(const QString &s, int pos)
{
    const QString probe = QStringLiteral("&") + QStringView(s).mid(pos, 10);
    QString dummy;
    return matchEntity(probe, 0, &dummy) > 0;
}

// Decode entities once, in a single left-to-right pass over already-decoded
// output, so "&amp;amp;" yields "&amp;" rather than "&". One guard beyond
// the plan text: an entity decoding to '&' stays literal when a decodable
// tail follows ("&amp;lt;" is left as-is), because decoding it would emit
// "&lt;" — an entity again — and the next load would decode that too,
// dropping one escape level per load/save cycle. Idempotence is the
// normalizer's load-bearing property; this is the only rule that preserves
// it for multi-escaped input.
QString decodeEntities(const QString &s)
{
    if (!s.contains(QLatin1Char('&')))
        return s;
    QString out;
    out.reserve(s.size());
    int i = 0;
    while (i < s.size()) {
        if (s.at(i) == QLatin1Char('&')) {
            QString decoded;
            const int len = matchEntity(s, i, &decoded);
            if (len > 0) {
                if (decoded == QLatin1String("&") && entityTailAt(s, i + len))
                    out += QStringView(s).mid(i, len);  // keep the escape level
                else
                    out += decoded;
                i += len;
                continue;
            }
        }
        out += s.at(i);
        ++i;
    }
    return out;
}

// ---- fix 3, inline part: \( ... \) to $...$ ----

// Port of ReplyDisplay.vue:735 — single line, non-greedy to the first \).
// The content is trimmed (the inline-math matcher requires a non-space
// immediately inside both dollars) and space-normalized (fix 4). An
// unmatched \( or an empty pair stays literal.
QString convertInlineMath(const QString &s)
{
    if (!s.contains(QLatin1String("\\(")))
        return s;
    QString out;
    out.reserve(s.size());
    int i = 0;
    while (i < s.size()) {
        if (s.at(i) == QLatin1Char('\\') && i + 1 < s.size()
            && s.at(i + 1) == QLatin1Char('(')) {
            const int close = s.indexOf(QLatin1String("\\)"), i + 2);
            if (close >= 0) {
                const QString content =
                    normalizeMathSpaces(s.mid(i + 2, close - i - 2)).trimmed();
                if (!content.isEmpty()) {
                    out += QLatin1Char('$');
                    out += content;
                    out += QLatin1Char('$');
                    i = close + 2;
                    continue;
                }
            }
        }
        out += s.at(i);
        ++i;
    }
    return out;
}

// ---- fix 4, prose part: Unicode spaces inside $...$ spans ----

// Mirrors markdownformatter.cpp's MathMatcher (Pandoc adjacency): only a
// span the parser would recognize is touched, and only its interior — the
// boundary characters were checked non-space, so replacements cannot change
// whether the span parses.
QString normalizeInlineMathSpans(const QString &s)
{
    int pos = s.indexOf(QLatin1Char('$'));
    if (pos < 0)
        return s;
    QString out = s;
    while (pos < out.size()) {
        if (out.at(pos) != QLatin1Char('$')) {
            ++pos;
            continue;
        }
        if (pos > 0 && out.at(pos - 1) == QLatin1Char('\\')) {
            ++pos;  // an escaped \$ is literal
            continue;
        }
        const int contentStart = pos + 1;
        if (contentStart >= out.size())
            break;
        const QChar afterOpen = out.at(contentStart);
        if (afterOpen.isSpace() || afterOpen == QLatin1Char('$')) {
            pos = afterOpen == QLatin1Char('$') ? pos + 2 : pos + 1;
            continue;
        }
        int close = -1;
        for (int i = contentStart; i < out.size(); ++i) {
            if (out.at(i) != QLatin1Char('$'))
                continue;
            const bool spaceBefore = out.at(i - 1).isSpace();
            const bool digitAfter =
                i + 1 < out.size() && out.at(i + 1).isDigit();
            if (!spaceBefore && !digitAfter && i > contentStart)
                close = i;
            break;  // first $ decides: match or literal
        }
        if (close < 0) {
            pos = contentStart;
            continue;
        }
        for (int i = contentStart; i < close; ++i) {
            if (isMathUnicodeSpace(out.at(i)))
                out[i] = QLatin1Char(' ');
        }
        pos = close + 1;
    }
    return out;
}

// The single-line rewrites, applied to the parts of a prose line outside
// inline code spans (verbatim-region discipline).
QString transformProse(const QString &line)
{
    if (line.isEmpty())
        return line;
    const auto ranges = inlineCodeRanges(line);
    QString out;
    out.reserve(line.size());
    int pos = 0;
    const auto emitPlain = [&out](const QString &seg) {
        out += decodeEntities(
            normalizeInlineMathSpans(convertInlineMath(seg)));
    };
    for (const auto &r : ranges) {
        if (r.first > pos)
            emitPlain(line.mid(pos, r.first - pos));
        out += QStringView(line).mid(r.first, r.second - r.first);
        pos = r.second;
    }
    if (pos < line.size())
        emitPlain(line.mid(pos));
    return out;
}

// ---- pass A: fix 1, code fence inside a table cell ----

// A fence opening directly under a table row, closed by a "``` | cells |"
// line, swallows the rest of the document today (the closing line never
// closes the fence). The repair inlines the code into the row as a code
// span: table integrity wins over code line breaks (decision of record).
// Port of ReplyDisplay.vue:608. The trigger requires the row on the
// IMMEDIATELY preceding line — the chat reference's rule — not merely the
// previous non-blank line: with a blank line between, a canonical
// table-then-code-block document would false-trigger whenever the code
// contains a "``` extra text" line, breaking the fixed-point guarantee.
QStringList tableFencePass(const QStringList &lines)
{
    QStringList out;
    out.reserve(lines.size());
    bool inCode = false;
    Fence fence;
    int i = 0;
    while (i < lines.size()) {
        const QString &line = lines.at(i);
        if (inCode) {
            out.append(line);
            if (fenceClose(line, fence))
                inCode = false;
            ++i;
            continue;
        }
        Fence f;
        if (!fenceOpen(line, &f)) {
            out.append(line);
            ++i;
            continue;
        }
        const bool tableContext = f.ch == QLatin1Char('`') && !out.isEmpty()
            && out.last().contains(QLatin1Char('|'));
        if (tableContext) {
            // The first subsequent line containing ``` decides: trailing
            // cells mean the broken-table shape; a bare fence is an
            // ordinary code block after the table.
            int closeAt = -1;
            for (int j = i + 1;
                 j < lines.size() && j - i <= kLookaheadLines; ++j) {
                if (lines.at(j).contains(QLatin1String("```"))) {
                    closeAt = j;
                    break;
                }
            }
            if (closeAt >= 0) {
                const QString &closeLine = lines.at(closeAt);
                const int tick = closeLine.indexOf(QLatin1String("```"));
                int afterPos = tick;
                while (afterPos < closeLine.size()
                       && closeLine.at(afterPos) == QLatin1Char('`'))
                    ++afterPos;
                const QString before = closeLine.left(tick).trimmed();
                const QString after = closeLine.mid(afterPos).trimmed();
                if (!after.isEmpty()) {
                    QStringList codeParts;
                    for (int q = i + 1; q < closeAt; ++q) {
                        const QString piece = lines.at(q).trimmed();
                        if (!piece.isEmpty())
                            codeParts.append(piece);
                    }
                    if (!before.isEmpty())
                        codeParts.append(before);
                    QString code = codeParts.join(QLatin1Char(' '));
                    // Pipes in the code would split the cell; \| survives
                    // TableData's unescape-escape round-trip.
                    code.replace(QLatin1String("|"), QLatin1String("\\|"));
                    const QString span = code.isEmpty() ? QString()
                        : code.contains(QLatin1Char('`'))
                            ? code
                            : QLatin1Char('`') + code + QLatin1Char('`');
                    QString row = stripTrailingWhitespace(out.last());
                    if (row.endsWith(QLatin1Char('|'))) {
                        row.chop(1);
                        row = stripTrailingWhitespace(row);
                    }
                    row += QLatin1String(" | ") + span
                         + QLatin1Char(' ') + after;
                    out.last() = row;
                    i = closeAt + 1;
                    continue;
                }
            }
        }
        out.append(line);
        inCode = true;
        fence = f;
        ++i;
    }
    return out;
}

// ---- pass B: fix 3, display part: \[ ... \] to a $$ fence ----

// Port of ReplyDisplay.vue:741, tempered the same way (the first \] closes;
// an inner one cannot be jumped). Unlike the chat app the content is not
// flattened — the multi-line form IS Kvit's MathBlock. Text before the \[
// and after the \] stays, on its own lines; the suffix re-enters the scan
// (it may hold another construct). An unmatched \[ stays literal, as does
// one whose closer would sit past the lookahead bound or inside a code
// fence (verbatim-region discipline).
QStringList displayMathPass(const QStringList &lines)
{
    QStringList out;
    out.reserve(lines.size());
    bool inCode = false;
    bool inMath = false;
    Fence fence;
    int i = 0;
    QString carry;
    bool hasCarry = false;
    while (hasCarry || i < lines.size()) {
        QString line;
        if (hasCarry) {
            line = carry;
            hasCarry = false;
        } else {
            line = lines.at(i++);
        }
        if (inCode) {
            out.append(line);
            if (fenceClose(line, fence))
                inCode = false;
            continue;
        }
        if (inMath) {
            out.append(line);
            if (line.trimmed() == QLatin1String("$$"))
                inMath = false;
            continue;
        }
        if (fenceOpen(line, &fence)) {
            out.append(line);
            inCode = true;
            continue;
        }
        const QString t = line.trimmed();
        if (t == QLatin1String("$$")) {
            out.append(line);
            inMath = true;
            continue;
        }
        if (isSingleLineMathBlock(t)) {
            out.append(line);
            continue;
        }
        // The opener must sit outside inline code spans (a doc ABOUT LaTeX
        // quotes "\[" in backticks); the chat regex has no such guard.
        int k = -1;
        {
            const auto codeRanges = inlineCodeRanges(line);
            int from = 0;
            while ((k = line.indexOf(QLatin1String("\\["), from)) >= 0) {
                bool inSpan = false;
                for (const auto &r : codeRanges) {
                    if (k >= r.first && k < r.second) {
                        inSpan = true;
                        from = r.second;
                        break;
                    }
                }
                if (!inSpan)
                    break;
            }
        }
        if (k < 0) {
            out.append(line);
            continue;
        }
        QString content;
        QString suffix;
        bool found = false;
        const int sameLine = line.indexOf(QLatin1String("\\]"), k + 2);
        if (sameLine >= 0) {
            // Inside a table row, splitting the line into a $$ fence would
            // destroy the row; an inline $...$ span keeps both the math and
            // the table (fix 1's rule: table integrity wins).
            if (line.contains(QLatin1Char('|'))) {
                const QString inlined = normalizeMathSpaces(
                    line.mid(k + 2, sameLine - k - 2)).trimmed();
                // Re-enter the scan: the rest of the row may hold another
                // pair; each pass shrinks the line, so this terminates.
                carry = line.left(k)
                    + (inlined.isEmpty() ? QString()
                                         : QLatin1Char('$') + inlined
                                           + QLatin1Char('$'))
                    + line.mid(sameLine + 2);
                hasCarry = true;
                continue;
            }
            content = line.mid(k + 2, sameLine - k - 2);
            suffix = line.mid(sameLine + 2);
            found = true;
        } else {
            int closeLine = -1;
            int closePos = -1;
            for (int j = i; j < lines.size() && j - i < kLookaheadLines; ++j) {
                if (fenceOpen(lines.at(j), nullptr))
                    break;  // never reach into fenced code
                const int p = lines.at(j).indexOf(QLatin1String("\\]"));
                if (p >= 0) {
                    closeLine = j;
                    closePos = p;
                    break;
                }
            }
            if (closeLine >= 0) {
                QStringList parts;
                parts.append(line.mid(k + 2));
                for (int q = i; q < closeLine; ++q)
                    parts.append(lines.at(q));
                parts.append(lines.at(closeLine).left(closePos));
                content = parts.join(QLatin1Char('\n'));
                suffix = lines.at(closeLine).mid(closePos + 2);
                i = closeLine + 1;
                found = true;
            }
        }
        if (!found) {
            out.append(line);
            continue;
        }
        const QString pre = stripTrailingWhitespace(line.left(k));
        if (!pre.trimmed().isEmpty())
            out.append(pre);
        out.append(QStringLiteral("$$"));
        const QString trimmedContent = normalizeMathSpaces(content).trimmed();
        if (!trimmedContent.isEmpty())
            out.append(trimmedContent.split(QLatin1Char('\n')));
        out.append(QStringLiteral("$$"));
        const QString rest = suffix.trimmed();
        if (!rest.isEmpty()) {
            carry = rest;
            hasCarry = true;
        }
    }
    return out;
}

// ---- pass C: the per-line rewrites ----

QStringList lineRewritePass(const QStringList &lines)
{
    QStringList out;
    out.reserve(lines.size());
    bool inCode = false;
    bool inMath = false;
    Fence fence;
    for (const QString &line : lines) {
        if (inCode) {
            out.append(line);
            if (fenceClose(line, fence))
                inCode = false;
            continue;
        }
        if (inMath) {
            // The whole line is math content (fix 4); the closer passes
            // through untouched.
            if (line.trimmed() == QLatin1String("$$")) {
                out.append(line);
                inMath = false;
            } else {
                out.append(normalizeMathSpaces(line));
            }
            continue;
        }
        if (fenceOpen(line, &fence)) {
            out.append(line);
            inCode = true;
            continue;
        }
        const QString t = line.trimmed();
        if (t == QLatin1String("$$")) {
            out.append(line);
            inMath = true;
            continue;
        }
        if (isSingleLineMathBlock(t)) {
            out.append(QStringLiteral("$$")
                       + normalizeMathSpaces(t.mid(2, t.size() - 4))
                       + QStringLiteral("$$"));
            continue;
        }
        out.append(transformProse(line));
    }
    return out;
}

} // namespace

QString LlmNormalizer::normalize(const QString &markdown)
{
    if (markdown.isEmpty())
        return markdown;
    QStringList lines = markdown.split(QLatin1Char('\n'));
    lines = tableFencePass(lines);
    lines = displayMathPass(lines);
    lines = lineRewritePass(lines);
    return lines.join(QLatin1Char('\n'));
}
