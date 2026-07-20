// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
#ifndef QUERYDATA_H
#define QUERYDATA_H

#include <QString>
#include <QStringList>
#include <QList>

class NoteCollection;

// The collection query block's pure core,
// following the TableData/KanbanData pattern: parse/evaluate functions
// with no GUI dependencies, so the whole feature is headless-testable.
//
// The spec grammar is line-oriented "key: value" — deliberately the same
// shape as front-matter:
//
//   from: projects/            folder prefix filter
//   where: status = active     one condition per line, or comma-separated
//   view: table | board
//   columns: title, status, due
//   group-by: status           (board view)
//   sort: due asc              field asc|desc, repeatable / comma list
//   limit: 20
//
// Conditions are "<field> <op> <value>" with ops = != < > <= >= contains
// has, plus the one-token-value-free form "<field> exists". Unknown keys,
// views, and operators are ERRORS surfaced in the block, not silently
// ignored — this is a spec the user edits by hand.
//
// Fields resolve first against the built-in pseudo-fields (title, path,
// folder, modified, created, words, tags — from the NoteEntry), then the
// note's front-matter key map. Comparisons are type-inferred: dates as
// dates, numbers as numbers, else case-insensitive strings.
namespace QueryData {

enum class Op { Eq, Ne, Lt, Gt, Le, Ge, Contains, Has, Exists };
enum class View { Table, Board };

struct Condition {
    QString field;
    Op op = Op::Eq;
    QString value; // empty for Exists
};

struct SortKey {
    QString field;
    bool ascending = true;
};

struct Spec {
    QString from;             // folder prefix, "" = whole collection
    QList<Condition> where;
    View view = View::Table;
    QStringList columns;      // defaulted to title, modified when empty
    QString groupBy;          // board view's grouping key
    QList<SortKey> sort;
    int limit = 0;            // 0 = unlimited
};

struct ParseResult {
    Spec spec;
    QString error; // empty = ok
    bool ok = false;
};

ParseResult parse(const QString &body);

struct Row {
    QString relPath;
    QStringList cells; // one per Spec::columns entry, display-ready
};

struct Group {
    QString name;      // group-by value; "(none)" for notes without it
    QList<Row> rows;
};

struct Result {
    QStringList columns;  // the effective (defaulted) column list
    QList<Row> rows;      // table view; also the flat card list
    QList<Group> groups;  // board view only
};

Result evaluate(const Spec &spec, const NoteCollection &collection);

} // namespace QueryData

#endif // QUERYDATA_H
