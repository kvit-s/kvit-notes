// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
#ifndef MERMAIDSTATE_H
#define MERMAIDSTATE_H

#include <QString>

#include "mermaidast.h"

// State-diagram parser (diagrams-prd.md §9), built grammar-first against the
// pinned mermaid@11.16.0 stateDiagram.jison: stateDiagram / stateDiagram-v2,
// states with descriptions (`s1 : text`, `state "long" as s1`), transitions
// with labels, `[*]` start/end pseudo-states scoped to their composite,
// composite states with nested documents and local direction, <<fork>> /
// <<join>> / <<choice>> (and the [[...]] spellings), notes (left/right,
// single-line and `end note` blocks, floating `note "x" as n`), classDef /
// class / style / `:::`, and accTitle/accDescr. `scale`, concurrency `--`
// dividers, and click/href are retained with a warning — never an
// unrecognized-token failure.
namespace Mermaid {

// Parses a stateDiagram body (frontmatter already stripped) into
// result.stateDiagram, appending diagnostics with one-based line positions.
void parseStateDiagram(const QString &body, int baseOffset,
                       ParseResult &result);

} // namespace Mermaid

#endif // MERMAIDSTATE_H
