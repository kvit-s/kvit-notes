// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
// The network trust boundary: what the app is allowed to contact, and what
// holds on the wire once it does. Everything here runs against a loopback
// HTTP server, so the suite never touches the real internet.
#include <QtTest/QtTest>

#include <QDir>
#include <QQmlContext>
#include <QQmlEngine>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>

#include "appcontext.h"
#include "egressfetcher.h"
#include "egresspolicy.h"
#include "embedmetadata.h"
#include "notecollection.h"
#include "settingsstore.h"

// A minimal HTTP/1.0 server on 127.0.0.1. Records the request lines it
// receives, counts the bytes it manages to hand over, and can be told to
// answer with a redirect instead of a body.
class LocalServer : public QObject
{
    Q_OBJECT
public:
    QStringList requests;
    qint64 bytesDelivered = 0;
    QByteArray body = "<html><head><meta property=\"og:title\" content=\"Local\">"
                      "</head></html>";
    QByteArray contentType = "text/html";
    QString redirectTo;          // non-empty: answer 302 to this Location
    // false: send no Content-Length and let the body run to EOF. Only a
    // streaming cap can stop that, so it is the case that tells the
    // enforcement mechanisms apart.
    bool declareLength = true;

    bool start()
    {
        connect(&m_server, &QTcpServer::newConnection, this, &LocalServer::onConnection);
        return m_server.listen(QHostAddress::LocalHost);
    }
    quint16 port() const { return m_server.serverPort(); }
    QString url(const QString &path = QStringLiteral("/page")) const
    {
        return QStringLiteral("http://127.0.0.1:%1%2").arg(port()).arg(path);
    }

private slots:
    void onConnection()
    {
        QTcpSocket *sock = m_server.nextPendingConnection();
        connect(sock, &QTcpSocket::bytesWritten, this,
                [this](qint64 n) { bytesDelivered += n; });
        connect(sock, &QTcpSocket::readyRead, this, [this, sock]() {
            const QByteArray req = sock->readAll();
            requests << QString::fromUtf8(req.left(req.indexOf('\r')));
            QByteArray resp;
            if (!redirectTo.isEmpty()) {
                resp = "HTTP/1.0 302 Found\r\nLocation: " + redirectTo.toUtf8()
                     + "\r\nContent-Length: 0\r\n\r\n";
            } else if (declareLength) {
                resp = "HTTP/1.0 200 OK\r\nContent-Type: " + contentType + "\r\n"
                       "Content-Length: " + QByteArray::number(body.size())
                     + "\r\n\r\n" + body;
            } else {
                resp = "HTTP/1.0 200 OK\r\nContent-Type: " + contentType
                     + "\r\n\r\n" + body;
            }
            sock->write(resp);
            sock->disconnectFromHost();
        });
    }

private:
    QTcpServer m_server;
};

class TestEgressPolicy : public QObject
{
    Q_OBJECT

private slots:
    // AppContext's collection is not open in these tests, so EmbedMetadata
    // caches under the per-user cache directory, which outlives the run. A
    // cached URL is reported without any request, which is correct behavior
    // and would quietly invalidate the assertions below.
    void init()
    {
        QDir(QDir(QStandardPaths::writableLocation(QStandardPaths::CacheLocation))
                 .filePath(QStringLiteral("embedcache")))
            .removeRecursively();
    }

    // ---- The decision layer ----
    void addressClassification_data();
    void addressClassification();
    void schemeAndCredentialRules();
    void originGranularity();
    void consentPersistsThroughSettings();

    // ---- Opening a note fetches nothing ----
    void openingANoteMakesNoRequest();
    void approvedOriginFetches();
    void cachedMetadataNeedsNoNewConsent();

    // ---- What holds on the wire ----
    void nameResolvingToPrivateAddressIsRefused();
    void aNameAnsweringWithAnyRefusedAddressIsRefused();
    void redirectToPrivateAddressIsRefused();
    void redirectToUnapprovedOriginIsRefused();
    void oversizedResponseIsCutOffWhileReceiving();
    void oversizedResponseWithNoDeclaredLengthIsCutOff();
    void wrongContentTypeIsRefused();

    // ---- Metadata-selected images ----
    void metadataImagesObeyTheSamePolicy();
};

// A policy with loopback permitted, which is the only address a hermetic
// test can serve from.
static void allowLoopback(EgressPolicy *policy)
{
    policy->setLoopbackAllowedForTests(true);
}

void TestEgressPolicy::addressClassification_data()
{
    QTest::addColumn<QString>("address");
    QTest::addColumn<bool>("blocked");

    QTest::newRow("public v4") << "93.184.216.34" << false;
    QTest::newRow("public v6") << "2606:2800:220:1:248:1893:25c8:1946" << false;
    QTest::newRow("loopback v4") << "127.0.0.1" << true;
    QTest::newRow("loopback v4 alt") << "127.99.12.3" << true;
    QTest::newRow("loopback v6") << "::1" << true;
    QTest::newRow("rfc1918 10") << "10.0.0.5" << true;
    QTest::newRow("rfc1918 172.16") << "172.16.0.1" << true;
    QTest::newRow("rfc1918 172.31") << "172.31.255.254" << true;
    QTest::newRow("rfc1918 192.168") << "192.168.1.1" << true;
    QTest::newRow("cloud metadata") << "169.254.169.254" << true;
    QTest::newRow("link-local v4") << "169.254.1.1" << true;
    QTest::newRow("link-local v6") << "fe80::1" << true;
    QTest::newRow("unique-local v6") << "fd00::1" << true;
    QTest::newRow("carrier nat") << "100.64.0.1" << true;
    QTest::newRow("unspecified") << "0.0.0.0" << true;
    QTest::newRow("multicast") << "224.0.0.1" << true;
    QTest::newRow("reserved") << "240.0.0.1" << true;
    // The IPv4-mapped form of a blocked address is the same host.
    QTest::newRow("v4-mapped loopback") << "::ffff:127.0.0.1" << true;
    QTest::newRow("v4-mapped private") << "::ffff:10.0.0.5" << true;
    // 172.32 is outside the /12 and must stay reachable.
    QTest::newRow("just outside rfc1918") << "172.32.0.1" << false;
}

void TestEgressPolicy::addressClassification()
{
    QFETCH(QString, address);
    QFETCH(bool, blocked);
    EgressPolicy policy;
    QCOMPARE(policy.addressIsBlocked(QHostAddress(address)), blocked);
}

void TestEgressPolicy::schemeAndCredentialRules()
{
    EgressPolicy policy;
    policy.setAutoLoadRemoteContent(true);   // no settings store: stays off

    // Only http(s) is ever fetched.
    QVERIFY(!policy.isAllowed("file:///etc/passwd"));
    QVERIFY(!policy.isAllowed("data:text/html,<b>x</b>"));
    QVERIFY(!policy.isAllowed("ftp://example.com/x"));
    QVERIFY(!policy.isAllowed("javascript:alert(1)"));
    QVERIFY(!policy.refusalReason("file:///etc/passwd").isEmpty());

    // Credentials in the URL are refused outright, approval or not.
    policy.allowOrigin("https://user:pw@example.com/x");
    QVERIFY(!policy.isAllowed("https://user:pw@example.com/x"));
    QVERIFY(!policy.canRequestConsent("https://user:pw@example.com/x"));

    // A local path is not an egress question at all: imageSourceFor passes
    // it through so ordinary note images keep working.
    QCOMPARE(policy.imageSourceFor("file:///home/a/pic.png"),
             QString("file:///home/a/pic.png"));
    QCOMPARE(policy.imageSourceFor("https://cdn.example.com/pic.png"), QString());
}

void TestEgressPolicy::originGranularity()
{
    EgressPolicy policy;
    QCOMPARE(EgressPolicy::originOf("https://example.com/a/b?c=d"),
             QString("https://example.com"));
    // A non-default port is a different origin; the scheme matters too.
    QCOMPARE(EgressPolicy::originOf("https://example.com:8443/x"),
             QString("https://example.com:8443"));
    QCOMPARE(EgressPolicy::originOf("http://example.com:80/x"),
             QString("http://example.com"));

    policy.allowOrigin("https://example.com/article");
    QVERIFY(policy.isAllowed("https://example.com/other"));      // same origin
    QVERIFY(!policy.isAllowed("http://example.com/other"));      // other scheme
    QVERIFY(!policy.isAllowed("https://example.com:8443/x"));    // other port
    QVERIFY(!policy.isAllowed("https://evil.example.com/x"));    // other host
    QVERIFY(!policy.isAllowed("https://example.com.evil.test/x"));

    policy.forgetOrigin("https://example.com/anything");
    QVERIFY(!policy.isAllowed("https://example.com/other"));
}

void TestEgressPolicy::consentPersistsThroughSettings()
{
    QTemporaryDir dir;
    const QString path = QDir(dir.path()).filePath("settings.json");

    {
        SettingsStore store;
        QVERIFY(store.open(path));
        EgressPolicy policy;
        policy.setSettings(&store);
        QVERIFY(!policy.autoLoadRemoteContent());   // off by default
        policy.allowOrigin("https://example.com/a");
        policy.setAutoLoadRemoteContent(true);
        store.flush();
    }
    {
        SettingsStore store;
        QVERIFY(store.open(path));
        EgressPolicy policy;
        policy.setSettings(&store);
        QCOMPARE(policy.allowedOrigins(), QStringList{"https://example.com"});
        QVERIFY(policy.autoLoadRemoteContent());
    }
}

// The finding this whole file exists for: rendering a note must not make a
// request. Drives the production wiring (AppContext installs the real
// fetcher) against a loopback server and asserts nothing arrives.
void TestEgressPolicy::openingANoteMakesNoRequest()
{
    LocalServer server;
    QVERIFY(server.start());

    AppContext ctx;
    QQmlEngine engine;
    ctx.installContextProperties(&engine);
    allowLoopback(ctx.egressPolicy());
    // Reached the way EmbedBlock.qml reaches it: the `Kvit` module singleton
    // this engine's AppContext resolves to. It was a context property until
    // that name moved to the module; this is the same object by the path
    // production now uses.
    auto *embed = engine.singletonInstance<EmbedMetadata *>(
        QStringLiteral("Kvit"), QStringLiteral("EmbedMetadata"));
    QVERIFY(embed);

    QSignalSpy consent(embed, &EmbedMetadata::consentRequired);
    QSignalSpy ready(embed, &EmbedMetadata::metadataReady);

    // Exactly what EmbedBlock.qml does when a card is created.
    embed->requestMetadata(server.url());

    QTRY_COMPARE(consent.count(), 1);
    QTest::qWait(300);            // give any stray request time to land
    QVERIFY2(server.requests.isEmpty(),
             "a request reached the server without the reader approving it");
    QCOMPARE(ready.count(), 0);
    QVERIFY(embed->needsConsent(server.url()));
}

void TestEgressPolicy::approvedOriginFetches()
{
    LocalServer server;
    QVERIFY(server.start());

    AppContext ctx;
    QQmlEngine engine;
    ctx.installContextProperties(&engine);
    allowLoopback(ctx.egressPolicy());
    // Reached the way EmbedBlock.qml reaches it: the `Kvit` module singleton
    // this engine's AppContext resolves to. It was a context property until
    // that name moved to the module; this is the same object by the path
    // production now uses.
    auto *embed = engine.singletonInstance<EmbedMetadata *>(
        QStringLiteral("Kvit"), QStringLiteral("EmbedMetadata"));
    QVERIFY(embed);

    // What clicking "Load preview" does.
    ctx.egressPolicy()->allowOrigin(server.url());
    QSignalSpy ready(embed, &EmbedMetadata::metadataReady);
    embed->requestMetadata(server.url());

    QTRY_VERIFY_WITH_TIMEOUT(ready.count() > 0, 10000);
    QCOMPARE(server.requests.size(), 1);
    QCOMPARE(embed->cachedMetadata(server.url()).value("title").toString(),
             QString("Local"));
    QVERIFY(!embed->needsConsent(server.url()));
}

void TestEgressPolicy::cachedMetadataNeedsNoNewConsent()
{
    QTemporaryDir dir;
    NoteCollection coll;
    QVERIFY(coll.openRoot(dir.path()));

    EgressPolicy policy;
    EmbedMetadata em;
    em.setCollection(&coll);
    em.setPolicy(&policy);

    // No fetcher and no approval: nothing is written and nothing is claimed.
    QSignalSpy consent(&em, &EmbedMetadata::consentRequired);
    em.requestMetadata("https://example.com/page");
    QCOMPARE(consent.count(), 1);
    QVERIFY(em.cachedMetadata("https://example.com/page").isEmpty());

    // Once approved and fetched, re-reading the cache asks nothing further,
    // even after the approval is withdrawn: no request is involved.
    policy.allowOrigin("https://example.com/page");
    em.requestMetadata("https://example.com/page");     // no fetcher: fallback
    QVERIFY(!em.cachedMetadata("https://example.com/page").isEmpty());
    policy.forgetOrigin("https://example.com/page");
    QVERIFY(!em.needsConsent("https://example.com/page"));
}

void TestEgressPolicy::nameResolvingToPrivateAddressIsRefused()
{
    // The forbidden address is a live loopback server rather than an
    // unroutable one, so the refusal is observable as "nothing reached the
    // host" instead of as a connection that would have failed anyway. Aimed
    // at a real private address, a regression here shows up as a connect
    // timeout, which reads like a flaky test and tempts the next person to
    // raise the timeout; aimed at a public one it would dial the internet.
    LocalServer forbidden;
    QVERIFY(forbidden.start());

    EgressPolicy policy;      // loopback is blocked: no test seam here
    policy.allowOrigin(QStringLiteral("http://intranet.example.test:%1/x")
                           .arg(forbidden.port()));

    EgressFetcher fetcher;
    fetcher.setPolicy(&policy);
    // A name the reader approved that answers with an address the policy
    // refuses: the classic SSRF shape. The address check runs on what DNS
    // returned, so the approval does not get it past the gate.
    fetcher.setResolverForTests([](const QString &) {
        return QList<QHostAddress>{QHostAddress(QStringLiteral("127.0.0.1"))};
    });

    bool called = false, result = true;
    fetcher.request(QUrl(QStringLiteral("http://intranet.example.test:%1/x")
                             .arg(forbidden.port())),
                    EgressFetcher::Purpose::EmbedPreview,
                    [&](bool ok, const QByteArray &, const QString &) {
                        called = true;
                        result = ok;
                    });
    QTRY_VERIFY(called);
    QVERIFY2(!result, "a name resolving to a refused address was fetched");
    QTest::qWait(200);
    QVERIFY2(forbidden.requests.isEmpty(),
             "a request reached the host the address check should have stopped");
}

// Every address DNS returns must pass, not just the one that would be used.
// A name answering with an allowed address and a refused one is a rebinding
// setup, and taking the first would be luck rather than a decision.
void TestEgressPolicy::aNameAnsweringWithAnyRefusedAddressIsRefused()
{
    LocalServer reachable;
    QVERIFY(reachable.start());

    EgressPolicy policy;
    allowLoopback(&policy);   // 127.0.0.1 is allowed for this case
    policy.allowOrigin(reachable.url());

    EgressFetcher fetcher;
    fetcher.setPolicy(&policy);
    // The allowed address first, so a "check only the address we will use"
    // regression connects and the server records it.
    fetcher.setResolverForTests([](const QString &) {
        return QList<QHostAddress>{QHostAddress(QStringLiteral("127.0.0.1")),
                                   QHostAddress(QStringLiteral("169.254.169.254"))};
    });

    bool called = false, result = true;
    fetcher.request(QUrl(reachable.url()), EgressFetcher::Purpose::EmbedPreview,
                    [&](bool ok, const QByteArray &, const QString &) {
                        called = true;
                        result = ok;
                    });
    QTRY_VERIFY(called);
    QVERIFY2(!result, "a name answering with a refused address was fetched");
    QTest::qWait(200);
    QVERIFY2(reachable.requests.isEmpty(),
             "the fetch used the allowed address and ignored the refused one");
}

void TestEgressPolicy::redirectToPrivateAddressIsRefused()
{
    LocalServer server;
    QVERIFY(server.start());
    server.redirectTo = "http://10.0.0.5/internal";

    EgressPolicy policy;
    allowLoopback(&policy);
    policy.allowOrigin(server.url());
    policy.setAutoLoadRemoteContent(false);

    EgressFetcher fetcher;
    fetcher.setPolicy(&policy);
    // Resolve literals to themselves; the redirect target then fails the
    // address check rather than a name lookup.
    fetcher.setResolverForTests([](const QString &host) {
        return QList<QHostAddress>{QHostAddress(host)};
    });

    bool called = false, result = true;
    fetcher.request(QUrl(server.url()), EgressFetcher::Purpose::EmbedPreview,
                    [&](bool ok, const QByteArray &, const QString &) {
                        called = true;
                        result = ok;
                    });
    QTRY_VERIFY_WITH_TIMEOUT(called, 10000);
    QVERIFY2(!result, "a redirect to a private address was followed");
    QCOMPARE(server.requests.size(), 1);   // the first hop only
}

void TestEgressPolicy::redirectToUnapprovedOriginIsRefused()
{
    LocalServer first;
    LocalServer second;
    QVERIFY(first.start());
    QVERIFY(second.start());
    first.redirectTo = second.url("/elsewhere");

    EgressPolicy policy;
    allowLoopback(&policy);
    policy.allowOrigin(first.url());       // only the first origin is approved

    EgressFetcher fetcher;
    fetcher.setPolicy(&policy);
    fetcher.setResolverForTests([](const QString &host) {
        return QList<QHostAddress>{QHostAddress(host)};
    });

    bool called = false, result = true;
    fetcher.request(QUrl(first.url()), EgressFetcher::Purpose::EmbedPreview,
                    [&](bool ok, const QByteArray &, const QString &) {
                        called = true;
                        result = ok;
                    });
    QTRY_VERIFY_WITH_TIMEOUT(called, 10000);
    QVERIFY2(!result,
             "a redirect to an origin the reader never approved was followed");
    QTest::qWait(200);
    QVERIFY2(second.requests.isEmpty(),
             "the redirect reached an origin the reader never approved");
}

void TestEgressPolicy::oversizedResponseIsCutOffWhileReceiving()
{
    LocalServer server;
    // Two orders of magnitude over the 512 kB embed cap. The size matters:
    // socket and reply buffers absorb a few megabytes before any handler
    // runs, so a body that merely exceeds the cap cannot distinguish a
    // streaming cap from truncation after the fact. At 32 MB it can.
    server.body = QByteArray(32 * 1024 * 1024, 'x');
    QVERIFY(server.start());

    EgressPolicy policy;
    allowLoopback(&policy);
    policy.allowOrigin(server.url());

    EgressFetcher fetcher;
    fetcher.setPolicy(&policy);
    fetcher.setResolverForTests([](const QString &host) {
        return QList<QHostAddress>{QHostAddress(host)};
    });

    bool called = false, result = true;
    fetcher.request(QUrl(server.url("/big")), EgressFetcher::Purpose::EmbedPreview,
                    [&](bool ok, const QByteArray &, const QString &) {
                        called = true;
                        result = ok;
                    });
    QTRY_VERIFY_WITH_TIMEOUT(called, 20000);
    QVERIFY2(!result, "an oversized response was accepted");
    // The point of a streaming cap: the transfer stops early instead of the
    // whole body being buffered and then truncated.
    QVERIFY2(server.bytesDelivered < 8 * 1024 * 1024,
             qPrintable(QStringLiteral("server delivered %1 bytes before the "
                                       "transfer was abandoned")
                            .arg(server.bytesDelivered)));
}

// The case that separates a streaming cap from a declared-length check. With
// no Content-Length the server's claim cannot be consulted, so refusing this
// requires actually stopping the transfer as the body arrives. A mutation
// audit found that the test above passes with EITHER mechanism removed,
// because each alone handles a response that declares its size; this one
// fails unless the body is capped while it is being read.
void TestEgressPolicy::oversizedResponseWithNoDeclaredLengthIsCutOff()
{
    LocalServer server;
    server.body = QByteArray(32 * 1024 * 1024, 'x');
    server.declareLength = false;
    QVERIFY(server.start());

    EgressPolicy policy;
    allowLoopback(&policy);
    policy.allowOrigin(server.url());

    EgressFetcher fetcher;
    fetcher.setPolicy(&policy);
    fetcher.setResolverForTests([](const QString &host) {
        return QList<QHostAddress>{QHostAddress(host)};
    });

    bool called = false, result = true;
    qint64 received = -1;
    fetcher.request(QUrl(server.url("/undeclared")),
                    EgressFetcher::Purpose::EmbedPreview,
                    [&](bool ok, const QByteArray &body, const QString &) {
                        called = true;
                        result = ok;
                        received = body.size();
                    });
    QTRY_VERIFY_WITH_TIMEOUT(called, 30000);
    QVERIFY2(!result, "an undeclared oversized response was accepted");
    QVERIFY2(received <= EgressFetcher::maxBytesFor(
                             EgressFetcher::Purpose::EmbedPreview) + 1,
             qPrintable(QStringLiteral("buffered %1 bytes past the cap")
                            .arg(received)));
    // Well under the whole body, rather than near the cap: kernel socket
    // buffers and Qt's own read-ahead absorb several megabytes before any
    // handler can act, measured around 8.5 MB here. The property worth
    // asserting is that the transfer is abandoned rather than completed; the
    // in-process bound is the assertion above.
    QVERIFY2(server.bytesDelivered < server.body.size() / 2,
             qPrintable(QStringLiteral("server delivered %1 of %2 bytes: the "
                                       "transfer ran to completion instead of "
                                       "being abandoned")
                            .arg(server.bytesDelivered)
                            .arg(server.body.size())));
}

void TestEgressPolicy::wrongContentTypeIsRefused()
{
    LocalServer server;
    server.contentType = "application/zip";
    server.body = "PK\x03\x04not-a-page";
    QVERIFY(server.start());

    EgressPolicy policy;
    allowLoopback(&policy);
    policy.allowOrigin(server.url());

    EgressFetcher fetcher;
    fetcher.setPolicy(&policy);
    fetcher.setResolverForTests([](const QString &host) {
        return QList<QHostAddress>{QHostAddress(host)};
    });

    bool called = false, result = true;
    fetcher.request(QUrl(server.url()), EgressFetcher::Purpose::EmbedPreview,
                    [&](bool ok, const QByteArray &, const QString &) {
                        called = true;
                        result = ok;
                    });
    QTRY_VERIFY_WITH_TIMEOUT(called, 10000);
    QVERIFY2(!result, "a non-HTML response was accepted as a page");
}

// A preview's thumbnail and favicon are chosen by the fetched page, so they
// are as untrusted as the page: approving one site must not hand a different
// site a request.
void TestEgressPolicy::metadataImagesObeyTheSamePolicy()
{
    EgressPolicy policy;
    policy.allowOrigin("https://example.com/article");

    // The page's own assets ride along on the approved origin.
    QVERIFY(policy.isAllowed("https://example.com/thumb.jpg"));
    QVERIFY(!policy.imageSourceFor("https://example.com/thumb.jpg").isEmpty());
    QVERIFY(policy.imageSourceFor("https://example.com/thumb.jpg")
                .startsWith("image://remote/"));

    // An og:image pointing somewhere else does not.
    QVERIFY(!policy.isAllowed("https://tracker.example.net/pixel.gif"));
    QCOMPARE(policy.imageSourceFor("https://tracker.example.net/pixel.gif"),
             QString());
    // Including the shapes a page might use to smuggle a request out.
    QCOMPARE(policy.imageSourceFor("http://127.0.0.1:8080/probe.png"), QString());
    QCOMPARE(policy.imageSourceFor("https://user:pw@example.com/x.png"), QString());

    // The provider id round-trips the URL rather than mangling it.
    const QString source =
        policy.imageSourceFor("https://example.com/a b?c=d&e=f#g");
    const QString id = source.mid(QStringLiteral("image://remote/").size());
    QCOMPARE(QUrl::fromPercentEncoding(id.toUtf8()),
             QString("https://example.com/a b?c=d&e=f#g"));
}

QTEST_MAIN(TestEgressPolicy)
#include "test_egresspolicy.moc"
