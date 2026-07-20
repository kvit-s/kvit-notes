// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
#include "embedmetadata.h"
#include "notecollection.h"
#include "imageassets.h"
#include "egresspolicy.h"

#include <QDir>
#include <QFile>
#include <QSaveFile>
#include <QCryptographicHash>
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

bool EmbedMetadata::isEmbedUrl(const QString &url)
{
    const QString u = url.trimmed();
    if (!(u.startsWith(QLatin1String("http://"), Qt::CaseInsensitive)
          || u.startsWith(QLatin1String("https://"), Qt::CaseInsensitive)))
        return false;
    // A remote image or media *file* stays an image/media block; a web page
    // or video host (no recognized media extension) is an embed.
    return ImageAssets::kindForExtension(u) == ImageAssets::Kind::None;
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
        return QDir(m_collection->rootPath())
            .filePath(QStringLiteral(".kvit/embedcache"));
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
    return doc.object().toVariantMap();
}

void EmbedMetadata::writeCache(const QString &url, const QVariantMap &meta)
{
    QDir().mkpath(cacheDir());
    QSaveFile f(cachePathFor(url));
    if (!f.open(QIODevice::WriteOnly))
        return;
    f.write(QJsonDocument(QJsonObject::fromVariantMap(meta)).toJson());
    f.commit();
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
    m_fetcher->fetch(url, [this, url](bool ok, const QString &html) {
        QVariantMap meta = ok ? parseOpenGraph(html, url)
                              : parseOpenGraph(QString(), url);
        if (!ok)
            meta[QStringLiteral("ok")] = false;  // the fallback card
        writeCache(url, meta);
        m_inFlight.remove(url);
        emit metadataReady(url);
    });
}
