// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
#include "kanbandata.h"

#include <QDate>
#include <QRegularExpression>

#include <algorithm>

namespace {
const QString kCalendar = QString::fromUtf8("\xF0\x9F\x93\x85"); // 📅

// A `#label` token is recognized only at a token boundary: the start of the
// card text or right after whitespace. Anything else — most importantly a URL
// fragment such as https://example.com/#intro — stays part of the title. A
// run of backslashes may precede the hash; an odd-length run escapes it into a
// literal hash, and the run itself halves (the usual escaping convention), so
// `\#` is the literal `#` and `\\#tag` is a backslash followed by the label.
//
// The label itself comes in two spellings. The bare one — everything up to the
// next space or hash — is what boards written by hand and by every earlier
// version of Kvit contain, and it still reads exactly as it did. It cannot
// express a label containing a space or a hash, so `client work` written bare
// comes back as the label `client` plus the title word `work`. The quoted
// spelling `#"client work"` closes that gap: inside it a backslash escapes the
// next character, so `"` and `\` survive too. serialize() writes the bare form
// whenever it fits, so ordinary boards keep their ordinary syntax.
const QRegularExpression &labelRe()
{
    static const QRegularExpression re(
        QStringLiteral("(^|\\s)(\\\\*)#(?:\"((?:\\\\.|[^\"\\\\])*)\""
                       "|([^\\s#]*))"));
    return re;
}

// The due-date marker, with the same backslash-escape convention as the hash:
// a title that genuinely reads "📅 2026-07-15" is written with the marker
// escaped, so it stays title text instead of being read back as the due date.
const QRegularExpression &dueRe()
{
    static const QRegularExpression re(
        QStringLiteral("(\\\\*)") + kCalendar
        + QStringLiteral("\\s*(\\d{4}-\\d{2}-\\d{2})"));
    return re;
}

// A date the calendar has: 2026-02-30 has the shape but not the day. Reader
// and writer both ask this, so what serialize() is willing to write after the
// marker is exactly what parseCardBody() reads back from it — otherwise a
// value that survived one direction would be dropped in the other.
bool isRealDate(const QString &text)
{
    return QDate::fromString(text, QStringLiteral("yyyy-MM-dd")).isValid();
}

// Undo the escaping writeLabel() applies inside a quoted label: the two
// characters it escapes, and no others, so a hand-written `#"a\b"` keeps its
// backslash rather than losing it to an escape nobody wrote.
QString unescapeLabel(const QString &text)
{
    QString out;
    out.reserve(text.size());
    for (int i = 0; i < text.size(); ++i) {
        const QChar c = text.at(i);
        if (c == u'\\' && i + 1 < text.size()
            && (text.at(i + 1) == u'\\' || text.at(i + 1) == u'"')) {
            out.append(text.at(++i));
            continue;
        }
        out.append(c);
    }
    return out;
}

// A label as it goes on the card line, without the leading hash. Empty for a
// label that cannot be written at all (the empty string), which serialize()
// then omits.
QString writeLabel(const QString &label)
{
    if (label.isEmpty())
        return QString();
    bool bare = true;
    for (const QChar c : label) {
        if (c.isSpace() || c == u'#' || c == u'"' || c == u'\\') {
            bare = false;
            break;
        }
    }
    if (bare)
        return label;
    QString out;
    out.reserve(label.size() + 2);
    out.append(u'"');
    for (const QChar c : label) {
        if (c == u'"' || c == u'\\')
            out.append(u'\\');
        out.append(c);
    }
    out.append(u'"');
    return out;
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

    // Due date (📅 <date>). The first unescaped marker is the card's; an
    // escaped one keeps its text and halves its backslash run, exactly as the
    // hash does.
    QList<Edit> edits;
    QRegularExpressionMatchIterator dueIt = dueRe().globalMatch(body);
    while (dueIt.hasNext()) {
        const QRegularExpressionMatch m = dueIt.next();
        const qsizetype slashes = m.capturedLength(1);
        const QString keptSlashes(slashes / 2, QLatin1Char('\\'));
        if (slashes % 2 == 1) {
            edits.append({ m.capturedStart(1), slashes, keptSlashes });
            continue;
        }
        if (!card.due.isEmpty() || !isRealDate(m.captured(2)))
            continue;   // later or unreal dates stay part of the title
        card.due = m.captured(2);
        edits.append({ m.capturedStart(1),
                       m.capturedEnd(0) - m.capturedStart(1), keptSlashes });
    }
    applyEdits(body, edits);
    edits.clear();

    // Labels, collected in order and removed from the title.
    QRegularExpressionMatchIterator it = labelRe().globalMatch(body);
    while (it.hasNext()) {
        const QRegularExpressionMatch m = it.next();
        const qsizetype slashes = m.capturedLength(2);
        const QString label = m.hasCaptured(3) ? unescapeLabel(m.captured(3))
                                               : m.captured(4);
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

// Inverse of the rules above: a hash or a due marker that would be read back
// as structure gets escaped so the title survives a round trip as text.
// Doubling the run and adding one backslash leaves an odd run, which is what
// parseCardBody reads as "literal", and halving there restores the original.
QString escapeTitle(const QString &title)
{
    QList<Edit> edits;
    QRegularExpressionMatchIterator dueIt = dueRe().globalMatch(title);
    while (dueIt.hasNext()) {
        const QRegularExpressionMatch m = dueIt.next();
        const qsizetype slashes = m.capturedLength(1);
        if (!isRealDate(m.captured(2)))
            continue;   // the reader leaves this as text, so leave it alone
        edits.append({ m.capturedStart(1), slashes + kCalendar.size(),
                       QString(slashes * 2, QLatin1Char('\\'))
                           + QLatin1Char('\\') + kCalendar });
    }
    QRegularExpressionMatchIterator it = labelRe().globalMatch(title);
    while (it.hasNext()) {
        const QRegularExpressionMatch m = it.next();
        const qsizetype slashes = m.capturedLength(2);
        edits.append({ m.capturedStart(2), slashes + 1,
                       QString(slashes * 2, QLatin1Char('\\'))
                           + QStringLiteral("\\#") });
    }
    // applyEdits works back to front, so the two disjoint sets have to arrive
    // in source order.
    std::sort(edits.begin(), edits.end(),
              [](const Edit &a, const Edit &b) { return a.start < b.start; });
    QString out = title;
    applyEdits(out, edits);
    return out;
}

// A card line, with whatever indent, bullet and checkbox it was written with:
// group 1 is that prefix and group 2 the single space that may follow it.
// Everything after is the card's text.
const QRegularExpression &cardPrefixRe()
{
    static const QRegularExpression re(
        QStringLiteral("^(\\s*[-*] \\[[ xX]\\])( ?)"));
    return re;
}

// The comment carrying a card's dates, at the end of its line. It is matched
// off the end of the line before anything else looks at the text, so nothing
// inside it can be read as a label, a due date or title words.
const QRegularExpression &stampRe()
{
    static const QRegularExpression re(
        QStringLiteral("\\s*<!--kvit((?:\\s+\\w+=[0-9-]+)*)\\s*-->\\s*$"));
    return re;
}

// One `name=value` out of that comment.
QString stampValue(const QString &fields, const QString &name)
{
    const QRegularExpression re(
        name + QStringLiteral("=(\\d{4}-\\d{2}-\\d{2})"));
    const QRegularExpressionMatch m = re.match(fields);
    return m.hasMatch() && isRealDate(m.captured(1)) ? m.captured(1)
                                                     : QString();
}

// The fields of that comment this version does not interpret, verbatim and in
// source order. Everything in the comment is rewritten from the model on the
// next edit, so anything not carried here is deleted by an edit that had
// nothing to do with it.
QString stampExtraFields(const QString &fields)
{
    static const QRegularExpression fieldRe(QStringLiteral("(\\w+)=([0-9-]+)"));
    QStringList kept;
    QRegularExpressionMatchIterator it = fieldRe.globalMatch(fields);
    while (it.hasNext()) {
        const QRegularExpressionMatch m = it.next();
        const QString name = m.captured(1);
        if (name != QLatin1String("created")
            && name != QLatin1String("modified")) {
            kept << m.captured(0);
        }
    }
    return kept.join(QLatin1Char(' '));
}

// The run of spaces and tabs a line starts with.
QString leadingIndent(const QString &line)
{
    int i = 0;
    while (i < line.size()
           && (line.at(i) == QLatin1Char(' ') || line.at(i) == QLatin1Char('\t')))
        ++i;
    return line.left(i);
}

// Everything before that comment, which is the card's text.
QString withoutStamp(const QString &line)
{
    const QRegularExpressionMatch m = stampRe().match(line);
    return m.hasMatch() ? line.left(m.capturedStart(0)) : line;
}

// The comment for a card that has dates, and nothing at all for one that does
// not: an untouched board grows no comments. `modified` is written only when
// it differs from `created`, so a card that was added and left alone carries
// one date rather than the same date twice.
QString stampComment(const KanbanData::Card &card)
{
    QStringList fields;
    if (!card.created.isEmpty())
        fields << QStringLiteral("created=") + card.created;
    if (!card.modified.isEmpty() && card.modified != card.created)
        fields << QStringLiteral("modified=") + card.modified;
    // Whatever else the comment held goes back in. See Card::stampExtras.
    if (!card.stampExtras.isEmpty())
        fields << card.stampExtras;
    if (fields.isEmpty())
        return QString();
    return QStringLiteral(" <!--kvit ") + fields.join(QLatin1Char(' '))
         + QStringLiteral("-->");
}

// Record that this card changed today: the day it was added if it has no such
// day yet, and the day it was last changed either way. An empty `today` is a
// caller that does not keep dates, and nothing is recorded for it.
//
// The card's source line is edited in place rather than dropped, so a card
// whose dates changed keeps the spacing, the bullet and the label order it was
// written with — the same reason toggleCardDone() edits only the checkbox.
void stampCard(KanbanData::Card &card, const QString &today, bool creating)
{
    if (today.isEmpty() || !isRealDate(today))
        return;
    if (creating && card.created.isEmpty())
        card.created = today;
    card.modified = today;
    if (!card.rawLine.isEmpty())
        card.rawLine = withoutStamp(card.rawLine) + stampComment(card);
}

// The card's text as the fields describe it: what serialize() writes for a
// card with no source line of its own.
QString renderedCardBody(const KanbanData::Card &card)
{
    QString body = escapeTitle(card.title);
    for (const QString &label : card.labels) {
        const QString token = writeLabel(label);
        if (!token.isEmpty())
            body += QStringLiteral(" #") + token;
    }
    // Only a real calendar date goes after the marker: the storage grammar
    // reads nothing else back, so writing "📅 tomorrow" would silently move
    // the text into the title and clear the field. setCard() rejects such a
    // value at the boundary; this is the invariant restated where the line is
    // built.
    if (isRealDate(card.due))
        body += QLatin1Char(' ') + kCalendar + QLatin1Char(' ') + card.due;
    return body;
}

// The card's text as the file has it: everything on its line after the
// checkbox and before the comment carrying its dates. A card a mutation
// synthesized has no line yet, so its text is rendered from the fields
// instead — which is the same string serialize() is about to write.
QString cardBody(const KanbanData::Card &card)
{
    if (card.rawLine.isEmpty())
        return renderedCardBody(card);
    const QRegularExpressionMatch m = cardPrefixRe().match(card.rawLine);
    return withoutStamp(m.hasMatch() ? card.rawLine.mid(m.capturedEnd(0))
                                     : card.rawLine);
}

// What goes in front of that text when it is rewritten: the card's own
// indent, bullet and checkbox, so a `*` bullet or an indented card stays what
// it was and the done state survives an edit of the text beside it.
QString cardPrefix(const KanbanData::Card &card)
{
    const QRegularExpressionMatch m = cardPrefixRe().match(card.rawLine);
    if (m.hasMatch())
        return m.captured(1) + QLatin1Char(' ');
    return card.done ? QStringLiteral("- [x] ") : QStringLiteral("- [ ] ");
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

bool isValidDue(const QString &due)
{
    // The stored form is the todo convention's ISO date and nothing else. The
    // same test the parser applies, so a value this accepts is a value the
    // next parse reads back.
    return isRealDate(due);
}

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

    // A blank line inside a description is part of it — a description with a
    // paragraph break is ordinary prose — but a blank line after the last one
    // is what separates this card from the next. The two are the same line and
    // only what follows tells them apart, so blanks are held here until an
    // indented line claims them for the description or something else sends
    // them to trivia. Ending the run on the first blank, which is what used to
    // happen, cut every such description in half.
    QStringList pendingBlanks;
    auto flushBlanksToTrivia = [&]() {
        for (const QString &blank : pendingBlanks)
            keepTrivia(blank);
        pendingBlanks.clear();
    };

    for (const QString &raw : lines) {
        if (descIdx >= 0 && raw.trimmed().isEmpty()) {
            pendingBlanks.append(raw);
            continue;
        }
        if (raw.startsWith(QStringLiteral("## "))) {
            flushBlanksToTrivia();
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
            flushBlanksToTrivia();
            keepTrivia(raw);
            continue;
        }
        const QRegularExpressionMatch cm = cardRe.match(raw.trimmed());
        // A card line has no leading indent (indent → description).
        const bool indented = raw.startsWith(QStringLiteral("  "))
                              || raw.startsWith(QStringLiteral("\t"));
        if (cm.hasMatch() && !indented) {
            flushBlanksToTrivia();
            Card c;
            c.done = cm.captured(1) != QStringLiteral(" ");
            c.rawLine = raw;
            // The dates come off the end of the line first, so what is read
            // as title, labels and due date is only what the reader typed.
            QString body = cm.captured(2);
            const QRegularExpressionMatch sm = stampRe().match(body);
            if (sm.hasMatch()) {
                c.created = stampValue(sm.captured(1), QStringLiteral("created"));
                c.modified = stampValue(sm.captured(1), QStringLiteral("modified"));
                if (c.modified.isEmpty())
                    c.modified = c.created;
                c.stampExtras = stampExtraFields(sm.captured(1));
                body = body.left(sm.capturedStart(0));
            }
            parseCardBody(body, c);
            board.columns[colIdx].cards.append(c);
            descIdx = board.columns[colIdx].cards.size() - 1;
            lastCardIdx = descIdx;
            continue;
        }
        if (indented && descIdx >= 0) {
            Card &c = board.columns[colIdx].cards[descIdx];
            if (!pendingBlanks.isEmpty() && c.rawDescription.isEmpty()) {
                // A blank line between the card and this one means this is not
                // the card's description: a description starts on the line
                // after its card. Unchanged from before, and the answer that
                // keeps the blank where it was written — trivia is emitted
                // after the description, so absorbing this line would move the
                // blank past it.
                flushBlanksToTrivia();
                descIdx = -1;
                keepTrivia(raw);
                continue;
            }
            // The blanks held back are inside this description after all.
            for (const QString &blank : pendingBlanks) {
                c.description += QLatin1Char('\n');
                c.rawDescription.append(blank);
            }
            pendingBlanks.clear();
            if (c.rawDescription.isEmpty())
                c.descriptionIndent = leadingIndent(raw);
            // Only the description's own indent comes off. Trimming every line
            // flattened whatever was written inside one — a nested list, an
            // indented quotation — into a run of top-level lines, and the next
            // edit through the card editor wrote that flattened form back to
            // the file.
            QString text = raw;
            if (!c.descriptionIndent.isEmpty()
                && text.startsWith(c.descriptionIndent))
                text.remove(0, c.descriptionIndent.size());
            else
                text = text.trimmed();
            c.description = c.description.isEmpty()
                ? text : c.description + QLatin1Char('\n') + text;
            c.rawDescription.append(raw);
            continue;
        }
        // Any other line ends the current card's description run, but is still
        // carried through as trivia.
        flushBlanksToTrivia();
        descIdx = -1;
        keepTrivia(raw);
    }
    flushBlanksToTrivia();
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
                out << (card.done ? QStringLiteral("- [x] ")
                                  : QStringLiteral("- [ ] "))
                        + renderedCardBody(card) + stampComment(card);
            }
            if (!card.rawDescription.isEmpty()) {
                out << card.rawDescription;
            } else if (!card.description.isEmpty()) {
                // The indent the description was read with, so a description
                // rewritten from the field lands where the old one was; two
                // spaces for one the editor invented.
                const QString indent = card.descriptionIndent.isEmpty()
                    ? QStringLiteral("  ") : card.descriptionIndent;
                for (const QString &d : card.description.split(QLatin1Char('\n')))
                    out << (d.isEmpty() ? QString() : indent + d);
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

QString addCard(const QString &content, int col, const QString &title,
                const QString &today)
{
    Board b = parse(content);
    if (col < 0 || col >= b.columnCount())
        return content;
    Column &c = b.columns[col];
    Card card;
    card.title = title;
    stampCard(card, today, true);
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

QString toggleCardDone(const QString &content, int col, int index,
                       const QString &today)
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
    stampCard(card, today, false);
    return serialize(b);
}

QString moveCard(const QString &content, int fromCol, int fromIndex,
                 int toCol, int toIndex, const QString &today)
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
    // Carrying a card to another column says something about it; sliding it
    // up or down inside the one it is already in does not.
    if (fromCol != toCol)
        stampCard(card, today, false);
    b.columns[toCol].cards.insert(dest, card);
    return serialize(b);
}

QString setCardLine(const QString &content, int col, int index,
                    const QString &text, const QString &today)
{
    Board b = parse(content);
    if (col < 0 || col >= b.columnCount()
        || index < 0 || index >= b.columns[col].cards.size())
        return content;
    Card &c = b.columns[col].cards[index];
    QString body = text;
    // One card is one line: a break typed into it would open a second card or
    // a description line the next time the board is read.
    body.replace(QLatin1Char('\n'), QLatin1Char(' '));
    // The dates the card already carries go back on the end of the rebuilt
    // line. They are not the reader's text and were never in what was typed,
    // so writing only the typed part would quietly forget them.
    c.rawLine = cardPrefix(c) + body + stampComment(c);
    // The line is where the title, the labels and the due date come from, so
    // read them back out of it rather than leaving the board describing text
    // it no longer holds.
    c.title.clear();
    c.labels.clear();
    c.due.clear();
    parseCardBody(body, c);
    stampCard(c, today, false);
    return serialize(b);
}

QString setCardDescription(const QString &content, int col, int index,
                           const QString &text, const QString &today)
{
    Board b = parse(content);
    if (col < 0 || col >= b.columnCount()
        || index < 0 || index >= b.columns[col].cards.size())
        return content;
    Card &c = b.columns[col].cards[index];
    c.description = text;
    // The description was just written from the field, so the source lines it
    // used to have are gone and serialize() renders it from the text: one
    // indented line per line, which is what the next parse reads back.
    c.rawDescription.clear();
    stampCard(c, today, false);
    return serialize(b);
}

QString setCard(const QString &content, int col, int index,
                const QString &title, bool done, const QStringList &labels,
                const QString &due, const QString &description,
                const QString &today)
{
    Board b = parse(content);
    if (col < 0 || col >= b.columnCount()
        || index < 0 || index >= b.columns[col].cards.size())
        return content;
    Card &c = b.columns[col].cards[index];
    c.title = title;
    c.done = done;
    // Normalize what the editor hands over to what the storage grammar can
    // hold, so the board in memory is the board the next parse produces. An
    // empty label has no `#token` to write, and a due value that is not an ISO
    // date has nowhere to go: keeping either would make the card read back
    // differently from the card just saved. isValidDue() is public so the
    // editor can refuse the date before the user loses it.
    c.labels.clear();
    for (const QString &label : labels) {
        if (!label.isEmpty())
            c.labels.append(label);
    }
    c.due = isValidDue(due) ? due : QString();
    c.description = description;
    // Every modelled field is overwritten, so the source line and description
    // lines have to be rebuilt from them.
    c.rawLine.clear();
    c.rawDescription.clear();
    stampCard(c, today, false);
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
                // The line's own text, which is what the inline editor edits.
                { QStringLiteral("line"), cardBody(card) },
                { QStringLiteral("created"), card.created },
                { QStringLiteral("modified"), card.modified },
            });
        }
        columns.append(QVariantMap{
            { QStringLiteral("name"), col.name },
            { QStringLiteral("cards"), cards },
        });
    }
    return { { QStringLiteral("columns"), columns } };
}
