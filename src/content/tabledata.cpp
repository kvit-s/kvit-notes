// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
#include "tabledata.h"

#include <QRegularExpression>
#include <algorithm>

namespace {

// A line break inside a cell, as markdown can carry it: <br>. A table row is
// one line of the file, so a real newline in a cell would end the row; the tag
// is what GitHub, Obsidian and Pandoc-to-HTML all render as a break, and what
// an LLM already writes when it wants one. A reader that handles no inline
// HTML at all shows the literal tag, which is the accepted cost of the
// convention.
//
// Whitespace immediately BEFORE the tag belongs to it — that is the padding an
// author writes in "a <br> b" — and goes away with the break. Whitespace AFTER
// it is content: it is where an indented line inside the cell carries its
// indentation, so a code listing in a cell keeps its shape.
QString breaksToNewlines(const QString &cell)
{
    static const QRegularExpression brRe(
        QStringLiteral("[ \\t]*<br\\s*/?>"),
        QRegularExpression::CaseInsensitiveOption);
    QString s = cell;
    s.replace(brRe, QStringLiteral("\n"));
    return s;
}

// Split a table row on unescaped '|', dropping the optional leading/trailing
// border pipes, unescaping \| → | and trimming each cell.
QStringList splitCells(const QString &line)
{
    const QString s = line.trimmed();
    QStringList cells;
    QString cur;
    // Trimming happens before the breaks are restored, so the padding around
    // the pipes is what goes, never the indentation of a line within a cell.
    const auto flush = [&cells, &cur] {
        cells.append(breaksToNewlines(cur.trimmed()));
        cur.clear();
    };
    for (int i = 0; i < s.length(); ++i) {
        if (s[i] == '\\' && i + 1 < s.length() && s[i + 1] == '|') {
            cur += '|';
            ++i;
            continue;
        }
        if (s[i] == '|') {
            flush();
            continue;
        }
        cur += s[i];
    }
    flush();
    // Drop empty cells produced by the optional leading/trailing border pipe.
    if (cells.size() > 1 && cells.first().isEmpty())
        cells.removeFirst();
    if (cells.size() > 1 && cells.last().isEmpty())
        cells.removeLast();
    return cells;
}

QString escapeCell(const QString &cell)
{
    QString e = cell;
    e.replace(QStringLiteral("|"), QStringLiteral("\\|"));
    e.replace(QStringLiteral("\n"), QStringLiteral("<br>"));
    return e;
}

QString alignmentMarker(TableData::Align a)
{
    switch (a) {
    case TableData::Align::Left:   return QStringLiteral(":---");
    case TableData::Align::Center: return QStringLiteral(":---:");
    case TableData::Align::Right:  return QStringLiteral("---:");
    case TableData::Align::None:   break;
    }
    return QStringLiteral("---");
}

TableData::Align alignmentOf(const QString &delimCell)
{
    const QString c = delimCell.trimmed();
    const bool left = c.startsWith(':');
    const bool right = c.endsWith(':');
    if (left && right) return TableData::Align::Center;
    if (left)  return TableData::Align::Left;
    if (right) return TableData::Align::Right;
    return TableData::Align::None;
}

} // namespace

namespace TableData {

bool isDelimiterRow(const QString &line)
{
    const QStringList cells = splitCells(line);
    if (cells.isEmpty())
        return false;
    static const QRegularExpression re(QStringLiteral("^:?-+:?$"));
    for (const QString &c : cells) {
        if (!re.match(c.trimmed()).hasMatch())
            return false;
    }
    return true;
}

bool looksLikeTableStart(const QString &headerLine, const QString &delimiterLine)
{
    // A header row must contain a pipe (else a lone "---" delimiter would
    // masquerade as a one-column table), and the second line must be a
    // delimiter with the same column count.
    if (!headerLine.contains('|'))
        return false;
    if (!isDelimiterRow(delimiterLine))
        return false;
    return splitCells(headerLine).size() == splitCells(delimiterLine).size();
}

int cellCount(const QString &line)
{
    return splitCells(line).size();
}

Table parse(const QString &markdown)
{
    Table t;
    const QStringList lines = markdown.split('\n');
    if (lines.size() < 2)
        return t;
    if (!looksLikeTableStart(lines[0], lines[1]))
        return t;

    t.headers = splitCells(lines[0]);
    const int cols = t.headers.size();
    const QStringList delim = splitCells(lines[1]);
    for (int c = 0; c < cols; ++c)
        t.alignments.append(c < delim.size() ? alignmentOf(delim[c]) : Align::None);

    for (int i = 2; i < lines.size(); ++i) {
        if (lines[i].trimmed().isEmpty())
            continue;
        QStringList row = splitCells(lines[i]);
        while (row.size() < cols) row.append(QString());   // ragged → squared
        while (row.size() > cols) row.removeLast();
        t.rows.append(row);
    }
    t.valid = true;
    return t;
}

QString serialize(const Table &t)
{
    // Cells are trimmed on the way out because they are trimmed on the way in:
    // splitCells drops the padding around every cell, so writing a value that
    // still carries leading or trailing whitespace produces markdown that
    // parses back to something else. The live cell editor commits on every
    // keystroke and then re-reads the parsed cell, so an untrimmed write made
    // a space typed at the end of a cell vanish — the rewrite came straight
    // back trimmed. Only a second space stuck, because by then the serialized
    // markdown matched what was already stored and no rewrite was issued.
    //
    // On a multi-line cell this drops blank leading and trailing lines, and it
    // means the FIRST line of a cell cannot carry indentation — the padding
    // around the pipes and that indentation are the same characters, and the
    // parser has no way to tell them apart. Every line after the first keeps
    // its own.
    auto rowLine = [](const QStringList &cells) {
        QStringList escaped;
        for (const QString &c : cells)
            escaped.append(escapeCell(c.trimmed()));
        return QStringLiteral("| ") + escaped.join(QStringLiteral(" | "))
             + QStringLiteral(" |");
    };
    QStringList out;
    out << rowLine(t.headers);
    QStringList delim;
    for (Align a : t.alignments)
        delim.append(alignmentMarker(a));
    out << (QStringLiteral("| ") + delim.join(QStringLiteral(" | "))
            + QStringLiteral(" |"));
    for (const QStringList &row : t.rows)
        out << rowLine(row);
    return out.join('\n');
}

QString setCell(const QString &md, int row, int col, const QString &value)
{
    Table t = parse(md);
    if (!t.valid || col < 0 || col >= t.columnCount())
        return md;
    // Newlines are kept: serialize writes them back as <br>, so the row stays
    // one line of the file while the cell keeps its shape.
    const QString v = value;
    if (row == -1) {
        t.headers[col] = v;
    } else if (row >= 0 && row < t.rowCount()) {
        t.rows[row][col] = v;
    } else {
        return md;
    }
    return serialize(t);
}

QString insertRow(const QString &md, int afterRow)
{
    Table t = parse(md);
    if (!t.valid)
        return md;
    QStringList empty;
    for (int c = 0; c < t.columnCount(); ++c)
        empty.append(QString());
    int at = qBound(0, afterRow + 1, t.rowCount());
    t.rows.insert(at, empty);
    return serialize(t);
}

QString insertColumn(const QString &md, int afterCol)
{
    Table t = parse(md);
    if (!t.valid)
        return md;
    int at = qBound(0, afterCol + 1, t.columnCount());
    t.headers.insert(at, QString());
    t.alignments.insert(at, Align::None);
    for (QStringList &row : t.rows)
        row.insert(qMin(at, row.size()), QString());
    return serialize(t);
}

QString removeRow(const QString &md, int row)
{
    Table t = parse(md);
    if (!t.valid || row < 0 || row >= t.rowCount())
        return md;
    t.rows.removeAt(row);
    return serialize(t);
}

QString removeColumn(const QString &md, int col)
{
    Table t = parse(md);
    if (!t.valid || col < 0 || col >= t.columnCount() || t.columnCount() <= 1)
        return md;   // never remove the last column
    t.headers.removeAt(col);
    t.alignments.removeAt(col);
    for (QStringList &row : t.rows) {
        if (col < row.size())
            row.removeAt(col);
    }
    return serialize(t);
}

QString sortByColumn(const QString &md, int col, bool ascending)
{
    Table t = parse(md);
    if (!t.valid || col < 0 || col >= t.columnCount())
        return md;
    std::stable_sort(t.rows.begin(), t.rows.end(),
                     [col, ascending](const QStringList &a, const QStringList &b) {
        const QString sa = col < a.size() ? a[col] : QString();
        const QString sb = col < b.size() ? b[col] : QString();
        bool oka = false, okb = false;
        const double da = sa.toDouble(&oka);
        const double db = sb.toDouble(&okb);
        int cmp;
        if (oka && okb)
            cmp = da < db ? -1 : (da > db ? 1 : 0);
        else
            cmp = QString::compare(sa, sb, Qt::CaseInsensitive);
        return ascending ? cmp < 0 : cmp > 0;
    });
    return serialize(t);
}

QString setAlignment(const QString &md, int col, Align a)
{
    Table t = parse(md);
    if (!t.valid || col < 0 || col >= t.columnCount())
        return md;
    t.alignments[col] = a;
    return serialize(t);
}

QString emptyTable(int columns, int rows)
{
    columns = qMax(1, columns);
    rows = qMax(0, rows);
    Table t;
    t.valid = true;
    for (int c = 0; c < columns; ++c) {
        t.headers.append(QString());
        t.alignments.append(Align::None);
    }
    for (int r = 0; r < rows; ++r) {
        QStringList row;
        for (int c = 0; c < columns; ++c)
            row.append(QString());
        t.rows.append(row);
    }
    return serialize(t);
}

} // namespace TableData

namespace {
QString alignName(TableData::Align a)
{
    switch (a) {
    case TableData::Align::Left:   return QStringLiteral("left");
    case TableData::Align::Center: return QStringLiteral("center");
    case TableData::Align::Right:  return QStringLiteral("right");
    case TableData::Align::None:   break;
    }
    return QStringLiteral("none");
}
TableData::Align alignFromName(const QString &n)
{
    if (n == QLatin1String("left"))   return TableData::Align::Left;
    if (n == QLatin1String("center")) return TableData::Align::Center;
    if (n == QLatin1String("right"))  return TableData::Align::Right;
    return TableData::Align::None;
}
} // namespace

QVariantMap TableTools::parse(const QString &md) const
{
    const TableData::Table t = TableData::parse(md);
    QVariantMap m;
    m.insert(QStringLiteral("valid"), t.valid);
    m.insert(QStringLiteral("columns"), t.columnCount());
    m.insert(QStringLiteral("rowCount"), t.rowCount());
    m.insert(QStringLiteral("headers"), QVariant(QStringList(t.headers)));
    QStringList aligns;
    for (TableData::Align a : t.alignments)
        aligns.append(alignName(a));
    m.insert(QStringLiteral("alignments"), QVariant(aligns));
    QVariantList rows;
    for (const QStringList &r : t.rows)
        rows.append(QVariant(QStringList(r)));
    m.insert(QStringLiteral("rows"), rows);
    return m;
}

QString TableTools::cellValue(const QString &md, int row, int col) const
{
    const TableData::Table t = TableData::parse(md);
    if (!t.valid || col < 0 || col >= t.columnCount())
        return QString();
    if (row == -1)
        return t.headers.value(col);
    if (row >= 0 && row < t.rowCount())
        return t.rows[row].value(col);
    return QString();
}

QString TableTools::setAlignment(const QString &md, int col,
                                 const QString &align) const
{
    return TableData::setAlignment(md, col, alignFromName(align));
}
