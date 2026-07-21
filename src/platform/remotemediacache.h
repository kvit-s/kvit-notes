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
//
// What it retains is bounded twice over: the transport allows only a few
// media requests at a time, and this cache holds a fixed number of finished
// downloads totalling a fixed number of bytes, dropping the least recently
// used when either budget is exceeded. Without both, a note naming enough
// distinct media URLs decides how much temporary disk and how many open
// descriptors the app uses.
class RemoteMediaCache : public QObject
{
    Q_OBJECT
    Q_PROPERTY(int revision READ revision NOTIFY revisionChanged)

public:
    explicit RemoteMediaCache(QObject *parent = nullptr);

    // Retention budget. 8 files and 192 MiB is roughly three of the largest
    // downloads the transport will accept, which is more than a page of a
    // note shows at once and far less than a note can name.
    static constexpr int DefaultMaxEntries = 8;
    static constexpr qint64 DefaultMaxBytes = 192LL * 1024 * 1024;

    void setFetcher(EgressFetcher *fetcher) { m_fetcher = fetcher; }
    int revision() const { return m_revision; }

    Q_INVOKABLE QString sourceFor(const QString &remoteUrl) const;
    Q_INVOKABLE bool failedFor(const QString &remoteUrl) const;
    Q_INVOKABLE void request(const QString &remoteUrl);
    // Drop one download, or all of them. Deleting the last reference to the
    // temporary file removes it from disk, so a player still holding the
    // source will stop working -- which is why this is explicit rather than
    // something the cache does on its own beyond the budget.
    Q_INVOKABLE void release(const QString &remoteUrl);
    Q_INVOKABLE void clear();

    Q_INVOKABLE qint64 cachedBytes() const { return m_cachedBytes; }
    Q_INVOKABLE int cachedCount() const { return m_entries.size(); }

    // Test seam: the shipped budget needs hundreds of megabytes to exercise.
    void setBudgetForTests(int maxEntries, qint64 maxBytes)
    {
        m_maxEntries = maxEntries;
        m_maxBytes = maxBytes;
        evictToBudget(QString());
    }

signals:
    void revisionChanged();
    void mediaReady(const QString &remoteUrl, const QString &localSource);
    void mediaFailed(const QString &remoteUrl);
    // The download was dropped to stay inside the budget (or on request);
    // whatever was playing it has to ask again.
    void mediaReleased(const QString &remoteUrl);

private:
    struct Entry {
        QSharedPointer<QTemporaryFile> file;
        QString source;
        qint64 bytes = 0;
        quint64 lastUse = 0;
    };

    // Evicts least-recently-used entries until both budgets hold. `keep` is
    // never evicted, so the download that just landed is not immediately
    // discarded by its own arrival.
    void evictToBudget(const QString &keep);
    void touch(const QString &remoteUrl) const;

    EgressFetcher *m_fetcher = nullptr;
    // Mutable because reading a source is a use, and the use order is what
    // decides what to drop; sourceFor() is const to QML.
    mutable QHash<QString, Entry> m_entries;
    mutable quint64 m_useCounter = 0;
    qint64 m_cachedBytes = 0;
    int m_maxEntries = DefaultMaxEntries;
    qint64 m_maxBytes = DefaultMaxBytes;
    QSet<QString> m_pending;
    QSet<QString> m_failed;
    int m_revision = 0;
};

#endif // REMOTEMEDIACACHE_H
