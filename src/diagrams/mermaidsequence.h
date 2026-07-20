// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
#ifndef MERMAIDSEQUENCE_H
#define MERMAIDSEQUENCE_H

#include <QString>

#include "mermaidast.h"

// Sequence-diagram parser (diagrams-prd.md §9), built grammar-first against
// the pinned mermaid@11.16.0 sequenceDiagram.jison: participants/actors with
// aliases, every arrow form, +/- activation shorthand, loop/alt/opt/par/
// critical/break/rect blocks, boxes, notes, autonumber, titles and
// accessibility directives. Productions Kvit does not render (links/link/
// properties/details, participant @{...} config, create/destroy lifecycles,
// central `()` connections) are retained with a warning — never an
// unrecognized-token failure.
namespace Mermaid {

// Parses a sequenceDiagram body (frontmatter already stripped) into
// result.sequence, appending diagnostics with one-based line positions.
void parseSequence(const QString &body, int baseOffset,
                   ParseResult &result);

} // namespace Mermaid

#endif // MERMAIDSEQUENCE_H
