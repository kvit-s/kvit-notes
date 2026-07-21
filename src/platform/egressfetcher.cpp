// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
#include "egressfetcher.h"

#include <QDir>
#include <QElapsedTimer>
#include <QHostInfo>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPointer>
#include <QSslConfiguration>
#include <QTemporaryFile>
#include <QTimer>

#include <memory>

#include "egresspolicy.h"

namespace {

const QByteArray kUserAgent = QByteArrayLiteral("KvitNotes/1.0");

// A failure that says "this address did not answer" rather than "the server
// said no". Only these are worth retrying against the next address a name
// resolved to; a 404 or a bad certificate would say the same thing again.
bool addressLevelFailure(QNetworkReply::NetworkError error)
{
    switch (error) {
    case QNetworkReply::ConnectionRefusedError:
    case QNetworkReply::HostNotFoundError:
    case QNetworkReply::TimeoutError:
    case QNetworkReply::RemoteHostClosedError:
    case QNetworkReply::TemporaryNetworkFailureError:
    case QNetworkReply::NetworkSessionFailedError:
    case QNetworkReply::UnknownNetworkError:
        return true;
    default:
        return false;
    }
}

// Everything one in-flight request needs to survive its redirect chain. Held
// by shared_ptr because each hop's lambdas outlive the call that made them.
struct JobState {
    EgressFetcher::Purpose purpose = EgressFetcher::Purpose::EmbedPreview;
    std::function<void(bool, const QByteArray &, const QString &)> done;
    EgressFetcher::FileDone doneFile;
    int hops = 0;
    bool finished = false;
    bool holdsSlot = false;
    QByteArray body;
    QPointer<QNetworkReply> reply;
    QPointer<QTimer> deadline;
    // One clock for the whole job, started before the name is resolved: the
    // resolver's own wait is part of what the caller was promised, and a
    // redirect chain must not be able to renew the promise per hop.
    QElapsedTimer clock;
    int budgetMs = 0;
    int lookupId = -1;
    // The origin the job started from, which a redirect of the update check
    // may not leave.
    QString initialOrigin;
    // Addresses the current hop's name resolved to, and which one is being
    // tried.
    QList<QHostAddress> addresses;
    int addressIndex = 0;
    // Streaming sink: set when the caller asked for a file rather than bytes.
    bool wantsFile = false;
    QString fileSuffix;
    QSharedPointer<QTemporaryFile> file;
    qint64 received = 0;
};

} // namespace

struct EgressFetcher::Job : JobState {};

EgressFetcher::EgressFetcher(QObject *parent)
    : QObject(parent)
{
}

qint64 EgressFetcher::maxBytesFor(Purpose purpose)
{
    switch (purpose) {
    case Purpose::EmbedPreview:
        return 512 * 1024;        // OpenGraph tags live in the <head>
    case Purpose::RemoteImage:
        return 8 * 1024 * 1024;   // a preview thumbnail or an inline image
    case Purpose::RemoteMedia:
        return 64 * 1024 * 1024;  // a bounded file handed to QtMultimedia
    case Purpose::UpdateCheck:
        return 256 * 1024;        // one release object
    }
    return 256 * 1024;
}

int EgressFetcher::timeoutMsFor(Purpose purpose)
{
    if (purpose == Purpose::RemoteMedia)
        return 30000;
    return purpose == Purpose::RemoteImage ? 15000 : 8000;
}

int EgressFetcher::maxConcurrentFor(Purpose purpose)
{
    switch (purpose) {
    case Purpose::EmbedPreview:
        return 4;
    case Purpose::RemoteImage:
        return 6;
    case Purpose::RemoteMedia:
        // Two at 64 MiB each is the app's whole media footprint. Media is
        // also the purpose a note can most cheaply multiply.
        return 2;
    case Purpose::UpdateCheck:
        return 1;
    }
    return 1;
}

QStringList EgressFetcher::acceptedTypesFor(Purpose purpose)
{
    switch (purpose) {
    case Purpose::EmbedPreview:
        return {QStringLiteral("text/html"), QStringLiteral("application/xhtml+xml")};
    case Purpose::RemoteImage:
        return {QStringLiteral("image/")};
    case Purpose::RemoteMedia:
        return {QStringLiteral("audio/"), QStringLiteral("video/"),
                QStringLiteral("application/ogg"),
                QStringLiteral("application/octet-stream")};
    case Purpose::UpdateCheck:
        return {QStringLiteral("application/json"), QStringLiteral("text/json"),
                QStringLiteral("application/vnd.github")};
    }
    return {};
}

void EgressFetcher::request(
    const QUrl &url, Purpose purpose,
    std::function<void(bool, const QByteArray &, const QString &)> done)
{
    auto job = std::make_shared<Job>();
    job->purpose = purpose;
    job->done = std::move(done);
    begin(job, url);
}

void EgressFetcher::requestToFile(const QUrl &url, Purpose purpose,
                                  const QString &fileSuffix, FileDone done)
{
    auto job = std::make_shared<Job>();
    job->purpose = purpose;
    job->wantsFile = true;
    job->fileSuffix = fileSuffix;
    job->doneFile = std::move(done);
    begin(job, url);
}

// Admission: a job either takes one of the budget's slots and starts, waits
// for one, or is refused because the queue is already as long as it may get.
void EgressFetcher::begin(const std::shared_ptr<Job> &job, const QUrl &url)
{
    job->initialOrigin = EgressPolicy::originOf(url.toString());
    if (acquireSlot(job)) {
        armDeadline(job);
        startHop(job, url);
        return;
    }
    if (m_queue.size() >= MaxQueued) {
        finish(job, false, QByteArray(), QString());
        return;
    }
    m_queue.append({job, url});
}

bool EgressFetcher::acquireSlot(const std::shared_ptr<Job> &job)
{
    const int purpose = static_cast<int>(job->purpose);
    if (m_inFlight >= MaxConcurrentTotal)
        return false;
    if (m_inFlightByPurpose.value(purpose) >= maxConcurrentFor(job->purpose))
        return false;
    ++m_inFlight;
    m_inFlightByPurpose[purpose] = m_inFlightByPurpose.value(purpose) + 1;
    job->holdsSlot = true;
    return true;
}

void EgressFetcher::releaseSlot(const std::shared_ptr<Job> &job)
{
    if (!job->holdsSlot)
        return;
    job->holdsSlot = false;
    const int purpose = static_cast<int>(job->purpose);
    --m_inFlight;
    m_inFlightByPurpose[purpose] = m_inFlightByPurpose.value(purpose) - 1;
}

// Started from finish() through the event loop rather than called directly:
// a queued job can fail on the spot (an origin whose consent was withdrawn
// while it waited), and starting the next one from inside that failure would
// recurse once per queued entry.
void EgressFetcher::pumpQueue()
{
    m_pumpScheduled = false;
    while (!m_queue.isEmpty()) {
        auto next = m_queue.first();
        if (!acquireSlot(next.first))
            return;
        m_queue.removeFirst();
        armDeadline(next.first);
        startHop(next.first, next.second);
    }
}

int EgressFetcher::remainingMs(const std::shared_ptr<Job> &job) const
{
    if (!job->clock.isValid())
        return job->budgetMs;
    const qint64 left = job->budgetMs - job->clock.elapsed();
    return left > 0 ? int(left) : 0;
}

// One deadline for the whole job, armed before anything is resolved. The
// previous shape started a timer per connection, so a name that took the
// resolver's own timeout to fail and then five redirects each allowed the
// full budget could keep a nominal eight-second job alive for a minute, with
// every one of those jobs holding memory and a socket.
void EgressFetcher::armDeadline(const std::shared_ptr<Job> &job)
{
    job->budgetMs = m_timeoutOverrideMs > 0 ? m_timeoutOverrideMs
                                            : timeoutMsFor(job->purpose);
    job->clock.start();
    QTimer *timer = new QTimer(this);
    timer->setSingleShot(true);
    job->deadline = timer;
    connect(timer, &QTimer::timeout, this, [this, job]() {
        if (job->reply && job->reply->isRunning())
            job->reply->abort();
        finish(job, false, QByteArray(), QString());
    });
    timer->start(job->budgetMs);
}

// One hop of a request: the consent and shape checks that do not need the
// network. Runs for the original URL and again for every redirect target.
void EgressFetcher::startHop(const std::shared_ptr<Job> &job, const QUrl &url)
{
    if (job->finished)
        return;
    if (!m_policy) {
        // No policy wired means no permission to reach the network. Failing
        // closed keeps a partially-constructed app from making requests.
        finish(job, false, QByteArray(), QString());
        return;
    }
    if (!m_policy->refusalReason(url.toString()).isEmpty()) {
        finish(job, false, QByteArray(), QString());
        return;
    }
    // The update endpoint is consented to by the Settings toggle that turns
    // the check on; a URL that came out of a note needs its origin approved.
    if (job->purpose != Purpose::UpdateCheck && !m_policy->isAllowed(url.toString())) {
        finish(job, false, QByteArray(), QString());
        return;
    }
    // The update check's consent is the setting, and what that setting
    // discloses is one endpoint. A redirect is chosen by whoever answers,
    // so following one off that origin would turn the disclosed check into a
    // request to an undisclosed host. Origins carry their scheme, so this
    // also refuses an https endpoint redirecting to http.
    if (job->purpose == Purpose::UpdateCheck && job->hops > 0
        && EgressPolicy::originOf(url.toString()) != job->initialOrigin) {
        finish(job, false, QByteArray(), QString());
        return;
    }
    resolveAndConnect(job, url);
}

void EgressFetcher::resolveAndConnect(const std::shared_ptr<Job> &job, const QUrl &url)
{
    if (remainingMs(job) <= 0) {
        finish(job, false, QByteArray(), QString());
        return;
    }
    const QString host = url.host();

    const auto validateAndIssue = [this, job, url](const QList<QHostAddress> &addresses) {
        // A lookup that lands after the deadline, or after a redirect took
        // the job elsewhere, has nothing left to contribute.
        if (job->finished)
            return;
        job->lookupId = -1;
        if (addresses.isEmpty()) {
            finish(job, false, QByteArray(), QString());
            return;
        }
        // Every answer must be acceptable, not merely the one that would be
        // used: a name answering with both a public and a private address is
        // a rebinding setup, and picking the public one would be luck.
        for (const QHostAddress &addr : addresses) {
            if (m_policy->addressIsBlocked(addr)) {
                finish(job, false, QByteArray(), QString());
                return;
            }
        }
        job->addresses = addresses.mid(0, MaxConnectAttempts);
        job->addressIndex = 0;
        issue(job, url);
    };

    // A literal address still goes through validation; QHostInfo returns it
    // unchanged, so there is one code path rather than a special case that
    // could skip the check.
    if (m_resolver) {
        validateAndIssue(m_resolver(host));
        return;
    }
    auto stillResolving = std::make_shared<bool>(true);
    const int lookupId = QHostInfo::lookupHost(
        host, this, [job, validateAndIssue, stillResolving](const QHostInfo &info) {
            *stillResolving = false;
            if (info.error() != QHostInfo::NoError) {
                validateAndIssue({});
                return;
            }
            validateAndIssue(info.addresses());
        });
    // A cached name answers before lookupHost() returns, so record the id
    // only while there is still a lookup for the deadline to abort.
    if (*stillResolving)
        job->lookupId = lookupId;
}

void EgressFetcher::issue(const std::shared_ptr<Job> &job, const QUrl &url)
{
    if (job->finished)
        return;
    if (remainingMs(job) <= 0 || job->addressIndex >= job->addresses.size()) {
        finish(job, false, QByteArray(), QString());
        return;
    }
    const QHostAddress address = job->addresses.at(job->addressIndex);

    // Pin the connection to the address that was just validated, while the
    // request still presents the original hostname: Host header for the
    // server's virtual-host routing, peerVerifyName so TLS is still verified
    // against the name the note asked for rather than the bare address.
    QUrl pinned = url;
    pinned.setHost(address.toString());

    QNetworkRequest req(pinned);
    req.setHeader(QNetworkRequest::UserAgentHeader, kUserAgent);
    // The full authority, not just host(): the Host header has to carry a
    // non-default port, and an IPv6 literal has to keep its brackets, or the
    // server is being told a name that is not the one the URL asked for.
    // FullyEncoded so an internationalized name is sent in its ASCII form.
    QString authority = url.host(QUrl::FullyEncoded);
    if (authority.contains(QLatin1Char(':')))
        authority = QLatin1Char('[') + authority + QLatin1Char(']');
    if (url.port(-1) != -1)
        authority += QLatin1Char(':') + QString::number(url.port());
    req.setRawHeader(QByteArrayLiteral("Host"), authority.toUtf8());
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                     QNetworkRequest::ManualRedirectPolicy);
    if (job->purpose == Purpose::UpdateCheck) {
        req.setRawHeader(QByteArrayLiteral("Accept"),
                         QByteArrayLiteral("application/vnd.github+json"));
    }
    // Verify the certificate against the name the note asked for, not the
    // pinned address, and send that name as SNI.
    req.setPeerVerifyName(url.host());

    const qint64 cap = maxBytesFor(job->purpose);

    QNetworkReply *reply = m_nam.get(req);
    job->reply = reply;
    job->body.clear();
    job->received = 0;
    if (job->file) {
        // A retry or a redirect: whatever the abandoned attempt wrote is not
        // part of this response.
        job->file->resize(0);
        job->file->seek(0);
    }
    // Bound what Qt itself will hold. Without this the reply reads as fast as
    // the socket delivers and buffers the whole body, so the cap below would
    // only trim an allocation that had already happened -- which is the shape
    // of the bug this replaces. Capping the read buffer applies TCP
    // backpressure instead, so an endless response costs bounded memory. A
    // streamed job needs only a window, since its bytes leave for a file.
    reply->setReadBufferSize(job->wantsFile ? qMin<qint64>(cap + 1, 256 * 1024)
                                            : cap + 1);

    // A declared length over the cap is refused before the body is read; the
    // running total below is what actually enforces it, because
    // Content-Length is a claim by the server and may be absent or a lie.
    QObject::connect(reply, &QNetworkReply::metaDataChanged, reply, [reply, cap]() {
        bool ok = false;
        const qint64 declared =
            reply->header(QNetworkRequest::ContentLengthHeader).toLongLong(&ok);
        if (ok && declared > cap)
            reply->abort();
    });

    QObject::connect(reply, &QNetworkReply::readyRead, reply, [this, reply, job, cap]() {
        const QByteArray chunk = reply->read(cap + 1 - job->received);
        if (chunk.isEmpty())
            return;
        job->received += chunk.size();
        if (job->wantsFile) {
            if (!job->file) {
                const QString pattern =
                    QDir(QDir::tempPath())
                        .filePath(QStringLiteral("kvit-media-XXXXXX.%1")
                                      .arg(job->fileSuffix.isEmpty()
                                               ? QStringLiteral("media")
                                               : job->fileSuffix));
                job->file = QSharedPointer<QTemporaryFile>::create(pattern);
                job->file->setAutoRemove(true);
                if (!job->file->open()) {
                    job->file.reset();
                    reply->abort();
                    return;
                }
            }
            if (job->file->write(chunk) != chunk.size()) {
                job->file.reset();
                reply->abort();
                return;
            }
        } else {
            job->body.append(chunk);
            m_peakBufferedBytes = qMax(m_peakBufferedBytes,
                                       qint64(job->body.size()));
        }
        if (job->received > cap)
            reply->abort();   // stop the transfer, do not buffer the rest
    });

    QObject::connect(reply, &QNetworkReply::finished, reply,
                     [this, job, reply, url]() {
        reply->deleteLater();
        if (job->finished)
            return;

        const int status =
            reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const QString contentType =
            reply->header(QNetworkRequest::ContentTypeHeader).toString();

        if (status >= 300 && status < 400) {
            const QByteArray location = reply->rawHeader(QByteArrayLiteral("Location"));
            if (location.isEmpty() || job->hops >= MaxRedirects) {
                finish(job, false, QByteArray(), QString());
                return;
            }
            // Resolve against the hop that produced it so a relative
            // Location works, then run the full check again on the target.
            // The deadline is not renewed: it belongs to the job.
            const QUrl next = url.resolved(QUrl(QString::fromUtf8(location)));
            ++job->hops;
            startHop(job, next);
            return;
        }

        if (reply->error() != QNetworkReply::NoError || status < 200 || status >= 300) {
            // A name with several answers may have a dead one first --
            // commonly an IPv6 address on a host with no IPv6 route. Every
            // answer was validated before the first connection, so trying the
            // next one crosses no boundary; they share the one deadline, so
            // this cannot extend the job.
            if (addressLevelFailure(reply->error())
                && job->addressIndex + 1 < job->addresses.size()
                && remainingMs(job) > 0) {
                ++job->addressIndex;
                issue(job, url);
                return;
            }
            finish(job, false, QByteArray(), QString());
            return;
        }
        if (job->received > maxBytesFor(job->purpose)) {
            finish(job, false, QByteArray(), QString());
            return;
        }
        // A response of the wrong type is not what was asked for; refusing it
        // keeps the parsers off attacker-chosen bytes. An absent type is
        // refused with the rest: it is the cheapest way for a server to hand
        // the HTML parser, the image decoder or the multimedia backend bytes
        // that the purpose contract says they will never see.
        const QStringList accepted = acceptedTypesFor(job->purpose);
        bool typeOk = accepted.isEmpty();
        for (const QString &prefix : accepted) {
            if (contentType.startsWith(prefix, Qt::CaseInsensitive)) {
                typeOk = true;
                break;
            }
        }
        if (!typeOk) {
            finish(job, false, QByteArray(), QString());
            return;
        }
        finish(job, true, job->body, contentType);
    });
}

void EgressFetcher::finish(const std::shared_ptr<Job> &job, bool ok,
                           const QByteArray &body, const QString &contentType)
{
    if (job->finished)
        return;
    job->finished = true;
    if (job->deadline) {
        job->deadline->stop();
        job->deadline->deleteLater();
        job->deadline = nullptr;
    }
    if (job->lookupId != -1) {
        QHostInfo::abortHostLookup(job->lookupId);
        job->lookupId = -1;
    }
    if (job->reply && job->reply->isRunning())
        job->reply->abort();
    releaseSlot(job);
    if (!m_pumpScheduled && !m_queue.isEmpty()) {
        m_pumpScheduled = true;
        QTimer::singleShot(0, this, &EgressFetcher::pumpQueue);
    }

    if (job->wantsFile) {
        QSharedPointer<QTemporaryFile> file = job->file;
        job->file.reset();
        if (ok && file) {
            ok = file->flush();
            file->close();
        }
        if (!ok)
            file.reset();
        if (job->doneFile)
            job->doneFile(ok, file, job->received, contentType);
        return;
    }
    if (job->done)
        job->done(ok, body, contentType);
}

void EgressFetcher::fetch(const QString &url,
                          std::function<void(bool, const QString &)> done)
{
    request(QUrl(url), Purpose::EmbedPreview,
            [done = std::move(done)](bool ok, const QByteArray &body, const QString &) {
                done(ok, QString::fromUtf8(body));
            });
}

void EgressFetcher::fetch(const QUrl &url,
                          std::function<void(bool, const QByteArray &)> done)
{
    request(url, Purpose::UpdateCheck,
            [done = std::move(done)](bool ok, const QByteArray &body, const QString &) {
                done(ok, body);
            });
}
