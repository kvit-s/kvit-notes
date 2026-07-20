// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
#include "querydata.h"

#include <QDate>
#include <QDateTime>
#include <algorithm>

#include "notecollection.h"

namespace QueryData {

namespace {

// One field value with its inferred type, so a pair of values compares
// as dates when both are dates, as numbers when both are numbers, and as
// case-insensitive strings otherwise (pre-launch-plan.md §1.2).
struct TypedValue {
    QString text;
    QDateTime date;       // valid when the text parses as a date
    double number = 0;
    bool isDate = false;
    bool isNumber = false;
    bool exists = false;
};

TypedValue typedFromString(const QString &raw, bool exists = true)
{
    TypedValue value;
    value.text = raw.trimmed();
    value.exists = exists;
    if (value.text.isEmpty())
        return value;

    QDateTime dt = QDateTime::fromString(value.text, Qt::ISODate);
    if (!dt.isValid()) {
        const QDate d = QDate::fromString(value.text, Qt::ISODate);
        if (d.isValid())
            dt = d.startOfDay();
    }
    if (dt.isValid()) {
        value.date = dt;
        value.isDate = true;
        return value;
    }

    bool numOk = false;
    const double n = value.text.toDouble(&numOk);
    if (numOk) {
        value.number = n;
        value.isNumber = true;
    }
    return value;
}

const QStringList &pseudoFields()
{
    static const QStringList kFields = {
        QStringLiteral("title"),    QStringLiteral("path"),
        QStringLiteral("folder"),   QStringLiteral("modified"),
        QStringLiteral("created"),  QStringLiteral("words"),
        QStringLiteral("tags"),
    };
    return kFields;
}

// The field's value on one note: pseudo-fields from the entry, everything
// else from the front-matter key map.
TypedValue fieldValue(const NoteCollection::NoteEntry &entry,
                      const QString &field)
{
    if (field == QLatin1String("title"))
        return typedFromString(entry.title);
    if (field == QLatin1String("path"))
        return typedFromString(entry.relPath);
    if (field == QLatin1String("folder"))
        return typedFromString(entry.folder);
    if (field == QLatin1String("modified")) {
        TypedValue v;
        v.date = entry.modified;
        v.isDate = v.date.isValid();
        v.exists = v.isDate;
        v.text = v.isDate
            ? v.date.toString(QStringLiteral("yyyy-MM-dd HH:mm")) : QString();
        return v;
    }
    if (field == QLatin1String("created")) {
        TypedValue v;
        v.date = entry.created;
        v.isDate = v.date.isValid();
        v.exists = v.isDate;
        v.text = v.isDate
            ? v.date.toString(QStringLiteral("yyyy-MM-dd")) : QString();
        return v;
    }
    if (field == QLatin1String("words")) {
        TypedValue v;
        v.number = entry.wordCount;
        v.isNumber = true;
        v.exists = true;
        v.text = QString::number(entry.wordCount);
        return v;
    }
    if (field == QLatin1String("tags")) {
        TypedValue v;
        v.text = entry.meta.tags.join(QLatin1String(", "));
        v.exists = !entry.meta.tags.isEmpty();
        return v;
    }
    if (!entry.meta.fields.contains(field))
        return typedFromString(QString(), false);
    return typedFromString(entry.meta.fieldString(field));
}

// The list a `has` condition scans: tags for the tags pseudo-field, the
// parsed inline/comma list otherwise.
QStringList fieldListValue(const NoteCollection::NoteEntry &entry,
                           const QString &field)
{
    if (field == QLatin1String("tags"))
        return entry.meta.tags;
    return entry.meta.fieldList(field);
}

// -1 / 0 / +1 with type-inferred ordering.
int compareTyped(const TypedValue &a, const TypedValue &b)
{
    if (a.isDate && b.isDate) {
        if (a.date == b.date)
            return 0;
        return a.date < b.date ? -1 : 1;
    }
    if (a.isNumber && b.isNumber) {
        if (a.number == b.number)
            return 0;
        return a.number < b.number ? -1 : 1;
    }
    return QString::compare(a.text, b.text, Qt::CaseInsensitive);
}

bool conditionHolds(const Condition &cond,
                    const NoteCollection::NoteEntry &entry)
{
    const TypedValue lhs = fieldValue(entry, cond.field);
    // A note without the field never satisfies a comparison (an empty
    // string would otherwise order below everything); it differs from any
    // value, so only != holds.
    if (!lhs.exists)
        return cond.op == Op::Ne;
    switch (cond.op) {
    case Op::Exists:
        return lhs.exists;
    case Op::Contains:
        return lhs.text.contains(cond.value, Qt::CaseInsensitive);
    case Op::Has: {
        const QStringList items = fieldListValue(entry, cond.field);
        for (const QString &item : items) {
            if (item.compare(cond.value, Qt::CaseInsensitive) == 0)
                return true;
        }
        return false;
    }
    default:
        break;
    }
    const int cmp = compareTyped(lhs, typedFromString(cond.value));
    switch (cond.op) {
    case Op::Eq: return cmp == 0;
    case Op::Ne: return cmp != 0;
    case Op::Lt: return cmp < 0;
    case Op::Gt: return cmp > 0;
    case Op::Le: return cmp <= 0;
    case Op::Ge: return cmp >= 0;
    default:     return false;
    }
}

bool parseCondition(const QString &text, Condition *out, QString *error)
{
    const QString trimmed = text.trimmed();
    if (trimmed.isEmpty()) {
        *error = QStringLiteral("empty condition in 'where:'");
        return false;
    }

    // "<field> exists"
    static const QString kExists = QStringLiteral("exists");
    if (trimmed.endsWith(QLatin1Char(' ') + kExists, Qt::CaseInsensitive)) {
        const QString field =
            trimmed.left(trimmed.size() - kExists.size() - 1).trimmed();
        if (!field.isEmpty() && !field.contains(QLatin1Char(' '))) {
            out->field = field;
            out->op = Op::Exists;
            out->value.clear();
            return true;
        }
    }

    struct OpToken { const char *token; Op op; };
    // Longer tokens first so "<=" wins over "<".
    static const OpToken kOps[] = {
        {"!=", Op::Ne}, {"<=", Op::Le}, {">=", Op::Ge},
        {"=", Op::Eq},  {"<", Op::Lt},  {">", Op::Gt},
        {" contains ", Op::Contains}, {" has ", Op::Has},
    };
    for (const OpToken &op : kOps) {
        const int at = trimmed.indexOf(QLatin1String(op.token), 0,
                                       Qt::CaseInsensitive);
        if (at <= 0)
            continue;
        const QString field = trimmed.left(at).trimmed();
        const QString value =
            trimmed.mid(at + int(qstrlen(op.token))).trimmed();
        if (field.isEmpty() || field.contains(QLatin1Char(' ')))
            continue;
        if (value.isEmpty()) {
            *error = QStringLiteral("missing value in condition \"%1\"")
                         .arg(trimmed);
            return false;
        }
        out->field = field;
        out->op = op.op;
        out->value = value;
        return true;
    }
    *error = QStringLiteral("cannot parse condition \"%1\" — expected "
                            "\"field op value\" or \"field exists\"")
                 .arg(trimmed);
    return false;
}

QString displayCell(const NoteCollection::NoteEntry &entry,
                    const QString &column)
{
    return fieldValue(entry, column).text;
}

} // namespace

ParseResult parse(const QString &body)
{
    ParseResult result;
    Spec spec;
    bool viewSet = false;

    const QStringList lines = body.split(QLatin1Char('\n'));
    for (const QString &rawLine : lines) {
        const QString line = rawLine.trimmed();
        if (line.isEmpty() || line.startsWith(QLatin1Char('#')))
            continue; // blank and comment lines are structure, not spec

        const int colon = line.indexOf(QLatin1Char(':'));
        if (colon <= 0) {
            result.error =
                QStringLiteral("expected \"key: value\", got \"%1\"")
                    .arg(line);
            return result;
        }
        const QString key = line.left(colon).trimmed().toLower();
        const QString value = line.mid(colon + 1).trimmed();

        if (key == QLatin1String("from")) {
            QString folder = value;
            while (folder.endsWith(QLatin1Char('/')))
                folder.chop(1);
            while (folder.startsWith(QLatin1Char('/')))
                folder.remove(0, 1);
            spec.from = folder;
        } else if (key == QLatin1String("where")) {
            const QStringList parts = value.split(QLatin1Char(','),
                                                  Qt::SkipEmptyParts);
            if (parts.isEmpty()) {
                result.error = QStringLiteral("empty 'where:' line");
                return result;
            }
            for (const QString &part : parts) {
                Condition cond;
                QString error;
                if (!parseCondition(part, &cond, &error)) {
                    result.error = error;
                    return result;
                }
                spec.where.append(cond);
            }
        } else if (key == QLatin1String("view")) {
            const QString v = value.toLower();
            if (v == QLatin1String("table")) {
                spec.view = View::Table;
            } else if (v == QLatin1String("board")) {
                spec.view = View::Board;
            } else {
                result.error =
                    QStringLiteral("unknown view \"%1\" — use table or board")
                        .arg(value);
                return result;
            }
            viewSet = true;
        } else if (key == QLatin1String("columns")) {
            const QStringList parts = value.split(QLatin1Char(','));
            for (const QString &part : parts) {
                const QString column = part.trimmed();
                if (!column.isEmpty())
                    spec.columns.append(column);
            }
        } else if (key == QLatin1String("group-by")) {
            if (value.isEmpty() || value.contains(QLatin1Char(' '))) {
                result.error =
                    QStringLiteral("'group-by:' needs one field name");
                return result;
            }
            spec.groupBy = value;
        } else if (key == QLatin1String("sort")) {
            const QStringList parts = value.split(QLatin1Char(','),
                                                  Qt::SkipEmptyParts);
            for (const QString &part : parts) {
                const QStringList words =
                    part.trimmed().split(QLatin1Char(' '),
                                         Qt::SkipEmptyParts);
                if (words.isEmpty() || words.size() > 2) {
                    result.error =
                        QStringLiteral("cannot parse sort \"%1\" — expected "
                                       "\"field [asc|desc]\"").arg(part.trimmed());
                    return result;
                }
                SortKey sortKey;
                sortKey.field = words.first();
                if (words.size() == 2) {
                    const QString dir = words.at(1).toLower();
                    if (dir == QLatin1String("asc")) {
                        sortKey.ascending = true;
                    } else if (dir == QLatin1String("desc")) {
                        sortKey.ascending = false;
                    } else {
                        result.error =
                            QStringLiteral("sort direction must be asc or "
                                           "desc, got \"%1\"").arg(words.at(1));
                        return result;
                    }
                }
                spec.sort.append(sortKey);
            }
        } else if (key == QLatin1String("limit")) {
            bool ok = false;
            const int limit = value.toInt(&ok);
            if (!ok || limit < 1) {
                result.error =
                    QStringLiteral("'limit:' needs a positive integer");
                return result;
            }
            spec.limit = limit;
        } else {
            result.error = QStringLiteral(
                "unknown key \"%1\" — known keys: from, where, view, "
                "columns, group-by, sort, limit").arg(key);
            return result;
        }
    }

    if (spec.view == View::Board && spec.groupBy.isEmpty()) {
        result.error =
            QStringLiteral("view: board needs a 'group-by:' field");
        return result;
    }
    if (spec.view == View::Table && !spec.groupBy.isEmpty() && viewSet) {
        result.error =
            QStringLiteral("'group-by:' only applies to view: board");
        return result;
    }
    // "group-by:" with no explicit view implies the board.
    if (!viewSet && !spec.groupBy.isEmpty())
        spec.view = View::Board;

    result.spec = spec;
    result.ok = true;
    return result;
}

Result evaluate(const Spec &spec, const NoteCollection &collection)
{
    Result result;
    result.columns = spec.columns;
    if (result.columns.isEmpty())
        result.columns = {QStringLiteral("title"), QStringLiteral("modified")};

    struct Match {
        const NoteCollection::NoteEntry *entry;
    };
    QList<Match> matches;
    const QStringList paths = collection.noteRelPaths();
    for (const QString &relPath : paths) {
        const NoteCollection::NoteEntry *entry = collection.note(relPath);
        if (!entry)
            continue;
        if (!spec.from.isEmpty()
            && entry->folder.compare(spec.from, Qt::CaseInsensitive) != 0
            && !entry->folder.startsWith(spec.from + QLatin1Char('/'),
                                         Qt::CaseInsensitive))
            continue;
        bool holds = true;
        for (const Condition &cond : spec.where) {
            if (!conditionHolds(cond, *entry)) {
                holds = false;
                break;
            }
        }
        if (holds)
            matches.append({entry});
    }

    // Stable multi-key sort: apply the keys in reverse so the first key
    // dominates. The pre-sort by relPath (noteRelPaths is sorted) makes
    // the whole result deterministic.
    for (int k = spec.sort.size() - 1; k >= 0; --k) {
        const SortKey &key = spec.sort.at(k);
        std::stable_sort(matches.begin(), matches.end(),
                         [&key](const Match &a, const Match &b) {
            const int cmp = compareTyped(fieldValue(*a.entry, key.field),
                                         fieldValue(*b.entry, key.field));
            return key.ascending ? cmp < 0 : cmp > 0;
        });
    }

    if (spec.limit > 0 && matches.size() > spec.limit)
        matches = matches.mid(0, spec.limit);

    for (const Match &match : matches) {
        Row row;
        row.relPath = match.entry->relPath;
        for (const QString &column : result.columns)
            row.cells.append(displayCell(*match.entry, column));
        result.rows.append(row);
    }

    if (spec.view == View::Board) {
        // Group in first-appearance order of the (sorted) rows, with the
        // no-value group last.
        QStringList order;
        QHash<QString, QList<Row>> byGroup;
        static const QString kNone = QStringLiteral("(none)");
        for (int i = 0; i < matches.size(); ++i) {
            const TypedValue v = fieldValue(*matches.at(i).entry,
                                            spec.groupBy);
            const QString name = v.text.isEmpty() ? kNone : v.text;
            if (!byGroup.contains(name))
                order.append(name);
            byGroup[name].append(result.rows.at(i));
        }
        if (order.removeAll(kNone) > 0)
            order.append(kNone);
        for (const QString &name : order)
            result.groups.append({name, byGroup.value(name)});
    }

    return result;
}

} // namespace QueryData
