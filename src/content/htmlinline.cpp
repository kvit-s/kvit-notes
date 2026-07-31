// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
#include "htmlinline.h"
#include "markdownformatter.h"

#include <QLatin1String>

#include <functional>

namespace HtmlInline {

QString esc(const QString &text)
{
    QString out;
    out.reserve(text.size());
    for (const QChar &c : text) {
        if (c == '&') out += QStringLiteral("&amp;");
        else if (c == '<') out += QStringLiteral("&lt;");
        else if (c == '>') out += QStringLiteral("&gt;");
        else if (c == '"') out += QStringLiteral("&quot;");
        else out += c;
    }
    return out;
}

QString escFlowing(const QString &text)
{
    QString out = esc(text);
    out.replace(QLatin1Char('\n'), QStringLiteral("<br>"));
    return out;
}

QString renderInline(const QString &markdown, bool mathJax, bool *sawMath)
{
    const QString &md = markdown;
    MarkdownFormatter fmt;
    const QList<FormattedSpan> spans = fmt.parseSpans(md);

    // Recursive walk over the span tree (children are absolute coordinates).
    std::function<QString(const QList<FormattedSpan> &, int, int)> renderList;
    std::function<QString(const FormattedSpan &)> renderOne;

    renderOne = [&](const FormattedSpan &span) -> QString {
        const int cstart = span.start + span.openLen;
        const int cend = span.end - span.closeLen;
        QString inner;
        if (span.type == QLatin1String("code")
            || span.type == QLatin1String("autolink")
            || span.type == QLatin1String("math")
            || span.type == QLatin1String("escape"))
            // For an escape span the content is the bare character: the
            // export emits it without the backslash.
            inner = esc(md.mid(cstart, cend - cstart));
        else
            inner = renderList(span.children, cstart, cend);

        const QString &t = span.type;
        if (t == QLatin1String("math")) {
            if (mathJax) {
                if (sawMath)
                    *sawMath = true;
                return "\\(" + inner + "\\)";
            }
            return inner; // image-embed modes keep the raw-TeX fallthrough
        }
        if (t == QLatin1String("bold")) return "<strong>" + inner + "</strong>";
        if (t == QLatin1String("italic")) return "<em>" + inner + "</em>";
        if (t == QLatin1String("bolditalic"))
            return "<strong><em>" + inner + "</em></strong>";
        if (t == QLatin1String("strike")) return "<s>" + inner + "</s>";
        if (t == QLatin1String("underline")) return "<u>" + inner + "</u>";
        if (t == QLatin1String("highlight")) return "<mark>" + inner + "</mark>";
        if (t == QLatin1String("code")) return "<code>" + inner + "</code>";
        if (t == QLatin1String("superscript")) return "<sup>" + inner + "</sup>";
        if (t == QLatin1String("subscript")) return "<sub>" + inner + "</sub>";
        if (t == QLatin1String("color"))
            return "<span style=\"color:" + esc(span.color) + "\">" + inner
                 + "</span>";
        if (t == QLatin1String("link") || t == QLatin1String("autolink"))
            return "<a href=\"" + esc(span.url) + "\">" + inner + "</a>";
        return inner;
    };

    renderList = [&](const QList<FormattedSpan> &list, int lo, int hi) -> QString {
        QString out;
        int pos = lo;
        for (const FormattedSpan &span : list) {
            if (span.start < lo || span.end > hi)
                continue;
            if (pos < span.start)
                out += escFlowing(md.mid(pos, span.start - pos));
            out += renderOne(span);
            pos = span.end;
        }
        if (pos < hi)
            out += escFlowing(md.mid(pos, hi - pos));
        return out;
    };

    return renderList(spans, 0, md.length());
}

} // namespace HtmlInline
