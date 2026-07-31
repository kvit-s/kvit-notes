// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
#include "blockstyle.h"
#include "blockattributes.h"

namespace BlockStyle {

Attributes parse(const QString &payload)
{
    return BlockAttributes::parseMap(payload);
}

bool has(const Attributes &attrs, QLatin1String key)
{
    return attrs.contains(QString(key));
}

QString str(const Attributes &attrs, QLatin1String key, const QString &fallback)
{
    const auto it = attrs.constFind(QString(key));
    return (it == attrs.constEnd() || it->isEmpty()) ? fallback : *it;
}

int num(const Attributes &attrs, QLatin1String key, int fallback)
{
    bool ok = false;
    const int value = str(attrs, key).toInt(&ok);
    return ok ? value : fallback;
}

QString cssColor(const QString &value)
{
    if (value.isEmpty() || value.size() > 32)
        return QString();
    if (value.startsWith(QLatin1Char('#'))) {
        const QStringView digits = QStringView(value).mid(1);
        if (digits.size() != 3 && digits.size() != 4 && digits.size() != 6
            && digits.size() != 8)
            return QString();
        for (const QChar c : digits) {
            if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f')
                  || (c >= 'A' && c <= 'F')))
                return QString();
        }
        return value;
    }
    for (const QChar c : value) {
        if (!c.isLetter())
            return QString();
    }
    return value;
}

QString cssFontFamily(const QString &value)
{
    if (value.isEmpty() || value.size() > 64)
        return QString();
    for (const QChar c : value) {
        if (!(c.isLetterOrNumber() || c == ' ' || c == '-'))
            return QString();
    }
    return QStringLiteral("'") + value + QLatin1Char('\'');
}

QString styleAttr(const QStringList &declarations)
{
    if (declarations.isEmpty())
        return QString();
    return QStringLiteral(" style=\"") + declarations.join(QLatin1Char(';'))
         + QLatin1Char('"');
}

QString textAlign(const Attributes &attrs, const QString &kindDefault)
{
    const QString align = str(attrs, QLatin1String("align"), kindDefault);
    if (align == kindDefault)
        return QString();
    if (align == QLatin1String("center") || align == QLatin1String("right")
        || align == QLatin1String("justify") || align == QLatin1String("left"))
        return QStringLiteral("text-align:") + align;
    return QString();
}

QString withDropCap(const QString &html, const Attributes &attrs)
{
    const int lines = num(attrs, QLatin1String("dropcap"), 0);
    if (lines < 2 || html.isEmpty())
        return html;

    // The first character outside a tag. An entity is one character to a
    // reader, so it is taken whole.
    int start = 0;
    while (start < html.size() && html.at(start) == QLatin1Char('<')) {
        const int close = html.indexOf(QLatin1Char('>'), start);
        if (close < 0)
            return html;
        start = close + 1;
    }
    if (start >= html.size())
        return html;
    // A backslash is never a letter a reader sees, and in rendered markup it
    // opens a MathJax delimiter: a paragraph that begins with inline maths
    // starts `\( … \)`, and wrapping that backslash on its own splits the
    // delimiter, so the browser typesets nothing and the formula disappears.
    // A paragraph opening in maths has no initial to enlarge.
    if (html.at(start) == QLatin1Char('\\'))
        return html;
    int end = start + 1;
    if (html.at(start) == QLatin1Char('&')) {
        const int semi = html.indexOf(QLatin1Char(';'), start);
        if (semi > start && semi - start <= 10)
            end = semi + 1;
    }

    QStringList declarations;
    // The overlay sizes the initial at the body size times the line count
    // times 1.15; in em that is the same multiple of whatever the surrounding
    // text is set at.
    declarations << QStringLiteral("font-size:%1em").arg(lines * 1.15, 0, 'f', 2);
    const QString color = cssColor(str(attrs, QLatin1String("dropcapcolor")));
    if (!color.isEmpty())
        declarations << QStringLiteral("color:") + color;
    const QString family =
        cssFontFamily(str(attrs, QLatin1String("dropcapfont")));
    if (!family.isEmpty())
        declarations << QStringLiteral("font-family:") + family;

    return html.left(start) + "<span class=\"dropcap\""
         + styleAttr(declarations) + ">" + html.mid(start, end - start)
         + "</span>" + html.mid(end);
}

} // namespace BlockStyle
