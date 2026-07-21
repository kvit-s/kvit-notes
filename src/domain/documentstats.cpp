// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
#include "documentstats.h"
#include "inlinemarkdown.h"
#include "blockmodel.h"

#include <QRegularExpression>
#include <cmath>

namespace {
const QRegularExpression &whitespace()
{
    static const QRegularExpression re(QStringLiteral("\\s+"));
    return re;
}
} // namespace

DocumentStats::DocumentStats(QObject *parent)
    : QObject(parent)
{
}

void DocumentStats::setModel(BlockModel *model)
{
    m_model = model;
}

int DocumentStats::wordCount(const QString &displayText)
{
    return displayText.split(whitespace(), Qt::SkipEmptyParts).size();
}

int DocumentStats::charCount(const QString &displayText, bool withSpaces)
{
    // Code points, not UTF-16 code units: skipping low surrogates makes
    // every single-code-point emoji count as 1. Zero-width joiners and
    // variation selectors are invisible glue, also skipped (👨‍👩‍👧 counts
    // as 3, not 1 — the ZWJ family overcount is accepted: a stats readout
    // does not justify grapheme iteration on every recount). Mirrors
    // Block's counters.
    int count = 0;
    for (const QChar c : displayText) {
        const ushort u = c.unicode();
        if (c.isLowSurrogate() || u == 0x200D
            || (u >= 0xFE00 && u <= 0xFE0F))
            continue;
        if (!withSpaces && c.isSpace())
            continue;
        ++count;
    }
    return count;
}

int DocumentStats::readingMinutes(int words)
{
    if (words <= 0)
        return 0;
    // 200 words per minute, rounded, but never rounding a real read to zero.
    return std::max(1, int(std::lround(words / 200.0)));
}

QVariantMap DocumentStats::documentStats() const
{
    int words = 0;
    int charsWith = 0;
    int charsNo = 0;
    int paragraphs = 0;
    int blocks = 0;

    if (m_model) {
        blocks = m_model->count();
        words = m_model->documentWordCount();
        charsWith = m_model->documentCharCount();
        charsNo = m_model->documentCharsNoSpaces();
        paragraphs = m_model->documentParagraphCount();
    }

    return QVariantMap{
        {QStringLiteral("words"), words},
        {QStringLiteral("charsWithSpaces"), charsWith},
        {QStringLiteral("charsNoSpaces"), charsNo},
        {QStringLiteral("paragraphs"), paragraphs},
        {QStringLiteral("blocks"), blocks},
        {QStringLiteral("readingMinutes"), readingMinutes(words)},
    };
}

QString DocumentStats::displayTextFor(const QString &markdown, bool verbatim) const
{
    return verbatim ? markdown : InlineMarkdown::displayText(markdown);
}

QVariantMap DocumentStats::statsForText(const QString &displayText) const
{
    const int words = wordCount(displayText);
    int paragraphs = 0;
    const QStringList lines = displayText.split(QLatin1Char('\n'));
    for (const QString &line : lines)
        if (!line.trimmed().isEmpty())
            ++paragraphs;
    return QVariantMap{
        {QStringLiteral("words"), words},
        {QStringLiteral("charsWithSpaces"), charCount(displayText, true)},
        {QStringLiteral("charsNoSpaces"), charCount(displayText, false)},
        {QStringLiteral("paragraphs"), paragraphs},
        {QStringLiteral("blocks"), paragraphs},
        {QStringLiteral("readingMinutes"), readingMinutes(words)},
    };
}
