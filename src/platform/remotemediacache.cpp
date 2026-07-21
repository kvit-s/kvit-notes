// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
#include "remotemediacache.h"

#include <QDir>
#include <QFileInfo>
#include <QPointer>
#include <QTemporaryFile>
#include <QUrl>

#include "egressfetcher.h"

RemoteMediaCache::RemoteMediaCache(QObject *parent)
    : QObject(parent)
{
}

QString RemoteMediaCache::sourceFor(const QString &remoteUrl) const
{
    return m_sources.value(remoteUrl);
}

bool RemoteMediaCache::failedFor(const QString &remoteUrl) const
{
    return m_failed.contains(remoteUrl);
}

void RemoteMediaCache::request(const QString &remoteUrl)
{
    const QUrl url(remoteUrl);
    if (!m_fetcher || !url.isValid()
        || (url.scheme() != QLatin1String("http")
            && url.scheme() != QLatin1String("https"))
        || m_sources.contains(remoteUrl) || m_pending.contains(remoteUrl)) {
        return;
    }

    if (m_failed.remove(remoteUrl)) {
        ++m_revision;
        emit revisionChanged();
    }
    m_pending.insert(remoteUrl);
    const QPointer<RemoteMediaCache> self(this);
    m_fetcher->request(
        url, EgressFetcher::Purpose::RemoteMedia,
        [self, remoteUrl, url](bool ok, const QByteArray &body, const QString &) {
            if (!self)
                return;
            self->m_pending.remove(remoteUrl);
            const auto fail = [&]() {
                self->m_failed.insert(remoteUrl);
                ++self->m_revision;
                emit self->revisionChanged();
                emit self->mediaFailed(remoteUrl);
            };
            if (!ok || body.isEmpty()) {
                fail();
                return;
            }

            QString suffix = QFileInfo(url.path()).suffix().toLower();
            bool suffixIsSafe = !suffix.isEmpty() && suffix.size() <= 10;
            for (const QChar ch : suffix)
                suffixIsSafe = suffixIsSafe && ch.isLetterOrNumber();
            if (!suffixIsSafe)
                suffix = QStringLiteral("media");

            const QString pattern =
                QDir(QDir::tempPath())
                    .filePath(QStringLiteral("kvit-media-XXXXXX.%1").arg(suffix));
            auto file = QSharedPointer<QTemporaryFile>::create(pattern);
            file->setAutoRemove(true);
            if (!file->open()) {
                fail();
                return;
            }

            qint64 written = 0;
            while (written < body.size()) {
                const qint64 n = file->write(body.constData() + written,
                                             body.size() - written);
                if (n <= 0) {
                    file->close();
                    fail();
                    return;
                }
                written += n;
            }
            if (!file->flush()) {
                file->close();
                fail();
                return;
            }
            const QString localSource = QUrl::fromLocalFile(file->fileName()).toString();
            file->close();
            self->m_files.insert(remoteUrl, file);
            self->m_sources.insert(remoteUrl, localSource);
            self->m_failed.remove(remoteUrl);
            ++self->m_revision;
            emit self->revisionChanged();
            emit self->mediaReady(remoteUrl, localSource);
        });
}
