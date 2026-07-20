// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
#ifndef EMBEDMETADATA_H
#define EMBEDMETADATA_H

#include <QObject>
#include <QString>
#include <QVariantMap>
#include <QSet>
#include <functional>

class NoteCollection;

// The network seam (phase11 decision 11): fetch a page's HTML. The app wires a
// QNetworkAccessManager-backed implementation; tests wire a fake that returns
// canned HTML (or a canned failure), so the suite is hermetic and never
// touches the network. `done(success, html)` may be called synchronously (the
// fake) or asynchronously (the real fetcher).
class EmbedFetcher
{
public:
    virtual ~EmbedFetcher() = default;
    virtual void fetch(const QString &url,
                       std::function<void(bool, const QString &)> done) = 0;
};

// Embed preview metadata (features.md §1.2.14; phase11 decision 11): an
// `![](url)` whose URL is a web page or a recognized video host renders as a
// preview card built from the target's OpenGraph tags (title, description,
// image, favicon), fetched through an injectable seam and cached under
// `.kvit/embedcache/` keyed by URL hash. Exposed as the `embedMetadata`
// context property. Storage is the ordinary image expression, so an embed
// round-trips byte-identically and a non-Kvit editor shows a plain image link.
class EmbedMetadata : public QObject
{
    Q_OBJECT

public:
    explicit EmbedMetadata(QObject *parent = nullptr);

    // The fetcher and the cache root. The collection provides the cache dir
    // (<root>/.kvit/embedcache) when open; otherwise a per-user cache path.
    void setFetcher(EmbedFetcher *fetcher) { m_fetcher = fetcher; }
    void setCollection(NoteCollection *collection) { m_collection = collection; }

    // Request metadata for a URL: emits metadataReady(url) when available
    // (immediately from cache, or after the fetch). A second request for a
    // URL already in flight is coalesced. QML re-reads via cachedMetadata.
    Q_INVOKABLE void requestMetadata(const QString &url);

    // The cached metadata map, or an empty map if not yet fetched. Keys:
    // "url", "title", "description", "image", "favicon", "video" (bool),
    // "ok" (bool — false is the fetched-but-failed fallback state).
    Q_INVOKABLE QVariantMap cachedMetadata(const QString &url) const;

    // Classifier (decision 11): an http(s) URL that is not a recognized image
    // or media *file* — a web page or a video host.
    Q_INVOKABLE static bool isEmbedUrl(const QString &url);
    // A known video host (adds a play affordance to the card).
    Q_INVOKABLE static bool isVideoHost(const QString &url);

    // Pure OpenGraph parse (unit-tested): title, description, og:image,
    // favicon (falls back to the URL's title/host). `ok` true when a title or
    // description was found.
    static QVariantMap parseOpenGraph(const QString &html, const QString &url);

signals:
    void metadataReady(const QString &url);

private:
    QString cacheDir() const;
    QString cachePathFor(const QString &url) const;
    QVariantMap readCache(const QString &url) const;
    void writeCache(const QString &url, const QVariantMap &meta);

    EmbedFetcher *m_fetcher = nullptr;
    NoteCollection *m_collection = nullptr;
    QSet<QString> m_inFlight;
};

#endif // EMBEDMETADATA_H
