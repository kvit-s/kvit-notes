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

// The built-in delegate kinds for code fences whose language selects a
// renderer of its own. They live here rather than in BlockModel so the
// registry can seed itself without depending on the model; BlockModel keeps
// its historical constant names as aliases.
namespace BlockKinds {
// A `kanban`-tagged fence renders as a board (phase10-plan.md decision 9).
constexpr int Kanban = 100;
// A `toc`-tagged fence renders as a read-only linked heading list
// (phase11-plan.md decision 4).
constexpr int Toc = 101;
// An image expression whose URL is a web page or video host renders as a
// preview card (phase11-plan.md decision 11). Derived from block CONTENT, not
// from a fence language, so it is not a registry entry — the value is listed
// here only to keep the numbering in one place.
constexpr int Embed = 102;
// A `mermaid`-tagged fence renders as a native diagram (diagrams-prd.md §5.1).
constexpr int Mermaid = 103;
// A `query`-tagged fence renders as a live collection query
// (pre-launch-plan.md §1.4).
constexpr int Query = 104;
}

// The fence-language → delegate-kind registry (chat.md §8, seam 2).
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

    // The process-wide registry. BlockModel::delegateKindForBlock() reads it,
    // and it is the object main.qml sees as the `blockKinds` context property.
    static BlockKindRegistry &instance();

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
