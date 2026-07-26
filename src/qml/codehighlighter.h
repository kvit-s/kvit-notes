// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
#ifndef CODEHIGHLIGHTER_H
#define CODEHIGHLIGHTER_H

#include <QObject>
#include <QPointer>
#include <QString>
#include <QQuickTextDocument>
#include <QtQml/qqmlregistration.h>

#include "theme.h"

class LanguageHighlighter;

// Syntax coloring for a plain QML TextArea, and nothing else.
//
// BlockEditorEngine already colors code blocks, but it cannot serve this
// purpose: it OWNS the document it attaches to. It rebuilds the text from its
// own `markdown` property, turns the document's undo stack off, and maps every
// edit back to storage. An editor that holds its own text — the Mermaid source
// editor in DiagramBlock.qml is the one that does — would have its content
// erased the moment such an engine attached.
//
// So this class attaches a QSyntaxHighlighter and stops there. It reads the
// document, writes only character formats, and never mutates text, so an
// editor keeps its own text binding, its own undo stack and its own key
// handling. Colors come from the same five theme tokens and the same
// CodeLanguages scanners the code blocks use, so one language looks the same
// wherever it is shown.
class CodeHighlighter : public QObject
{
    Q_OBJECT
    QML_ELEMENT

    // The TextArea's document, as `document: myTextArea.textDocument`.
    Q_PROPERTY(QQuickTextDocument *document READ document WRITE setDocument
                   NOTIFY documentChanged)
    // A CodeLanguages id or alias. An empty or unrecognized value paints
    // nothing, leaving plain monospace text.
    Q_PROPERTY(QString language READ language WRITE setLanguage
                   NOTIFY languageChanged)
    // Optional. Without a theme the built-in light colors apply, which keeps
    // a theme-unaware test rendering something sensible. A theme switch
    // repaints in place.
    Q_PROPERTY(Theme *theme READ theme WRITE setTheme NOTIFY themeChanged)

public:
    explicit CodeHighlighter(QObject *parent = nullptr);
    ~CodeHighlighter() override;

    QQuickTextDocument *document() const { return m_quickDocument; }
    void setDocument(QQuickTextDocument *document);

    QString language() const { return m_language; }
    void setLanguage(const QString &language);

    Theme *theme() const { return m_theme; }
    void setTheme(Theme *theme);

signals:
    void documentChanged();
    void languageChanged();
    void themeChanged();

private:
    void rehighlight();

    QPointer<QQuickTextDocument> m_quickDocument;
    QPointer<QTextDocument> m_doc;
    LanguageHighlighter *m_highlighter = nullptr;
    QString m_language;
    QPointer<Theme> m_theme;

    friend class LanguageHighlighter;
};

#endif // CODEHIGHLIGHTER_H
