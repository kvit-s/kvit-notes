// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
#ifndef VAULTWINDOW_H
#define VAULTWINDOW_H

#include <QObject>
#include <QQmlApplicationEngine>
#include <QString>
#include <QUrl>

#include "appcontext.h"

class ProcessServices;
class WindowRouter;
class QQuickWindow;

// One editor window: a per-vault AppContext composed onto its own QQmlEngine,
// which loads the shell (KvitShell) into a single ApplicationWindow. A process
// holds one of these per open vault (and per loose file), all sharing one
// ProcessServices.
//
// Member order is load-bearing: the AppContext is declared before the engine,
// so the engine — and the whole QML object tree bound to this context's
// services — is destroyed first, while those services are still alive. This
// mirrors KvitApplication's original context-before-engine ordering.
class VaultWindow : public QObject
{
    Q_OBJECT

public:
    VaultWindow(ProcessServices &globals, WindowRouter *router,
                const QUrl &shellUrl, QObject *parent = nullptr);
    ~VaultWindow() override;

    // Composes the context onto the engine and loads the shell. Returns false
    // if the shell failed to load (a QML error or a missing resource), in which
    // case the caller discards this window.
    bool load();

    // Opens the startup target in this window: a directory opens as a vault, a
    // file as a standalone document, an empty string as the default vault. This
    // reuses AppContext::applyStartupArguments so it behaves exactly like the
    // same path given on the command line.
    void openTarget(const QString &target);

    // The registry's key for this window: the canonical vault root (or loose
    // file path) it was opened on. Set by the registry, not derived from live
    // collection state — the vault opens asynchronously on the first frame, so
    // a state-derived key would be empty exactly when the registry needs it to
    // dedupe. Re-set by the registry when the window switches vault in place.
    QString key() const { return m_key; }
    void setKey(const QString &key) { m_key = key; }

    // Whether this window shows a vault (a folder) rather than a loose file.
    // The registry sets it and uses it to persist only vaults for session
    // restore; a single-file window is not restored as a vault.
    bool isVault() const { return m_isVault; }
    void setIsVault(bool isVault) { m_isVault = isVault; }

    AppContext *context() { return &m_context; }

    // The root ApplicationWindow, or null before load() has run.
    QQuickWindow *window() const;

    // Bring this window to the front and give it keyboard focus.
    void raiseWindow();

signals:
    // This window is closing for good; the registry tears it down (releasing
    // the vault lock). Driven by QML's onClosing through AppActions.
    void closed(VaultWindow *self);
    // The first frame of this window has rendered. The launcher uses the first
    // window's signal for the one process-level startup timing mark.
    void firstFrameRendered(QQuickWindow *window);

private:
    void instrumentFirstFrame();

    ProcessServices &m_globals;
    QUrl m_shellUrl;
    QString m_key;
    bool m_isVault = true;
    // Declared before the engine so it outlives it during teardown.
    AppContext m_context;
    QQmlApplicationEngine m_engine;
};

#endif // VAULTWINDOW_H
