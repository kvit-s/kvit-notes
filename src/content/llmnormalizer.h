// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
#ifndef LLMNORMALIZER_H
#define LLMNORMALIZER_H

#include <QString>

// Rewrites LLM-markdown constructs into the dialect DocumentSerializer::parse
// reads. Fence-aware: content inside code fences is never touched. Idempotent,
// and a fixed point on canonical serializer output that contains none of the
// targeted constructs.
//
// The rewrites (plan fixes 1, 3, 4, 7):
//  - a code fence embedded in a table cell collapses to an inline code span
//    so the table survives (fix 1);
//  - \(...\) and \[...\] math delimiters become $...$ spans / $$ fences
//    (fix 3);
//  - Unicode spaces (U+202F, U+00A0, U+2009, U+200A) become ASCII spaces
//    inside math content only (fix 4);
//  - HTML entities decode once, outside code fences and inline code spans
//    (fix 7).
//
// Multi-line constructs use bounded lookahead, so the pass stays linear on
// pathological input regardless of file size.
class LlmNormalizer
{
public:
    static QString normalize(const QString &markdown);
};

#endif // LLMNORMALIZER_H
