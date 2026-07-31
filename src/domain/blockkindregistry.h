// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
#ifndef BLOCKKINDREGISTRY_H
#define BLOCKKINDREGISTRY_H

#include <QHash>
#include <QList>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariantList>

#include "block.h"
#include "blockkind.h"

#include <memory>

class BlockKindDef;

// The fence-language → delegate-kind registry.
//
// Every block carries a "delegate kind": the value the QML DelegateChooser
// watches to decide which delegate renders the row. For most blocks the kind
// follows the block type, but a code fence's kind depends on its language, so
// `kanban`, `toc`, `mermaid` and `query` fences each render through their own
// delegate instead of a plain code block. Those four are built in.
//
// A module linked on top of the core library registers its own fence language
// here at startup together with the QML file that renders it. Adding a block
// kind therefore no longer means editing BlockModel and main.qml: main.qml
// turns each registered entry into a DelegateChoice when the block list is
// created.
//
// Registration happens once during startup, before any block is rendered.
// Lookups are read-only afterwards and safe from any thread.
class BlockKindRegistry : public QObject
{
    Q_OBJECT

public:
    // Module-registered kinds are numbered from here: above every Block type
    // value and every built-in kind, so a module can never collide with core.
    static constexpr int FirstRegisteredKind = 200;

    // Instance owned, deliberately. It is one-per-process, so ProcessServices
    // holds the one the application runs on and every window publishes it as
    // the `blockKinds` context property; a test constructs its own (via an
    // AppContext that owns its ProcessServices) and cannot disturb, or be
    // disturbed by, anything else in the process. There is no instance() and
    // there should not be: a static registry made every test that touched a
    // fence kind depend on reset() being called in the right order.
    explicit BlockKindRegistry(QObject *parent = nullptr);

    // Registers `language` as a block kind of its own, rendered by the QML
    // file at `delegateUrl` (for example "qrc:/module/MyBlock.qml").
    // Returns the assigned kind. Registering a language that is already
    // registered returns the existing kind and leaves its delegate alone, so
    // a module cannot take over a built-in fence.
    int registerFenceLanguage(const QString &language,
                              const QString &delegateUrl);

    // The kind for a fence language, or 0 when the language is not
    // registered. Zero is the paragraph/heading kind and never a fence kind,
    // so it reads unambiguously as "no fence kind for this language".
    Q_INVOKABLE int kindForLanguage(const QString &language) const;

    // The QML file rendering a kind, empty when the kind shares another
    // kind's delegate — as all four heading levels share the paragraph's.
    Q_INVOKABLE QString delegateUrl(int kind) const;

    // Every kind that has a delegate of its own, as a
    // {kind, id, delegateUrl} map, in catalog order. The shell builds one
    // DelegateChoice per entry.
    //
    // This used to list only the kinds a module registered, because the
    // seventeen built-in choices were written out in main.qml by hand — and
    // a kind whose choice nobody remembered to add rendered as an empty row
    // with nothing to say so. A kind reaches the screen by being registered
    // now, which is the same rule for a built-in and for a module's.
    Q_INVOKABLE QVariantList delegateChoices() const;

    // Registers a kind a module implements itself, as a BlockKindDef of its
    // own. The def must outlive the registry — a module's defs are statics in
    // the module, exactly as the built-ins are statics here — and nothing is
    // owned or deleted. Returns the assigned kind number, or the existing one
    // when the def's fence language is already claimed.
    //
    // This is the fuller form of registerFenceLanguage above. A module that
    // calls only that one gets a kind number and a delegate, and its blocks
    // behave as code blocks in every other respect; a module that supplies a
    // def decides its own serialization, text and export as well.
    int registerKind(const BlockKindDef *def);

    // The definition of a kind: everything decided per kind, in one object.
    // Null for a number nothing has registered.
    const BlockKindDef *def(BlockKind kind) const;
    const BlockKindDef *def(int kind) const;

    // The definition a block's stored state resolves to. Never null.
    const BlockKindDef *defFor(const Block::State &state) const;

    // Every kind this registry knows, built-ins first in the order the block
    // menu lists them, then module kinds in registration order.
    //
    // A list, not a hash: the block menu emits a group heading whenever an
    // entry's group differs from the one before it, and breaks search ties by
    // catalog position. Iterating a hash there would give duplicate headings
    // and results that changed between runs.
    QList<const BlockKindDef *> all() const;

    // The registered fence languages, built-ins included.
    QStringList languages() const;

    // Drops module registrations and restores the built-ins. Tests use it to
    // start from a known state; the app never calls it.
    void reset();

private:
    void registerBuiltins();

    struct Entry {
        QString language;
        int kind = 0;
        QString delegateUrl;
    };

    QHash<QString, Entry> m_byLanguage;
    QHash<int, QString> m_delegateByKind;
    // Defs a module supplied, and the adapters made for a module that
    // registered a fence language without one. Both are keyed by kind number
    // and neither is owned; the adapters are the one exception and are held
    // in m_ownedAdapters below.
    QHash<int, const BlockKindDef *> m_moduleDefs;
    QList<int> m_moduleOrder;
    QList<std::shared_ptr<const BlockKindDef>> m_ownedAdapters;
    int m_nextKind = FirstRegisteredKind;
};

#endif // BLOCKKINDREGISTRY_H
