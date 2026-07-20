// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
#ifndef BLOCKKINDREGISTRY_H
#define BLOCKKINDREGISTRY_H

#include <QHash>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QtQml/qqmlregistration.h>

// The built-in delegate kinds for code fences whose language selects a
// renderer of its own. They live here rather than in BlockModel so the
// registry can seed itself without depending on the model; BlockModel keeps
// its historical constant names as aliases.
//
// This is a Q_NAMESPACE enum rather than a set of integer constants so QML
// can name the same values. main.qml writes `roleValue: BlockKinds.Kanban`,
// and the numbers exist in exactly one place. They used to be repeated as
// literals in the DelegateChooser with a comment naming the C++ constant,
// which is a pairing nothing checked.
namespace BlockKinds {
Q_NAMESPACE
QML_ELEMENT

enum Kind {
    // A `kanban`-tagged fence renders as a board.
    Kanban = 100,
    // A `toc`-tagged fence renders as a read-only linked heading list.
    Toc = 101,
    // An image expression whose URL is a web page or video host renders as a
    // preview card. Derived from block CONTENT, not from a fence language, so
    // it is not a registry entry — the value lives here to keep the numbering
    // in one place, and it still needs a delegate like the others.
    Embed = 102,
    // A `mermaid`-tagged fence renders as a native diagram.
    Mermaid = 103,
    // A `query`-tagged fence renders as a live collection query.
    Query = 104,
};
Q_ENUM_NS(Kind)
}

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

    // Instance owned, deliberately. AppContext holds the one the application
    // runs on and publishes it as the `blockKinds` context property; a test
    // constructs its own and cannot disturb, or be disturbed by, anything
    // else in the process. There is no instance() and there should not be:
    // a process-global registry made every test that touched a fence kind
    // depend on reset() being called in the right order.
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

    // The QML file rendering a registered kind. Empty for the built-in kinds,
    // whose delegates are declared statically in main.qml.
    Q_INVOKABLE QString delegateUrl(int kind) const;

    // Every module-registered kind as a {kind, language, delegateUrl} map.
    // main.qml appends one DelegateChoice per entry.
    Q_INVOKABLE QVariantList registeredDelegates() const;

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
    int m_nextKind = FirstRegisteredKind;
};

#endif // BLOCKKINDREGISTRY_H
