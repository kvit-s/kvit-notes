// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
#ifndef EGRESSFETCHER_H
#define EGRESSFETCHER_H

#include <QByteArray>
#include <QHash>
#include <QHostAddress>
#include <QList>
#include <QNetworkAccessManager>
#include <QObject>
#include <QSharedPointer>
#include <QString>
#include <QStringList>
#include <QUrl>

#include <functional>
#include <memory>

class QTemporaryFile;

#include "embedfetcher.h"
#include "updatechecker.h"

class EgressPolicy;

// The one transport every outbound request in the app travels over.
//
// EgressPolicy decides whether a URL may be fetched at all; this class makes
// the decision hold on the wire, where the interesting failures live:
//
//   * DNS is resolved first and every returned address is checked against the
//     policy, so a public hostname that answers with 127.0.0.1 or 10.0.0.5 is
//     refused rather than connected to.
//   * The connection is pinned to the address that was checked, with the
//     original hostname carried in the Host header and in TLS peer
//     verification. Without pinning the name would be resolved a second time
//     by the network stack, and a DNS server that answers differently on the
//     second query (a rebinding attack) would get the connection the check
//     was supposed to prevent.
//   * Redirects are not followed by Qt. Each hop comes back here and runs the
//     whole check again -- scheme, consent, resolution, address -- because a
//     public URL redirecting to a private one is otherwise a complete bypass.
//   * The byte cap is enforced while the body arrives and again against a
//     declared Content-Length, so an oversized or endless response is
//     abandoned instead of buffered.
//
// It implements both existing network seams (EmbedFetcher and UpdateFetcher),
// so the embed cache and the update check share this path rather than each
// owning a QNetworkAccessManager.
class EgressFetcher : public QObject, public EmbedFetcher, public UpdateFetcher
{
    Q_OBJECT

public:
    // What the request is for. The purpose selects the byte cap, the
    // acceptable content types, and whether per-origin consent is required:
    // the update check is a fixed endpoint the user enables in Settings, so
    // its consent is that setting rather than an origin approval, while
    // everything reachable from a note's text needs the reader's approval.
    enum class Purpose {
        EmbedPreview,
        RemoteImage,
        RemoteMedia,
        UpdateCheck,
    };

    explicit EgressFetcher(QObject *parent = nullptr);

    void setPolicy(EgressPolicy *policy) { m_policy = policy; }
    EgressPolicy *policy() const { return m_policy; }

    // Test seam for the resolution step: lets a hermetic test map a name to
    // the address it wants without a DNS server. Production leaves this
    // unset and QHostInfo does the lookup.
    using Resolver = std::function<QList<QHostAddress>(const QString &host)>;
    void setResolverForTests(Resolver resolver) { m_resolver = std::move(resolver); }

    // The generic entry point. `done(ok, body, contentType)`; on refusal it
    // reports failure with an empty body and never opens a socket.
    void request(const QUrl &url, Purpose purpose,
                 std::function<void(bool, const QByteArray &, const QString &)> done);

    // The same request, with the body written to a temporary file as it
    // arrives instead of being accumulated in memory. Media is capped at tens
    // of megabytes each, so buffering a handful of them at once costs more RAM
    // than the rest of the app uses; the caller wants a file anyway, since
    // QtMultimedia plays from one. `fileSuffix` (no dot) names the temporary
    // file's extension so the multimedia backend can pick a demuxer.
    //
    // The callback owns the file: the last reference dropping deletes it from
    // disk. On failure it receives a null pointer.
    using FileDone = std::function<void(bool ok,
                                        const QSharedPointer<QTemporaryFile> &file,
                                        qint64 bytes, const QString &contentType)>;
    void requestToFile(const QUrl &url, Purpose purpose,
                       const QString &fileSuffix, FileDone done);

    // EmbedFetcher: a page's HTML for the preview card.
    void fetch(const QString &url,
               std::function<void(bool, const QString &)> done) override;

    // UpdateFetcher: the release-check JSON.
    void fetch(const QUrl &url,
               std::function<void(bool, const QByteArray &)> done) override;

    // Caps, exposed so the tests assert the shipped numbers rather than
    // their own copies.
    static qint64 maxBytesFor(Purpose purpose);
    static int timeoutMsFor(Purpose purpose);
    static QStringList acceptedTypesFor(Purpose purpose);
    // A redirect chain longer than this is abandoned.
    static constexpr int MaxRedirects = 5;

    // How many requests of one purpose may be in flight at once, and how many
    // in total. A note naming two hundred distinct image URLs used to open two
    // hundred requests, each entitled to its own byte cap, so peak memory,
    // socket count and file descriptors were all set by the note rather than
    // by the app. Everything past the budget waits in a queue.
    static int maxConcurrentFor(Purpose purpose);
    static constexpr int MaxConcurrentTotal = 8;
    // Requests offered past this many already waiting are refused outright
    // rather than queued, so a pathological document cannot grow the queue
    // without limit either.
    static constexpr int MaxQueued = 256;
    // Addresses tried before a job gives up, when a name resolves to several
    // and the first does not answer. All attempts share the one deadline.
    static constexpr int MaxConnectAttempts = 3;

    // ---- Test seams ----
    // Shortens every purpose's deadline so a test can watch one expire.
    void setTimeoutMsForTests(int ms) { m_timeoutOverrideMs = ms; }
    // The largest body this fetcher has ever held in memory at once, so a
    // test can show that a streamed request does not accumulate one.
    qint64 peakBufferedBytesForTests() const { return m_peakBufferedBytes; }
    int inFlightForTests() const { return m_inFlight; }
    int queuedForTests() const { return m_queue.size(); }

private:
    struct Job;
    void begin(const std::shared_ptr<Job> &job, const QUrl &url);
    void startHop(const std::shared_ptr<Job> &job, const QUrl &url);
    void resolveAndConnect(const std::shared_ptr<Job> &job, const QUrl &url);
    void issue(const std::shared_ptr<Job> &job, const QUrl &url);
    void finish(const std::shared_ptr<Job> &job, bool ok,
                const QByteArray &body, const QString &contentType);
    // Milliseconds left of the job's single overall deadline; <= 0 means it
    // is spent.
    int remainingMs(const std::shared_ptr<Job> &job) const;
    void armDeadline(const std::shared_ptr<Job> &job);
    bool acquireSlot(const std::shared_ptr<Job> &job);
    void releaseSlot(const std::shared_ptr<Job> &job);
    void pumpQueue();

    QNetworkAccessManager m_nam;
    EgressPolicy *m_policy = nullptr;
    Resolver m_resolver;
    QList<QPair<std::shared_ptr<Job>, QUrl>> m_queue;
    QHash<int, int> m_inFlightByPurpose;
    int m_inFlight = 0;
    int m_timeoutOverrideMs = 0;
    qint64 m_peakBufferedBytes = 0;
    bool m_pumpScheduled = false;
};

#endif // EGRESSFETCHER_H
