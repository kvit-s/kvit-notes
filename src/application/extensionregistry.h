// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
#ifndef EXTENSIONREGISTRY_H
#define EXTENSIONREGISTRY_H

#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariantMap>

#include <memory>
#include <vector>

class BlockKindRegistry;
class QQmlContext;

// The names of the UI slots main.qml offers an extension. Each is an empty
// QML Loader in the shell: the bottom bar sits between the editor and the
// status bar, the banner strip joins the conflict and oversized-file banners
// at the top, and the side panel docks beside the outline and backlinks
// panels.
namespace KvitSlots {
inline const char *BottomBar = "bottomBar";
inline const char *Banner = "banner";
inline const char *SidePanel = "sidePanel";
}

// A module linked on top of the core library.
//
// The open editor has no extensions of its own; the interface exists so the
// premium build can add its block kinds, its QML objects and its panels
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
    // Instance owned, like BlockKindRegistry: AppContext holds the one the
    // application runs on, publishes it as the `extensions` QML context
    // property, and hands it to main() through KvitApplication. A test builds
    // its own and is isolated by construction.
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

private:
    std::vector<std::unique_ptr<KvitExtension>> m_extensions;
    QStringList m_publishedNamespaces;
};

#endif // EXTENSIONREGISTRY_H
