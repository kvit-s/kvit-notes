// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
#include "egressfetcher.h"

#include <QHostInfo>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPointer>
#include <QSslConfiguration>
#include <QTimer>

#include <memory>

#include "egresspolicy.h"

namespace {

const QByteArray kUserAgent = QByteArrayLiteral("KvitNotes/1.0");

// Everything one in-flight request needs to survive its redirect chain. Held
// by shared_ptr because each hop's lambdas outlive the call that made them.
struct JobState {
    EgressFetcher::Purpose purpose = EgressFetcher::Purpose::EmbedPreview;
    std::function<void(bool, const QByteArray &, const QString &)> done;
    int hops = 0;
    bool finished = false;
    QByteArray body;
    QPointer<QNetworkReply> reply;
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
    case Purpose::UpdateCheck:
        return 256 * 1024;        // one release object
    }
    return 256 * 1024;
}

int EgressFetcher::timeoutMsFor(Purpose purpose)
{
    return purpose == Purpose::RemoteImage ? 15000 : 8000;
}

QStringList EgressFetcher::acceptedTypesFor(Purpose purpose)
{
    switch (purpose) {
    case Purpose::EmbedPreview:
        return {QStringLiteral("text/html"), QStringLiteral("application/xhtml+xml")};
    case Purpose::RemoteImage:
        return {QStringLiteral("image/")};
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
    startHop(job, url);
}

// One hop of a request: the consent and shape checks that do not need the
// network. Runs for the original URL and again for every redirect target.
void EgressFetcher::startHop(const std::shared_ptr<Job> &job, const QUrl &url)
{
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
    resolveAndConnect(job, url);
}

void EgressFetcher::resolveAndConnect(const std::shared_ptr<Job> &job, const QUrl &url)
{
    const QString host = url.host();

    const auto validateAndIssue = [this, job, url](const QList<QHostAddress> &addresses) {
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
        issue(job, url, addresses.first());
    };

    // A literal address still goes through validation; QHostInfo returns it
    // unchanged, so there is one code path rather than a special case that
    // could skip the check.
    if (m_resolver) {
        validateAndIssue(m_resolver(host));
        return;
    }
    QHostInfo::lookupHost(host, this, [job, validateAndIssue](const QHostInfo &info) {
        if (info.error() != QHostInfo::NoError) {
            validateAndIssue({});
            return;
        }
        validateAndIssue(info.addresses());
    });
}

void EgressFetcher::issue(const std::shared_ptr<Job> &job, const QUrl &url,
                          const QHostAddress &address)
{
    // Pin the connection to the address that was just validated, while the
    // request still presents the original hostname: Host header for the
    // server's virtual-host routing, peerVerifyName so TLS is still verified
    // against the name the note asked for rather than the bare address.
    QUrl pinned = url;
    pinned.setHost(address.toString());

    QNetworkRequest req(pinned);
    req.setHeader(QNetworkRequest::UserAgentHeader, kUserAgent);
    req.setRawHeader(QByteArrayLiteral("Host"), url.host().toUtf8());
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
    // Bound what Qt itself will hold. Without this the reply reads as fast as
    // the socket delivers and buffers the whole body, so the cap below would
    // only trim an allocation that had already happened -- which is the shape
    // of the bug this replaces. Capping the read buffer applies TCP
    // backpressure instead, so an endless response costs bounded memory.
    reply->setReadBufferSize(cap + 1);

    QTimer *timer = new QTimer(reply);
    timer->setSingleShot(true);
    QObject::connect(timer, &QTimer::timeout, reply, &QNetworkReply::abort);
    timer->start(timeoutMsFor(job->purpose));

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

    QObject::connect(reply, &QNetworkReply::readyRead, reply, [reply, job, cap]() {
        job->body.append(reply->read(cap + 1 - job->body.size()));
        if (job->body.size() > cap)
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
            const QUrl next = url.resolved(QUrl(QString::fromUtf8(location)));
            ++job->hops;
            startHop(job, next);
            return;
        }

        if (reply->error() != QNetworkReply::NoError || status < 200 || status >= 300) {
            finish(job, false, QByteArray(), QString());
            return;
        }
        if (job->body.size() > maxBytesFor(job->purpose)) {
            finish(job, false, QByteArray(), QString());
            return;
        }
        // A response of the wrong type is not what was asked for; refusing it
        // keeps the parsers off attacker-chosen bytes.
        const QStringList accepted = acceptedTypesFor(job->purpose);
        bool typeOk = accepted.isEmpty() || contentType.isEmpty();
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
