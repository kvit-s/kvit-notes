// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
#include "processservices.h"

#include <QDir>
#include <QStandardPaths>
#include <QVariant>

#include "perflog.h"

ProcessServices::ProcessServices(QObject *parent)
    : ProcessServices(Options{}, parent)
{
}

ProcessServices::ProcessServices(const Options &options, QObject *parent)
    : QObject(parent)
    , m_options(options)
    , m_egressFetcher(std::make_unique<EgressFetcher>())
{
    wire();
}

ProcessServices::~ProcessServices() = default;

void ProcessServices::wire()
{
    // Every outbound request in the app runs over one fetcher, which asks one
    // policy. Embed previews, the images those previews name, remote images
    // and media in a note, and the update check all pass through here.
    m_egressFetcher->setPolicy(&m_egressPolicy);
    m_remoteMediaCache.setFetcher(m_egressFetcher.get());

    // The tray shows only where a status-notifier host exists; it routes its
    // actions through its signals so the in-app path works regardless.
    if (m_options.showSystemTray)
        m_systemTray.show();

    // No system-wide grab is registered on ANY platform yet: GlobalHotkey is a
    // seam with no backend behind it, so this is false everywhere. The
    // configured chord still works while a window has focus, through the
    // Shortcut in main.qml that reads the same setting.
    m_globalHotkey.setSupported(false);
}

void ProcessServices::openSettings(const QString &settingsPath)
{
    // Per-user settings. The store flushes any pending debounced write when
    // it is destroyed with this object.
    const QString path = settingsPath.isEmpty()
        ? QDir(QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation))
              .filePath(QStringLiteral("settings.json"))
        : settingsPath;
    m_settings.open(path);

    // Theme and typography snapshot the store's values when attached, so they
    // attach here, after open() — attaching before would read an empty store
    // and discard the persisted theme.id and type.* values.
    m_theme.setSettings(&m_settings);
    m_typography.setSettings(&m_settings);

    PerfLog &perfLog = PerfLog::instance();
    if (m_options.configureLoggingFromSettings
        && !perfLog.hasEnvironmentOverride())
        perfLog.configureFromSetting(
            m_settings.value(QStringLiteral("perf.logging"), QVariant()));
    if (m_options.configureLoggingFromSettings && perfLog.enabled()
        && !perfLog.hasLogFilePath()) {
        perfLog.setLogFilePath(
            QDir(QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation))
                .filePath(QStringLiteral("perf.log")));
    }

    // The quick-capture chord, both now and whenever it is edited. The in-app
    // Shortcut in main.qml reads the same key, so the two never disagree about
    // which chord the user chose.
    const auto applyQuickCaptureChord = [this]() {
        m_globalHotkey.registerShortcut(
            m_settings.value(QStringLiteral("hotkey.quickCapture"),
                             QStringLiteral("Ctrl+Alt+N")).toString());
    };
    applyQuickCaptureChord();
    connect(&m_settings, &SettingsStore::valueChanged,
            &m_globalHotkey, [applyQuickCaptureChord](const QString &key) {
                if (key == QLatin1String("hotkey.quickCapture"))
                    applyQuickCaptureChord();
            });

    // The disclosed opt-out update check reads its enabled flag and
    // once-per-day stamp from the same store.
    m_updateChecker.setSettings(&m_settings);

    // Remote-content consent: the master switch and the origins the reader has
    // approved. Attached here, like Theme, because the policy reads its stored
    // values on attach and an unopened store would drop every approval.
    m_egressPolicy.setSettings(&m_settings);

    // Close-to-tray is opt-in (tray.closeToTray, default off): closing the
    // last window quits unless the user chose to stay resident in the tray.
    m_systemTray.setSettings(&m_settings);
}
