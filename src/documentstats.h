// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
#ifndef DOCUMENTSTATS_H
#define DOCUMENTSTATS_H

#include <QObject>
#include <QString>
#include <QVariantMap>

class BlockModel;

// Document statistics (features.md §19.1; phase11 decision 7): word,
// character-with-spaces, character-without-spaces, paragraph, block, and
// reading-time counts for the whole document or a selection. Exposed as the
// `documentStats` context property.
//
// Counts run over DISPLAY text — markers stripped, exactly the representation
// the status bar and the note list already count (NoteCollection's
// wordCountForMarkdown rules), so the numbers never disagree. Code blocks
// count their verbatim content. Reading time is words ÷ 200 wpm, rounded, at
// least one minute for any prose. The projection recomputes behind the status
// bar's existing 200 ms coalescing timer, never per keystroke.
class DocumentStats : public QObject
{
    Q_OBJECT

public:
    explicit DocumentStats(QObject *parent = nullptr);

    void setModel(BlockModel *model);
    BlockModel *model() const { return m_model; }

    // Whole-document statistics as a map: words, charsWithSpaces,
    // charsNoSpaces, paragraphs, blocks, readingMinutes.
    Q_INVOKABLE QVariantMap documentStats() const;

    // Statistics over an arbitrary already-display-text string (a selection):
    // blocks and paragraphs are the newline-separated non-empty runs.
    Q_INVOKABLE QVariantMap statsForText(const QString &displayText) const;

    // Display text of a block's markdown (markers stripped), or the content
    // verbatim for a code block — the same rule documentStats() applies. QML
    // uses it to assemble a selection's display text for statsForText().
    Q_INVOKABLE QString displayTextFor(const QString &markdown, bool verbatim) const;

    // ---- Pure helpers (unit-tested without a GUI) ----
    static int wordCount(const QString &displayText);
    static int charCount(const QString &displayText, bool withSpaces);
    // words / 200 wpm, rounded; at least 1 for any prose, 0 for none.
    static int readingMinutes(int words);

private:
    BlockModel *m_model = nullptr;
};

#endif // DOCUMENTSTATS_H
