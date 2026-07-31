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

QString safeHref(const QString &url)
{
    // What the browser will see. It removes tab, carriage return and line feed
    // from a URL and trims the leading control characters before deciding what
    // the scheme is, so the scheme has to be read off the same string rather
    // than off the text as it was written.
    QString probe;
    probe.reserve(url.size());
    for (const QChar c : url) {
        if (c == QLatin1Char('\t') || c == QLatin1Char('\n')
            || c == QLatin1Char('\r'))
            continue;
        probe.append(c);
    }
    while (!probe.isEmpty() && probe.at(0).unicode() <= 0x20)
        probe.remove(0, 1);

    // A scheme is a letter followed by letters, digits, '+', '-' or '.', up to
    // a colon. Anything else before the first colon — a slash, a '#', a '?' —
    // means there is no scheme and this is a relative reference, which cannot
    // name anything but a place in the exported document's own directory.
    int colon = -1;
    for (int i = 0; i < probe.size(); ++i) {
        const char16_t c = probe.at(i).unicode();
        if (c == u':') {
            colon = i;
            break;
        }
        const bool letter = (c >= u'a' && c <= u'z') || (c >= u'A' && c <= u'Z');
        const bool rest = letter || (c >= u'0' && c <= u'9') || c == u'+'
                          || c == u'-' || c == u'.';
        if (i == 0 ? !letter : !rest)
            return url;
    }
    if (colon < 0)
        return url;

    static const QStringList navigational{
        QStringLiteral("http"),   QStringLiteral("https"),
        QStringLiteral("mailto"), QStringLiteral("ftp"),
        QStringLiteral("ftps"),   QStringLiteral("file"),
        QStringLiteral("tel"),    QStringLiteral("sms"),
    };
    return navigational.contains(probe.left(colon).toLower()) ? url : QString();
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
        if (t == QLatin1String("link") || t == QLatin1String("autolink")) {
            const QString href = safeHref(span.url);
            // A refused target keeps its text and loses its anchor: the reader
            // still sees what the note said, and following it can do nothing.
            if (href.isEmpty())
                return inner;
            return "<a href=\"" + esc(href) + "\">" + inner + "</a>";
        }
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
