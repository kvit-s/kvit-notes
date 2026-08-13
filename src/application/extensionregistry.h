// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
#ifndef EXTENSIONREGISTRY_H
#define EXTENSIONREGISTRY_H

#include <QHash>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariantMap>
#include <QVariantList>

#include <memory>
#include <functional>
#include <vector>

class BlockKindRegistry;
class QQmlContext;

// The names of the UI slots main.qml offers an extension. Each is an empty
// QML Loader in the shell: the bottom bar sits between the editor and the
// status bar, the banner strip joins the conflict and oversized-file banners
// at the top, the side panel docks beside the outline and backlinks panels,
// and the document header is a strip across the top of the editor column that
// stays put while the document scrolls under it.
//
// The document header is the only one inside the editor pane rather than
// around it. Anything that has to sit BETWEEN or BESIDE the blocks themselves
// is not a slot at all: those are registered with DocumentDecorations, which
// places them per block rather than once per window.
namespace KvitSlots {
inline const char *BottomBar = "bottomBar";
inline const char *Banner = "banner";
inline const char *SidePanel = "sidePanel";
inline const char *DocumentHeader = "documentHeader";
inline const char *BottomDock = "bottomDock";
}

// A module linked on top of the core library.
//
// The open editor has no extensions of its own; the interface exists so a
// linked module can add its block kinds, its QML objects and its panels
// without any of that code — or any conditional referring to it — living in
// the core. An extension is installed into ExtensionRegistry from main()
// before the shell loads, and the core calls back into it at three points:
// once to claim block kinds, once to publish QML context properties, and
// whenever the shell asks which QML file fills a UI slot.
class KvitExtension
{
public:
    virtual ~KvitExtension() = default;

    // Identifies the module in diagnostics; must be unique.
    virtual QString name() const = 0;

    // The QML identifier this module's objects appear under. Every object the
    // module publishes is reached as `<qmlNamespace>.<key>`, so a module can
    // never take a bare global name and a collision between two modules, or
    // with one of the core's own names, is refused at install time and named
    // in the warning rather than silently shadowing something.
    //
    // Must be a valid QML identifier: a lowercase letter or underscore
    // followed by letters, digits or underscores.
    //
    // It must also not match a name the core occupies, COMPARED WITHOUT
    // REGARD TO CASE. The core's own objects reach QML as singletons of the
    // `Kvit` module — `Theme`, `BlockModel`, `NoteCollection` and the rest —
    // and those are capitalised while a namespace must start lowercase, so
    // the two can never collide as identifiers. They would instead coexist:
    // `theme.x` and `Theme.x` in one file, one character apart, standing for
    // entirely unrelated objects. Refusing the lowercase form costs a module
    // author one name and removes a class of bug that is very hard to see at
    // a distance from its cause.
    //
    // This is a deliberate restriction rather than an accident of the
    // implementation, and it is enforced in
    // ExtensionRegistry::installContextProperties. The refusal warning names
    // the core singleton it collided with and suggests an alternative, so a
    // module author meets the reason rather than only the rule. Please do not
    // relax it to an exact-match comparison without deciding again that the
    // confusion above is acceptable.
    virtual QString qmlNamespace() const = 0;

    // Claim fence languages and their delegates. Called before any block is
    // rendered, and before the QML engine exists.
    virtual void registerBlockKinds(BlockKindRegistry &registry);

    // The objects to publish under this module's namespace, keyed by the name
    // QML uses. Ownership stays with the module.
    //
    // This replaced a `installContextProperties(QQmlContext *)` callback that
    // handed each module the shell's root context and let it set any global
    // name it liked. The modules are first party and compiled into the same
    // binary, so that was never a security boundary — but it made the set of
    // names the shell exposes impossible to know by reading the core, and
    // two modules claiming one name would have resolved to whichever
    // installed last.
    virtual QVariantMap contextObjects();

    // The QML file that fills a named UI slot (see KvitSlots), or an empty
    // string for slots this module leaves alone.
    virtual QString qmlSlot(const QString &slot) const;

    // Named, independently selectable panes for the resizable bottom dock and
    // the sidebar view rail. Each map has id, title and source keys. Unlike a
    // one-owner slot, these contributions aggregate across modules.
    virtual QVariantList bottomDockTabs() const;
    virtual QVariantList sidebarViews() const;

    // ---- what this module adds to a note's export --------------------------
    //
    // A module that draws content BESIDE a note rather than inside it — through
    // the containers and margin items DocumentDecorations provides — has
    // nothing in the note's block model, so everything it draws was missing
    // from every export of that note and there was no seam for it to say so.
    // Exporting separately and concatenating afterwards is not the same thing:
    // it produces two files, and in HTML and PDF the two cannot be joined at
    // all without re-rendering, since each is a complete document with its own
    // inlined assets.
    //
    // A contribution is MARKDOWN, appended to the note's own. The exporter
    // renders it exactly as it renders the note's markdown, so it reaches all
    // four formats and every scope that exports whole notes without the module
    // knowing anything about any of them. Nothing here lets a module write: the
    // note, its block model and its undo stack are untouched by an export.

    // The markdown to append to the export of the note at `noteRelPath`, which
    // is vault-relative. Empty — the default — leaves that note's export
    // byte-identical to what it would have been with no module installed.
    virtual QString exportAppendix(const QString &noteRelPath) const;

    // The directory a relative image path inside that markdown is written
    // against. A contribution may name pictures that live nowhere near the
    // note, and the exporter's image context is the note's own folder, so a
    // module with pictures either answers here or writes absolute paths.
    // Empty means the paths are already absolute or there are none.
    virtual QString exportAppendixBaseDir() const;

    // What to call this module's contribution where the reader is told about
    // it, in the reader's own words: "Review comments", "Conversation". A
    // reader exporting a note is choosing what leaves the application, so an
    // export that would carry contributed content says so before it happens,
    // and a label is what it says.
    //
    // Empty — the default — means the module contributes to no export, and it
    // is what the notice is gated on. A module that returns a label and then
    // has nothing to add to a particular note simply adds nothing.
    virtual QString exportAppendixLabel() const;
};

// The installed extensions, in installation order.
//
// The registry is a plain list rather than a discovery mechanism: modules are
// linked into the binary and install themselves from main(), so nothing is
// loaded at runtime and the open build — which installs nothing — behaves
// exactly as if the seam did not exist.
class ExtensionRegistry : public QObject
{
    Q_OBJECT

public:
    // Instance owned, like BlockKindRegistry: it is process-global, so
    // ProcessServices holds the one the application runs on, every window
    // publishes it as the `extensions` QML context property, and main() reaches
    // it through KvitApplication. A test builds its own (via an AppContext that
    // owns its ProcessServices) and is isolated by construction.
    explicit ExtensionRegistry(QObject *parent = nullptr);
    ~ExtensionRegistry() override;

    // Takes ownership. Installing a module whose name is already installed is
    // ignored, so a double install from a second entry point is harmless.
    void install(std::unique_ptr<KvitExtension> extension);

    QStringList names() const;
    int count() const { return static_cast<int>(m_extensions.size()); }

    // The QML file filling `slot`, or an empty string when no installed module
    // fills it — which leaves the shell's Loader inactive and zero-sized. The
    // first module claiming a slot keeps it.
    Q_INVOKABLE QString slotSource(const QString &slot) const;
    Q_INVOKABLE QVariantList bottomDockTabs() const;
    Q_INVOKABLE QVariantList sidebarViews() const;
    Q_INVOKABLE QString sidebarViewSource(const QString &id) const;

    // A module may report background state for any root, including one that
    // is not current. The root rail observes revision and asks for the mark.
    Q_PROPERTY(int rootStatusRevision READ rootStatusRevision
                   NOTIFY rootStatusChanged)
    int rootStatusRevision() const { return m_rootStatusRevision; }
    Q_INVOKABLE QVariantMap rootStatus(const QString &rootPath) const;
    Q_INVOKABLE void setRootStatus(const QString &rootPath,
                                   const QString &color,
                                   const QString &tooltip);
    Q_INVOKABLE void clearRootStatus(const QString &rootPath);

    // What every installed module adds to the export of one note, in
    // installation order (see KvitExtension::exportAppendix). A module with
    // nothing to add for this note contributes no entry, so an empty list is
    // what the open build always answers and what leaves an export
    // byte-identical.
    //
    // Each contribution keeps its own base directory rather than being joined
    // into one string here: two modules may write relative image paths against
    // different folders, and joining first would lose which was which.
    struct ExportContribution
    {
        QString module;     // KvitExtension::name(), for diagnostics
        QString label;      // what the reader is told it is
        QString markdown;
        QString baseDir;
    };
    QList<ExportContribution> exportContributions(const QString &noteRelPath) const;

    // The labels of the modules that add content to exports at all, in
    // installation order. This is what the export dialog shows, and it is a
    // question about the installed set rather than about one note, so a
    // collection export can ask it once instead of per note.
    Q_INVOKABLE QStringList exportAppendixLabels() const;

    // Fan-out of the two setup callbacks, in installation order.
    void registerBlockKinds(BlockKindRegistry &registry);

    // Publishes one context property per installed module: the module's
    // qmlNamespace(), holding its contextObjects(). A namespace that is not a
    // valid identifier, that collides with another module, or that collides
    // with a name the core already published is refused with a warning and
    // the module contributes nothing to QML.
    //
    // `reservedNames` is what the core has already put on the context.
    void installContextProperties(QQmlContext *context,
                                  const QStringList &reservedNames = {});

    // The namespaces that were actually published, in installation order.
    // Empty for a module whose namespace was refused.
    QStringList publishedNamespaces() const { return m_publishedNamespaces; }

    // Removes every installed module. Tests use it to isolate cases; the app
    // never calls it.
    void clear();

signals:
    // Emitted when the installed set changes, so shell Loaders bound to
    // slotSource() re-resolve. In practice this fires only during startup.
    void extensionsChanged();
    void rootStatusChanged();

private:
    std::vector<std::unique_ptr<KvitExtension>> m_extensions;
    QStringList m_publishedNamespaces;
    QHash<QString, QVariantMap> m_rootStatuses;
    int m_rootStatusRevision = 0;
};

#endif // EXTENSIONREGISTRY_H
