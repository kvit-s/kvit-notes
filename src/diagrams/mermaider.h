// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
#ifndef MERMAIDER_H
#define MERMAIDER_H

#include <QString>

#include "mermaidast.h"

// ER-diagram parser (diagrams-prd.md §9), built grammar-first against the
// pinned mermaid@11.16.0 erDiagram.jison: entities (bare, quoted, and
// `NAME["alias"]`), attribute blocks with types, names, PK/FK/UK key lists and
// quoted comments, relationships with every cardinality spelling (symbolic
// crow's-foot pairs and the verbose `one or more` forms), identifying `--` vs
// non-identifying `..`/`.-`/`-.` lines (and the textual `to` / `optionally
// to`), relationship roles, direction, classDef/class/style and `:::`, and
// accTitle/accDescr.
namespace Mermaid {

// Parses an erDiagram body (frontmatter already stripped) into result.er,
// appending diagnostics with one-based line positions.
void parseErDiagram(const QString &body, int baseOffset,
                    ParseResult &result);

} // namespace Mermaid

#endif // MERMAIDER_H
