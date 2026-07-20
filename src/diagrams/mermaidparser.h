// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
#ifndef MERMAIDPARSER_H
#define MERMAIDPARSER_H

#include <QString>

#include "mermaidast.h"

// The Mermaid-compatible parser. It detects the diagram family from the
// header, and for the v1 flowchart family builds a typed FlowchartAst with
// one-based line/column diagnostics, recovering at statement boundaries so
// one bad line still yields useful later diagnostics. Every other family
// parses only its header and returns the unsupported-family diagnostic, so no
// source is ever discarded. Label text is treated as plain Unicode, never
// HTML, and the resource limits below are enforced.
namespace Mermaid {

// Resource limits.
constexpr int kMaxNodes = 1000;
constexpr int kMaxEdges = 2000;
constexpr int kMaxDepth = 32;
constexpr int kMaxLabelChars = 16 * 1024;
constexpr int kMaxSourceChars = 256 * 1024;

class MermaidParser
{
public:
    ParseResult parse(const QString &source) const;
};

// Parse safe `fill:#f9f,stroke:#333,stroke-width:2px` style declarations into
// a ClassDef (the restricted-syntax allowlist). Shared by the flowchart
// and class-diagram parsers; unrecognized properties are ignored.
void parseStyleDeclarations(ClassDef &def, const QString &styles);

} // namespace Mermaid

#endif // MERMAIDPARSER_H
