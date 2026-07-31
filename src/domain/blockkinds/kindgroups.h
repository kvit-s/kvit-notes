// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
#ifndef KINDGROUPS_H
#define KINDGROUPS_H

#include <QList>

class BlockKindDef;

// The built-in kind definitions, grouped by the file that implements them.
//
// Each group's classes are file-local: nothing outside its own translation
// unit names ParagraphKindDef or QueryKindDef, because nothing needs to. A
// kind is reached through BlockKindDefs::builtin() or through the pointer a
// block holds, and both of those speak in BlockKindDef.
//
// The order within each list, and the order BlockKindDefs::builtins()
// concatenates them in, is the order the block menu lists its rows.
namespace BlockKindGroups {

// Paragraph and the four heading levels: the five kinds that share one
// delegate and therefore one delegate-kind value.
const QList<const BlockKindDef *> &text();

// The three list-family kinds and the quote, which share indentation and the
// tight-list serialization.
const QList<const BlockKindDef *> &lists();

// A code fence, a display-math fence and a divider.
const QList<const BlockKindDef *> &code();

// The two container kinds, whose content is parsed by a component of its own:
// a callout and a pipe table.
const QList<const BlockKindDef *> &containers();

// An image, an audio/video file, and the preview card an image expression
// becomes when its URL names a web page.
const QList<const BlockKindDef *> &media();

// The four kinds a code fence's info string selects: a task board, a table of
// contents, a Mermaid diagram and a collection query.
const QList<const BlockKindDef *> &fences();

} // namespace BlockKindGroups

#endif // KINDGROUPS_H
