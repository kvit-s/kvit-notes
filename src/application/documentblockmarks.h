// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
#ifndef DOCUMENTBLOCKMARKS_H
#define DOCUMENTBLOCKMARKS_H

#include <QColor>
#include <QObject>
#include <QString>
#include <QVariantList>
#include <QVariantMap>

#include <vector>

// The marked ranges of ONE drawn document (qml/ReadOnlyDocument.qml).
//
// A surface draws a markdown document somewhere other than the editor pane —
// a stored version of the open note, a referring note's context, a document
// built from a string — and a caller routinely knows something about part of
// what it drew: which characters differ from the note as it stands, which
// phrase a search hit fell on, which passage a panel beside it is about.
// Before this there was no way to say so. Putting the marks into the markdown
// was not an answer, because a surface exists to show a document faithfully
// and its `blockMarkdown()` and its clipboard are expected to give back what
// was handed in.
//
// A mark names {block, start, length} in the block's DISPLAY text — the text
// with the inline markers taken out, which is what the reader sees and the
// same coordinates the editor's search hits and its module spans use — and
// carries up to two colors: a `wash` painted behind the characters and an
// `outline` drawn around them, one box per visual line. The two compose, so a
// persistent categorical mark and a transient "this is the current one" mark
// can fall on the same words and both stay readable. An entry with neither
// color would have no channel to draw through, so it is refused.
//
// The ranges belong to ONE surface. That is the difference from
// DocumentDecorations, whose spans are per window because the note is: two
// surfaces on screen at once — a preview beside a list, two panels in one
// window — each hold marks of their own, and nothing a caller registers on
// one appears on the other. A surface creates its own instance and publishes
// it as its `marks` property; nothing here is a singleton and nothing
// addresses one.
//
// Marks do not follow the text. A surface whose `markdown` is replaced
// re-lays its blocks, and a caller re-places its ranges; until it does, a
// range that no longer lands on anything draws nothing and costs nothing.
// This is the same contract a module's spans have in the editor, for the same
// reason: the core draws what it is handed rather than guessing what an
// anchor was meant to point at.
//
// Nothing here makes a surface writable. A mark is drawn over text the
// surface has already laid out: the document, its `blockMarkdown()`, its
// clipboard output and its selection are what they were with no marks
// registered.
class DocumentBlockMarks : public QObject
{
    Q_OBJECT

    // Bumped whenever anything about the entries changes. A row binds to it to
    // re-run its query, which is the idiom the search, selection and
    // decoration models already use — a method call alone would subscribe to
    // nothing.
    Q_PROPERTY(int revision READ revision NOTIFY changed)
    // False while nothing is registered, which is the state a surface with no
    // marks stays in. A row reads it before doing any per-block work.
    Q_PROPERTY(bool active READ isActive NOTIFY changed)
    Q_PROPERTY(int count READ count NOTIFY changed)

public:
    explicit DocumentBlockMarks(QObject *parent = nullptr);
    ~DocumentBlockMarks() override;

    int revision() const { return m_revision; }
    bool isActive() const { return !m_marks.empty(); }
    int count() const { return static_cast<int>(m_marks.size()); }

    // Mark [start, start + length) of block `block`, in that block's DISPLAY
    // text, painting a background in `wash` and a border in `outline`. Either
    // color may be invalid, which switches that channel off; both invalid, or
    // a length of zero or less, registers nothing and returns an empty id.
    // Callers wanting a wash alone leave the outline out, which is what the
    // default argument is for — QML sees both arities.
    //
    // The core draws what it is handed. A range running past the end of the
    // block's text draws as much of itself as there is text for, a block with
    // no text of its own (a divider, a picture) draws nothing, and a block
    // index the surface does not hold costs nothing until the caller
    // re-places it.
    Q_INVOKABLE QString add(int block, int start, int length,
                            const QColor &wash,
                            const QColor &outline = QColor());

    // Move an existing mark, keeping its id and its colors. A move may take
    // the length to zero, where registering one at zero is refused: an entry
    // that never marked anything is a caller's mistake, while a mark whose
    // text has just gone is an ordinary state to keep an id through.
    Q_INVOKABLE bool move(const QString &id, int block, int start, int length);
    Q_INVOKABLE bool setColors(const QString &id, const QColor &wash,
                               const QColor &outline);
    Q_INVOKABLE bool remove(const QString &id);
    Q_INVOKABLE void clear();

    // The marks on one block, in registration order — which is the order they
    // paint in, and therefore which of two washes on the same characters is
    // the one underneath. Each element is
    // { id, block, start, length, wash, outline }, where the two colors are
    // strings and an empty one means the entry does not use that channel.
    // This is exactly the shape BlockEditorEngine's `decorationSpans` takes,
    // so a row hands the answer straight to the engine that lays its text
    // out.
    Q_INVOKABLE QVariantList marksForBlock(int blockIndex) const;

    // One mark by id, in the same shape, or an empty map for an id that is
    // not registered. What a caller asking where a mark was drawn needs
    // first: the block it is on decides which row can answer.
    Q_INVOKABLE QVariantMap mark(const QString &id) const;

signals:
    void changed();

private:
    struct Entry
    {
        QString id;
        int block = 0;
        int start = 0;
        int length = 0;
        QColor wash;
        QColor outline;
    };

    Entry *find(const QString &id);
    const Entry *find(const QString &id) const;
    static QVariantMap describe(const Entry &entry);
    void bump();

    std::vector<Entry> m_marks;
    int m_revision = 0;
    int m_nextId = 1;
};

#endif // DOCUMENTBLOCKMARKS_H
