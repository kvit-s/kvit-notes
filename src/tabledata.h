// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
#ifndef TABLEDATA_H
#define TABLEDATA_H

#include <QList>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariantMap>

// Pipe-table parse/serialize/mutate (phase10-plan.md decision 8). A table
// block's content is the raw GitHub-flavored pipe-table markdown — content IS
// the syntax here, unlike the prefix-state block types, because a grid does
// not decompose into per-block fields. This pure component maps that markdown
// to a cell grid and back and applies every mutation as a whole-markdown
// rewrite, so the delegate routes each edit through one model content update
// (one undo step). Escaped pipes (\|) survive in cells; per-column alignment
// comes from the delimiter row's colons. Serialization is canonical, so
// hand-authored ragged/padded tables normalize on save (a documented change,
// like loose lists tightening).
namespace TableData {

enum class Align { None, Left, Center, Right };

struct Table {
    bool valid = false;
    QStringList headers;         // the header row's cells
    QList<Align> alignments;     // one per column
    QList<QStringList> rows;     // data rows (each padded to columnCount)

    int columnCount() const { return headers.size(); }
    int rowCount() const { return rows.size(); }   // data rows only
};

// True when `line` is a table delimiter row (| --- | :--: | …).
bool isDelimiterRow(const QString &line);

// Does the two-line pair (header, delimiter) begin a pipe table? Used by the
// serializer to detect a table run.
bool looksLikeTableStart(const QString &headerLine, const QString &delimiterLine);

// Parse pipe-table markdown into a grid; invalid table → Table{valid=false}.
Table parse(const QString &markdown);

// Canonical markdown for a grid (unpadded, one space around each cell).
QString serialize(const Table &t);

// ---- Mutations: each takes and returns whole-table markdown ----

// Set a cell's value. row == -1 addresses the header row.
QString setCell(const QString &md, int row, int col, const QString &value);
// Insert an empty data row after `afterRow` (-1 → before the first data row,
// i.e. at the top; rowCount()-1 or beyond → append).
QString insertRow(const QString &md, int afterRow);
// Insert an empty column after `afterCol` (-1 → at the left).
QString insertColumn(const QString &md, int afterCol);
QString removeRow(const QString &md, int row);
QString removeColumn(const QString &md, int col);
// Sort the data rows by a column's text, ascending or descending (one step).
QString sortByColumn(const QString &md, int col, bool ascending);
// Set a column's alignment.
QString setAlignment(const QString &md, int col, Align a);

// Build an empty table of the given size (for the grid-picker insertion).
QString emptyTable(int columns, int rows);

} // namespace TableData

// QML context object (tableTools) wrapping the pure functions: the delegate
// reads the grid and applies mutations through these, each returning new
// whole-table markdown for one model content update.
class TableTools : public QObject
{
    Q_OBJECT
public:
    explicit TableTools(QObject *parent = nullptr) : QObject(parent) {}

    // {valid, columns, rowCount, headers:[…], alignments:["left"|…],
    //  rows:[[…], …]}.
    Q_INVOKABLE QVariantMap parse(const QString &md) const;

    Q_INVOKABLE QString setCell(const QString &md, int row, int col,
                                const QString &value) const
    { return TableData::setCell(md, row, col, value); }
    Q_INVOKABLE QString cellValue(const QString &md, int row, int col) const;
    Q_INVOKABLE QString insertRow(const QString &md, int afterRow) const
    { return TableData::insertRow(md, afterRow); }
    Q_INVOKABLE QString insertColumn(const QString &md, int afterCol) const
    { return TableData::insertColumn(md, afterCol); }
    Q_INVOKABLE QString removeRow(const QString &md, int row) const
    { return TableData::removeRow(md, row); }
    Q_INVOKABLE QString removeColumn(const QString &md, int col) const
    { return TableData::removeColumn(md, col); }
    Q_INVOKABLE QString sortByColumn(const QString &md, int col, bool ascending) const
    { return TableData::sortByColumn(md, col, ascending); }
    Q_INVOKABLE QString setAlignment(const QString &md, int col,
                                     const QString &align) const;
    Q_INVOKABLE QString emptyTable(int columns, int rows) const
    { return TableData::emptyTable(columns, rows); }
};

#endif // TABLEDATA_H
