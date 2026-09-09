// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
#ifndef BLOCKPOSITIONS_H
#define BLOCKPOSITIONS_H

class BlockModel;

// Position mapping for one block of an open document, addressed by its
// index in the model.
//
// A block's text has two coordinate systems. The MARKDOWN is what the
// block holds and what is written to the file: `This is **bold** text`,
// twenty-one characters. The DISPLAY text is what a reader sees once the
// inline markers are hidden: `This is bold text`, seventeen. Anything
// stored against a passage of a note is stored in markdown offsets,
// because those survive the file being reopened, while anything a reader
// points at — a selection, a search hit, a range a panel is about —
// arrives in display coordinates, which are recomputed from the content
// on every edit. Both directions of the translation are therefore needed.
//
// The translation itself is InlineMarkdown::documentToMarkdown and
// InlineMarkdown::markdownToDocument (src/content/inlinemarkdown.h), which
// work on a markdown string. What a caller normally holds is not a string
// but a block index, and getting from one to the other takes three steps
// that are easy to get wrong: resolve the index through the model and
// answer sensibly when it does not resolve; treat a verbatim block — a
// code block, whose display text IS its content — as the identity; and
// pass an empty revealed-span list, which means "no span is being edited,
// so every marker is hidden", the state everything outside a focused
// editor is in.
//
// Those steps live here so that every caller gets the same answer.
// DocumentSearch is one caller: a match is found in display text and
// reported to the QML layer as a markdown offset. A linked module that
// stores a comment or a marked range against a passage is another, and it
// needs the answer for blocks whose editor is not up, which is most of
// them — the text delegates draw an unfocused block through a read-only
// path with no BlockEditorEngine behind it.
//
// This is C++ only, deliberately. Callers below the presentation layer
// link kvit-domain and call these directly. QML already has the two
// answers it asks for: DocumentSearch::currentMatchInfo carries `mdStart`
// for the open document, and CollectionSearch::markdownPosition answers
// for a note that is not open, by relative path. A QML consumer that needs
// the general open-document answer should get a thin Q_INVOKABLE on
// whatever object already holds the model, forwarding to these — a second
// exposure, never a second implementation.
namespace BlockPositions {

// The markdown offset in `blockIndex`'s content that display position
// `displayPos` corresponds to.
//
// `displayPos` is clamped into [0, length of the block's display text]
// before mapping, so a position past the end answers the end of the
// content and a negative one answers whatever display position 0 maps to.
// That last is not always 0: a block starting with a marker, `**bold**`,
// has its first visible character at markdown offset 2, and both display
// position 0 and any negative position map there. The result is always a
// valid offset into the block's content.
//
// A verbatim block maps one to one, clamped to its content length. A
// null model, an index the model does not hold, and a block with no
// content all answer 0.
int markdownPosition(const BlockModel *model, int blockIndex, int displayPos);

// The display position in `blockIndex` that markdown offset `mdPos`
// corresponds to: the reverse of markdownPosition, with the same clamping
// and the same answers for a block that does not resolve.
//
// An offset inside a hidden marker has no display position of its own and
// clamps to the nearest edge of the content the marker wraps, which is
// what InlineMarkdown::markdownToDocument does and what a caret does when
// the markers are hidden.
int displayPosition(const BlockModel *model, int blockIndex, int mdPos);

} // namespace BlockPositions

#endif // BLOCKPOSITIONS_H
