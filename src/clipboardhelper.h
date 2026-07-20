// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
#ifndef CLIPBOARDHELPER_H
#define CLIPBOARDHELPER_H

#include <QObject>
#include <QString>

#include "htmltomarkdown.h"

// Exposes the system clipboard to QML as a context property.
// QML has no built-in clipboard access (Qt.clipboard does not exist),
// so paste-plain and copy-as-markdown go through this helper.
//
// The clipboard is a multi-format channel, not a text channel (features.md
// §5.1, §5.3). Copying writes three flavors of the same content — markdown as
// text/plain, rendered HTML for applications that want rich text, and a
// private marker identifying the payload as Kvit's own — and pasting inspects
// what is on offer before deciding how to interpret it.
class ClipboardHelper : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString text READ text WRITE setText NOTIFY contentsChanged)
    Q_PROPERTY(bool hasText READ hasText NOTIFY contentsChanged)
    Q_PROPERTY(QString html READ html NOTIFY contentsChanged)
    Q_PROPERTY(bool hasHtml READ hasHtml NOTIFY contentsChanged)
    // True when this application wrote the current clipboard content, so its
    // text/plain is known-good Kvit markdown and must NOT be run through the
    // HTML converter (§5.3 "internal format pastes losslessly").
    Q_PROPERTY(bool hasInternalMarkdown READ hasInternalMarkdown
                   NOTIFY contentsChanged)
    // True when markdown() resolves through the internal or HTML arm, i.e. the
    // payload carries block structure (headings, lists, quotes) rather than
    // being flat text. Callers insert a structured payload by PARSING it into
    // typed blocks; flat text instead splices literally at the caret, which is
    // what pasting several plain lines should do.
    Q_PROPERTY(bool hasStructuredMarkdown READ hasStructuredMarkdown
                   NOTIFY contentsChanged)
    // The clipboard holds exactly one URL and nothing else (§5.3).
    Q_PROPERTY(bool hasUrl READ hasUrl NOTIFY contentsChanged)
    Q_PROPERTY(QString url READ url NOTIFY contentsChanged)

public:
    // The private MIME type marking a Kvit-authored payload. A private type
    // (rather than a text/* one) keeps it invisible to other applications.
    static const char *internalMimeType();

    explicit ClipboardHelper(QObject *parent = nullptr);

    QString text() const;
    void setText(const QString &text);
    bool hasText() const;

    QString html() const;
    bool hasHtml() const;
    bool hasInternalMarkdown() const;
    bool hasStructuredMarkdown() const;

    bool hasUrl() const;
    QString url() const;

    // Copy markdown in every flavor at once: text/plain for anything that
    // reads text, text/html so rich-text targets receive formatting rather
    // than raw syntax, and the internal marker so a paste back into Kvit is
    // recognized as its own. `html` may be empty to skip the rich flavor.
    Q_INVOKABLE void setMarkdown(const QString &markdown,
                                 const QString &html = QString());

    // The clipboard resolved to markdown, applying the §5.3 format matrix in
    // priority order:
    //   1. Kvit's own payload  — text/plain is already markdown, use verbatim
    //      (converting it would double-escape its syntax).
    //   2. structured text/html — convert, so a browser copy keeps headings,
    //      lists, links and tables instead of collapsing to flat text.
    //   3. anything else       — text/plain unchanged.
    // Image data is handled upstream of this, on the image path.
    Q_INVOKABLE QString markdown() const;

signals:
    void contentsChanged();

private:
    // Stateless; owned by composition so the paste matrix has one home.
    HtmlToMarkdown m_htmlConverter;
};

#endif // CLIPBOARDHELPER_H
