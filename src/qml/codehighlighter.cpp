// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
#include "codehighlighter.h"

#include "codelanguages.h"

#include <QSyntaxHighlighter>
#include <QTextCharFormat>
#include <QTextDocument>

namespace {
// The same fallbacks BlockEditorEngine paints with when no theme is set, so a
// language looks the same in a diagram's source editor as in a code block.
const QColor kCodeKeyword("#a626a4");
const QColor kCodeType("#4078f2");
const QColor kCodeString("#50a14f");
const QColor kCodeComment("#a0a1a7");
const QColor kCodeNumber("#986801");
} // namespace

// One line at a time, with the previous line's carry-state, so a construct
// that spans lines (a Mermaid `%%{ … }%%` directive, a block comment) survives
// across them through QSyntaxHighlighter's block-state mechanism.
class LanguageHighlighter : public QSyntaxHighlighter
{
public:
    LanguageHighlighter(QTextDocument *doc, CodeHighlighter *owner)
        : QSyntaxHighlighter(doc)
        , m_owner(owner)
    {
    }

protected:
    void highlightBlock(const QString &text) override
    {
        const QString &lang = m_owner->m_language;
        if (lang.isEmpty() || !CodeLanguages::isSupported(lang)) {
            setCurrentBlockState(-1);
            return;
        }
        const int startState = qMax(0, previousBlockState());
        const CodeLanguages::LineResult res =
            CodeLanguages::highlightLine(lang, text, startState);
        setCurrentBlockState(res.endState);

        const Theme *theme = m_owner->m_theme;
        for (const CodeLanguages::Span &span : res.spans) {
            QColor color;
            switch (span.token) {
            case CodeLanguages::Token::Keyword:
                color = theme ? theme->codeKeyword() : kCodeKeyword; break;
            case CodeLanguages::Token::Type:
                color = theme ? theme->codeType() : kCodeType; break;
            case CodeLanguages::Token::String:
                color = theme ? theme->codeString() : kCodeString; break;
            case CodeLanguages::Token::Comment:
                color = theme ? theme->codeComment() : kCodeComment; break;
            case CodeLanguages::Token::Number:
                color = theme ? theme->codeNumber() : kCodeNumber; break;
            case CodeLanguages::Token::Plain:
                continue;
            }
            QTextCharFormat format;
            format.setForeground(color);
            setFormat(span.start, span.length, format);
        }
    }

private:
    CodeHighlighter *m_owner;
};

CodeHighlighter::CodeHighlighter(QObject *parent)
    : QObject(parent)
{
}

CodeHighlighter::~CodeHighlighter()
{
    delete m_highlighter;
}

void CodeHighlighter::setDocument(QQuickTextDocument *document)
{
    if (m_quickDocument == document)
        return;
    m_quickDocument = document;

    delete m_highlighter;
    m_highlighter = nullptr;
    // The outgoing document's destroyed-connection would otherwise outlive it
    // and clear the pointer to the NEXT document's highlighter.
    if (m_doc)
        disconnect(m_doc, nullptr, this, nullptr);
    m_doc = document ? document->textDocument() : nullptr;

    if (m_doc) {
        // A highlighter only runs once the document has a layout. A TextArea's
        // always does; a bare document in a unit test does not.
        (void)m_doc->documentLayout();
        m_highlighter = new LanguageHighlighter(m_doc, this);
        // The highlighter is the document's child and dies with it. Drop the
        // pointer at the same moment, or a later rehighlight would follow it
        // into freed memory.
        connect(m_doc, &QObject::destroyed, this, [this]() {
            m_highlighter = nullptr;
        });
    }
    emit documentChanged();
}

void CodeHighlighter::setLanguage(const QString &language)
{
    if (m_language == language)
        return;
    m_language = language;
    emit languageChanged();
    rehighlight();
}

void CodeHighlighter::setTheme(Theme *theme)
{
    if (m_theme == theme)
        return;
    if (m_theme)
        disconnect(m_theme, nullptr, this, nullptr);
    m_theme = theme;
    if (m_theme) {
        connect(m_theme, &Theme::themeChanged,
                this, &CodeHighlighter::rehighlight);
    }
    emit themeChanged();
    rehighlight();
}

void CodeHighlighter::rehighlight()
{
    if (m_highlighter)
        m_highlighter->rehighlight();
}
