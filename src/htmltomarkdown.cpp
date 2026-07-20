// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
#include "htmltomarkdown.h"

#include <QRegularExpression>
#include <QStringList>
#include <QTextBlock>
#include <QTextCharFormat>
#include <QTextDocument>
#include <QTextFragment>
#include <QTextFrame>
#include <QTextList>
#include <QTextTable>

namespace {

// Characters that would otherwise be read back as markdown syntax when they
// appear in text that came from HTML (where they carried no meaning).
QString escapeInline(const QString &text)
{
    QString out;
    out.reserve(text.size());
    for (const QChar c : text) {
        if (c == u'*' || c == u'_' || c == u'`' || c == u'[' || c == u']'
            || c == u'\\') {
            out.append(u'\\');
        }
        out.append(c);
    }
    return out;
}

// The longest run of backticks anywhere in text (0 when there is none).
int longestBacktickRun(const QString &text)
{
    int longest = 0;
    int run = 0;
    for (const QChar c : text) {
        if (c == u'`') {
            ++run;
            longest = qMax(longest, run);
        } else {
            run = 0;
        }
    }
    return longest;
}

// Wrap text as a CommonMark inline code span. The delimiter must be a
// backtick run longer than any run inside, or the content would close the
// span early; when the content starts or ends with a backtick, one space of
// padding on each side keeps the delimiters distinguishable (readers strip
// exactly that pair back off).
QString inlineCodeSpan(const QString &text)
{
    const QString fence(longestBacktickRun(text) + 1, u'`');
    const bool pad = text.startsWith(u'`') || text.endsWith(u'`');
    const QString body = pad ? u' ' + text + u' ' : text;
    return fence + body + fence;
}

// A link destination goes into "[text](DEST)", whose grammar admits neither
// spaces nor parentheses. Percent-encoding those is not an escape that a
// reader has to undo — it is the same URL, so the link still resolves.
QString encodeLinkDestination(const QString &href)
{
    QString out;
    out.reserve(href.size());
    for (const QChar c : href) {
        if (c == u'(')       out += QLatin1String("%28");
        else if (c == u')')  out += QLatin1String("%29");
        else if (c == u' ')  out += QLatin1String("%20");
        else if (c == u'<')  out += QLatin1String("%3C");
        else if (c == u'>')  out += QLatin1String("%3E");
        else                 out += c;
    }
    return out;
}

// A fragment is inline code when it is monospace but its whole block is not
// (a wholly monospace block is a <pre>, handled as a fence instead).
//
// Qt's HTML importer does NOT set fontFixedPitch for <code>/<tt>; it records
// the CSS family instead, so the family list is the reliable signal and the
// pitch/style-hint checks are only a backstop for other producers.
bool isMonospace(const QTextCharFormat &fmt)
{
    if (fmt.fontFixedPitch()
        || fmt.fontStyleHint() == QFont::Monospace
        || fmt.fontStyleHint() == QFont::TypeWriter) {
        return true;
    }
    const QVariant families = fmt.property(QTextFormat::FontFamilies);
    for (const QString &family : families.toStringList()) {
        const QString lowered = family.toLower();
        if (lowered.contains(QStringLiteral("mono"))
            || lowered.contains(QStringLiteral("courier"))
            || lowered.contains(QStringLiteral("consolas"))) {
            return true;
        }
    }
    return false;
}

bool blockIsPreformatted(const QTextBlock &block)
{
    if (!block.isValid() || block.text().isEmpty())
        return false;
    bool sawFragment = false;
    for (auto it = block.begin(); !it.atEnd(); ++it) {
        const QTextFragment fragment = it.fragment();
        if (!fragment.isValid() || fragment.text().isEmpty())
            continue;
        sawFragment = true;
        if (!isMonospace(fragment.charFormat()))
            return false;
    }
    return sawFragment;
}

} // namespace

HtmlToMarkdown::HtmlToMarkdown(QObject *parent)
    : QObject(parent)
{
}

bool HtmlToMarkdown::hasStructure(const QString &html) const
{
    // Anything beyond the wrapper Qt/browsers add around a bare text run.
    static const QRegularExpression meaningful(
        QStringLiteral("<\\s*(h[1-6]|p|br|hr|ul|ol|li|blockquote|pre|code|a|img"
                       "|table|tr|td|th|strong|b|em|i|del|s|strike|u)\\b"),
        QRegularExpression::CaseInsensitiveOption);
    return meaningful.match(html).hasMatch();
}

QString HtmlToMarkdown::inlineMarkdown(const QTextBlock &block,
                                       bool suppressBold) const
{
    QString out;
    for (auto it = block.begin(); !it.atEnd(); ++it) {
        const QTextFragment fragment = it.fragment();
        if (!fragment.isValid())
            continue;
        QString text = fragment.text();
        if (text.isEmpty())
            continue;
        // Qt represents an embedded object (an image, say) with U+FFFC.
        text.remove(QChar(0xFFFC));
        if (text.isEmpty())
            continue;

        const QTextCharFormat fmt = fragment.charFormat();
        const bool code = isMonospace(fmt);
        text = code ? text : escapeInline(text);

        // Emphasis wraps the trimmed run so the markers stay adjacent to the
        // text; the surrounding spaces are re-attached outside them, because
        // "** bold **" is not emphasis in markdown.
        int lead = 0;
        while (lead < text.size() && text.at(lead).isSpace())
            ++lead;
        int trail = text.size();
        while (trail > lead && text.at(trail - 1).isSpace())
            --trail;
        if (lead >= trail) {
            out.append(text);
            continue;
        }
        const QString leadSpace = text.left(lead);
        const QString tail = text.mid(trail);

        QString body = text.mid(lead, trail - lead);
        if (code)
            body = inlineCodeSpan(body);
        if (!suppressBold && fmt.fontWeight() >= QFont::Bold)
            body = QStringLiteral("**%1**").arg(body);
        if (fmt.fontItalic())
            body = QStringLiteral("*%1*").arg(body);
        if (fmt.fontStrikeOut())
            body = QStringLiteral("~~%1~~").arg(body);

        const QString href = fmt.anchorHref();
        if (!href.isEmpty())
            body = QStringLiteral("[%1](%2)")
                       .arg(body, encodeLinkDestination(href));

        out.append(leadSpace + body + tail);
    }
    return out;
}

QString HtmlToMarkdown::blockMarkdown(const QTextBlock &block) const
{
    if (!block.isValid())
        return QString();

    // <pre> becomes a fenced code block, keeping its text verbatim. The fence
    // must outrun any backtick run inside, or a ``` line in the pasted code
    // would close the block early and spill the rest into the document.
    if (blockIsPreformatted(block)) {
        const QString text = block.text();
        const QString fence(qMax(3, longestBacktickRun(text) + 1), u'`');
        return fence + u'\n' + text + u'\n' + fence;
    }

    const QTextBlockFormat blockFmt = block.blockFormat();
    const int heading = blockFmt.headingLevel();
    // Qt gives <h1>…<h6> a bold weight. The "#" prefix already carries that,
    // so emitting "**" as well would be redundant syntax in the result.
    const QString content = inlineMarkdown(block, heading >= 1).trimmed();

    if (QTextList *list = block.textList()) {
        const QTextListFormat listFmt = list->format();
        // Qt reports indent 1 for a top-level list; markdown nests by two
        // spaces per level below that.
        const int depth = qMax(0, listFmt.indent() - 1);
        const QString pad(depth * 2, u' ');
        const QTextListFormat::Style style = listFmt.style();
        const bool ordered = style == QTextListFormat::ListDecimal
                             || style == QTextListFormat::ListLowerAlpha
                             || style == QTextListFormat::ListUpperAlpha
                             || style == QTextListFormat::ListLowerRoman
                             || style == QTextListFormat::ListUpperRoman;
        if (content.isEmpty())
            return QString();
        if (ordered) {
            return QStringLiteral("%1%2. %3")
                .arg(pad, QString::number(qMax(1, list->itemNumber(block) + 1)),
                     content);
        }
        return QStringLiteral("%1- %2").arg(pad, content);
    }

    if (content.isEmpty())
        return QString();

    if (heading >= 1 && heading <= 6)
        return QStringLiteral("%1 %2").arg(QString(heading, u'#'), content);

    // <blockquote> carries no dedicated flag: Qt expresses it as symmetric
    // left/right margins (40 px each by default), and other producers use a
    // block indent, so both count.
    if (blockFmt.indent() > 0
        || (blockFmt.leftMargin() > 0 && blockFmt.rightMargin() > 0)) {
        return QStringLiteral("> %1").arg(content);
    }

    return content;
}

QString HtmlToMarkdown::frameMarkdown(QTextFrame *frame) const
{
    if (!frame)
        return QString();

    QStringList parts;
    for (auto it = frame->begin(); !it.atEnd(); ++it) {
        if (QTextFrame *child = it.currentFrame()) {
            if (auto *table = qobject_cast<QTextTable *>(child)) {
                // Pipe table, with the header separator after row 0 so the
                // result round-trips through the document serializer.
                QStringList rows;
                for (int r = 0; r < table->rows(); ++r) {
                    QStringList cells;
                    for (int c = 0; c < table->columns(); ++c) {
                        const QTextTableCell cell = table->cellAt(r, c);
                        QString cellText;
                        if (cell.isValid()) {
                            for (auto cit = cell.begin(); !cit.atEnd(); ++cit) {
                                // <th> is bold in Qt; the pipe table's own
                                // header row already conveys that, so the
                                // markers would only be noise.
                                const QString line =
                                    inlineMarkdown(cit.currentBlock(),
                                                   r == 0).trimmed();
                                if (!line.isEmpty()) {
                                    if (!cellText.isEmpty())
                                        cellText.append(u' ');
                                    cellText.append(line);
                                }
                            }
                        }
                        cells.append(cellText.replace(u'|', QStringLiteral("\\|")));
                    }
                    rows.append(QStringLiteral("| %1 |")
                                    .arg(cells.join(QStringLiteral(" | "))));
                    if (r == 0) {
                        QStringList sep;
                        for (int c = 0; c < table->columns(); ++c)
                            sep.append(QStringLiteral("---"));
                        rows.append(QStringLiteral("| %1 |")
                                        .arg(sep.join(QStringLiteral(" | "))));
                    }
                }
                if (!rows.isEmpty())
                    parts.append(rows.join(QStringLiteral("\n")));
                continue;
            }
            const QString nested = frameMarkdown(child);
            if (!nested.isEmpty())
                parts.append(nested);
            continue;
        }

        const QString line = blockMarkdown(it.currentBlock());
        if (!line.isEmpty())
            parts.append(line);
    }
    return parts.join(QStringLiteral("\n\n"));
}

QString HtmlToMarkdown::convert(const QString &html) const
{
    if (html.trimmed().isEmpty())
        return QString();

    QTextDocument doc;
    doc.setHtml(html);
    QString markdown = frameMarkdown(doc.rootFrame());

    // Collapse the runs of blank lines that empty source blocks leave behind.
    static const QRegularExpression blankRun(QStringLiteral("\n{3,}"));
    markdown.replace(blankRun, QStringLiteral("\n\n"));
    return markdown.trimmed();
}
