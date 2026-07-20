// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
#include "wikilinkscanner.h"

namespace WikiLinkScanner {
namespace {

bool isEscapedAt(const QString &text, int pos)
{
    int backslashes = 0;
    for (int i = pos - 1; i >= 0 && text.at(i) == QLatin1Char('\\'); --i)
        ++backslashes;
    return (backslashes & 1) != 0;
}

int runLength(const QString &text, int pos, QChar ch)
{
    int end = pos;
    while (end < text.size() && text.at(end) == ch)
        ++end;
    return end - pos;
}

int lineEnd(const QString &text, int pos)
{
    const int newline = text.indexOf(QLatin1Char('\n'), pos);
    return newline < 0 ? text.size() : newline + 1;
}

// Mirrors MarkdownFormatter's Pandoc-style inline-math adjacency rule.
int inlineMathEnd(const QString &text, int pos)
{
    const int contentStart = pos + 1;
    if (contentStart >= text.size() || text.at(contentStart).isSpace()
        || text.at(contentStart) == QLatin1Char('$'))
        return -1;
    for (int i = contentStart; i < text.size(); ++i) {
        if (text.at(i) == QLatin1Char('\n'))
            return -1;
        if (text.at(i) != QLatin1Char('$') || isEscapedAt(text, i))
            continue;
        const bool spaceBefore = text.at(i - 1).isSpace();
        const bool digitAfter = i + 1 < text.size() && text.at(i + 1).isDigit();
        return !spaceBefore && !digitAfter && i > contentStart ? i + 1 : -1;
    }
    return -1;
}

int displayMathEnd(const QString &text, int pos)
{
    for (int i = pos + 2; i + 1 < text.size(); ++i) {
        if (text.at(i) == QLatin1Char('$')
            && text.at(i + 1) == QLatin1Char('$') && !isEscapedAt(text, i))
            return i + 2;
    }
    return -1;
}

} // namespace

bool matchAt(const QString &text, int pos, Occurrence *occurrence)
{
    if (pos < 0 || pos + 1 >= text.size()
        || text.at(pos) != QLatin1Char('[')
        || text.at(pos + 1) != QLatin1Char('[') || isEscapedAt(text, pos))
        return false;

    const int close = text.indexOf(QLatin1String("]]"), pos + 2);
    if (close < 0)
        return false;
    const QString inner = text.mid(pos + 2, close - pos - 2);
    if (inner.isEmpty() || inner.contains(QLatin1Char('\n'))
        || inner.contains(QLatin1Char('[')) || inner.contains(QLatin1Char(']')))
        return false;

    const int pipe = inner.indexOf(QLatin1Char('|'));
    const QString targetPart = pipe >= 0 ? inner.left(pipe) : inner;
    const QString alias = pipe >= 0 ? inner.mid(pipe + 1) : QString();
    if (pipe >= 0 && (alias.trimmed().isEmpty()
                      || alias.contains(QLatin1Char('|'))))
        return false;

    const int hash = targetPart.indexOf(QLatin1Char('#'));
    const QString note = hash >= 0 ? targetPart.left(hash) : targetPart;
    const QString heading = hash >= 0 ? targetPart.mid(hash + 1) : QString();
    if (heading.contains(QLatin1Char('#'))
        || (hash >= 0 && heading.trimmed().isEmpty())
        || (note.trimmed().isEmpty() && hash < 0))
        return false;

    if (occurrence) {
        Occurrence out;
        out.start = pos;
        out.length = close + 2 - pos;
        out.targetStart = pos + 2;
        out.targetLength = targetPart.size();
        out.rawTarget = targetPart.trimmed();

        int begin = 0;
        int end = note.size();
        while (begin < end && note.at(begin).isSpace())
            ++begin;
        while (end > begin && note.at(end - 1).isSpace())
            --end;
        out.noteStart = pos + 2 + begin;
        out.noteLength = end - begin;
        out.note = note.mid(begin, out.noteLength);

        if (hash >= 0) {
            begin = hash + 1;
            end = targetPart.size();
            while (begin < end && targetPart.at(begin).isSpace())
                ++begin;
            while (end > begin && targetPart.at(end - 1).isSpace())
                --end;
            out.headingStart = pos + 2 + begin;
            out.headingLength = end - begin;
            out.heading = targetPart.mid(begin, out.headingLength);
        }
        if (pipe >= 0) {
            begin = pipe + 1;
            end = inner.size();
            while (begin < end && inner.at(begin).isSpace())
                ++begin;
            while (end > begin && inner.at(end - 1).isSpace())
                --end;
            out.aliasStart = pos + 2 + begin;
            out.aliasLength = end - begin;
            out.alias = inner.mid(begin, out.aliasLength);
        }
        *occurrence = out;
    }
    return true;
}

QList<Occurrence> scan(const QString &text)
{
    QList<Occurrence> occurrences;
    int pos = 0;
    int fenceLength = 0;
    QChar fenceChar;

    while (pos < text.size()) {
        const bool atLineStart = pos == 0 || text.at(pos - 1) == QLatin1Char('\n');
        if (atLineStart) {
            int marker = pos;
            while (marker < text.size() && text.at(marker) != QLatin1Char('\n')
                   && text.at(marker).isSpace())
                ++marker;
            const QChar ch = marker < text.size() ? text.at(marker) : QChar();
            const int run = ch == QLatin1Char('`') || ch == QLatin1Char('~')
                ? runLength(text, marker, ch) : 0;
            if (fenceLength > 0) {
                if (ch == fenceChar && run >= fenceLength) {
                    int rest = marker + run;
                    while (rest < text.size() && text.at(rest) != QLatin1Char('\n')
                           && text.at(rest).isSpace())
                        ++rest;
                    if (rest == text.size() || text.at(rest) == QLatin1Char('\n')) {
                        fenceLength = 0;
                        fenceChar = QChar();
                    }
                }
                pos = lineEnd(text, pos);
                continue;
            }
            if (run >= 3) {
                fenceLength = run;
                fenceChar = ch;
                pos = lineEnd(text, pos);
                continue;
            }
        }

        if (text.at(pos) == QLatin1Char('`') && !isEscapedAt(text, pos)) {
            const int ticks = runLength(text, pos, QLatin1Char('`'));
            int close = -1;
            int candidate = pos + ticks;
            while (candidate < text.size()) {
                candidate = text.indexOf(QLatin1Char('`'), candidate);
                if (candidate < 0)
                    break;
                const int run = runLength(text, candidate, QLatin1Char('`'));
                if (run == ticks) {
                    close = candidate;
                    break;
                }
                candidate += run;
            }
            if (close >= 0) {
                pos = close + ticks;
                continue;
            }
            pos += ticks;
            continue;
        }

        if (text.at(pos) == QLatin1Char('$') && !isEscapedAt(text, pos)) {
            const int end = pos + 1 < text.size()
                    && text.at(pos + 1) == QLatin1Char('$')
                ? displayMathEnd(text, pos) : inlineMathEnd(text, pos);
            if (end >= 0) {
                pos = end;
                continue;
            }
        }

        Occurrence occurrence;
        if (matchAt(text, pos, &occurrence)) {
            occurrences.append(occurrence);
            pos += occurrence.length;
            continue;
        }
        ++pos;
    }
    return occurrences;
}

} // namespace WikiLinkScanner
