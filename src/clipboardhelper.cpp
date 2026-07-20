// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
#include "clipboardhelper.h"

#include <QClipboard>
#include <QGuiApplication>
#include <QMimeData>
#include <QRegularExpression>
#include <QUrl>

namespace {

// A bare URL and nothing else: what §5.3 treats as a link paste rather than a
// text paste. Deliberately strict — any surrounding prose makes it text.
const QRegularExpression &loneUrlPattern()
{
    static const QRegularExpression re(
        QStringLiteral("\\A(https?://|www\\.)[^\\s<>\"]+\\z"),
        QRegularExpression::CaseInsensitiveOption);
    return re;
}

} // namespace

const char *ClipboardHelper::internalMimeType()
{
    return "application/x-kvit-markdown";
}

ClipboardHelper::ClipboardHelper(QObject *parent)
    : QObject(parent)
{
    connect(QGuiApplication::clipboard(), &QClipboard::dataChanged,
            this, &ClipboardHelper::contentsChanged);
}

QString ClipboardHelper::text() const
{
    return QGuiApplication::clipboard()->text();
}

void ClipboardHelper::setText(const QString &text)
{
    QGuiApplication::clipboard()->setText(text);
}

bool ClipboardHelper::hasText() const
{
    return !QGuiApplication::clipboard()->text().isEmpty();
}

QString ClipboardHelper::html() const
{
    const QMimeData *mime = QGuiApplication::clipboard()->mimeData();
    return mime ? mime->html() : QString();
}

bool ClipboardHelper::hasHtml() const
{
    const QMimeData *mime = QGuiApplication::clipboard()->mimeData();
    return mime && mime->hasHtml() && !mime->html().trimmed().isEmpty();
}

bool ClipboardHelper::hasInternalMarkdown() const
{
    const QMimeData *mime = QGuiApplication::clipboard()->mimeData();
    return mime && mime->hasFormat(QString::fromLatin1(internalMimeType()));
}

bool ClipboardHelper::hasUrl() const
{
    const QString candidate = text().trimmed();
    if (candidate.isEmpty())
        return false;
    return loneUrlPattern().match(candidate).hasMatch();
}

QString ClipboardHelper::url() const
{
    return hasUrl() ? text().trimmed() : QString();
}

bool ClipboardHelper::hasStructuredMarkdown() const
{
    if (hasInternalMarkdown())
        return true;
    if (!hasHtml())
        return false;
    const QString source = html();
    return m_htmlConverter.hasStructure(source)
           && !m_htmlConverter.convert(source).isEmpty();
}

QString ClipboardHelper::markdown() const
{
    if (hasInternalMarkdown())
        return text();
    if (hasHtml()) {
        const QString source = html();
        if (m_htmlConverter.hasStructure(source)) {
            const QString converted = m_htmlConverter.convert(source);
            if (!converted.isEmpty())
                return converted;
        }
    }
    return text();
}

void ClipboardHelper::setMarkdown(const QString &markdown, const QString &html)
{
    auto *mime = new QMimeData;
    mime->setText(markdown);
    if (!html.isEmpty())
        mime->setHtml(html);
    // The payload rides along under the private type so a paste back into
    // Kvit can be recognized without re-parsing anything.
    mime->setData(QString::fromLatin1(internalMimeType()), markdown.toUtf8());
    QGuiApplication::clipboard()->setMimeData(mime);
}
