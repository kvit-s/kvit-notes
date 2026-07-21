// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
#include "remotemediacache.h"

#include <QDir>
#include <QFileInfo>
#include <QPointer>
#include <QTemporaryFile>
#include <QUrl>

#include <limits>

#include "egressfetcher.h"

RemoteMediaCache::RemoteMediaCache(QObject *parent)
    : QObject(parent)
{
}

QString RemoteMediaCache::sourceFor(const QString &remoteUrl) const
{
    const auto it = m_entries.constFind(remoteUrl);
    if (it == m_entries.constEnd())
        return QString();
    touch(remoteUrl);
    return it->source;
}

bool RemoteMediaCache::failedFor(const QString &remoteUrl) const
{
    return m_failed.contains(remoteUrl);
}

void RemoteMediaCache::touch(const QString &remoteUrl) const
{
    const auto it = m_entries.find(remoteUrl);
    if (it != m_entries.end())
        it->lastUse = ++m_useCounter;
}

void RemoteMediaCache::evictToBudget(const QString &keep)
{
    bool evicted = false;
    while (m_entries.size() > m_maxEntries || m_cachedBytes > m_maxBytes) {
        QString victim;
        quint64 oldest = std::numeric_limits<quint64>::max();
        for (auto it = m_entries.constBegin(); it != m_entries.constEnd(); ++it) {
            if (it.key() == keep)
                continue;
            if (it->lastUse < oldest) {
                oldest = it->lastUse;
                victim = it.key();
            }
        }
        if (victim.isEmpty())
            break;   // only `keep` is left: one entry over budget is the floor
        const Entry entry = m_entries.take(victim);
        m_cachedBytes -= entry.bytes;
        evicted = true;
        emit mediaReleased(victim);
    }
    if (evicted) {
        ++m_revision;
        emit revisionChanged();
    }
}

void RemoteMediaCache::release(const QString &remoteUrl)
{
    const auto it = m_entries.find(remoteUrl);
    if (it == m_entries.end())
        return;
    m_cachedBytes -= it->bytes;
    m_entries.erase(it);
    ++m_revision;
    emit mediaReleased(remoteUrl);
    emit revisionChanged();
}

void RemoteMediaCache::clear()
{
    if (m_entries.isEmpty() && m_failed.isEmpty())
        return;
    const QList<QString> urls = m_entries.keys();
    m_entries.clear();
    m_cachedBytes = 0;
    m_failed.clear();
    ++m_revision;
    for (const QString &url : urls)
        emit mediaReleased(url);
    emit revisionChanged();
}

void RemoteMediaCache::request(const QString &remoteUrl)
{
    const QUrl url(remoteUrl);
    if (!m_fetcher || !url.isValid()
        || (url.scheme() != QLatin1String("http")
            && url.scheme() != QLatin1String("https"))
        || m_entries.contains(remoteUrl) || m_pending.contains(remoteUrl)) {
        return;
    }

    if (m_failed.remove(remoteUrl)) {
        ++m_revision;
        emit revisionChanged();
    }
    m_pending.insert(remoteUrl);

    // The suffix only tells the multimedia backend which demuxer to try, so
    // anything that is not a short alphanumeric extension is replaced rather
    // than sanitized further; the name itself is chosen by QTemporaryFile.
    QString suffix = QFileInfo(url.path()).suffix().toLower();
    bool suffixIsSafe = !suffix.isEmpty() && suffix.size() <= 10;
    for (const QChar ch : suffix)
        suffixIsSafe = suffixIsSafe && ch.isLetterOrNumber();
    if (!suffixIsSafe)
        suffix = QStringLiteral("media");

    const QPointer<RemoteMediaCache> self(this);
    // Streamed straight to disk: a 64 MiB response never exists as a
    // QByteArray, which is what made several concurrent downloads cost their
    // full cap in RAM apiece.
    m_fetcher->requestToFile(
        url, EgressFetcher::Purpose::RemoteMedia, suffix,
        [self, remoteUrl](bool ok, const QSharedPointer<QTemporaryFile> &file,
                          qint64 bytes, const QString &) {
            if (!self)
                return;
            self->m_pending.remove(remoteUrl);
            if (!ok || !file || bytes <= 0) {
                self->m_failed.insert(remoteUrl);
                ++self->m_revision;
                emit self->revisionChanged();
                emit self->mediaFailed(remoteUrl);
                return;
            }

            Entry entry;
            entry.file = file;
            entry.source = QUrl::fromLocalFile(file->fileName()).toString();
            entry.bytes = bytes;
            entry.lastUse = ++self->m_useCounter;
            self->m_entries.insert(remoteUrl, entry);
            self->m_cachedBytes += bytes;
            self->m_failed.remove(remoteUrl);
            self->evictToBudget(remoteUrl);
            ++self->m_revision;
            emit self->revisionChanged();
            emit self->mediaReady(remoteUrl, entry.source);
        });
}
