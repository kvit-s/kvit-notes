// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
#ifndef EXTENSIONREGISTRY_H
#define EXTENSIONREGISTRY_H

#include <QObject>
#include <QString>
#include <QStringList>

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

    // Claim fence languages and their delegates. Called before any block is
    // rendered, and before the QML engine exists.
    virtual void registerBlockKinds(BlockKindRegistry &registry);

    // Publish QML context properties on the shell's root context, the way
    // AppContext publishes the core's own objects.
    virtual void installContextProperties(QQmlContext *context);

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
    // The process-wide registry, which main() installs modules into and
    // AppContext reads. Also the `extensions` QML context property, so the
    // shell's Loaders can resolve their slot sources.
    static ExtensionRegistry &instance();

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
    void installContextProperties(QQmlContext *context);

    // Removes every installed module. Tests use it to isolate cases; the app
    // never calls it.
    void clear();

signals:
    // Emitted when the installed set changes, so shell Loaders bound to
    // slotSource() re-resolve. In practice this fires only during startup.
    void extensionsChanged();

private:
    std::vector<std::unique_ptr<KvitExtension>> m_extensions;
};

#endif // EXTENSIONREGISTRY_H
