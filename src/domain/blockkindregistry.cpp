// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
#include "blockkindregistry.h"
#include "blockkinddef.h"
#include "blockkinds.h"

namespace {

// What a module gets when it registers a fence language and nothing else.
//
// That form of registration says only "draw this fence with my delegate", and
// it has always meant the block behaves as a code block in every other
// respect: it serializes as a fence, its text is its content verbatim, and it
// exports as highlighted code. This adapter states that rather than leaving
// it to a default, and every method it does not forward is one the module's
// registration actually decided.
//
// A module that wants to decide the rest supplies a BlockKindDef of its own
// through registerKind() and none of this applies to it.
class FenceLanguageOnlyKindDef : public BlockKindDef
{
public:
    FenceLanguageOnlyKindDef(int kind, QString language, QString delegateUrl)
        : m_kind(kind)
        , m_language(std::move(language))
        , m_delegateUrl(std::move(delegateUrl))
        , m_codeBlock(BlockKindDefs::builtin(BlockKind::CodeBlock))
    {
    }

    BlockKind kind() const override { return static_cast<BlockKind>(m_kind); }
    QString id() const override { return m_language; }
    QString fenceLanguage() const override { return m_language; }

    QString serialize(const Block::State &state, int ordinal) const override
    {
        return m_codeBlock->serialize(state, ordinal);
    }
    bool attributeTagRidesOpeningLine() const override
    {
        return m_codeBlock->attributeTagRidesOpeningLine();
    }

    QString displayText(const Block::State &state) const override
    {
        return m_codeBlock->displayText(state);
    }
    QString statisticsText(const Block::State &state) const override
    {
        return m_codeBlock->statisticsText(state);
    }
    QString searchText(const Block::State &state) const override
    {
        return m_codeBlock->searchText(state);
    }

    bool isVerbatim() const override { return m_codeBlock->isVerbatim(); }
    bool foldsLineBreaks() const override
    {
        return m_codeBlock->foldsLineBreaks();
    }
    QString unfoldableTail(const Block::State &state) const override
    {
        return m_codeBlock->unfoldableTail(state);
    }
    int headingLevel() const override { return m_codeBlock->headingLevel(); }
    FontRole fontRole() const override { return m_codeBlock->fontRole(); }
    bool isAlignable() const override { return m_codeBlock->isAlignable(); }

    QString toHtml(const Block::State &state,
                   const RenderContext &ctx) const override
    {
        return m_codeBlock->toHtml(state, ctx);
    }
    QString toPlainText(const Block::State &state,
                        const RenderContext &ctx) const override
    {
        return m_codeBlock->toPlainText(state, ctx);
    }

    // A module's fence language is not a block-menu row: nothing in the core
    // knows what to call it or which group it belongs in. A module that wants
    // one supplies a def of its own.
    QList<MenuEntry> menuEntries() const override { return {}; }

    int delegateKind() const override { return m_kind; }
    QString delegateUrl() const override { return m_delegateUrl; }

private:
    const int m_kind;
    const QString m_language;
    const QString m_delegateUrl;
    const BlockKindDef *const m_codeBlock;
};

} // namespace

BlockKindRegistry::BlockKindRegistry(QObject *parent)
    : QObject(parent)
{
    registerBuiltins();
}

void BlockKindRegistry::registerBuiltins()
{
    // Seeded from the definitions themselves: a fence-backed kind states its
    // own info string, so "kanban" is written down in one place — the class
    // that draws a board — rather than here as well.
    //
    // The delegate URL is not in this table for any kind: it is the def's own
    // answer, and delegateChoices() reads it from there. This table maps a
    // fence's info string to a kind number and nothing else.
    for (const BlockKindDef *def : BlockKindDefs::builtins()) {
        const QString language = def->fenceLanguage();
        if (language.isEmpty())
            continue;
        m_byLanguage.insert(language,
                            Entry{language, static_cast<int>(def->kind()),
                                  QString()});
    }
}

int BlockKindRegistry::registerFenceLanguage(const QString &language,
                                             const QString &delegateUrl)
{
    if (language.isEmpty())
        return 0;

    const auto existing = m_byLanguage.constFind(language);
    if (existing != m_byLanguage.constEnd())
        return existing->kind;

    const int kind = m_nextKind++;
    m_byLanguage.insert(language, Entry{language, kind, delegateUrl});
    if (!delegateUrl.isEmpty())
        m_delegateByKind.insert(kind, delegateUrl);

    // The module decided the delegate and nothing else, so the rest of what
    // this kind does is a code block's. Held here because nothing else owns
    // it; every other def in the tree is a static.
    auto adapter = std::make_shared<const FenceLanguageOnlyKindDef>(
        kind, language, delegateUrl);
    m_ownedAdapters.append(adapter);
    m_moduleDefs.insert(kind, adapter.get());
    m_moduleOrder.append(kind);
    return kind;
}

int BlockKindRegistry::registerKind(const BlockKindDef *def)
{
    if (!def)
        return 0;

    const QString language = def->fenceLanguage();
    if (!language.isEmpty()) {
        const auto existing = m_byLanguage.constFind(language);
        if (existing != m_byLanguage.constEnd())
            return existing->kind;
    }

    const int kind = m_nextKind++;
    if (!language.isEmpty())
        m_byLanguage.insert(language,
                            Entry{language, kind, def->delegateUrl()});
    if (!def->delegateUrl().isEmpty())
        m_delegateByKind.insert(kind, def->delegateUrl());
    m_moduleDefs.insert(kind, def);
    m_moduleOrder.append(kind);
    return kind;
}

int BlockKindRegistry::kindForLanguage(const QString &language) const
{
    const auto entry = m_byLanguage.constFind(language);
    return entry == m_byLanguage.constEnd() ? 0 : entry->kind;
}

const BlockKindDef *BlockKindRegistry::def(BlockKind kind) const
{
    return def(static_cast<int>(kind));
}

const BlockKindDef *BlockKindRegistry::def(int kind) const
{
    if (const BlockKindDef *builtin =
            BlockKindDefs::builtin(static_cast<BlockKind>(kind)))
        return builtin;
    return m_moduleDefs.value(kind, nullptr);
}

const BlockKindDef *BlockKindRegistry::defFor(const Block::State &state) const
{
    // A module claims a fence language, so only a code fence can resolve to
    // one of its kinds. Everything else is decided by the built-in rules,
    // which a module cannot take part in.
    if (state.type == Block::CodeBlock && !state.language.isEmpty()) {
        const auto entry = m_byLanguage.constFind(state.language);
        if (entry != m_byLanguage.constEnd()) {
            if (const BlockKindDef *found = def(entry->kind))
                return found;
        }
    }
    return BlockKindDefs::forState(state);
}

QList<const BlockKindDef *> BlockKindRegistry::all() const
{
    QList<const BlockKindDef *> result = BlockKindDefs::builtins();
    for (int kind : m_moduleOrder) {
        if (const BlockKindDef *found = m_moduleDefs.value(kind, nullptr))
            result.append(found);
    }
    return result;
}

QString BlockKindRegistry::delegateUrl(int kind) const
{
    const BlockKindDef *found = def(kind);
    return found ? found->delegateUrl() : QString();
}

QVariantList BlockKindRegistry::delegateChoices() const
{
    QVariantList result;
    for (const BlockKindDef *kind : all()) {
        const QString url = kind->delegateUrl();
        if (url.isEmpty())
            continue;
        result.append(QVariantMap{
            { QStringLiteral("kind"), kind->delegateKind() },
            { QStringLiteral("id"), kind->id() },
            { QStringLiteral("delegateUrl"), url },
        });
    }
    return result;
}

QStringList BlockKindRegistry::languages() const
{
    return m_byLanguage.keys();
}

void BlockKindRegistry::reset()
{
    m_byLanguage.clear();
    m_delegateByKind.clear();
    m_moduleDefs.clear();
    m_moduleOrder.clear();
    // The adapters are NOT dropped. A Block that resolved one holds the
    // pointer until something changes its type, its content or its language,
    // and a reset that freed them would leave that pointer dangling — a crash
    // on the next thing that asks the block what it is. They are three
    // pointers each and one per module registration, so keeping them costs
    // nothing next to that.
    m_nextKind = FirstRegisteredKind;
    registerBuiltins();
}
