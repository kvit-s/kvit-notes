// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
#ifndef MERMAIDCLASS_H
#define MERMAIDCLASS_H

#include <QString>

#include "mermaidast.h"

// Class-diagram parser (diagrams-prd.md §9), built grammar-first against the
// pinned mermaid@11.16.0 classDiagram.jison: classes with labels, backquoted
// literal names, generics, member/method compartments as text, every relation
// end (extension, composition, aggregation, dependency, lollipop) on solid or
// dotted lines with cardinalities and labels, annotations, one-level
// namespaces, notes, direction, classDef/style/cssClass. Interactivity
// (click/callback/link/href) is retained with a warning — never an
// unrecognized-token failure.
namespace Mermaid {

// Parses a classDiagram body (frontmatter already stripped) into
// result.classDiagram, appending diagnostics with one-based line positions.
void parseClassDiagram(const QString &body, int baseOffset,
                       ParseResult &result);

} // namespace Mermaid

#endif // MERMAIDCLASS_H
