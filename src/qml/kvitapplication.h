// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
#ifndef KVITAPPLICATION_H
#define KVITAPPLICATION_H

#include <QElapsedTimer>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QUrl>

#include <memory>

#include "processservices.h"

class QApplication;
class SingleInstance;
class WindowRegistry;

// The editor's launcher and process shell: it owns the process-global services
// (ProcessServices) and the window registry, applies the application-level
// policy that needs the QApplication itself (window style, tray-driven quit),
// and hands the startup request to the registry, which opens the first window.
//
// Each editor window is a VaultWindow the registry owns — its own AppContext
// bound to its own QQmlEngine — so this class no longer owns a single context
// or engine. A superset build that adds a premium module installs its
// extensions into ProcessServices before calling start(), and otherwise reuses
// this class unchanged.
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

    // What start() decided the caller should do next.
    enum class StartOutcome {
        RunEventLoop,    // this process is the primary; run app.exec()
        AlreadyRunning,  // a primary took the request; exit cleanly (0)
        Failed,          // the first window failed to load; exit non-zero
    };

    // Composes the process globals and opens the first window for the startup
    // request, unless another instance is already running — in which case the
    // request is forwarded to it and this returns AlreadyRunning. `arguments`
    // is the whole argv-derived list.
    StartOutcome start(const QStringList &arguments);

    // Disable the single-instance channel (the in-process UI driver and any
    // harness that must run its own windows alongside a real instance). On by
    // default; the KVIT_NO_SINGLE_INSTANCE environment variable also disables it.
    void setSingleInstanceEnabled(bool on) { m_singleInstanceEnabled = on; }

    // The QML file each window loads as its shell. A superset build can point
    // this at its own root window before calling start(); it defaults to the
    // open shell.
    void setShellUrl(const QUrl &url) { m_shellUrl = url; }
    QUrl shellUrl() const { return m_shellUrl; }

    // The process-global composition, shared by every window. A premium main()
    // installs its module here before start().
    ProcessServices &processServices() { return m_processServices; }
    WindowRegistry *registry() { return m_registry.get(); }

private:
    QApplication &m_app;
    QElapsedTimer m_startupTimer;
    QUrl m_shellUrl{QStringLiteral("qrc:/qml/main.qml")};
    bool m_singleInstanceEnabled = true;
    std::unique_ptr<SingleInstance> m_single;
    // The globals outlive the registry (which owns the windows that borrow
    // them), so they are declared first and destroyed last.
    ProcessServices m_processServices;
    std::unique_ptr<WindowRegistry> m_registry;
};

#endif // KVITAPPLICATION_H
