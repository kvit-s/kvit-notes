// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
#ifndef BLOCKKIND_H
#define BLOCKKIND_H

#include "block.h"

#include <QObject>

// What kind of block this is: the one closed enumeration everything that
// decides something per kind resolves to.
//
// Before this existed, a kind was three unrelated things at once. The stored
// block type was an enum; a fence's kind was its language, compared as a raw
// string in the exporter and looked up in a hash by the delegate chooser; and
// the number the QML DelegateChooser watches was a plain `int` spanning both
// ranges plus a third for module-registered kinds. A switch over an `int` gets
// no checking from a compiler, which is how a `query` fence came to export as
// a code listing of its own `from:`/`where:` spec: the exporter tested four
// language strings and had no branch for the fifth.
//
// The values are the ones already in use, so nothing persisted changes:
//
//   0..15   a block type (Block::BlockType), which model roles, undo states
//           and the tests carry as an int and files carry as markdown;
//   100..   a kind derived from a code fence's language or from an image
//           block's URL, already numbered by BlockKinds::Kind and named from
//           QML as BlockKinds.Kanban and so on;
//   200..   a kind a linked module registered at startup, which can never be
//           an enumerator here and is handled as a value.
//
// Note what this enum is NOT: it is not the value the QML DelegateChooser
// watches. Paragraph and all four headings share one delegate, and the value
// they publish is deliberately the same for all five — see
// BlockKindDef::delegateKind().
namespace BlockKinds {
Q_NAMESPACE

enum class Kind : int {
    // ---- kinds that are a stored block type ----
    Paragraph    = Block::Paragraph,
    Heading1     = Block::Heading1,
    Heading2     = Block::Heading2,
    Heading3     = Block::Heading3,
    BulletList   = Block::BulletList,
    NumberedList = Block::NumberedList,
    Todo         = Block::Todo,
    Quote        = Block::Quote,
    CodeBlock    = Block::CodeBlock,
    Divider      = Block::Divider,
    Heading4     = Block::Heading4,
    Image        = Block::Image,
    Callout      = Block::Callout,
    MathBlock    = Block::MathBlock,
    Media        = Block::Media,
    Table        = Block::Table,

    // ---- kinds derived from a fence language or from content ----
    // A `kanban` fence draws a board, a `toc` fence a linked heading list, a
    // `mermaid` fence a diagram, a `query` fence a live view of the vault.
    // An Embed is an image expression whose URL names a web page rather than
    // an image file, so it is derived from the block's content and has no
    // fence language of its own.
    Kanban  = 100,
    Toc     = 101,
    Embed   = 102,
    Mermaid = 103,
    Query   = 104,

    // Module kinds are >= BlockKindRegistry::FirstRegisteredKind and are
    // never enumerators here: the core cannot know their names, and a switch
    // it compiled could not cover them. That is the reason this enumeration
    // is a key rather than the thing features switch on.
};
// Declared through Q_ENUM_NS so a guard can walk every enumerator from the
// metaobject rather than from a second list kept by hand. BlockKindDefTests
// uses it to check that each one has a definition: nothing else can catch an
// enumerator added without a class, since the compiler does not require one
// and the block silently resolves to a paragraph at runtime.
Q_ENUM_NS(Kind)

} // namespace BlockKinds

// The name the tree uses. An alias rather than the qualified spelling because
// the kind is named at sixty call sites and `BlockKind::Query` is what it is
// called everywhere else in this design; the namespace exists only to give
// the enumeration a metaobject.
using BlockKind = BlockKinds::Kind;

// Which entry of the frozen type scale a kind renders at. Typography turns a
// role into pixels; the ratios stay where they are.
//
// A role rather than a size, because the block knows what it is and the
// typography settings know how big that should be. Typography used to switch
// over Block::BlockType itself, which meant one more place a new kind had to
// be added to and nothing said so.
enum class FontRole {
    Body,
    Heading1,
    Heading2,
    Heading3,
    Heading4,
    // A code fence, and anything else drawn in the configured monospace
    // family at the code size.
    Mono,
};

#endif // BLOCKKIND_H
