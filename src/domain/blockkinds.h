// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
#ifndef BLOCKKINDS_H
#define BLOCKKINDS_H

#include "block.h"
#include "blockkind.h"

#include <QList>
#include <QString>

class BlockKindDef;

// The built-in block kinds: the one instance describing each, and the
// resolution from a block's stored state to the kind it is.
//
// Reachable without a registry, through function-local statics, and that is
// deliberate. Three worker paths construct a Block on the stack off the GUI
// thread — the vault scan, the search indexer and the collection's snippet
// cache — once per block per note across a whole vault, purely to compute
// display text. They cannot be made to carry a registry pointer, allocate, or
// acquire thread affinity for it.
//
// A module's kind is reachable only through a BlockKindRegistry, which is
// what a BlockModel holds. That asymmetry is the same one delegate kinds
// already have: a module kind exists for a document being edited in a window,
// not for a background scan of files.
namespace BlockKindDefs {

// The one instance describing a built-in kind. Null only for a value that is
// not a built-in enumerator — a module kind, which the registry answers for.
const BlockKindDef *builtin(BlockKind kind);

// Every built-in def, in the order the block menu lists them.
const QList<const BlockKindDef *> &builtins();

// The built-in kind a fence's info string selects, or BlockKind::CodeBlock
// when no built-in claims it.
//
// Exact and case-sensitive, because that is what the delegate registry has
// always done: a ```Mermaid fence renders as plain code today, and making the
// lookup case-insensitive would change how files already on disk are drawn.
BlockKind kindForFenceLanguage(const QString &language);

// The built-in kind a block's stored state resolves to.
//
// It takes the whole state rather than a type and a language, because one
// kind is decided by content: an image expression whose URL names a web page
// rather than an image file is an embed card.
BlockKind kindForState(const Block::State &state);

// The def for a block's stored state. Never null: an unrecognised state
// resolves to the paragraph, which is the same coercion Block::sanitized
// applies to an unknown type.
const BlockKindDef *forState(const Block::State &state);

} // namespace BlockKindDefs

#endif // BLOCKKINDS_H
