// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
#include "windowregistry.h"

#include <QCloseEvent>
#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QQuickWindow>
#include <QStandardPaths>
#include <QStringList>
#include <QVariant>

#include <algorithm>

#include "appactions.h"
#include "appcontext.h"
#include "processservices.h"
#include "settingsstore.h"
#include "vaultpaths.h"
#include "vaultwindow.h"

namespace {
// The recent-vaults list is a switching convenience, not history; a short cap
// keeps the menu usable.
constexpr int kMaxRecentVaults = 10;
}

WindowRegistry::WindowRegistry(ProcessServices &globals, const QUrl &shellUrl,
                               QObject *parent)
    : QObject(parent)
    , m_globals(globals)
    , m_shellUrl(shellUrl)
{
}

WindowRegistry::~WindowRegistry() = default;

QString WindowRegistry::canonicalKey(const QString &path)
{
    // The same canonicalization NoteCollection uses for containment and
    // VaultLock uses for its lock key, so the registry, the lock and the
    // repository all agree on when two paths are the same vault.
    return path.isEmpty() ? QString() : VaultPaths::canonicalizeMissingOk(path);
}

QString WindowRegistry::defaultVaultPath()
{
    return QDir(QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation))
        .filePath(QStringLiteral("Kvit"));
}

QString WindowRegistry::keyForTarget(const QString &target) const
{
    if (target.isEmpty())
        return canonicalKey(defaultVaultPath());
    const QFileInfo info(target);
    return canonicalKey(info.exists() ? info.absoluteFilePath() : target);
}

bool WindowRegistry::openStartup(const QString &target)
{
    const QString key = keyForTarget(target);
    if (VaultWindow *existing = m_byKey.value(key)) {
        existing->retryTargetIfUnopened();
        existing->raiseWindow();
        setActive(existing);
        return true;
    }
    return createWindow(target, key) != nullptr;
}

bool WindowRegistry::openSession()
{
    // Reopen the vaults that were open at the last quit. Copy the list first:
    // each openStartup persists the (growing) open set, which rewrites the same
    // setting we are iterating.
    const QStringList vaults =
        m_globals.settings()->value(QStringLiteral("session.openVaults"))
            .toStringList();
    bool any = false;
    for (const QString &path : vaults) {
        if (QFileInfo(path).isDir() && openStartup(path))
            any = true;
    }
    // First run, or a session with no vaults: fall back to the default vault,
    // created and seeded on first open.
    if (!any)
        return openStartup(QString());
    return true;
}

void WindowRegistry::openVaultInNewWindow(const QString &path)
{
    const QString key = keyForTarget(path);
    if (VaultWindow *existing = m_byKey.value(key)) {
        // A window is registered for its vault from the moment it is created,
        // but the vault is taken by the deferred startup that runs after the
        // first frame — and that is where another process holding the lock
        // refuses it. Raising a window showing that refusal is not an answer
        // to "open this vault", so it is offered again first.
        existing->retryTargetIfUnopened();
        existing->raiseWindow();
        setActive(existing);
        return;
    }
    createWindow(path, key);
}

void WindowRegistry::openFileInNewWindow(const QString &path)
{
    // A loose file is keyed like a vault; createWindow's openTarget routes it to
    // single-file mode because the target names a file, not a directory.
    openVaultInNewWindow(path);
}

void WindowRegistry::openVaultInWindow(AppContext *requester, const QString &path)
{
    const QString key = keyForTarget(path);
    if (VaultWindow *existing = m_byKey.value(key)) {
        // Already open somewhere — raise it rather than open a duplicate, even
        // when the requester is a different window. (When it IS the requester's
        // own current vault, this simply raises the requester.) A window
        // registered for a vault it never managed to take is offered it again
        // rather than merely raised; see openVaultInNewWindow.
        existing->retryTargetIfUnopened();
        existing->raiseWindow();
        setActive(existing);
        return;
    }

    VaultWindow *reqWin = nullptr;
    for (const auto &w : m_windows) {
        if (w->context() == requester) {
            reqWin = w.get();
            break;
        }
    }
    if (!reqWin) {
        createWindow(path, key);
        return;
    }

    // Switch this window in place. openVaultRoot sets the new root
    // synchronously (and is refused — leaving the old vault open — when a
    // different process holds the lock), so on success we re-key at once.
    if (reqWin->context()->openVaultRoot(path)) {
        m_byKey.remove(reqWin->key());
        reqWin->setKey(key);
        reqWin->setIsVault(true);   // switched from a file window or another vault
        m_byKey.insert(key, reqWin);
        recordRecentVault(key);
        persistOpenVaults();
    }
}

VaultWindow *WindowRegistry::createWindow(const QString &target,
                                          const QString &key)
{
    auto owned = std::make_unique<VaultWindow>(m_globals, this, m_shellUrl);
    VaultWindow *w = owned.get();
    w->setKey(key);
    // Queued: the close arrives from inside QML's onClosing, so defer teardown
    // until that call stack unwinds rather than deleting the window under it.
    connect(w, &VaultWindow::closed, this,
            [this](VaultWindow *self) { removeWindow(self); },
            Qt::QueuedConnection);

    if (!w->load())
        return nullptr;   // owned is destroyed here; the shell failed to load
    w->openTarget(target);

    if (QQuickWindow *win = w->window()) {
        connect(win, &QWindow::activeChanged, this, [this, w, win]() {
            if (win->isActive())
                setActive(w);
        });
    }

    // A folder target (or the default) is a vault; a file target is a loose
    // single-file window, which is neither remembered as recent nor restored.
    const bool isVault = target.isEmpty() || QFileInfo(target).isDir();
    w->setIsVault(isVault);

    m_byKey.insert(key, w);
    m_windows.push_back(std::move(owned));
    setActive(w);   // now that w is in m_windows, so the tray gate can find it

    if (isVault && !key.isEmpty())
        recordRecentVault(key);
    persistOpenVaults();

    emit windowOpened(w);
    emit windowCountChanged();
    return w;
}

bool WindowRegistry::requestCloseAll()
{
    // Snapshot the windows: an accepted close tells the registry to release
    // that window, which erases from m_windows. That teardown is queued, so it
    // cannot fire inside this loop (nothing here runs an event loop), but the
    // snapshot makes the loop independent of it either way.
    std::vector<VaultWindow *> windows;
    windows.reserve(m_windows.size());
    for (const auto &w : m_windows)
        windows.push_back(w.get());

    for (VaultWindow *w : windows) {
        QQuickWindow *win = w->window();
        if (!win)
            continue;

        // Ask, rather than close. The shell's onClosing handler is where the
        // orderly save lives, and it answers by refusing the close when it
        // cannot finish: a failed write, or a never-saved document whose
        // question it has just put on screen. QQuickWindow::close() is the
        // wrong instrument for that question, because it runs the handler for
        // a window on screen and silently skips it for one already hidden
        // into the tray — and a tray-resident window is exactly the one whose
        // document nobody has looked at recently.
        QCloseEvent closing;
        QCoreApplication::sendEvent(win, &closing);
        if (!closing.isAccepted()) {
            w->raiseWindow();   // put what is in the way in front of the user
            return false;
        }
        // Accepted. The handler has already done everything that has to happen
        // — saved the document, and released the vault unless the window is
        // only hiding into the tray — so all that is left is to take the
        // window off the screen. setVisible() rather than close(), which would
        // deliver a second close event and run the handler (and its
        // notifyWindowClosing) twice.
        win->setVisible(false);
    }
    return true;
}

void WindowRegistry::removeWindow(VaultWindow *w)
{
    if (!w)
        return;
    if (m_byKey.value(w->key()) == w)
        m_byKey.remove(w->key());
    if (m_active == w)
        m_active = nullptr;
    const auto it = std::find_if(m_windows.begin(), m_windows.end(),
                                 [w](const std::unique_ptr<VaultWindow> &p) {
                                     return p.get() == w;
                                 });
    if (it != m_windows.end())
        m_windows.erase(it);   // destroys the VaultWindow (releases its lock)
    if (!m_active && !m_windows.empty())
        setActive(m_windows.back().get());
    persistOpenVaults();
    emit windowCountChanged();
}

void WindowRegistry::setActive(VaultWindow *w)
{
    m_active = w;
    // Exactly one window is the tray's target, so a tray menu action reaches
    // one window rather than firing in all of them.
    for (const auto &win : m_windows)
        win->context()->appActions()->setTrayTarget(win.get() == w);
}

void WindowRegistry::persistOpenVaults()
{
    // The vault windows currently open, so a bare cold launch can restore them.
    // Loose single-file windows are excluded.
    //
    // A window whose vault the deferred startup could not take — another
    // process was holding it — is still recorded, deliberately. It is the
    // vault the reader asked for, the refusal is usually temporary, and the
    // next launch retrying it is what they want; openSession() drops one whose
    // directory has gone. Filtering on "did this window actually get its
    // vault" was tried and is worse: a second process refused the same vault
    // would then rewrite this setting to an empty list and erase the running
    // session's own record of where it was.
    QStringList vaults;
    for (const auto &w : m_windows) {
        if (w->isVault() && !w->key().isEmpty())
            vaults << w->key();
    }
    m_globals.settings()->setValue(QStringLiteral("session.openVaults"), vaults);
}

void WindowRegistry::recordRecentVault(const QString &canonicalPath)
{
    if (canonicalPath.isEmpty())
        return;
    SettingsStore *settings = m_globals.settings();
    QStringList recent =
        settings->value(QStringLiteral("session.recentVaults")).toStringList();
    recent.removeAll(canonicalPath);
    recent.prepend(canonicalPath);
    while (recent.size() > kMaxRecentVaults)
        recent.removeLast();
    settings->setValue(QStringLiteral("session.recentVaults"), recent);
}
