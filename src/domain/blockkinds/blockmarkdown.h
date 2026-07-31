// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
#ifndef BLOCKMARKDOWN_H
#define BLOCKMARKDOWN_H

#include <QString>

// The markdown-writing pieces more than one block kind needs.
//
// Free functions, not a base-class body. A body on BlockKindDef is the shape
// this whole design exists to refuse: a kind that has not answered something
// must not silently inherit an answer. A kind that WANTS the shared fence
// writer says so by calling it.
namespace BlockMarkdown {

// The backtick run to fence `content` with: longer than any all-backtick line
// inside it, so no line of the content can close the fence early. Three at
// minimum, which is the ordinary case.
QString fenceFor(const QString &content);

// A whole fenced block: the opening fence with its info string, the content,
// the closing fence. `openingLine` is what the opener carries after the
// backticks — the language, with the attribute tag already attached to it if
// the block has one, since the tag cannot ride the bare closer.
QString fencedBlock(const QString &content, const QString &openingLine);

// One list item: its marker, its content, and any line break in the content
// written as a continuation line indented to the column the marker ends at.
// That is what CommonMark reads back as part of the same item, and what a
// language model emits when it wraps a long item, so one shape serves writing
// and reading. `marker` carries the item's own indentation ("  - ", "3. ",
// "- [x] "), so the pad matches whatever it is.
QString listItemLines(const QString &marker, const QString &content);

// Two spaces per indent level, which is what the parser reads back as one
// level of list nesting.
QString indentPrefix(int indentLevel);

} // namespace BlockMarkdown

#endif // BLOCKMARKDOWN_H
