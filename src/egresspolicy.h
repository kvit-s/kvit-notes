// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
#ifndef EGRESSPOLICY_H
#define EGRESSPOLICY_H

#include <QHostAddress>
#include <QObject>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QUrl>

class SettingsStore;

// The single place that decides whether the app may talk to a remote host.
//
// Every outbound request the editor makes -- embed preview metadata, the
// images and favicons those previews name, remote images and media inside a
// note, and the update check -- asks this object first, and every one of them
// travels over EgressFetcher, which asks again once DNS has resolved and once
// more after each redirect. Nothing else in the tree may construct a
// QNetworkAccessManager or bind a QML `source` to a remote URL.
//
// The rule the policy enforces is that opening a note is not consent. A note
// is an untrusted document: anyone who can hand you a .md file can name a URL
// in it, so fetching on sight would disclose the reader's address, user agent
// and reading time to whoever wrote the note, and would turn the editor into a
// blind request generator aimed at whatever the URL names -- including hosts
// only this machine can reach. Automatic loading is therefore off by default
// and the reader approves an origin before anything is fetched from it.
class EgressPolicy : public QObject
{
    Q_OBJECT

    // Master switch (settings key `network.autoLoadRemoteContent`). Off by
    // default: with it off, an origin loads only after the reader approves
    // it; with it on, any http(s) origin loads without asking, which is the
    // behavior a reader has to opt into deliberately.
    Q_PROPERTY(bool autoLoadRemoteContent READ autoLoadRemoteContent
                   WRITE setAutoLoadRemoteContent NOTIFY policyChanged)
    // Bumped on every decision-affecting change so QML bindings that call
    // isAllowed() re-evaluate; isAllowed() is a plain function, and a
    // binding over it would otherwise never update.
    Q_PROPERTY(int revision READ revision NOTIFY policyChanged)

public:
    explicit EgressPolicy(QObject *parent = nullptr);

    void setSettings(SettingsStore *settings);

    bool autoLoadRemoteContent() const;
    void setAutoLoadRemoteContent(bool on);
    int revision() const { return m_revision; }

    // The consent question QML asks before it renders anything remote, and
    // EgressFetcher asks before it opens a connection. True when the URL is a
    // well-formed http(s) URL whose origin the reader has approved (or when
    // the master switch is on).
    Q_INVOKABLE bool isAllowed(const QString &url) const;

    // True when the URL is fetchable in principle -- right scheme, no
    // embedded credentials, a host present -- and only consent is missing.
    // QML uses this to decide between offering a "Load" affordance and
    // showing an inert card that cannot be loaded at all.
    Q_INVOKABLE bool canRequestConsent(const QString &url) const;

    // "https://example.com" for any URL on that origin: scheme, host and a
    // non-default port. Consent is granted per origin, not per URL, because a
    // page and its thumbnail differ only in path and a reader approving a
    // preview means the site, not one image.
    Q_INVOKABLE static QString originOf(const QString &url);

    // Record (or withdraw) the reader's approval of a URL's origin.
    Q_INVOKABLE void allowOrigin(const QString &url);
    Q_INVOKABLE void forgetOrigin(const QString &url);
    Q_INVOKABLE bool isOriginAllowed(const QString &url) const;
    Q_INVOKABLE QStringList allowedOrigins() const;
    Q_INVOKABLE void forgetAllOrigins();

    // Why a URL is not fetchable, for the card's subtitle. Empty when it is.
    Q_INVOKABLE QString refusalReason(const QString &url) const;

    // What a QML Image's `source` should be for a resolved asset path. A
    // local file, a data: URI or a qrc: path passes through untouched; an
    // http(s) URL becomes an image://remote/... id so the bytes travel over
    // EgressFetcher, and an unapproved origin yields "" so the delegate shows
    // its placeholder and no request is made. Delegates must use this rather
    // than binding a URL to `source`, which would hand the fetch to Qt's
    // network stack outside every check made here.
    Q_INVOKABLE QString imageSourceFor(const QString &url) const;

    // ---- Address rules (applied by EgressFetcher after DNS resolves) ----

    // True for an address the app must never connect to: loopback, RFC1918
    // and IPv6 unique-local, link-local (which covers the 169.254.169.254
    // cloud metadata service), multicast, broadcast, unspecified, and the
    // IPv4-mapped forms of all of those. The check exists because a URL in a
    // note is chosen by whoever wrote the note, so without it the editor can
    // be aimed at the reader's router, printer, or cloud credentials
    // endpoint.
    bool addressIsBlocked(const QHostAddress &address) const;

    // Test seam: loopback is the only address family a hermetic test can
    // serve from, so the suite turns it back on explicitly. Deliberately not
    // Q_INVOKABLE and not backed by a setting -- there is no way to reach it
    // from QML, from settings.json, or from a note.
    void setLoopbackAllowedForTests(bool allowed) { m_allowLoopbackForTests = allowed; }

    // Schemes the app will fetch. Anything else (file:, data:, ftp:, javascript:)
    // is refused before a request is built.
    static bool isFetchableScheme(const QUrl &url);

signals:
    void policyChanged();

private:
    void load();

    SettingsStore *m_settings = nullptr;
    QSet<QString> m_allowedOrigins;
    int m_revision = 0;
    bool m_allowLoopbackForTests = false;
};

#endif // EGRESSPOLICY_H
