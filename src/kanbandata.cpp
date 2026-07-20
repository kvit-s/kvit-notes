// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
#include "kanbandata.h"

#include <QRegularExpression>

namespace {
const QString kCalendar = QString::fromUtf8("\xF0\x9F\x93\x85"); // 📅

// Parse a card line's text into title / labels / due date.
void parseCardBody(const QString &rest, KanbanData::Card &card)
{
    QString body = rest;

    // Due date (📅 <date>).
    static const QRegularExpression dueRe(
        kCalendar + QStringLiteral("\\s*(\\d{4}-\\d{2}-\\d{2})"));
    const QRegularExpressionMatch dm = dueRe.match(body);
    if (dm.hasMatch()) {
        card.due = dm.captured(1);
        body.remove(dm.capturedStart(0), dm.capturedLength(0));
    }

    // Labels (#word). Collected in order, removed from the title.
    static const QRegularExpression labelRe(QStringLiteral("#([^\\s#]+)"));
    QRegularExpressionMatchIterator it = labelRe.globalMatch(body);
    QList<QPair<int, int>> spans;
    while (it.hasNext()) {
        const QRegularExpressionMatch m = it.next();
        card.labels.append(m.captured(1));
        spans.append({m.capturedStart(0), m.capturedLength(0)});
    }
    for (int i = spans.size() - 1; i >= 0; --i)
        body.remove(spans[i].first, spans[i].second);

    card.title = body.simplified();
}
} // namespace

namespace KanbanData {

Board parse(const QString &content)
{
    Board board;
    const QStringList lines = content.split(QLatin1Char('\n'));
    Column *col = nullptr;
    Card *card = nullptr;

    static const QRegularExpression cardRe(
        QStringLiteral("^[-*] \\[( |x|X)\\] ?(.*)$"));

    for (const QString &raw : lines) {
        if (raw.startsWith(QStringLiteral("## "))) {
            board.columns.append(Column{ raw.mid(3).trimmed(), {} });
            col = &board.columns.last();
            card = nullptr;
            continue;
        }
        if (!col) {
            // Content before the first column header is ignored (an empty
            // board renders the add-column affordance).
            continue;
        }
        const QRegularExpressionMatch cm = cardRe.match(raw.trimmed());
        // A card line has no leading indent (indent → description).
        const bool indented = raw.startsWith(QStringLiteral("  "))
                              || raw.startsWith(QStringLiteral("\t"));
        if (cm.hasMatch() && !indented) {
            Card c;
            c.done = cm.captured(1) != QStringLiteral(" ");
            parseCardBody(cm.captured(2), c);
            col->cards.append(c);
            card = &col->cards.last();
            continue;
        }
        if (indented && card) {
            const QString text = raw.trimmed();
            card->description = card->description.isEmpty()
                ? text : card->description + QLatin1Char('\n') + text;
            continue;
        }
        // Any other line ends the current card's description run.
        card = nullptr;
    }
    return board;
}

QString serialize(const Board &b)
{
    QStringList out;
    for (const Column &col : b.columns) {
        out << QStringLiteral("## ") + col.name;
        for (const Card &card : col.cards) {
            QString line = (card.done ? QStringLiteral("- [x] ")
                                      : QStringLiteral("- [ ] ")) + card.title;
            for (const QString &label : card.labels)
                line += QStringLiteral(" #") + label;
            if (!card.due.isEmpty())
                line += QLatin1Char(' ') + kCalendar + QLatin1Char(' ') + card.due;
            out << line;
            if (!card.description.isEmpty()) {
                for (const QString &d : card.description.split(QLatin1Char('\n')))
                    out << QStringLiteral("  ") + d;
            }
        }
    }
    return out.join(QLatin1Char('\n'));
}

bool looksLikeBoard(const QString &content)
{
    // Any `## ` header makes it a board (an empty board is still a board).
    for (const QString &line : content.split(QLatin1Char('\n'))) {
        if (line.startsWith(QStringLiteral("## ")))
            return true;
    }
    return false;
}

QString addColumn(const QString &content, const QString &name)
{
    Board b = parse(content);
    b.columns.append(Column{ name, {} });
    return serialize(b);
}

QString renameColumn(const QString &content, int col, const QString &name)
{
    Board b = parse(content);
    if (col < 0 || col >= b.columnCount())
        return content;
    b.columns[col].name = name;
    return serialize(b);
}

QString removeColumn(const QString &content, int col)
{
    Board b = parse(content);
    if (col < 0 || col >= b.columnCount())
        return content;
    b.columns.removeAt(col);
    return serialize(b);
}

QString moveColumn(const QString &content, int fromCol, int toCol)
{
    Board b = parse(content);
    if (fromCol < 0 || fromCol >= b.columnCount()
        || toCol < 0 || toCol >= b.columnCount())
        return content;
    b.columns.move(fromCol, toCol);
    return serialize(b);
}

QString addCard(const QString &content, int col, const QString &title)
{
    Board b = parse(content);
    if (col < 0 || col >= b.columnCount())
        return content;
    Card c;
    c.title = title;
    b.columns[col].cards.append(c);
    return serialize(b);
}

QString removeCard(const QString &content, int col, int index)
{
    Board b = parse(content);
    if (col < 0 || col >= b.columnCount()
        || index < 0 || index >= b.columns[col].cards.size())
        return content;
    b.columns[col].cards.removeAt(index);
    return serialize(b);
}

QString toggleCardDone(const QString &content, int col, int index)
{
    Board b = parse(content);
    if (col < 0 || col >= b.columnCount()
        || index < 0 || index >= b.columns[col].cards.size())
        return content;
    b.columns[col].cards[index].done = !b.columns[col].cards[index].done;
    return serialize(b);
}

QString moveCard(const QString &content, int fromCol, int fromIndex,
                 int toCol, int toIndex)
{
    Board b = parse(content);
    if (fromCol < 0 || fromCol >= b.columnCount()
        || toCol < 0 || toCol >= b.columnCount()
        || fromIndex < 0 || fromIndex >= b.columns[fromCol].cards.size())
        return content;
    Card card = b.columns[fromCol].cards.takeAt(fromIndex);
    // toIndex names the insert-before slot in the column's ORIGINAL card
    // order. Removing the card first shifts every later slot in the same
    // column down by one, so a downward same-column move must compensate;
    // cross-column and upward moves need no adjustment.
    int dest = toIndex;
    if (fromCol == toCol && fromIndex < toIndex)
        --dest;
    dest = qBound(0, dest, b.columns[toCol].cards.size());
    b.columns[toCol].cards.insert(dest, card);
    return serialize(b);
}

QString setCard(const QString &content, int col, int index,
                const QString &title, bool done, const QStringList &labels,
                const QString &due, const QString &description)
{
    Board b = parse(content);
    if (col < 0 || col >= b.columnCount()
        || index < 0 || index >= b.columns[col].cards.size())
        return content;
    Card &c = b.columns[col].cards[index];
    c.title = title;
    c.done = done;
    c.labels = labels;
    c.due = due;
    c.description = description;
    return serialize(b);
}

} // namespace KanbanData

// ---- QML wrapper ----

QVariantMap KanbanTools::parse(const QString &content) const
{
    const KanbanData::Board b = KanbanData::parse(content);
    QVariantList columns;
    for (const KanbanData::Column &col : b.columns) {
        QVariantList cards;
        for (const KanbanData::Card &card : col.cards) {
            cards.append(QVariantMap{
                { QStringLiteral("title"), card.title },
                { QStringLiteral("done"), card.done },
                { QStringLiteral("labels"), QVariant(QStringList(card.labels)) },
                { QStringLiteral("due"), card.due },
                { QStringLiteral("description"), card.description },
            });
        }
        columns.append(QVariantMap{
            { QStringLiteral("name"), col.name },
            { QStringLiteral("cards"), cards },
        });
    }
    return { { QStringLiteral("columns"), columns } };
}
