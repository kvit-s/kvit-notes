// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
#include "blocktext.h"
#include "inlinemarkdown.h"

namespace {

// Every character that can open an inline span. Missing one here would make
// that span's markers show through as literal text everywhere the reader
// looks, so the list is the same one the span parser recognises.
bool hasDisplayMarkers(const QString &text)
{
    for (const QChar ch : text) {
        switch (ch.unicode()) {
        case '*':
        case '_':
        case '~':
        case '=':
        case '+':
        case '^':
        case '`':
        case '$':
        case '<':
        case '[':
            return true;
        default:
            break;
        }
    }
    return false;
}

} // namespace

namespace BlockText {

QString rendered(const QString &markdown)
{
    return hasDisplayMarkers(markdown) ? InlineMarkdown::displayText(markdown)
                                       : markdown;
}

QString renderedFully(const QString &markdown)
{
    return InlineMarkdown::displayText(markdown);
}

QString indent(const QString &text, int spaces)
{
    const QString pad(spaces, QLatin1Char(' '));
    QStringList out;
    const QStringList lines = text.split(QLatin1Char('\n'));
    out.reserve(lines.size());
    for (const QString &line : lines)
        out << (line.isEmpty() ? QString() : pad + line);
    return out.join(QLatin1Char('\n'));
}

QString alignedTable(const QStringList &headers, const QList<QStringList> &rows)
{
    int columns = headers.size();
    for (const QStringList &row : rows)
        columns = qMax(columns, row.size());
    if (columns == 0)
        return QString();

    const auto cellAt = [](const QStringList &row, int c) {
        return c < row.size() ? row.at(c).simplified() : QString();
    };

    QList<int> widths;
    widths.reserve(columns);
    for (int c = 0; c < columns; ++c) {
        int width = cellAt(headers, c).size();
        for (const QStringList &row : rows)
            width = qMax(width, cellAt(row, c).size());
        widths.append(width);
    }

    const auto renderRow = [&](const QStringList &row) {
        QStringList cells;
        cells.reserve(columns);
        for (int c = 0; c < columns; ++c)
            cells << cellAt(row, c).leftJustified(widths.at(c));
        // Padding on the last column is invisible, and would reach the file
        // as trailing whitespace.
        QString line = cells.join(QStringLiteral(" | "));
        while (line.endsWith(QLatin1Char(' ')))
            line.chop(1);
        return line;
    };

    QStringList out;
    if (!headers.isEmpty()) {
        out << renderRow(headers);
        QStringList rule;
        rule.reserve(columns);
        for (int c = 0; c < columns; ++c)
            rule << QString(widths.at(c), QLatin1Char('-'));
        out << rule.join(QStringLiteral("-+-"));
    }
    for (const QStringList &row : rows)
        out << renderRow(row);
    return out.join(QLatin1Char('\n'));
}

} // namespace BlockText
