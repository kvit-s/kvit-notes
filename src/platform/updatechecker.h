// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
#ifndef UPDATECHECKER_H
#define UPDATECHECKER_H

#include <QObject>
#include <QString>
#include <QUrl>

#include <functional>

class SettingsStore;

// Transport seam for the update check, mirroring EmbedFetcher: the real
// launcher installs EgressFetcher, tests inject a fake, and a checker with no
// fetcher does nothing at all - which is why no automated suite can ever
// reach the network through this path.
//
// The update check is the one request the app makes without asking per
// origin, because the endpoint is fixed and the Settings toggle that enables
// it is the consent. It still travels over EgressFetcher, so it gets the same
// address validation, redirect revalidation, byte cap and timeout as
// everything else.
class UpdateFetcher
{
public:
    virtual ~UpdateFetcher() = default;
    virtual void fetch(const QUrl &url,
                       std::function<void(bool ok, const QByteArray &body)> done) = 0;
};

// The disclosed, opt-out update check: on startup, at
// most once per calendar day, GET the GitHub Releases `latest` endpoint and,
// when it names a version newer than the running one, expose it as a passive
// status-bar notice. No telemetry, no auto-download; `updates.checkEnabled`
// in settings turns it off, and the README privacy section discloses it.
class UpdateChecker : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool updateAvailable READ updateAvailable NOTIFY stateChanged)
    Q_PROPERTY(QString latestVersion READ latestVersion NOTIFY stateChanged)
    Q_PROPERTY(QString releaseUrl READ releaseUrl NOTIFY stateChanged)
    Q_PROPERTY(bool enabled READ enabled WRITE setEnabled NOTIFY enabledChanged)

public:
    explicit UpdateChecker(QObject *parent = nullptr);

    void setSettings(SettingsStore *settings);
    // Non-owning; without one, maybeCheck() is a no-op.
    void setFetcher(UpdateFetcher *fetcher) { m_fetcher = fetcher; }
    void setCurrentVersion(const QString &version) { m_currentVersion = version; }
    void setEndpoint(const QUrl &endpoint) { m_endpoint = endpoint; }
    QUrl endpoint() const { return m_endpoint; }

    bool updateAvailable() const { return m_updateAvailable; }
    QString latestVersion() const { return m_latestVersion; }
    QString releaseUrl() const { return m_releaseUrl; }

    bool enabled() const;
    void setEnabled(bool enabled);

    // The startup entry point: respects the enabled setting, the
    // once-per-day stamp (written before the request, so a crash cannot
    // cause a second same-day call), and the KVIT_DISABLE_UPDATE_CHECK
    // environment override used by the UI driver.
    Q_INVOKABLE void maybeCheck();

    // ── Pure helpers, unit-tested directly ──
    // SemVer-ordered comparison, tolerant of a leading 'v' and of -rcN
    // prereleases: negative when a < b, 0 when equal, positive when a > b.
    static int compareVersions(const QString &a, const QString &b);
    // Extract tag_name (normalized, no leading 'v') and html_url from the
    // GitHub releases/latest JSON payload. False when the payload does not
    // parse or carries no tag.
    static bool parseLatestPayload(const QByteArray &json, QString *version,
                                   QString *url);

signals:
    void stateChanged();
    void enabledChanged();

private:
    void applyResult(bool ok, const QByteArray &body);

    SettingsStore *m_settings = nullptr;
    UpdateFetcher *m_fetcher = nullptr;
    QString m_currentVersion;
    QUrl m_endpoint{QStringLiteral(
        "https://api.github.com/repos/kvit-s/kvit-notes/releases/latest")};
    bool m_updateAvailable = false;
    QString m_latestVersion;
    QString m_releaseUrl;
};

#endif // UPDATECHECKER_H
