// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
#include "kanbandata.h"

#include <QRegularExpression>

namespace {
const QString kCalendar = QString::fromUtf8("\xF0\x9F\x93\x85"); // 📅

// A `#label` token is recognized only at a token boundary: the start of the
// card text or right after whitespace. Anything else — most importantly a URL
// fragment such as https://example.com/#intro — stays part of the title. A
// run of backslashes may precede the hash; an odd-length run escapes it into a
// literal hash, and the run itself halves (the usual escaping convention), so
// `\#` is the literal `#` and `\\#tag` is a backslash followed by the label.
const QRegularExpression &labelRe()
{
    static const QRegularExpression re(
        QStringLiteral("(^|\\s)(\\\\*)#([^\\s#]*)"));
    return re;
}

// One deferred text replacement, applied back to front so earlier offsets stay
// valid.
struct Edit {
    qsizetype start;
    qsizetype length;
    QString replacement;
};

void applyEdits(QString &text, const QList<Edit> &edits)
{
    for (int i = edits.size() - 1; i >= 0; --i)
        text.replace(edits[i].start, edits[i].length, edits[i].replacement);
}

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

    // Labels, collected in order and removed from the title.
    QRegularExpressionMatchIterator it = labelRe().globalMatch(body);
    QList<Edit> edits;
    while (it.hasNext()) {
        const QRegularExpressionMatch m = it.next();
        const qsizetype slashes = m.capturedLength(2);
        const QString label = m.captured(3);
        // The escape run always halves, whatever follows it.
        const QString keptSlashes(slashes / 2, QLatin1Char('\\'));
        if (slashes % 2 == 1 || label.isEmpty()) {
            // Escaped, or a bare `#` with no label text: keep the hash.
            edits.append({ m.capturedStart(2), slashes, keptSlashes });
            continue;
        }
        card.labels.append(label);
        edits.append({ m.capturedStart(2),
                       m.capturedEnd(0) - m.capturedStart(2), keptSlashes });
    }
    applyEdits(body, edits);

    card.title = body.simplified();
}

// Inverse of the label rule above: a hash that would be read back as a label
// gets escaped so the title survives a round trip as text.
QString escapeTitle(const QString &title)
{
    QRegularExpressionMatchIterator it = labelRe().globalMatch(title);
    QList<Edit> edits;
    while (it.hasNext()) {
        const QRegularExpressionMatch m = it.next();
        const qsizetype slashes = m.capturedLength(2);
        edits.append({ m.capturedStart(2), slashes + 1,
                       QString(slashes * 2, QLatin1Char('\\'))
                           + QStringLiteral("\\#") });
    }
    QString out = title;
    applyEdits(out, edits);
    return out;
}

bool isBlank(const QString &line)
{
    return line.trimmed().isEmpty();
}

// The trailing run of blank lines at an insertion point belongs after whatever
// is appended next: a board whose source ends in a newline should still end in
// one, rather than growing a blank line before every card that gets added.
QStringList takeTrailingBlanks(QStringList &trivia)
{
    int keep = trivia.size();
    while (keep > 0 && isBlank(trivia[keep - 1]))
        --keep;
    QStringList blanks;
    while (trivia.size() > keep)
        blanks.prepend(trivia.takeLast());
    return blanks;
}

// Where an append at the end of the board lands, and where trivia orphaned by
// a removal re-anchors. Valid only until the board is mutated again.
QStringList *lastTriviaSlot(KanbanData::Board &b)
{
    if (b.columns.isEmpty())
        return &b.preamble;
    KanbanData::Column &col = b.columns.last();
    return col.cards.isEmpty() ? &col.leadingTrivia
                               : &col.cards.last().trailingTrivia;
}

// The trivia slot immediately preceding column `col`.
QStringList *triviaSlotBefore(KanbanData::Board &b, int col)
{
    if (col <= 0)
        return &b.preamble;
    KanbanData::Column &prev = b.columns[col - 1];
    return prev.cards.isEmpty() ? &prev.leadingTrivia
                                : &prev.cards.last().trailingTrivia;
}

// The trivia slot immediately preceding card `index` of column `col`.
QStringList *triviaSlotBefore(KanbanData::Column &col, int index)
{
    return index <= 0 ? &col.leadingTrivia
                      : &col.cards[index - 1].trailingTrivia;
}
} // namespace

namespace KanbanData {

Board parse(const QString &content)
{
    Board board;
    // Splitting "" yields one empty line, which would round-trip back out as a
    // spurious newline the moment anything is appended to an empty board.
    const QStringList lines = content.isEmpty() ? QStringList()
                                                : content.split(QLatin1Char('\n'));
    // Indices rather than pointers: appending to either list reallocates it.
    int colIdx = -1;      // current column
    int descIdx = -1;     // card still collecting description lines
    int lastCardIdx = -1; // last card seen, the anchor for trailing trivia

    static const QRegularExpression cardRe(
        QStringLiteral("^[-*] \\[( |x|X)\\] ?(.*)$"));

    // Every line the board model does not represent is kept verbatim, anchored
    // to the last thing before it, so that serialize() can put it back exactly
    // where it was.
    auto keepTrivia = [&](const QString &raw) {
        if (colIdx < 0)
            board.preamble.append(raw);
        else if (lastCardIdx < 0)
            board.columns[colIdx].leadingTrivia.append(raw);
        else
            board.columns[colIdx].cards[lastCardIdx].trailingTrivia.append(raw);
    };

    for (const QString &raw : lines) {
        if (raw.startsWith(QStringLiteral("## "))) {
            Column c;
            c.name = raw.mid(3).trimmed();
            c.rawHeader = raw;
            board.columns.append(c);
            colIdx = board.columns.size() - 1;
            descIdx = -1;
            lastCardIdx = -1;
            continue;
        }
        if (colIdx < 0) {
            keepTrivia(raw);
            continue;
        }
        const QRegularExpressionMatch cm = cardRe.match(raw.trimmed());
        // A card line has no leading indent (indent → description).
        const bool indented = raw.startsWith(QStringLiteral("  "))
                              || raw.startsWith(QStringLiteral("\t"));
        if (cm.hasMatch() && !indented) {
            Card c;
            c.done = cm.captured(1) != QStringLiteral(" ");
            c.rawLine = raw;
            parseCardBody(cm.captured(2), c);
            board.columns[colIdx].cards.append(c);
            descIdx = board.columns[colIdx].cards.size() - 1;
            lastCardIdx = descIdx;
            continue;
        }
        if (indented && descIdx >= 0) {
            Card &c = board.columns[colIdx].cards[descIdx];
            const QString text = raw.trimmed();
            c.description = c.description.isEmpty()
                ? text : c.description + QLatin1Char('\n') + text;
            c.rawDescription.append(raw);
            continue;
        }
        // Any other line ends the current card's description run, but is still
        // carried through as trivia.
        descIdx = -1;
        keepTrivia(raw);
    }
    return board;
}

QString serialize(const Board &b)
{
    QStringList out;
    out << b.preamble;
    for (const Column &col : b.columns) {
        out << (col.rawHeader.isEmpty() ? QStringLiteral("## ") + col.name
                                        : col.rawHeader);
        out << col.leadingTrivia;
        for (const Card &card : col.cards) {
            // A card the mutation did not touch keeps its source line intact,
            // so nothing the model normalizes away (spacing, `*` bullets,
            // label order) is rewritten behind the user's back.
            if (!card.rawLine.isEmpty()) {
                out << card.rawLine;
            } else {
                QString line = (card.done ? QStringLiteral("- [x] ")
                                          : QStringLiteral("- [ ] "))
                               + escapeTitle(card.title);
                for (const QString &label : card.labels)
                    line += QStringLiteral(" #") + label;
                if (!card.due.isEmpty())
                    line += QLatin1Char(' ') + kCalendar + QLatin1Char(' ')
                            + card.due;
                out << line;
            }
            if (!card.rawDescription.isEmpty()) {
                out << card.rawDescription;
            } else if (!card.description.isEmpty()) {
                for (const QString &d : card.description.split(QLatin1Char('\n')))
                    out << QStringLiteral("  ") + d;
            }
            out << card.trailingTrivia;
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
    Column c;
    c.name = name;
    c.leadingTrivia = takeTrailingBlanks(*lastTriviaSlot(b));
    b.columns.append(c);
    return serialize(b);
}

QString renameColumn(const QString &content, int col, const QString &name)
{
    Board b = parse(content);
    if (col < 0 || col >= b.columnCount())
        return content;
    b.columns[col].name = name;
    b.columns[col].rawHeader.clear(); // the header line is what changed
    return serialize(b);
}

QString removeColumn(const QString &content, int col)
{
    Board b = parse(content);
    if (col < 0 || col >= b.columnCount())
        return content;
    // The column's own header and card lines go, but the unmodelled lines
    // inside it re-anchor to the preceding position rather than disappearing.
    QStringList orphaned = b.columns[col].leadingTrivia;
    for (const Card &card : b.columns[col].cards)
        orphaned += card.trailingTrivia;
    b.columns.removeAt(col);
    if (!orphaned.isEmpty())
        *triviaSlotBefore(b, col) += orphaned;
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
    Column &c = b.columns[col];
    Card card;
    card.title = title;
    card.trailingTrivia = takeTrailingBlanks(
        *(c.cards.isEmpty() ? &c.leadingTrivia : &c.cards.last().trailingTrivia));
    c.cards.append(card);
    return serialize(b);
}

QString removeCard(const QString &content, int col, int index)
{
    Board b = parse(content);
    if (col < 0 || col >= b.columnCount()
        || index < 0 || index >= b.columns[col].cards.size())
        return content;
    Column &c = b.columns[col];
    const QStringList orphaned = c.cards[index].trailingTrivia;
    c.cards.removeAt(index);
    if (!orphaned.isEmpty())
        *triviaSlotBefore(c, index) += orphaned;
    return serialize(b);
}

QString toggleCardDone(const QString &content, int col, int index)
{
    Board b = parse(content);
    if (col < 0 || col >= b.columnCount()
        || index < 0 || index >= b.columns[col].cards.size())
        return content;
    Card &card = b.columns[col].cards[index];
    card.done = !card.done;
    // Edit the checkbox in place instead of dropping the source line, so the
    // rest of the card — bullet style, spacing, label order — is untouched.
    static const QRegularExpression boxRe(
        QStringLiteral("^(\\s*[-*] \\[)( |x|X)(\\])"));
    const QRegularExpressionMatch m = boxRe.match(card.rawLine);
    if (m.hasMatch()) {
        card.rawLine.replace(m.capturedStart(2), m.capturedLength(2),
                             card.done ? QStringLiteral("x")
                                       : QStringLiteral(" "));
    } else {
        card.rawLine.clear();
    }
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
    // Trivia is a property of the position, not of the card, so the lines that
    // followed the card stay where they were and the card travels without them.
    const QStringList orphaned = card.trailingTrivia;
    card.trailingTrivia.clear();
    if (!orphaned.isEmpty())
        *triviaSlotBefore(b.columns[fromCol], fromIndex) += orphaned;
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
    // Every modelled field is overwritten, so the source line and description
    // lines have to be rebuilt from them.
    c.rawLine.clear();
    c.rawDescription.clear();
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
