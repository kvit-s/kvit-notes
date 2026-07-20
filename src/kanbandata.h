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

// Kanban board parse/serialize/mutate (phase10-plan.md decision 9). A board is
// a fenced code block whose language tag is `kanban`, so it needs no stored
// block type — only a derived delegate kind — and inherits the fence's
// round-trip safety. Its content is ordinary markdown a human can read and
// edit anywhere:
//
//   ## To do
//   - [ ] Card title #label 📅 2026-07-15
//     Indented lines are the card description.
//   - [x] A finished card
//
// `## ` lines open columns; `- [ ] ` / `- [x] ` lines are cards (done when
// [x]); `#label` tokens on the card line are its labels; 📅 <date> is the due
// date (the todo convention, decision 10); indented plain lines under a card
// are its description. This pure component maps that to a board and back, and
// applies every mutation as a whole-content rewrite (one undo step).
namespace KanbanData {

struct Card {
    QString title;
    bool done = false;
    QStringList labels;
    QString due;
    QString description;
};

struct Column {
    QString name;
    QList<Card> cards;
};

struct Board {
    QList<Column> columns;
    int columnCount() const { return columns.size(); }
};

Board parse(const QString &content);
QString serialize(const Board &b);

// True when this is the content of a `kanban` fence worth rendering as a board
// (at least one column). Used to decide the delegate kind.
bool looksLikeBoard(const QString &content);

// ---- Mutations (each takes and returns whole-board content) ----
QString addColumn(const QString &content, const QString &name);
QString renameColumn(const QString &content, int col, const QString &name);
QString removeColumn(const QString &content, int col);
QString moveColumn(const QString &content, int fromCol, int toCol);
QString addCard(const QString &content, int col, const QString &title);
QString removeCard(const QString &content, int col, int index);
QString toggleCardDone(const QString &content, int col, int index);
// Move a card within or between columns to a target index in the target
// column (the drag-drop primitive).
QString moveCard(const QString &content, int fromCol, int fromIndex,
                 int toCol, int toIndex);
// Overwrite a card's fields (the card-editor popover).
QString setCard(const QString &content, int col, int index,
                const QString &title, bool done, const QStringList &labels,
                const QString &due, const QString &description);

} // namespace KanbanData

// QML context object (kanbanTools).
class KanbanTools : public QObject
{
    Q_OBJECT
public:
    explicit KanbanTools(QObject *parent = nullptr) : QObject(parent) {}

    // {columns:[{name, cards:[{title, done, labels:[…], due, description}]}]}.
    Q_INVOKABLE QVariantMap parse(const QString &content) const;
    Q_INVOKABLE bool looksLikeBoard(const QString &content) const
    { return KanbanData::looksLikeBoard(content); }

    Q_INVOKABLE QString addColumn(const QString &c, const QString &name) const
    { return KanbanData::addColumn(c, name); }
    Q_INVOKABLE QString renameColumn(const QString &c, int col, const QString &name) const
    { return KanbanData::renameColumn(c, col, name); }
    Q_INVOKABLE QString removeColumn(const QString &c, int col) const
    { return KanbanData::removeColumn(c, col); }
    Q_INVOKABLE QString moveColumn(const QString &c, int from, int to) const
    { return KanbanData::moveColumn(c, from, to); }
    Q_INVOKABLE QString addCard(const QString &c, int col, const QString &title) const
    { return KanbanData::addCard(c, col, title); }
    Q_INVOKABLE QString removeCard(const QString &c, int col, int idx) const
    { return KanbanData::removeCard(c, col, idx); }
    Q_INVOKABLE QString toggleCardDone(const QString &c, int col, int idx) const
    { return KanbanData::toggleCardDone(c, col, idx); }
    Q_INVOKABLE QString moveCard(const QString &c, int fromCol, int fromIdx,
                                 int toCol, int toIdx) const
    { return KanbanData::moveCard(c, fromCol, fromIdx, toCol, toIdx); }
    Q_INVOKABLE QString setCard(const QString &c, int col, int idx,
                                const QString &title, bool done,
                                const QStringList &labels, const QString &due,
                                const QString &description) const
    { return KanbanData::setCard(c, col, idx, title, done, labels, due, description); }
};

#endif // KANBANDATA_H
