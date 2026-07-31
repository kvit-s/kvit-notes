// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
#include "embedmetadata.h"
#include "notecollection.h"
#include "imageassets.h"
#include "egresspolicy.h"
#include "notefileio.h"

#include <QDir>
#include <QFile>
#include <QPointer>
#include <QSaveFile>
#include <QCryptographicHash>
#include <QDateTime>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QUrl>
#include <QStandardPaths>

namespace {

// Known video hosts (a play affordance on the card).
const char *kVideoHosts[] = {
    "youtube.com", "youtu.be", "vimeo.com", "dailymotion.com",
    "twitch.tv", "ted.com",
};

QString hostOf(const QString &url)
{
    return QUrl(url).host().toLower();
}

QString ogTag(const QString &html, const QString &prop)
{
    // <meta property="og:*" content="..."> in either attribute order.
    QRegularExpression re(
        QStringLiteral("<meta[^>]+(?:property|name)=[\"']")
            + QRegularExpression::escape(prop) + QStringLiteral("[\"'][^>]*>"),
        QRegularExpression::CaseInsensitiveOption);
    QRegularExpressionMatch m = re.match(html);
    if (!m.hasMatch()) {
        QRegularExpression re2(
            QStringLiteral("<meta[^>]+content=[\"']([^\"']*)[\"'][^>]*(?:property|name)=[\"']")
                + QRegularExpression::escape(prop) + QStringLiteral("[\"']"),
            QRegularExpression::CaseInsensitiveOption);
        m = re2.match(html);
        return m.hasMatch() ? m.captured(1) : QString();
    }
    QRegularExpression cre(QStringLiteral("content=[\"']([^\"']*)[\"']"),
                           QRegularExpression::CaseInsensitiveOption);
    QRegularExpressionMatch cm = cre.match(m.captured(0));
    return cm.hasMatch() ? cm.captured(1) : QString();
}

QString titleTag(const QString &html)
{
    QRegularExpression re(QStringLiteral("<title[^>]*>([^<]*)</title>"),
                          QRegularExpression::CaseInsensitiveOption);
    QRegularExpressionMatch m = re.match(html);
    return m.hasMatch() ? m.captured(1).trimmed() : QString();
}

} // namespace

EmbedMetadata::EmbedMetadata(QObject *parent)
    : QObject(parent)
{
}

bool EmbedMetadata::isVideoHost(const QString &url)
{
    const QString host = hostOf(url);
    for (const char *h : kVideoHosts) {
        const QString hh = QString::fromLatin1(h);
        if (host == hh || host.endsWith(QLatin1Char('.') + hh))
            return true;
    }
    return false;
}

QVariantMap EmbedMetadata::parseOpenGraph(const QString &html, const QString &url)
{
    QString title = ogTag(html, QStringLiteral("og:title"));
    if (title.isEmpty())
        title = titleTag(html);
    const QString desc = ogTag(html, QStringLiteral("og:description"));
    const QString image = ogTag(html, QStringLiteral("og:image"));
    QString favicon = ogTag(html, QStringLiteral("og:image"));
    // A crude favicon: the site's /favicon.ico (good enough for the card).
    const QUrl u(url);
    if (!u.host().isEmpty())
        favicon = u.scheme() + QStringLiteral("://") + u.host()
                + QStringLiteral("/favicon.ico");

    const bool ok = !title.isEmpty() || !desc.isEmpty();
    return QVariantMap{
        {QStringLiteral("url"), url},
        {QStringLiteral("title"), title.isEmpty() ? u.host() : title},
        {QStringLiteral("description"), desc},
        {QStringLiteral("image"), image},
        {QStringLiteral("favicon"), favicon},
        {QStringLiteral("video"), isVideoHost(url)},
        {QStringLiteral("ok"), ok},
    };
}

QString EmbedMetadata::cacheDir() const
{
    if (m_collection && m_collection->isOpen())
        return QDir(NoteFileIo::vaultCacheDir(m_collection->rootPath()))
            .filePath(QStringLiteral("embedcache"));
    return QDir(QStandardPaths::writableLocation(QStandardPaths::CacheLocation))
        .filePath(QStringLiteral("embedcache"));
}

QString EmbedMetadata::cachePathFor(const QString &url) const
{
    const QString hash = QString::fromLatin1(
        QCryptographicHash::hash(url.toUtf8(), QCryptographicHash::Sha1)
            .toHex());
    return QDir(cacheDir()).filePath(hash + QStringLiteral(".json"));
}

QVariantMap EmbedMetadata::readCache(const QString &url) const
{
    QFile f(cachePathFor(url));
    if (!f.open(QIODevice::ReadOnly))
        return {};
    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll());
    const QVariantMap meta = doc.object().toVariantMap();
    // A remembered failure past its window reads as no entry at all, so the
    // card asks again rather than displaying "preview unavailable" for the
    // life of the note. An entry written before this app recorded fetch times
    // has no timestamp, and a failure among those has been in place at least
    // as long as the upgrade, so it expires on sight.
    if (!meta.isEmpty() && meta.value(QStringLiteral("ok")).toBool() == false) {
        const qint64 fetchedAt =
            meta.value(QStringLiteral("fetchedAt")).toLongLong();
        if (QDateTime::currentSecsSinceEpoch() - fetchedAt > FailedRetryAfterSecs)
            return {};
    }
    return meta;
}

void EmbedMetadata::writeCache(const QString &url, const QVariantMap &meta)
{
    writeCacheAt(cachePathFor(url), m_collection && m_collection->isOpen()
                                        ? m_collection->rootPath() : QString(),
                 meta);
}

void EmbedMetadata::writeCacheAt(const QString &cachePath,
                                 const QString &vaultRoot,
                                 const QVariantMap &meta)
{
    // Tag the cache subtree even if an embed is fetched before the first scan
    // has set it up; idempotent.
    if (!vaultRoot.isEmpty())
        NoteFileIo::ensureVaultCacheDir(vaultRoot);
    QDir().mkpath(QFileInfo(cachePath).absolutePath());
    QSaveFile f(cachePath);
    if (!f.open(QIODevice::WriteOnly))
        return;
    // Stamped on the way in, because a failure is only kept for a while and
    // the file's own mtime is not the fetch time once a backup, a sync client
    // or a copied vault has touched it.
    QVariantMap stamped = meta;
    stamped[QStringLiteral("fetchedAt")] = QDateTime::currentSecsSinceEpoch();
    f.write(QJsonDocument(QJsonObject::fromVariantMap(stamped)).toJson());
    f.commit();
}

void EmbedMetadata::retryMetadata(const QString &url)
{
    if (url.isEmpty() || m_inFlight.contains(url))
        return;
    QFile::remove(cachePathFor(url));
    requestMetadata(url);
}

QVariantMap EmbedMetadata::cachedMetadata(const QString &url) const
{
    return readCache(url);
}

bool EmbedMetadata::needsConsent(const QString &url) const
{
    if (url.isEmpty() || !readCache(url).isEmpty())
        return false;
    return !m_policy || !m_policy->isAllowed(url);
}

void EmbedMetadata::requestMetadata(const QString &url)
{
    if (url.isEmpty())
        return;
    // Cached already? Report immediately. A cache entry is metadata the
    // reader has already allowed to be fetched once, so re-reading it makes
    // no request and needs no new approval.
    if (!readCache(url).isEmpty()) {
        emit metadataReady(url);
        return;
    }
    if (m_inFlight.contains(url))
        return;
    // Opening a note is not consent to contact the hosts it names. Nothing
    // below this line runs until the reader has approved the origin.
    if (!m_policy || !m_policy->isAllowed(url)) {
        emit consentRequired(url);
        return;
    }
    if (!m_fetcher) {
        // No fetcher (e.g., offline test with no seam): write the fallback.
        writeCache(url, parseOpenGraph(QString(), url));
        emit metadataReady(url);
        return;
    }
    m_inFlight.insert(url);
    // Where the answer goes is decided now, not when it arrives. A fetch spans
    // many turns of the event loop and this window can switch vaults inside
    // one of them, so resolving the path in the callback wrote the preview for
    // a note in one vault into whichever vault happened to be open by the time
    // the page came back — the entry missing where it belongs, and a record of
    // what the other vault links to sitting somewhere it has no business.
    const QString cachePath = cachePathFor(url);
    const QString vaultRoot = m_collection && m_collection->isOpen()
                                  ? m_collection->rootPath() : QString();
    // The fetcher is process-global (ProcessServices) and outlives this cache,
    // so a fetch still in flight when this window closes would otherwise call
    // back into a freed EmbedMetadata. The callback runs on the GUI thread,
    // where this object is also destroyed, so a QPointer guard is enough.
    QPointer<EmbedMetadata> guard(this);
    m_fetcher->fetch(url, [this, guard, url, cachePath, vaultRoot](
                              bool ok, const QString &html) {
        if (!guard)
            return;
        QVariantMap meta = ok ? parseOpenGraph(html, url)
                              : parseOpenGraph(QString(), url);
        if (!ok)
            meta[QStringLiteral("ok")] = false;  // the fallback card
        writeCacheAt(cachePath, vaultRoot, meta);
        m_inFlight.remove(url);
        emit metadataReady(url);
    });
}
