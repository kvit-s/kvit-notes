// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
#include "documentcompare.h"

#include <QList>
#include <QVariantMap>

#include <algorithm>

#include "block.h"
#include "documentserializer.h"
#include "inlinemarkdown.h"

namespace {

// The text a reader sees in a block, in the coordinates a mark is addressed
// in.
//
// This has to agree with what the drawing end lays out, which is
// BlockEditorEngine with no cursor in the block: a verbatim block's content IS
// its text, and every other block goes through the inline-span stripper. The
// three kinds that hold no running text at all answer with nothing, since a
// character range cannot address a rule or a picture.
QString displayTextOf(const Block::State &state)
{
    switch (state.type) {
    case Block::Divider:
    case Block::Image:
    case Block::Media:
        return QString();
    case Block::CodeBlock:
    case Block::MathBlock:
    case Block::Table:
        return state.content;
    default:
        return InlineMarkdown::displayText(state.content);
    }
}

// Everything that makes two blocks the same block for the purpose of the
// alignment below. Field for field rather than "the content is equal": a
// paragraph promoted to a heading, a list item indented one level, or a todo
// that has since been ticked are all changes a reader wants to see, and all of
// them leave the content string alone.
QString keyOf(const Block::State &state)
{
    const QChar unit(QLatin1Char('\x1f'));
    return QString::number(static_cast<int>(state.type)) + unit
        + QString::number(state.indentLevel) + unit
        + (state.checked ? QLatin1Char('1') : QLatin1Char('0')) + unit
        + state.language + unit + state.calloutTitle + unit + state.content;
}

// The alignment: which blocks of `a` have a counterpart in `b`.
//
// A longest common subsequence of the two key sequences, so a block inserted
// into either document shifts nothing after it — which a positional
// comparison cannot do, and which is the difference between "one paragraph
// changed" and "everything from here down changed".
//
// The common prefix and the common suffix are taken off first. That is what
// keeps the table small: an edit inside one paragraph of a long note leaves
// one block in the middle for the table to cover, whatever the note's length.
//
// `budget` caps the table for the case the trimming does not reduce — two
// documents that share nothing, which is what a comparison against a
// different note looks like. Over the cap, every untrimmed block of `a`
// counts as unmatched, which is the truthful answer for two documents with no
// common structure and the one the caller would reach anyway.
struct Alignment
{
    // For each block of `a`, the index in `b` it corresponds to, or -1.
    QList<int> partner;
};

Alignment align(const QList<QString> &a, const QList<QString> &b)
{
    constexpr qint64 kCellBudget = 4'000'000;

    Alignment result;
    result.partner = QList<int>(a.size(), -1);

    int head = 0;
    while (head < a.size() && head < b.size() && a.at(head) == b.at(head)) {
        result.partner[head] = head;
        ++head;
    }
    int tail = 0;
    while (tail < a.size() - head && tail < b.size() - head
           && a.at(a.size() - 1 - tail) == b.at(b.size() - 1 - tail)) {
        result.partner[a.size() - 1 - tail] = b.size() - 1 - tail;
        ++tail;
    }

    const int n = a.size() - head - tail;
    const int m = b.size() - head - tail;
    if (n <= 0 || m <= 0)
        return result;
    if (static_cast<qint64>(n) * static_cast<qint64>(m) > kCellBudget)
        return result;

    // Classic LCS table over the middle, then a walk back through it.
    QList<int> table(static_cast<qsizetype>(n + 1) * (m + 1), 0);
    const auto at = [m](int i, int j) {
        return static_cast<qsizetype>(i) * (m + 1) + j;
    };
    for (int i = n - 1; i >= 0; --i) {
        for (int j = m - 1; j >= 0; --j) {
            table[at(i, j)] = a.at(head + i) == b.at(head + j)
                ? table.at(at(i + 1, j + 1)) + 1
                : std::max(table.at(at(i + 1, j)), table.at(at(i, j + 1)));
        }
    }
    int i = 0;
    int j = 0;
    while (i < n && j < m) {
        if (a.at(head + i) == b.at(head + j)) {
            result.partner[head + i] = head + j;
            ++i;
            ++j;
        } else if (table.at(at(i + 1, j)) >= table.at(at(i, j + 1))) {
            ++i;
        } else {
            ++j;
        }
    }
    return result;
}

QVariantMap range(int block, int start, int length)
{
    return QVariantMap{{QStringLiteral("block"), block},
                       {QStringLiteral("start"), start},
                       {QStringLiteral("length"), length}};
}

} // namespace

DocumentCompare::DocumentCompare(QObject *parent)
    : QObject(parent)
{
}

QVariantList DocumentCompare::changedRanges(const QString &markdown,
                                            const QString &baseline)
{
    DocumentSerializer serializer;
    const QList<Block::State> mine = serializer.parse(markdown);
    const QList<Block::State> theirs = serializer.parse(baseline);

    QList<QString> myKeys;
    myKeys.reserve(mine.size());
    for (const Block::State &state : mine)
        myKeys.append(keyOf(state));
    QList<QString> theirKeys;
    theirKeys.reserve(theirs.size());
    for (const Block::State &state : theirs)
        theirKeys.append(keyOf(state));

    const Alignment alignment = align(myKeys, theirKeys);

    // The unmatched blocks of each side, in order. An unmatched block of mine
    // is either an edit of an unmatched block of theirs or a block they do not
    // have at all, and the two sequences are paired off in order to decide
    // which: pairing by position within the unmatched runs is what turns "this
    // paragraph was reworded" into a mark on the words rather than on the
    // paragraph.
    QList<int> unmatchedTheirs;
    QList<bool> takenTheirs(theirs.size(), false);
    for (int i = 0; i < alignment.partner.size(); ++i) {
        const int partner = alignment.partner.at(i);
        if (partner >= 0 && partner < takenTheirs.size())
            takenTheirs[partner] = true;
    }
    for (int j = 0; j < theirs.size(); ++j) {
        if (!takenTheirs.at(j))
            unmatchedTheirs.append(j);
    }

    QVariantList out;
    int nextTheirs = 0;
    for (int i = 0; i < mine.size(); ++i) {
        if (alignment.partner.at(i) >= 0)
            continue;
        const QString text = displayTextOf(mine.at(i));
        const int counterpart = nextTheirs < unmatchedTheirs.size()
            ? unmatchedTheirs.at(nextTheirs++)
            : -1;
        if (text.isEmpty())
            continue;
        if (counterpart < 0) {
            // Nothing on the other side to compare against: the whole block
            // is new.
            out.append(range(i, 0, text.length()));
            continue;
        }

        const QString other = displayTextOf(theirs.at(counterpart));
        if (text == other) {
            // Same words, different block: the kind, the indent, the tick on a
            // to-do or a fence's language changed. There is no run of
            // characters to point at, so the whole block is the answer — the
            // only way a character range can say "this line is not the line
            // you have".
            out.append(range(i, 0, text.length()));
            continue;
        }
        int prefix = 0;
        const int shortest = std::min(text.length(), other.length());
        while (prefix < shortest && text.at(prefix) == other.at(prefix))
            ++prefix;
        int suffix = 0;
        while (suffix < shortest - prefix
               && text.at(text.length() - 1 - suffix)
                      == other.at(other.length() - 1 - suffix)) {
            ++suffix;
        }
        const int length = text.length() - prefix - suffix;
        if (length > 0)
            out.append(range(i, prefix, length));
    }
    return out;
}
