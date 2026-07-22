// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
#include "kvitapplication.h"

#include <QApplication>
#include <QFile>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QTimer>

#include <memory>

#include "blockkindregistry.h"
#include "extensionregistry.h"
#include "perflog.h"
#include "updatechecker.h"

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

bool KvitApplication::start(const QStringList &arguments)
{
    AppContext::registerQmlTypes();

    // Installed modules claim their fence languages before anything renders,
    // so the first block the shell lays out already resolves to the right
    // delegate. The open build installs no module, and this is a no-op.
    m_context.extensions()->registerBlockKinds(*m_context.blockKinds());

    m_context.openSettings();
    m_context.applyStartupArguments(arguments);

    // Closing the last window quits unless the user opted into staying
    // resident in the tray (tray.closeToTray) and a tray actually exists.
    // Applied live so the Settings toggle takes effect without a restart.
    const auto applyQuitPolicy = [this]() {
        m_app.setQuitOnLastWindowClosed(
            !(m_context.systemTray()->available()
              && m_context.systemTray()->closeToTray()));
    };
    applyQuitPolicy();
    connect(m_context.systemTray(), &SystemTray::closeToTrayChanged,
            &m_app, applyQuitPolicy);
    connect(m_context.systemTray(), &SystemTray::quitRequested,
            &m_app, &QApplication::quit);

    m_context.installContextProperties(&m_engine);

    const QUrl url = m_shellUrl;
    connect(&m_engine, &QQmlApplicationEngine::objectCreated,
            &m_app, [url](QObject *obj, const QUrl &objUrl) {
                if (!obj && url == objUrl)
                    QCoreApplication::exit(-1);
            }, Qt::QueuedConnection);

    PerfLog::instance().mark(
        QStringLiteral("startup.pre_qml"),
        m_startupTimer.elapsed(),
        QVariantMap{
            {QStringLiteral("notes"), m_context.noteCollection()->noteCount()},
            {QStringLiteral("blocks"), m_context.blockModel()->count()},
        });

    m_engine.load(m_shellUrl);
    if (m_engine.rootObjects().isEmpty())
        return false;

    instrumentFirstFrame();

    // The update check runs only in the real launcher path: tests compose
    // AppContext directly and never receive a fetcher, so they cannot reach
    // the network here. Delayed well past first paint to stay off the
    // startup path; UpdateChecker itself enforces opt-out and once-per-day.
    UpdateChecker *updates = m_context.updateChecker();
    updates->setCurrentVersion(m_app.applicationVersion());
    updates->setFetcher(m_context.egressFetcher());
    QTimer::singleShot(5000, updates, &UpdateChecker::maybeCheck);

    return true;
}

void KvitApplication::instrumentFirstFrame()
{
    QQuickWindow *window =
        qobject_cast<QQuickWindow *>(m_engine.rootObjects().first());
    if (!window)
        return;

    auto firstFrameLogged = std::make_shared<bool>(false);
    auto frameTimer = std::make_shared<QElapsedTimer>();
    frameTimer->start();
    connect(window, &QQuickWindow::afterFrameEnd, window,
            [this, firstFrameLogged, frameTimer]() {
        PerfLog &perfLog = PerfLog::instance();
        const qint64 frameMs = frameTimer->restart();
        BlockModel *blockModel = m_context.blockModel();
        if (!*firstFrameLogged) {
            *firstFrameLogged = true;
            perfLog.mark(
                QStringLiteral("startup.first_frame"),
                m_startupTimer.elapsed(),
                QVariantMap{
                    {QStringLiteral("notes"),
                     m_context.noteCollection()->noteCount()},
                    {QStringLiteral("blocks"), blockModel->count()},
                });
            QMetaObject::invokeMethod(m_context.startupController(), "start",
                                      Qt::QueuedConnection);
        }
        perfLog.record(
            QStringLiteral("frame"), frameMs,
            QVariantMap{{QStringLiteral("blocks"), blockModel->count()}},
            PerfLog::Verbose, 16.0);
    });
}
