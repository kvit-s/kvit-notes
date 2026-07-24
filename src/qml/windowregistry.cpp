// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
#include "windowregistry.h"

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
        // own current vault, this simply raises the requester.)
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
