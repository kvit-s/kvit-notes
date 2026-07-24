// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
#ifndef PROCESSSERVICES_H
#define PROCESSSERVICES_H

#include <QObject>
#include <QString>

#include <memory>

#include "blockkindregistry.h"
#include "egressfetcher.h"
#include "egresspolicy.h"
#include "extensionregistry.h"
#include "globalhotkey.h"
#include "remotemediacache.h"
#include "settingsstore.h"
#include "systemtray.h"
#include "theme.h"
#include "typography.h"
#include "updatechecker.h"

// The process-global half of the composition.
//
// A process runs one window per open vault, each its own AppContext bound to
// its own QQmlEngine. Most services are per-vault and live in AppContext; the
// ones here must be exactly one for the whole process because each owns a
// unique operating-system resource or a piece of user-global state:
//
//   * the settings file — one debounced writer, or two windows race on flush;
//   * the one network transport (the single QNetworkAccessManager) and its
//     process-wide egress budget, plus the consent policy in front of it;
//   * the bounded remote-media cache the transport feeds;
//   * the system tray icon and the system-wide hotkey;
//   * the once-a-day update check;
//   * the module and block-kind registries, installed once at startup;
//   * the user-global theme and typography.
//
// Every window shares one ProcessServices; AppContext registers these
// instances into each engine's service table alongside its own per-vault
// services, so the matching QML singletons resolve to the one shared object in
// every window.
//
// Member order is load-bearing (members destroy in reverse declaration order):
// the policy precedes the fetcher that borrows it, and the fetcher precedes the
// media cache and the update check that borrow it. ProcessServices must outlive
// every AppContext, so its owner declares it before anything holding a window.
class ProcessServices : public QObject
{
    Q_OBJECT

public:
    // The parts of the composition that reach outside the process, and so
    // cannot run the same way in a headless harness. Both fields configure
    // process-global services (the tray and the perf log), which is why they
    // live here rather than in AppContext.
    struct Options {
        // SystemTray::show() asks the desktop session for a status-notifier
        // item. Offscreen there is no session to ask.
        bool showSystemTray = true;
        // PerfLog writes to a file path taken from settings. A harness keeps
        // its own logging configuration.
        bool configureLoggingFromSettings = true;
    };

    explicit ProcessServices(QObject *parent = nullptr);
    explicit ProcessServices(const Options &options, QObject *parent = nullptr);
    ~ProcessServices() override;

    // Opens the per-user settings file, defaulting to settings.json under the
    // platform's application-config location, and attaches every global that
    // reads it (theme, typography, egress policy, update checker, tray, the
    // quick-capture hotkey, and perf logging). Call once per process, before
    // any window is created. A test or a second binary can pass its own path.
    void openSettings(const QString &settingsPath = QString());

    SettingsStore *settings() { return &m_settings; }
    Theme *theme() { return &m_theme; }
    Typography *typography() { return &m_typography; }
    EgressPolicy *egressPolicy() { return &m_egressPolicy; }
    EgressFetcher *egressFetcher() { return m_egressFetcher.get(); }
    RemoteMediaCache *remoteMediaCache() { return &m_remoteMediaCache; }
    ExtensionRegistry *extensions() { return &m_extensions; }
    BlockKindRegistry *blockKinds() { return &m_blockKinds; }
    UpdateChecker *updateChecker() { return &m_updateChecker; }
    SystemTray *systemTray() { return &m_systemTray; }
    GlobalHotkey *globalHotkey() { return &m_globalHotkey; }

private:
    void wire();

    const Options m_options;

    // Declaration order = construction order; destruction runs in reverse.
    // Settings first: theme, typography, the egress policy, the update checker
    // and the tray all read it on attach. The policy precedes the fetcher that
    // borrows it; the fetcher precedes the media cache and update check.
    SettingsStore m_settings;
    Theme m_theme;
    Typography m_typography;
    EgressPolicy m_egressPolicy;
    std::unique_ptr<EgressFetcher> m_egressFetcher;
    RemoteMediaCache m_remoteMediaCache;
    ExtensionRegistry m_extensions;
    BlockKindRegistry m_blockKinds;
    UpdateChecker m_updateChecker;
    SystemTray m_systemTray;
    GlobalHotkey m_globalHotkey;
};

#endif // PROCESSSERVICES_H
