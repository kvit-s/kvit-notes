// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
#include "kvitapplication.h"

#include <QApplication>
#include <QFile>
#include <QQuickWindow>
#include <QTimer>

#include "appcontext.h"
#include "blockkindregistry.h"
#include "extensionregistry.h"
#include "perflog.h"
#include "singleinstance.h"
#include "systemtray.h"
#include "updatechecker.h"
#include "vaultwindow.h"
#include "windowregistry.h"

#ifndef KVIT_VERSION
#define KVIT_VERSION "0.0.0"
#endif

void KvitApplication::applyPlatformWorkarounds()
{
    if (!qEnvironmentVariableIsEmpty("KVIT_ALLOW_GPU_GL"))
        return;
    // An explicit non-GL rendering choice already avoids the broken path.
    if (!qEnvironmentVariableIsEmpty("QT_QUICK_BACKEND")
        || !qEnvironmentVariableIsEmpty("LIBGL_ALWAYS_SOFTWARE"))
        return;
    QFile version(QStringLiteral("/proc/version"));
    if (!version.open(QIODevice::ReadOnly))
        return;
    const QByteArray kernel = version.readAll().toLower();
    if (!kernel.contains("microsoft") && !kernel.contains("wsl"))
        return;
    qputenv("GALLIUM_DRIVER", "llvmpipe");
}

KvitApplication::KvitApplication(QApplication &app, QObject *parent)
    : QObject(parent)
    , m_app(app)
{
    m_startupTimer.start();

    m_app.setOrganizationName(QStringLiteral("Kvit"));
    m_app.setApplicationName(QStringLiteral("Kvit Notes"));
    m_app.setApplicationVersion(QStringLiteral(KVIT_VERSION));

    PerfLog::instance().configureFromEnvironment();

    AppContext::applyQuickStyle();
}

KvitApplication::~KvitApplication() = default;

KvitApplication::StartOutcome KvitApplication::start(const QStringList &arguments)
{
    AppContext::registerQmlTypes();

    const QString target = arguments.size() > 1 ? arguments.at(1) : QString();

    // Single-instance: a later launch forwards its request to the already
    // running process and exits, so tray-resident processes never accumulate.
    // Disabled for the in-process UI driver and by KVIT_NO_SINGLE_INSTANCE.
    if (m_singleInstanceEnabled
        && qEnvironmentVariableIsEmpty("KVIT_NO_SINGLE_INSTANCE")) {
        m_single = std::make_unique<SingleInstance>(
            SingleInstance::defaultServerName());
        if (!m_single->tryBecomePrimary()) {
            m_single->forwardToPrimary(target);
            return StartOutcome::AlreadyRunning;
        }
        connect(m_single.get(), &SingleInstance::requestReceived, this,
                [this](const QString &t) {
                    if (!m_registry)
                        return;
                    // A bare relaunch (no path) just brings the app forward.
                    if (t.isEmpty()) {
                        if (VaultWindow *w = m_registry->activeWindow()) {
                            w->raiseWindow();
                            return;
                        }
                    }
                    m_registry->openStartup(t);
                    if (VaultWindow *w = m_registry->activeWindow())
                        w->raiseWindow();
                });
    }

    // Installed modules claim their fence languages before anything renders, so
    // the first block a window lays out already resolves to the right delegate.
    // The registries are process-global; this runs once. The open build
    // installs no module, and this is a no-op.
    m_processServices.extensions()->registerBlockKinds(
        *m_processServices.blockKinds());

    m_processServices.openSettings();

    // Closing the last window quits unless the user opted into staying resident
    // in the tray (tray.closeToTray) and a tray exists. Qt's
    // quitOnLastWindowClosed already generalizes from one window to the set, so
    // the policy is unchanged from the single-window launcher. Applied live so
    // the Settings toggle takes effect without a restart.
    const auto applyQuitPolicy = [this]() {
        m_app.setQuitOnLastWindowClosed(
            !(m_processServices.systemTray()->available()
              && m_processServices.systemTray()->closeToTray()));
    };
    applyQuitPolicy();
    connect(m_processServices.systemTray(), &SystemTray::closeToTrayChanged,
            &m_app, applyQuitPolicy);
    connect(m_processServices.systemTray(), &SystemTray::quitRequested,
            &m_app, &QApplication::quit);

    m_registry = std::make_unique<WindowRegistry>(m_processServices, m_shellUrl);

    // Tray menu actions (new note, quick capture, show) are handled in each
    // window's shell (qml/SystemIntegration.qml), gated on AppActions.trayTarget
    // so only the window the registry marked active responds to the one shared
    // tray. The registry sets that flag as the active window changes.

    PerfLog::instance().mark(QStringLiteral("startup.pre_qml"),
                             m_startupTimer.elapsed());

    // The one process-level first-frame mark, taken from the first window.
    connect(m_registry.get(), &WindowRegistry::windowOpened, this,
            [this](VaultWindow *w) {
                connect(w, &VaultWindow::firstFrameRendered, this,
                        [this](QQuickWindow *) {
                            PerfLog::instance().mark(
                                QStringLiteral("startup.first_frame"),
                                m_startupTimer.elapsed());
                        }, Qt::SingleShotConnection);
            }, Qt::SingleShotConnection);

    // A bare launch reopens the last session's windows; a specific file or
    // folder opens just that.
    const bool opened = target.isEmpty() ? m_registry->openSession()
                                         : m_registry->openStartup(target);
    if (!opened)
        return StartOutcome::Failed;

    // The update check runs only in the real launcher path: tests compose
    // AppContext directly and never receive a fetcher. Delayed well past first
    // paint; UpdateChecker enforces opt-out and once-per-day.
    UpdateChecker *updates = m_processServices.updateChecker();
    updates->setCurrentVersion(m_app.applicationVersion());
    updates->setFetcher(m_processServices.egressFetcher());
    QTimer::singleShot(5000, updates, &UpdateChecker::maybeCheck);

    return StartOutcome::RunEventLoop;
}
