// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
#include "vaultwindow.h"

#include <QQuickWindow>

#include <memory>

#include "appactions.h"
#include "startupcontroller.h"
#include "windowrouter.h"

VaultWindow::VaultWindow(ProcessServices &globals, WindowRouter *router,
                         const QUrl &shellUrl, QObject *parent)
    : QObject(parent)
    , m_globals(globals)
    , m_shellUrl(shellUrl)
    , m_context(globals)
{
    // This window's open actions reach the registry through the router; its
    // close reaches the registry through AppActions::windowClosing.
    m_context.setWindowRouter(router);
    connect(m_context.appActions(), &AppActions::windowClosing, this,
            [this]() { emit closed(this); });
}

VaultWindow::~VaultWindow() = default;

bool VaultWindow::load()
{
    m_context.installContextProperties(&m_engine);
    m_engine.load(m_shellUrl);
    if (m_engine.rootObjects().isEmpty())
        return false;
    instrumentFirstFrame();
    return true;
}

void VaultWindow::openTarget(const QString &target)
{
    // Reuse the command-line startup logic: a placeholder program name plus the
    // target, or a single-element list for the default vault.
    m_context.applyStartupArguments(
        target.isEmpty() ? QStringList{QString()}
                         : QStringList{QString(), target});
}

bool VaultWindow::retryTargetIfUnopened()
{
    if (!m_isVault || m_key.isEmpty())
        return false;   // a loose-file window has no vault to be missing
    NoteCollection *collection = m_context.noteCollection();
    if (collection->isOpen())
        return true;
    if (!m_context.openVaultRoot(m_key))
        return false;
    // The vault is open now, but the document is still what the failed startup
    // left behind — the sample fallback, with no note from this vault behind
    // it. Choose and load one, which is the same selection a cold launch
    // makes.
    m_context.startupController()->restartForOpenRoot();
    return true;
}

QQuickWindow *VaultWindow::window() const
{
    const QList<QObject *> roots = m_engine.rootObjects();
    return roots.isEmpty() ? nullptr
                           : qobject_cast<QQuickWindow *>(roots.first());
}

void VaultWindow::raiseWindow()
{
    QQuickWindow *win = window();
    if (!win)
        return;
    win->show();
    win->raise();
    win->requestActivate();
}

void VaultWindow::instrumentFirstFrame()
{
    QQuickWindow *win = window();
    if (!win)
        return;
    // Startup — the collection scan and initial note load — is deferred to
    // after the first frame so it never blocks this window's creation, matching
    // the single-window launcher's behaviour.
    auto started = std::make_shared<bool>(false);
    connect(win, &QQuickWindow::afterFrameEnd, this,
            [this, win, started]() {
                if (*started)
                    return;
                *started = true;
                emit firstFrameRendered(win);
                QMetaObject::invokeMethod(m_context.startupController(), "start",
                                          Qt::QueuedConnection);
            });
}
