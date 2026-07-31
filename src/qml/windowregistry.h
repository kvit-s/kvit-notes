// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
#ifndef WINDOWREGISTRY_H
#define WINDOWREGISTRY_H

#include <QHash>
#include <QObject>
#include <QString>
#include <QUrl>

#include <memory>
#include <vector>

#include "windowrouter.h"

class AppContext;
class ProcessServices;
class VaultWindow;

// The process-wide map from an open vault (or loose file) to the window showing
// it. It creates a window when something new is opened, raises the existing one
// when it is already open, and tears a window down when it closes (releasing
// that vault's kernel lock). "Is this already open here?" is answered here,
// which is what turns the cross-process lock's refusal into an in-process
// window raise.
//
// Every open request — a menu item, a command-line argument, or a forwarded
// second launch — funnels through openStartup() or the WindowRouter methods, so
// the dedupe rule lives in exactly one place.
class WindowRegistry : public QObject, public WindowRouter
{
    Q_OBJECT

public:
    WindowRegistry(ProcessServices &globals, const QUrl &shellUrl,
                   QObject *parent = nullptr);
    ~WindowRegistry() override;

    // A directory opens as a vault, a file as a standalone document, an empty
    // string as the default vault. An already-open target raises its window;
    // otherwise a new window is created. Returns false only when a newly
    // created window failed to load its shell.
    bool openStartup(const QString &target);

    // Reopen the vaults that were open at the last quit (a bare cold launch).
    // Falls back to the default vault when nothing was remembered — a first run
    // or a session with no vaults. Returns false only when every attempt failed
    // to load its shell.
    bool openSession();

    // WindowRouter — actions from a menu or dialog inside a window.
    void openVaultInWindow(AppContext *requester, const QString &path) override;
    void openVaultInNewWindow(const QString &path) override;
    void openFileInNewWindow(const QString &path) override;

    int windowCount() const { return int(m_windows.size()); }

    // Ask every window to close, exactly as closing each one by hand would.
    // Returns false as soon as one refuses — a save that failed, or a document
    // that has never been saved and whose question is now on screen — leaving
    // that window in front of the user and the rest of them open. Quitting
    // from the tray goes through here, because the orderly save that protects
    // unsaved work lives in the shell's close handler and nowhere else.
    bool requestCloseAll();

    // The most recently focused window, or the last one opened; null when none
    // are open. Used to route tray actions and a warm bare launch.
    VaultWindow *activeWindow() const { return m_active; }

signals:
    void windowCountChanged();
    // A new window was created (before its first frame). The launcher hooks the
    // first one for its process-level startup timing mark.
    void windowOpened(VaultWindow *window);

private:
    static QString canonicalKey(const QString &path);
    static QString defaultVaultPath();
    QString keyForTarget(const QString &target) const;
    VaultWindow *createWindow(const QString &target, const QString &key);
    void removeWindow(VaultWindow *w);
    void recordRecentVault(const QString &canonicalPath);
    void persistOpenVaults();
    // Make `w` the active window and the sole target of the shared tray's menu
    // actions (every other window's tray gate closes).
    void setActive(VaultWindow *w);

    ProcessServices &m_globals;
    QUrl m_shellUrl;
    // Ownership. Keyed lookup is the parallel m_byKey; both are kept in step.
    std::vector<std::unique_ptr<VaultWindow>> m_windows;
    QHash<QString, VaultWindow *> m_byKey;
    VaultWindow *m_active = nullptr;
};

#endif // WINDOWREGISTRY_H
