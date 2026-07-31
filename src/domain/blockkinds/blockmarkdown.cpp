// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
#include "blockmarkdown.h"

#include <QStringList>
#include <QtGlobal>

namespace BlockMarkdown {

QString fenceFor(const QString &content)
{
    int longest = 2;
    const QStringList lines = content.split(QLatin1Char('\n'));
    for (const QString &line : lines) {
        const QString trimmed = line.trimmed();
        if (trimmed.isEmpty())
            continue;
        bool allTicks = true;
        for (const QChar &c : trimmed) {
            if (c != QLatin1Char('`')) {
                allTicks = false;
                break;
            }
        }
        if (allTicks)
            longest = qMax(longest, int(trimmed.size()));
    }
    return QString(qMax(3, longest + 1), QLatin1Char('`'));
}

QString fencedBlock(const QString &content, const QString &openingLine)
{
    const QString fence = fenceFor(content);
    QString result = fence + openingLine + QLatin1Char('\n');
    if (!content.isEmpty())
        result += content + QLatin1Char('\n');
    result += fence;
    return result;
}

QString listItemLines(const QString &marker, const QString &content)
{
    const QStringList lines = content.split(QLatin1Char('\n'));
    QString out = marker + lines.first();
    const QString pad(marker.size(), QLatin1Char(' '));
    for (int i = 1; i < lines.size(); ++i) {
        out += QLatin1Char('\n');
        // A pad-only line would be trailing whitespace, which parses as a
        // blank line either way; write it bare.
        if (!lines.at(i).isEmpty())
            out += pad + lines.at(i);
    }
    return out;
}

QString indentPrefix(int indentLevel)
{
    return QString(2 * qMax(0, indentLevel), QLatin1Char(' '));
}

} // namespace BlockMarkdown
