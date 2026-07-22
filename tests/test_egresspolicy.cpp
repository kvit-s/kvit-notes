// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
// The network trust boundary: what the app is allowed to contact, and what
// holds on the wire once it does. Everything here runs against a loopback
// HTTP server, so the suite never touches the real internet.
#include <QtTest/QtTest>

#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QLocalServer>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QQmlContext>
#include <QQmlEngine>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QThread>
#include <QBuffer>
#include <QImage>

#include "appcontext.h"
#include "egressfetcher.h"
#include "egresspolicy.h"
#include "embedmetadata.h"
#include "notecollection.h"
#include "localimageprovider.h"
#include "remoteimageprovider.h"
#include "remotemediacache.h"
#include "settingsstore.h"

// A minimal HTTP/1.0 server on 127.0.0.1. Records the request lines it
// receives, counts the bytes it manages to hand over, and can be told to
// answer with a redirect instead of a body.
class LocalServer : public QObject
{
    Q_OBJECT
public:
    QStringList requests;
    QList<QByteArray> requestHeads;   // the whole request head, for header checks
    qint64 bytesDelivered = 0;
    QByteArray body = "<html><head><meta property=\"og:title\" content=\"Local\">"
                      "</head></html>";
    QByteArray contentType = "text/html";
    QString redirectTo;          // non-empty: answer 302 to this Location
    // How many more requests answer with that redirect; -1 is "every one".
    // A finite count lets one test watch a redirect chain end in a body.
    int redirectsRemaining = -1;
    // false: send no Content-Length and let the body run to EOF. Only a
    // streaming cap can stop that, so it is the case that tells the
    // enforcement mechanisms apart.
    bool declareLength = true;
    qint64 declaredLengthOverride = -1;
    // false: omit the Content-Type header entirely.
    bool sendContentType = true;
    // false: accept the connection, read the request, and answer nothing --
    // the shape a hung server has, and the only way to observe how many
    // requests the client keeps open at once.
    bool respond = true;
    int responseDelayMs = 0;
    int peakConnections = 0;
    int liveConnections = 0;

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
        peakConnections = qMax(peakConnections, ++liveConnections);
        connect(sock, &QTcpSocket::disconnected, this,
                [this]() { --liveConnections; });
        connect(sock, &QTcpSocket::bytesWritten, this,
                [this](qint64 n) { bytesDelivered += n; });
        connect(sock, &QTcpSocket::readyRead, this, [this, sock]() {
            const QByteArray req = sock->readAll();
            requests << QString::fromUtf8(req.left(req.indexOf('\r')));
            requestHeads << req;
            if (!respond)
                return;
            QByteArray resp;
            if (!redirectTo.isEmpty() && redirectsRemaining != 0) {
                if (redirectsRemaining > 0)
                    --redirectsRemaining;
                resp = "HTTP/1.0 302 Found\r\nLocation: " + redirectTo.toUtf8()
                     + "\r\nContent-Length: 0\r\n\r\n";
            } else {
                const QByteArray typeHeader = sendContentType
                    ? "Content-Type: " + contentType + "\r\n" : QByteArray();
                if (declareLength) {
                    const qint64 declared = declaredLengthOverride >= 0
                        ? declaredLengthOverride : body.size();
                    resp = "HTTP/1.0 200 OK\r\n" + typeHeader
                         + "Content-Length: " + QByteArray::number(declared)
                         + "\r\n\r\n" + body;
                } else {
                    resp = "HTTP/1.0 200 OK\r\n" + typeHeader + "\r\n" + body;
                }
            }
            if (responseDelayMs > 0) {
                QTimer::singleShot(responseDelayMs, sock, [sock, resp]() {
                    sock->write(resp);
                    sock->disconnectFromHost();
                });
                return;
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
    void imageSourceSchemesAreAnAllowlist();
    void aNetworkCapableSchemeReachesALocalSocketAndIsRefused();
    void originGranularity();
    void ipv6OriginsRoundTripThroughSettings();
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
    void absentContentTypeIsRefused();
    void oneDeadlineCoversResolutionAndEveryRedirect();
    void aDeadFirstAddressFallsBackToTheNext();
    void hostHeaderCarriesTheWholeAuthority();
    void updateRedirectsStayOnTheDisclosedOrigin();
    void concurrentRequestsAreCappedAndQueued();
    void oversizedImageDimensionsAreRejectedBeforeDecode();
    void oversizedLocalImageIsRejectedBeforeDecode();
    void remoteMediaIsPlayedOnlyFromBoundedLocalCache();
    void remoteMediaIsStreamedToDiskNotBuffered();
    void remoteMediaCacheEvictsLeastRecentlyUsedPastItsBudget();
    void remoteMediaRevalidatesRedirectsAndSize();

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

    // ---- Special-use ranges that used to pass ----
    // IPv6 site-local. Deprecated, but still routed inside networks that
    // deployed it, so a note-driven request can reach a local service there.
    QTest::newRow("site-local v6") << "fec0::1" << true;
    QTest::newRow("site-local v6 top") << "feff:ffff::1" << true;
    // 198.18.0.0/15, the benchmarking range, which lab and campus networks
    // route internally.
    QTest::newRow("benchmarking low") << "198.18.0.1" << true;
    QTest::newRow("benchmarking high") << "198.19.255.254" << true;
    // The boundaries either side of that /15 stay reachable.
    QTest::newRow("just below benchmarking") << "198.17.255.254" << false;
    QTest::newRow("just above benchmarking") << "198.20.0.1" << false;
    // Documentation and test ranges: never a real destination.
    QTest::newRow("test-net-1") << "192.0.2.1" << true;
    QTest::newRow("test-net-2") << "198.51.100.7" << true;
    QTest::newRow("test-net-3") << "203.0.113.5" << true;
    QTest::newRow("v6 documentation") << "2001:db8::1" << true;
    QTest::newRow("v6 discard") << "100::1" << true;

    // An IPv6 address that carries an IPv4 one inside it delivers to that
    // IPv4 host, so the payload is what has to be classified.
    QTest::newRow("6to4 wrapping rfc1918") << "2002:0a00:0001::" << true;
    QTest::newRow("6to4 wrapping public") << "2002:5db8:d822::" << false;
    QTest::newRow("nat64 wrapping metadata") << "64:ff9b::a9fe:a9fe" << true;
    QTest::newRow("nat64 wrapping public") << "64:ff9b::5db8:d822" << false;
    // Teredo stores the client's IPv4 address inverted in the last 32 bits;
    // 0xf5fffffe is ~10.0.0.1.
    QTest::newRow("teredo wrapping rfc1918") << "2001:0:0:0:0:0:f5ff:fffe" << true;

    // Ordinary public IPv6 that merely starts with the same nibbles as a
    // special range must stay reachable.
    QTest::newRow("public v6 near site-local") << "fe00::1" << false;
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

    // A local path is not an egress question at all, and it still reaches
    // QML. It goes by way of the app's own in-process provider, which holds
    // it to the same decoded-size budget as a remote image; what matters here
    // is that it resolves to something loadable rather than to "".
    QCOMPARE(policy.imageSourceFor("file:///home/a/pic.png"),
             QString("image://local/%2Fhome%2Fa%2Fpic.png"));
    QCOMPARE(policy.imageSourceFor("https://cdn.example.com/pic.png"), QString());
}

// What a QML `source` may name is an allowlist, not "anything that is not
// http(s)". Qt keeps adding schemes its network stack can open, and the
// delegates hand whatever comes back here straight to an Image.
void TestEgressPolicy::imageSourceSchemesAreAnAllowlist()
{
    EgressPolicy policy;

    // The four shapes that genuinely cannot leave the process are the ones
    // allowed through. A file: URL is rewritten onto the app's own local
    // provider on the way — still in-process, and now size-checked before it
    // is decoded — while the rest are handed over unchanged.
    QCOMPARE(policy.imageSourceFor("file:///home/a/pic.png"),
             QString("image://local/%2Fhome%2Fa%2Fpic.png"));
    QCOMPARE(policy.imageSourceFor("qrc:/icons/x.svg"), QString("qrc:/icons/x.svg"));
    QCOMPARE(policy.imageSourceFor("data:image/png;base64,AAAA"),
             QString("data:image/png;base64,AAAA"));
    QCOMPARE(policy.imageSourceFor("pictures/local.png"),
             QString("pictures/local.png"));
    // The app's own in-process providers, and only those.
    QCOMPARE(policy.imageSourceFor("image://math/AAAA"),
             QString("image://math/AAAA"));
    QCOMPARE(policy.imageSourceFor("image://remote/x"), QString("image://remote/x"));
    QCOMPARE(policy.imageSourceFor("image://elsewhere/x"), QString());

    // Everything else yields nothing rather than being handed to QML, whether
    // or not this test knows what Qt would do with it.
    const QStringList refused = {
        QStringLiteral("local+http://socketname/probe.png"),
        QStringLiteral("unix+http://%2Ftmp%2Fdocker.sock/probe.png"),
        QStringLiteral("local+https://socketname/probe.png"),
        QStringLiteral("ftp://example.com/x.png"),
        QStringLiteral("javascript:alert(1)"),
        QStringLiteral("//tracker.example.net/pixel.gif"),   // protocol-relative
        QStringLiteral("about:blank"),
    };
    for (const QString &url : refused) {
        QVERIFY2(policy.imageSourceFor(url).isEmpty(),
                 qPrintable(QStringLiteral("%1 was handed to QML as an image "
                                           "source").arg(url)));
    }
}

// The concrete case behind the rule above: `local+http:` is a scheme
// QNetworkAccessManager speaks, and it sends a real HTTP request over a unix
// socket. An approved page choosing that as its og:image would otherwise
// reach a local service with no DNS check, no address check, no redirect
// revalidation, no byte cap and no timeout, because none of those run for a
// URL the policy classified as local.
void TestEgressPolicy::aNetworkCapableSchemeReachesALocalSocketAndIsRefused()
{
    const QString name = QStringLiteral("kvit-egress-probe-%1")
                             .arg(QCoreApplication::applicationPid());
    QLocalServer::removeServer(name);
    QLocalServer local;
    QVERIFY(local.listen(name));

    int connections = 0;
    connect(&local, &QLocalServer::newConnection, &local, [&]() {
        ++connections;
        // Left parented to the server: destroying a socket from inside its
        // own signal is not something this test needs to do.
        local.nextPendingConnection();
    });

    const QString probe = QStringLiteral("local+http://%1/probe.png").arg(name);

    // Qt's own stack opens the socket, which is what makes this a boundary
    // question rather than a naming question.
    QNetworkAccessManager nam;
    QScopedPointer<QNetworkReply> reply(nam.get(QNetworkRequest(QUrl(probe))));
    QTRY_VERIFY_WITH_TIMEOUT(connections > 0 || reply->isFinished(), 3000);
    QVERIFY2(connections > 0,
             "local+http: did not reach the local socket in this Qt build; the "
             "regression below is then weaker than it looks");

    // The policy hands QML nothing for it, so no Image ever names it.
    EgressPolicy policy;
    policy.setAutoLoadRemoteContent(true);   // no settings store: stays off
    policy.allowOrigin(probe);               // not an http(s) origin: no-op
    QCOMPARE(policy.imageSourceFor(probe), QString());
    QVERIFY(!policy.isAllowed(probe));
    QVERIFY(!policy.canRequestConsent(probe));
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

// An origin is stored as a string and parsed back on the next launch, so an
// IPv6 origin has to be written in a form that parses. Written bare, "https://
// ::1:8443" is not a URL at all, and the reader's approval quietly vanished
// between sessions.
void TestEgressPolicy::ipv6OriginsRoundTripThroughSettings()
{
    QCOMPARE(EgressPolicy::originOf("http://[::1]:8443/x"),
             QString("http://[::1]:8443"));
    QCOMPARE(EgressPolicy::originOf("https://[2606:2800:220::1]/x"),
             QString("https://[2606:2800:220::1]"));
    // The serialized origin has to parse back to the same origin.
    QCOMPARE(EgressPolicy::originOf(EgressPolicy::originOf("http://[::1]:8443/x")),
             QString("http://[::1]:8443"));

    QTemporaryDir dir;
    const QString path = QDir(dir.path()).filePath("settings.json");
    {
        SettingsStore store;
        QVERIFY(store.open(path));
        EgressPolicy policy;
        policy.setSettings(&store);
        policy.allowOrigin("http://[::1]:8443/media.mp3");
        QVERIFY(policy.isAllowed("http://[::1]:8443/other.mp3"));
        store.flush();
    }
    {
        SettingsStore store;
        QVERIFY(store.open(path));
        EgressPolicy policy;
        policy.setSettings(&store);
        QCOMPARE(policy.allowedOrigins(), QStringList{"http://[::1]:8443"});
        QVERIFY2(policy.isAllowed("http://[::1]:8443/other.mp3"),
                 "an approved IPv6 origin did not survive a reload");
    }
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

// An absent Content-Type used to count as an acceptable one, so a server had
// only to omit the header to hand the HTML parser, the image decoder or the
// multimedia backend bytes the purpose contract says they never see.
void TestEgressPolicy::absentContentTypeIsRefused()
{
    LocalServer server;
    server.sendContentType = false;
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
    QVERIFY2(!result,
             "a response with no declared type was accepted as a page");
}

// The deadline belongs to the job, not to a connection. It starts before the
// name is resolved, so a resolver that takes longer than the whole budget
// spends it, and it is not renewed per redirect, so a chain of slow hops
// cannot multiply it by the hop limit. Both halves are checked here because
// each alone leaves jobs pending far past what the caller was promised.
void TestEgressPolicy::oneDeadlineCoversResolutionAndEveryRedirect()
{
    // --- resolution is inside the budget ---
    {
        LocalServer server;
        QVERIFY(server.start());

        EgressPolicy policy;
        allowLoopback(&policy);
        policy.allowOrigin(server.url());

        EgressFetcher fetcher;
        fetcher.setPolicy(&policy);
        fetcher.setTimeoutMsForTests(300);
        // A resolver that takes longer than the whole job is allowed. Real
        // resolution blocks in the OS for its own timeout, which is how a
        // nominally eight-second job waited half a minute.
        fetcher.setResolverForTests([](const QString &host) {
            QThread::msleep(600);
            return QList<QHostAddress>{QHostAddress(host)};
        });

        bool called = false, result = true;
        fetcher.request(QUrl(server.url()), EgressFetcher::Purpose::EmbedPreview,
                        [&](bool ok, const QByteArray &, const QString &) {
                            called = true;
                            result = ok;
                        });
        QTRY_VERIFY_WITH_TIMEOUT(called, 10000);
        QVERIFY2(!result, "a job whose resolution outlasted its deadline was "
                          "still issued");
        QTest::qWait(200);
        QVERIFY2(server.requests.isEmpty(),
                 "the connection was opened after the deadline had passed");
    }

    // --- redirects share that one budget ---
    {
        LocalServer server;
        QVERIFY(server.start());
        server.redirectTo = server.url("/next");   // redirects forever
        server.responseDelayMs = 300;

        EgressPolicy policy;
        allowLoopback(&policy);
        policy.allowOrigin(server.url());

        EgressFetcher fetcher;
        fetcher.setPolicy(&policy);
        fetcher.setTimeoutMsForTests(400);
        fetcher.setResolverForTests([](const QString &host) {
            return QList<QHostAddress>{QHostAddress(host)};
        });

        bool called = false, result = true;
        QElapsedTimer clock;
        clock.start();
        fetcher.request(QUrl(server.url()), EgressFetcher::Purpose::EmbedPreview,
                        [&](bool ok, const QByteArray &, const QString &) {
                            called = true;
                            result = ok;
                        });
        QTRY_VERIFY_WITH_TIMEOUT(called, 10000);
        const qint64 elapsed = clock.elapsed();
        QVERIFY(!result);
        // Five hops at 300 ms apiece is what a per-hop timer allows; one
        // budget of 400 ms is what the caller was promised.
        QVERIFY2(elapsed < 1000,
                 qPrintable(QStringLiteral("the redirect chain ran %1 ms on a "
                                           "400 ms budget: each hop was given a "
                                           "fresh timeout").arg(elapsed)));
    }
}

// A name that answers with several addresses commonly has a dead one first --
// an IPv6 answer on a host with no IPv6 route is the everyday case. Every
// answer is validated before anything is dialled, so moving to the next one
// crosses no boundary, and they share the single deadline.
void TestEgressPolicy::aDeadFirstAddressFallsBackToTheNext()
{
    LocalServer server;
    QVERIFY(server.start());   // IPv4 loopback only

    EgressPolicy policy;
    allowLoopback(&policy);
    policy.allowOrigin(server.url());

    EgressFetcher fetcher;
    fetcher.setPolicy(&policy);
    // The v6 loopback first, where nothing is listening on this port, then
    // the address that answers.
    fetcher.setResolverForTests([](const QString &) {
        return QList<QHostAddress>{QHostAddress(QStringLiteral("::1")),
                                   QHostAddress(QStringLiteral("127.0.0.1"))};
    });

    bool called = false, result = false;
    fetcher.request(QUrl(server.url()), EgressFetcher::Purpose::EmbedPreview,
                    [&](bool ok, const QByteArray &, const QString &) {
                        called = true;
                        result = ok;
                    });
    QTRY_VERIFY_WITH_TIMEOUT(called, 15000);
    QVERIFY2(result,
             "a dead first address failed the whole job even though a working "
             "address followed it");
    QCOMPARE(server.requests.size(), 1);
}

// The Host header tells the server which site is being asked for. Built from
// the hostname alone it loses a non-default port and the brackets an IPv6
// literal needs, so the server is told a name the URL never contained.
void TestEgressPolicy::hostHeaderCarriesTheWholeAuthority()
{
    LocalServer server;
    QVERIFY(server.start());

    EgressPolicy policy;
    allowLoopback(&policy);
    policy.allowOrigin(server.url());

    EgressFetcher fetcher;
    fetcher.setPolicy(&policy);
    fetcher.setResolverForTests([](const QString &host) {
        return QList<QHostAddress>{QHostAddress(host)};
    });

    bool called = false;
    fetcher.request(QUrl(server.url()), EgressFetcher::Purpose::EmbedPreview,
                    [&](bool ok, const QByteArray &, const QString &) {
                        called = true;
                        Q_UNUSED(ok);
                    });
    QTRY_VERIFY_WITH_TIMEOUT(called, 10000);
    QCOMPARE(server.requestHeads.size(), 1);
    const QByteArray expected =
        "Host: 127.0.0.1:" + QByteArray::number(server.port());
    QVERIFY2(server.requestHeads.first().contains(expected),
             qPrintable(QStringLiteral("the request did not carry %1:\n%2")
                            .arg(QString::fromUtf8(expected),
                                 QString::fromUtf8(server.requestHeads.first()))));
}

// The update check is the one request that needs no per-origin approval,
// because the endpoint is fixed and disclosed. A redirect is chosen by
// whoever answers, so following one anywhere would turn that disclosure into
// a request to an undisclosed host.
void TestEgressPolicy::updateRedirectsStayOnTheDisclosedOrigin()
{
    LocalServer endpoint;
    LocalServer elsewhere;
    QVERIFY(endpoint.start());
    QVERIFY(elsewhere.start());
    endpoint.redirectTo = elsewhere.url("/releases");
    elsewhere.contentType = "application/json";
    elsewhere.body = "{\"tag_name\":\"v9.9.9\"}";

    EgressPolicy policy;
    allowLoopback(&policy);

    EgressFetcher fetcher;
    fetcher.setPolicy(&policy);
    fetcher.setResolverForTests([](const QString &host) {
        return QList<QHostAddress>{QHostAddress(host)};
    });

    bool called = false, result = true;
    fetcher.request(QUrl(endpoint.url("/latest")),
                    EgressFetcher::Purpose::UpdateCheck,
                    [&](bool ok, const QByteArray &, const QString &) {
                        called = true;
                        result = ok;
                    });
    QTRY_VERIFY_WITH_TIMEOUT(called, 10000);
    QVERIFY2(!result, "the update check followed a redirect off its origin");
    QTest::qWait(200);
    QVERIFY2(elsewhere.requests.isEmpty(),
             "the update check reached a host the app never disclosed");

    // A redirect that stays on the endpoint's own origin is still followed:
    // the rule is about the destination, not about redirects.
    LocalServer sameOrigin;
    QVERIFY(sameOrigin.start());
    sameOrigin.redirectTo = sameOrigin.url("/v2");
    sameOrigin.redirectsRemaining = 1;
    sameOrigin.contentType = "application/json";
    sameOrigin.body = "{\"tag_name\":\"v9.9.9\"}";

    bool called2 = false, result2 = false;
    fetcher.request(QUrl(sameOrigin.url("/latest")),
                    EgressFetcher::Purpose::UpdateCheck,
                    [&](bool ok, const QByteArray &, const QString &) {
                        called2 = true;
                        result2 = ok;
                    });
    QTRY_VERIFY_WITH_TIMEOUT(called2, 10000);
    QVERIFY2(result2, "a same-origin redirect of the update check was refused");
    QCOMPARE(sameOrigin.requests.size(), 2);
}

// A note names as many URLs as its author likes. Without a budget every one
// of them became a request in flight, each entitled to its own byte cap, so
// peak memory, socket count and descriptor count were all decided by the
// document rather than by the app.
void TestEgressPolicy::concurrentRequestsAreCappedAndQueued()
{
    LocalServer server;
    server.respond = false;   // accept and hold, so nothing completes early
    QVERIFY(server.start());

    EgressPolicy policy;
    allowLoopback(&policy);
    policy.allowOrigin(server.url());

    EgressFetcher fetcher;
    fetcher.setPolicy(&policy);
    fetcher.setTimeoutMsForTests(600);
    fetcher.setResolverForTests([](const QString &host) {
        return QList<QHostAddress>{QHostAddress(host)};
    });

    const int requested = 10;
    int completed = 0;
    for (int i = 0; i < requested; ++i) {
        fetcher.request(QUrl(server.url(QStringLiteral("/clip%1.mp3").arg(i))),
                        EgressFetcher::Purpose::RemoteMedia,
                        [&](bool, const QByteArray &, const QString &) {
                            ++completed;
                        });
    }

    QTest::qWait(400);
    const int cap = EgressFetcher::maxConcurrentFor(
        EgressFetcher::Purpose::RemoteMedia);
    QVERIFY2(server.peakConnections <= cap,
             qPrintable(QStringLiteral("%1 connections were open at once for a "
                                       "budget of %2")
                            .arg(server.peakConnections).arg(cap)));
    QVERIFY(fetcher.inFlightForTests() <= cap);
    QVERIFY(fetcher.queuedForTests() > 0);

    // Queued, not dropped: every caller still hears back once the ones ahead
    // of it have given up.
    QTRY_COMPARE_WITH_TIMEOUT(completed, requested, 30000);
    QCOMPARE(fetcher.queuedForTests(), 0);
}

void TestEgressPolicy::oversizedImageDimensionsAreRejectedBeforeDecode()
{
    QImage source(RemoteImageProvider::MaxDimension + 1, 1,
                  QImage::Format_ARGB32_Premultiplied);
    source.fill(Qt::transparent);
    QByteArray compressed;
    QBuffer output(&compressed);
    QVERIFY(output.open(QIODevice::WriteOnly));
    QVERIFY(source.save(&output, "PNG"));
    QVERIFY(compressed.size() < 64 * 1024);

    const QImage decoded = RemoteImageProvider::decodeForDisplay(compressed);
    QVERIFY2(decoded.isNull(),
             "an over-dimension image was fully decoded before rejection");
}

// The same budget for a file on disk. A note is untrusted input and so is
// what it points at, and Image.sourceSize does not help here: it bounds the
// pixmap that is kept, but Qt's PNG handler still allocates the image at its
// stored size and scales afterwards, so the peak is the same either way.
// Refusing it from the header is what avoids the allocation.
void TestEgressPolicy::oversizedLocalImageIsRejectedBeforeDecode()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    // Over the dimension limit in one axis, and tiny once compressed, so the
    // test does not depend on being able to allocate what it is refusing.
    const QString tooLarge = dir.filePath(QStringLiteral("huge.png"));
    {
        QImage source(RemoteImageProvider::MaxDimension + 1, 1,
                      QImage::Format_ARGB32_Premultiplied);
        source.fill(Qt::transparent);
        QVERIFY(source.save(tooLarge, "PNG"));
    }
    QVERIFY2(LocalImageProvider::decodeFile(tooLarge).isNull(),
             "an over-dimension local image was decoded instead of refused");

    // An ordinary image still loads, and a requested size scales it down
    // rather than being ignored or upscaling it.
    const QString ordinary = dir.filePath(QStringLiteral("ok.png"));
    {
        QImage source(800, 400, QImage::Format_ARGB32_Premultiplied);
        source.fill(Qt::red);
        QVERIFY(source.save(ordinary, "PNG"));
    }
    QCOMPARE(LocalImageProvider::decodeFile(ordinary).size(), QSize(800, 400));
    QCOMPARE(LocalImageProvider::decodeFile(ordinary, QSize(200, 200)).size(),
             QSize(200, 100));
    QCOMPARE(LocalImageProvider::decodeFile(ordinary, QSize(4000, 4000)).size(),
             QSize(800, 400));

    // A path that is not a file at all resolves to nothing rather than to a
    // broken half-image.
    QVERIFY(LocalImageProvider::decodeFile(dir.filePath(QStringLiteral("nope.png")))
                .isNull());
    QVERIFY(LocalImageProvider::decodeFile(dir.path()).isNull());
}

void TestEgressPolicy::remoteMediaIsPlayedOnlyFromBoundedLocalCache()
{
    LocalServer server;
    server.contentType = "audio/mpeg";
    server.body = QByteArrayLiteral("small-test-media");
    QVERIFY(server.start());

    EgressPolicy policy;
    allowLoopback(&policy);
    policy.allowOrigin(server.url());

    EgressFetcher fetcher;
    fetcher.setPolicy(&policy);
    fetcher.setResolverForTests([](const QString &host) {
        return QList<QHostAddress>{QHostAddress(host)};
    });
    RemoteMediaCache cache;
    cache.setFetcher(&fetcher);

    const QString remote = server.url("/sound.mp3");
    QVERIFY(cache.sourceFor(remote).isEmpty());
    QSignalSpy ready(&cache, &RemoteMediaCache::mediaReady);
    cache.request(remote);
    QTRY_COMPARE_WITH_TIMEOUT(ready.count(), 1, 10000);

    const QUrl local(cache.sourceFor(remote));
    QVERIFY(local.isLocalFile());
    QFile file(local.toLocalFile());
    QVERIFY(file.open(QIODevice::ReadOnly));
    QCOMPARE(file.readAll(), server.body);
    QCOMPARE(EgressFetcher::maxBytesFor(EgressFetcher::Purpose::RemoteMedia),
             qint64(64 * 1024 * 1024));
}

// Media is capped at 64 MiB per download, so accumulating one in a QByteArray
// before writing it out costs that much RAM per download on top of the file.
// The bytes go to the file as they arrive instead.
void TestEgressPolicy::remoteMediaIsStreamedToDiskNotBuffered()
{
    LocalServer server;
    server.contentType = "audio/mpeg";
    server.body = QByteArray(4 * 1024 * 1024, 'm');
    QVERIFY(server.start());

    EgressPolicy policy;
    allowLoopback(&policy);
    policy.allowOrigin(server.url());

    EgressFetcher fetcher;
    fetcher.setPolicy(&policy);
    fetcher.setResolverForTests([](const QString &host) {
        return QList<QHostAddress>{QHostAddress(host)};
    });
    RemoteMediaCache cache;
    cache.setFetcher(&fetcher);

    const QString remote = server.url("/long.mp3");
    QSignalSpy ready(&cache, &RemoteMediaCache::mediaReady);
    cache.request(remote);
    QTRY_COMPARE_WITH_TIMEOUT(ready.count(), 1, 30000);

    const QUrl local(cache.sourceFor(remote));
    QVERIFY(local.isLocalFile());
    QCOMPARE(QFileInfo(local.toLocalFile()).size(), qint64(server.body.size()));
    QCOMPARE(cache.cachedBytes(), qint64(server.body.size()));
    QVERIFY2(fetcher.peakBufferedBytesForTests() < 512 * 1024,
             qPrintable(QStringLiteral("%1 bytes of media were accumulated in "
                                       "memory on the way to the file")
                            .arg(fetcher.peakBufferedBytesForTests())));
}

// Every finished download used to be retained until the cache was destroyed,
// so a note naming enough distinct media URLs decided how much temporary disk
// and how many descriptors the app held. Both budgets evict, oldest use first.
void TestEgressPolicy::remoteMediaCacheEvictsLeastRecentlyUsedPastItsBudget()
{
    LocalServer server;
    server.contentType = "audio/mpeg";
    server.body = QByteArray(1000, 'x');
    QVERIFY(server.start());

    EgressPolicy policy;
    allowLoopback(&policy);
    policy.allowOrigin(server.url());

    EgressFetcher fetcher;
    fetcher.setPolicy(&policy);
    fetcher.setResolverForTests([](const QString &host) {
        return QList<QHostAddress>{QHostAddress(host)};
    });

    RemoteMediaCache cache;
    cache.setFetcher(&fetcher);
    cache.setBudgetForTests(2, 10000);   // two entries, well inside the bytes
    QSignalSpy released(&cache, &RemoteMediaCache::mediaReleased);

    const QStringList urls = {server.url("/a.mp3"), server.url("/b.mp3"),
                              server.url("/c.mp3")};
    QSignalSpy ready(&cache, &RemoteMediaCache::mediaReady);
    for (int i = 0; i < urls.size(); ++i) {
        cache.request(urls.at(i));
        QTRY_COMPARE_WITH_TIMEOUT(ready.count(), i + 1, 15000);
        if (i == 1) {
            // Touch the first one so the second becomes the oldest use: the
            // rule is least-recently-used, not first-in.
            QVERIFY(!cache.sourceFor(urls.at(0)).isEmpty());
        }
    }

    QCOMPARE(cache.cachedCount(), 2);
    QCOMPARE(cache.cachedBytes(), qint64(2 * server.body.size()));
    QCOMPARE(released.count(), 1);
    QCOMPARE(released.at(0).at(0).toString(), urls.at(1));
    QVERIFY(cache.sourceFor(urls.at(1)).isEmpty());
    QVERIFY(!cache.sourceFor(urls.at(0)).isEmpty());
    QVERIFY(!cache.sourceFor(urls.at(2)).isEmpty());

    // The evicted download's file is gone from disk, not merely forgotten.
    const QString survivor = QUrl(cache.sourceFor(urls.at(2))).toLocalFile();
    QVERIFY(QFileInfo::exists(survivor));

    // The byte budget evicts on its own terms.
    cache.setBudgetForTests(8, 1500);
    QCOMPARE(cache.cachedCount(), 1);
    QVERIFY(cache.cachedBytes() <= 1500);

    // And the explicit release path empties it, deleting the file with it.
    cache.clear();
    QCOMPARE(cache.cachedCount(), 0);
    QCOMPARE(cache.cachedBytes(), qint64(0));
    QVERIFY2(!QFileInfo::exists(survivor),
             "clearing the cache left its temporary file on disk");
}

void TestEgressPolicy::remoteMediaRevalidatesRedirectsAndSize()
{
    LocalServer redirector;
    redirector.redirectTo = "http://10.0.0.5/private.mp3";
    QVERIFY(redirector.start());

    EgressPolicy policy;
    allowLoopback(&policy);
    policy.allowOrigin(redirector.url());
    EgressFetcher fetcher;
    fetcher.setPolicy(&policy);
    fetcher.setResolverForTests([](const QString &host) {
        return QList<QHostAddress>{QHostAddress(host)};
    });
    RemoteMediaCache cache;
    cache.setFetcher(&fetcher);
    QSignalSpy redirectFailed(&cache, &RemoteMediaCache::mediaFailed);
    const QString redirected = redirector.url("/redirect.mp3");
    cache.request(redirected);
    QTRY_COMPARE_WITH_TIMEOUT(redirectFailed.count(), 1, 10000);
    QVERIFY(cache.sourceFor(redirected).isEmpty());
    QCOMPARE(redirector.requests.size(), 1);

    LocalServer oversized;
    oversized.contentType = "audio/mpeg";
    oversized.body = "not actually a huge allocation";
    oversized.declaredLengthOverride =
        EgressFetcher::maxBytesFor(EgressFetcher::Purpose::RemoteMedia) + 1;
    QVERIFY(oversized.start());
    policy.allowOrigin(oversized.url());
    QSignalSpy sizeFailed(&cache, &RemoteMediaCache::mediaFailed);
    const QString tooLarge = oversized.url("/large.mp3");
    cache.request(tooLarge);
    QTRY_COMPARE_WITH_TIMEOUT(sizeFailed.count(), 1, 10000);
    QVERIFY(cache.sourceFor(tooLarge).isEmpty());
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
