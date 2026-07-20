// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
#ifndef KVITAPPLICATION_H
#define KVITAPPLICATION_H

#include <QElapsedTimer>
#include <QObject>
#include <QQmlApplicationEngine>
#include <QString>
#include <QStringList>
#include <QUrl>

#include "appcontext.h"

class QApplication;

// The editor's launcher: it owns the composed AppContext and the QML engine,
// applies the application-level policy that needs the QApplication itself
// (window style, tray-driven quit behaviour), loads the shell, and installs
// the startup performance instrumentation.
//
// Together with AppContext this is everything main() used to do, which leaves
// the stock main() a nine-line file (chat.md §8: the open repo's executable is
// a thin launcher linking the core library). A superset build that adds a
// premium module installs its extensions into ExtensionRegistry before calling
// start(), and otherwise reuses this class unchanged.
class KvitApplication : public QObject
{
    Q_OBJECT

public:
    // Environment repairs that must run BEFORE QApplication is constructed:
    // the platform plugin initializes EGL immediately, and Mesa reads its
    // driver selection from the environment at that moment. Under WSL, GPU
    // GL through the d3d12 Gallium driver corrupts Qt Quick glyph rendering
    // (text loses color channels or alpha), so GL is pinned to llvmpipe
    // there — including over an inherited GALLIUM_DRIVER=d3d12. Set
    // KVIT_ALLOW_GPU_GL=1 to opt back into whatever the environment says.
    // A no-op outside WSL.
    static void applyPlatformWorkarounds();

    // Sets the organization and application names and starts the startup
    // clock, so the timings cover everything after QApplication construction.
    explicit KvitApplication(QApplication &app, QObject *parent = nullptr);
    ~KvitApplication() override;

    // Composes the app and loads the QML shell. `arguments` is the whole
    // argv-derived list. Returns false when the shell failed to load, which
    // the caller reports as a non-zero exit status.
    bool start(const QStringList &arguments);

    // The QML file loaded as the shell. A superset build can point this at its
    // own root window before calling start(); it defaults to the open shell.
    void setShellUrl(const QUrl &url) { m_shellUrl = url; }
    QUrl shellUrl() const { return m_shellUrl; }

    AppContext &context() { return m_context; }
    QQmlApplicationEngine &engine() { return m_engine; }

private:
    void instrumentFirstFrame();

    QApplication &m_app;
    QElapsedTimer m_startupTimer;
    QUrl m_shellUrl{QStringLiteral("qrc:/qml/main.qml")};
    // Declared before the engine so it is destroyed AFTER it. QQmlContext
    // nulls QObject-valued properties as their targets are destroyed; if that
    // happened before QML teardown, live bindings would report cascades of
    // "Cannot read property ... of null" during application exit.
    AppContext m_context;
    QQmlApplicationEngine m_engine;
};

#endif // KVITAPPLICATION_H
