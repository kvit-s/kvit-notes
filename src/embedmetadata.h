// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
#ifndef EMBEDMETADATA_H
#define EMBEDMETADATA_H

#include "embedfetcher.h"

#include <QObject>
#include <QString>
#include <QVariantMap>
#include <QSet>
#include <functional>

class NoteCollection;
class EgressPolicy;

// Embed preview metadata (features.md §1.2.14): an `![](url)` whose URL is a
// web page or a recognized video host renders as a preview card built from the
// target's OpenGraph tags (title, description, image, favicon), fetched
// through an injectable seam and cached under `.kvit/cache/embedcache/` keyed by URL
// hash. Exposed as the `embedMetadata` context property. Storage is the
// ordinary image expression, so an embed round-trips byte-identically and a
// non-Kvit editor shows a plain image link.
class EmbedMetadata : public QObject
{
    Q_OBJECT

public:
    explicit EmbedMetadata(QObject *parent = nullptr);

    // The fetcher and the cache root. The collection provides the cache dir
    // (<root>/.kvit/cache/embedcache) when open; otherwise a per-user cache path.
    void setFetcher(EmbedFetcher *fetcher) { m_fetcher = fetcher; }
    void setCollection(NoteCollection *collection) { m_collection = collection; }
    // The consent gate. Without one wired, no fetch happens at all: a build
    // that forgot to install the policy must not fall back to fetching
    // whatever a note names.
    void setPolicy(EgressPolicy *policy) { m_policy = policy; }

    // Request metadata for a URL: emits metadataReady(url) when available
    // (immediately from cache, or after the fetch). A second request for a
    // URL already in flight is coalesced. QML re-reads via cachedMetadata.
    // Request metadata for a URL. Cached metadata is reported immediately;
    // anything else requires that the reader has approved the URL's origin,
    // because this is the call a note triggers just by being opened. A
    // refused request emits consentRequired(url) and touches no socket.
    Q_INVOKABLE void requestMetadata(const QString &url);

    // True when a card has no metadata yet and the policy would refuse to
    // fetch it: the state where the card must stay inert and offer to load.
    Q_INVOKABLE bool needsConsent(const QString &url) const;

    // The cached metadata map, or an empty map if not yet fetched. Keys:
    // "url", "title", "description", "image", "favicon", "video" (bool),
    // "ok" (bool — false is the fetched-but-failed fallback state).
    Q_INVOKABLE QVariantMap cachedMetadata(const QString &url) const;

    // A known video host (adds a play affordance to the card). The
    // web-page-versus-image-file classifier itself is
    // ImageAssets::isEmbedUrl, which needs neither a network nor a cache.
    Q_INVOKABLE static bool isVideoHost(const QString &url);

    // Pure OpenGraph parse (unit-tested): title, description, og:image,
    // favicon (falls back to the URL's title/host). `ok` true when a title or
    // description was found.
    static QVariantMap parseOpenGraph(const QString &html, const QString &url);

signals:
    void metadataReady(const QString &url);
    // The card was asked to load and the policy said no. QML re-reads
    // needsConsent() and keeps the inert card.
    void consentRequired(const QString &url);

private:
    QString cacheDir() const;
    QString cachePathFor(const QString &url) const;
    QVariantMap readCache(const QString &url) const;
    void writeCache(const QString &url, const QVariantMap &meta);

    EmbedFetcher *m_fetcher = nullptr;
    NoteCollection *m_collection = nullptr;
    EgressPolicy *m_policy = nullptr;
    QSet<QString> m_inFlight;
};

#endif // EMBEDMETADATA_H
