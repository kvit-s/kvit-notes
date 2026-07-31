// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
#include "blockkinds.h"
#include "blockkinddef.h"
#include "blockkinds/kindgroups.h"
#include "imageassets.h"

#include <QHash>

namespace {

// Every built-in def, once, in the order the block menu lists its rows: the
// paragraph and its headings, the lists, the quote and the fenced kinds, then
// the media kinds. A list rather than a hash, because that order is what the
// menu's group headings and its search tie-breaks are built on, and a hash
// would make both nondeterministic.
const QList<const BlockKindDef *> &allBuiltins()
{
    static const QList<const BlockKindDef *> kinds = [] {
        QList<const BlockKindDef *> out;
        out += BlockKindGroups::text();
        out += BlockKindGroups::lists();
        out += BlockKindGroups::code();
        out += BlockKindGroups::containers();
        out += BlockKindGroups::fences();
        out += BlockKindGroups::media();
        return out;
    }();
    return kinds;
}

const QHash<int, const BlockKindDef *> &byKind()
{
    static const QHash<int, const BlockKindDef *> map = [] {
        QHash<int, const BlockKindDef *> out;
        for (const BlockKindDef *def : allBuiltins())
            out.insert(static_cast<int>(def->kind()), def);
        return out;
    }();
    return map;
}

// The fence info strings the built-ins claim, read off the defs themselves so
// that "kanban" is written down in exactly one place — the kind that draws a
// board.
const QHash<QString, BlockKind> &byFenceLanguage()
{
    static const QHash<QString, BlockKind> map = [] {
        QHash<QString, BlockKind> out;
        for (const BlockKindDef *def : allBuiltins()) {
            const QString language = def->fenceLanguage();
            if (!language.isEmpty())
                out.insert(language, def->kind());
        }
        return out;
    }();
    return map;
}

} // namespace

namespace BlockKindDefs {

const BlockKindDef *builtin(BlockKind kind)
{
    return byKind().value(static_cast<int>(kind), nullptr);
}

const QList<const BlockKindDef *> &builtins()
{
    return allBuiltins();
}

BlockKind kindForFenceLanguage(const QString &language)
{
    return byFenceLanguage().value(language, BlockKind::CodeBlock);
}

BlockKind kindForState(const Block::State &state)
{
    // An image expression whose URL names a web page rather than an image
    // file draws as a preview card. This is the one kind decided by the
    // block's content, which is why resolution takes the whole state.
    if (state.type == Block::Image || state.type == Block::Media) {
        const ImageAssets::Parsed parsed = ImageAssets::parseLine(state.content);
        if (parsed.valid && ImageAssets::isEmbedUrl(parsed.path))
            return BlockKind::Embed;
    }
    if (state.type == Block::CodeBlock)
        return kindForFenceLanguage(state.language);
    return static_cast<BlockKind>(state.type);
}

const BlockKindDef *forState(const Block::State &state)
{
    const BlockKindDef *def = builtin(kindForState(state));
    // A state whose type is not a valid enumerator reads as a paragraph, the
    // same coercion Block::sanitized applies, so a corrupted block still
    // shows its text rather than crashing whatever asked what it is.
    return def ? def : builtin(BlockKind::Paragraph);
}

} // namespace BlockKindDefs
