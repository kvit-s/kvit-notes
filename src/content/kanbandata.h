// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
#ifndef KANBANDATA_H
#define KANBANDATA_H

#include <QList>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariantMap>

// Kanban board parse/serialize/mutate. A board is a fenced code block whose
// language tag is `kanban`, so it needs no stored block type — only a derived
// delegate kind — and inherits the fence's round-trip safety. Its content is
// ordinary markdown a human can read and edit anywhere:
//
//   ## To do
//   - [ ] Card title #label 📅 2026-07-15
//     Indented lines are the card description.
//   - [x] A finished card
//
// `## ` lines open columns; `- [ ] ` / `- [x] ` lines are cards (done when
// [x]); `#label` tokens on the card line are its labels, recognized only at the
// start of the text or after whitespace so that a URL fragment such as
// https://example.com/#intro is left alone, with `\#` writing a literal hash in
// that position; 📅 <date> is the due date (the todo convention), and `\📅`
// writes a literal marker the same way; indented plain lines under a card are
// its description. This pure component maps that to a board and back, and
// applies every mutation as a whole-content rewrite (one undo step).
//
// Every value the card editor can produce survives that round trip. A label
// that does not fit the bare `#token` spelling — one containing a space, a
// hash, a quote or a backslash — is written quoted, as `#"client work"`, with
// `\` and `"` escaped inside; the bare spelling is used wherever it fits, so
// boards written by hand or by earlier versions keep their ordinary syntax and
// still read the same way. The due date is the one field with no escape: the
// grammar holds an ISO `YYYY-MM-DD` naming a real day and nothing else, so
// setCard() drops anything else rather than writing a marker the next parse
// would read back as title text. isValidDue() is the same test, exposed so the
// editor can refuse the value while the user can still fix it.
//
// The board model does not describe everything a `kanban` fence may contain: an
// introductory paragraph, an HTML comment, a blank line, a stray list item. So
// each parsed Card, Column and Board also carries the source text it came from
// plus the unmodelled lines ("trivia") that followed it, and serialize() puts
// all of it back. Two properties follow, and both are pinned by tests:
// serialize(parse(x)) == x for any content, and a mutation only rewrites the
// lines it actually changes. Trivia belongs to a position rather than to a
// card, so a card that moves leaves its trivia behind, and trivia orphaned by a
// removal re-anchors to the preceding position instead of being dropped.
namespace KanbanData {

struct Card {
    QString title;
    bool done = false;
    QStringList labels;
    QString due;
    QString description;

    // When the card was added and when it was last changed, as ISO days.
    // Empty for a card written before either was recorded, which is why the
    // board shows what it has rather than inventing a date for the rest.
    //
    // These are the one part of a card the reader does not write, so they are
    // the one part not written where the reader types: they live in an HTML
    // comment at the end of the card's line,
    //
    //   - [ ] Ship the beta #release <!--kvit created=2026-07-20 modified=2026-07-26-->
    //
    // which every other markdown tool renders as nothing, and which the card's
    // own editor never shows — the text it edits is the line without it. The
    // marker convention the due date uses was the alternative and was rejected
    // for these two: a date the reader cannot edit has no business sitting in
    // the middle of the text they are editing, and a `modified` marker there
    // would be rewritten under the caret on every keystroke.
    QString created;
    QString modified;

    // ---- Source fidelity; not part of the logical model ----
    // Whatever else that `<!--kvit …-->` comment held, verbatim and in source
    // order. The comment is this application's own namespace, so a later
    // version — or the module that links this one — may keep something of its
    // own in it; an edit rewrites the whole comment from the two fields above,
    // so without this, opening a board in a build that does not know a field
    // and touching one card silently deletes it.
    QString stampExtras;
    // The card's exact source line, empty when the card was synthesized by a
    // mutation and the line has to be rendered from the fields above.
    QString rawLine;
    // The indent the description's first line carried, which every line of it
    // is measured against. Only this comes off when the description is read
    // and it goes back on when one is written, so a list nested inside a
    // description keeps its shape; trimming each line flattened it, and the
    // next edit wrote the flattened form to the file.
    QString descriptionIndent;
    // The exact source lines of the description, same convention.
    QStringList rawDescription;
    // Unmodelled lines that followed this card in the source.
    QStringList trailingTrivia;
};

struct Column {
    QString name;
    QList<Card> cards;
    // The exact source of the `## ` header line, empty when synthesized.
    QString rawHeader;
    // Unmodelled lines between the header and the first card.
    QStringList leadingTrivia;
};

struct Board {
    QList<Column> columns;
    // Unmodelled lines before the first column header. Content with no header
    // at all lands here in full.
    QStringList preamble;
    int columnCount() const { return columns.size(); }
};

Board parse(const QString &content);
QString serialize(const Board &b);

// Whether a due value is one the storage grammar can hold: an ISO `YYYY-MM-DD`
// naming a date that exists. setCard() drops anything else rather than writing
// a marker the next parse would read back as title text, and the card editor
// uses the same test to refuse the value while the user can still fix it.
bool isValidDue(const QString &due);

// True when this is the content of a `kanban` fence worth rendering as a board
// (at least one column). Used to decide the delegate kind.
bool looksLikeBoard(const QString &content);

// ---- Mutations (each takes and returns whole-board content) ----
//
// Every mutation that changes a card takes the day it is happening on, as an
// ISO `YYYY-MM-DD`, and stamps the card with it: `created` when the card is
// added, `modified` whenever its text, its fields, its done state or the
// column holding it change. The clock is the caller's rather than this
// component's, which keeps the transforms pure and their tests independent of
// the day they run on. The default — no day at all — stamps nothing, so a
// caller that does not care about dates is not made to invent one.
QString addColumn(const QString &content, const QString &name);
QString renameColumn(const QString &content, int col, const QString &name);
QString removeColumn(const QString &content, int col);
QString moveColumn(const QString &content, int fromCol, int toCol);
QString addCard(const QString &content, int col, const QString &title,
                const QString &today = QString());
QString removeCard(const QString &content, int col, int index);
QString toggleCardDone(const QString &content, int col, int index,
                       const QString &today = QString());
// Move a card within or between columns to a target index in the target
// column (the drag-drop primitive). A move to another column changes what the
// card says about itself and is stamped; reordering inside one column is not.
QString moveCard(const QString &content, int fromCol, int fromIndex,
                 int toCol, int toIndex, const QString &today = QString());
// Overwrite a card's fields (the card-details popover, the label chips and the
// due-date picker).
QString setCard(const QString &content, int col, int index,
                const QString &title, bool done, const QStringList &labels,
                const QString &due, const QString &description,
                const QString &today = QString());
// Replace the text on a card's line — everything after the checkbox — with
// what was typed, character for character. This is what the board's inline
// editor writes: the reader edits the line's own source
// (`Ship the beta #release 📅 2026-08-01`), so what they leave behind is what
// the file gets and the next parse is what turns `#release` back into a label
// and the marked date back into a due date. Only the indent, bullet and
// checkbox in front of the text are kept, so the card holds its done state
// and the file its style. A line break in the text becomes a space: one card
// is one line.
QString setCardLine(const QString &content, int col, int index,
                    const QString &text, const QString &today = QString());
// Replace a card's description — the indented lines under it — with what was
// typed. Line breaks are kept, each line written back indented, so a
// multi-line description round-trips; an empty text removes the description.
QString setCardDescription(const QString &content, int col, int index,
                           const QString &text,
                           const QString &today = QString());

} // namespace KanbanData

// QML context object (kanbanTools).
class KanbanTools : public QObject
{
    Q_OBJECT
public:
    explicit KanbanTools(QObject *parent = nullptr) : QObject(parent) {}

    // {columns:[{name, cards:[{title, done, labels:[…], due, description,
    // line, created, modified}]}]}, where `line` is the text on the card's own
    // line — title, labels and due date as they are written there, and never
    // the comment carrying the two dates — which is what the board's inline
    // editor puts in front of the reader.
    Q_INVOKABLE QVariantMap parse(const QString &content) const;
    Q_INVOKABLE bool looksLikeBoard(const QString &content) const
    { return KanbanData::looksLikeBoard(content); }
    Q_INVOKABLE bool isValidDue(const QString &due) const
    { return KanbanData::isValidDue(due); }

    Q_INVOKABLE QString addColumn(const QString &c, const QString &name) const
    { return KanbanData::addColumn(c, name); }
    Q_INVOKABLE QString renameColumn(const QString &c, int col, const QString &name) const
    { return KanbanData::renameColumn(c, col, name); }
    Q_INVOKABLE QString removeColumn(const QString &c, int col) const
    { return KanbanData::removeColumn(c, col); }
    Q_INVOKABLE QString moveColumn(const QString &c, int from, int to) const
    { return KanbanData::moveColumn(c, from, to); }
    Q_INVOKABLE QString addCard(const QString &c, int col, const QString &title,
                                const QString &today = QString()) const
    { return KanbanData::addCard(c, col, title, today); }
    Q_INVOKABLE QString removeCard(const QString &c, int col, int idx) const
    { return KanbanData::removeCard(c, col, idx); }
    Q_INVOKABLE QString toggleCardDone(const QString &c, int col, int idx,
                                       const QString &today = QString()) const
    { return KanbanData::toggleCardDone(c, col, idx, today); }
    Q_INVOKABLE QString moveCard(const QString &c, int fromCol, int fromIdx,
                                 int toCol, int toIdx,
                                 const QString &today = QString()) const
    { return KanbanData::moveCard(c, fromCol, fromIdx, toCol, toIdx, today); }
    Q_INVOKABLE QString setCardLine(const QString &c, int col, int idx,
                                    const QString &text,
                                    const QString &today = QString()) const
    { return KanbanData::setCardLine(c, col, idx, text, today); }
    Q_INVOKABLE QString setCardDescription(const QString &c, int col, int idx,
                                           const QString &text,
                                           const QString &today = QString()) const
    { return KanbanData::setCardDescription(c, col, idx, text, today); }
    Q_INVOKABLE QString setCard(const QString &c, int col, int idx,
                                const QString &title, bool done,
                                const QStringList &labels, const QString &due,
                                const QString &description,
                                const QString &today = QString()) const
    { return KanbanData::setCard(c, col, idx, title, done, labels, due,
                                 description, today); }
};

#endif // KANBANDATA_H
