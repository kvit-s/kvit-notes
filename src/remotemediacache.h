// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
#ifndef REMOTEMEDIACACHE_H
#define REMOTEMEDIACACHE_H

#include <QHash>
#include <QObject>
#include <QSet>
#include <QSharedPointer>
#include <QString>

class EgressFetcher;
class QTemporaryFile;

// Downloads approved remote media through the guarded egress transport and
// exposes only a temporary local file to QtMultimedia. MediaPlayer must never
// see an http(s) URL because its own network stack does not enforce Kvit's
// DNS, redirect, timeout, or response-size rules.
class RemoteMediaCache : public QObject
{
    Q_OBJECT
    Q_PROPERTY(int revision READ revision NOTIFY revisionChanged)

public:
    explicit RemoteMediaCache(QObject *parent = nullptr);

    void setFetcher(EgressFetcher *fetcher) { m_fetcher = fetcher; }
    int revision() const { return m_revision; }

    Q_INVOKABLE QString sourceFor(const QString &remoteUrl) const;
    Q_INVOKABLE bool failedFor(const QString &remoteUrl) const;
    Q_INVOKABLE void request(const QString &remoteUrl);

signals:
    void revisionChanged();
    void mediaReady(const QString &remoteUrl, const QString &localSource);
    void mediaFailed(const QString &remoteUrl);

private:
    EgressFetcher *m_fetcher = nullptr;
    QHash<QString, QSharedPointer<QTemporaryFile>> m_files;
    QHash<QString, QString> m_sources;
    QSet<QString> m_pending;
    QSet<QString> m_failed;
    int m_revision = 0;
};

#endif // REMOTEMEDIACACHE_H
