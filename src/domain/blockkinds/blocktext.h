// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
#ifndef BLOCKTEXT_H
#define BLOCKTEXT_H

#include <QList>
#include <QString>
#include <QStringList>

// Turning a block's markdown into the text a reader sees.
namespace BlockText {

// `text` with every non-blank line indented by `spaces`. A blank line stays
// bare, because a line of nothing but spaces is trailing whitespace.
QString indent(const QString &text, int spaces);

// A column-aligned plain-text table: the header row, a rule under it, then
// the data. A cell wider than its column widens the column; a cell holding
// its own line break folds to a space, which is the only thing a fixed-width
// table can do with one.
QString alignedTable(const QStringList &headers,
                     const QList<QStringList> &rows);

// `markdown` with its inline markers resolved: **bold** reads as bold, a
// link reads as its label.
//
// Scans for a marker character first and returns the string untouched when
// there is none. That fast path is why a vault scan can compute display text
// for every block of every note inside its budget: most prose has no markers
// at all, and the span parser is far more expensive than one pass looking for
// a dozen characters.
QString rendered(const QString &markdown);

// The same, with no fast path: every inline marker resolved unconditionally.
//
// rendered() differs from this in exactly one case, and it is a case worth
// knowing about. A backslash escape of a character that cannot itself open a
// span — `\#`, `\|`, `\-`, `\>`, `\]` — contains no opening marker, so the
// scan does not fire and the backslash survives where the span parser would
// conceal it.
//
// The two are kept apart rather than reconciled. The block's three text
// projections have always taken the fast path, so the editor draws that
// backslash and the word counts already written into every vault's sidecar
// index were computed with it; the export paths have always resolved
// unconditionally and do not. Making them agree changes one side or the
// other for files that already exist, which is a decision rather than a
// tidy-up.
QString renderedFully(const QString &markdown);

} // namespace BlockText

#endif // BLOCKTEXT_H
