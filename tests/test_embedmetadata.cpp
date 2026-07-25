// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
#include <QtTest/QtTest>
#include "embedmetadata.h"
#include "imageassets.h"
#include "egresspolicy.h"
#include "notecollection.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalSpy>
#include <QTemporaryDir>

// A fake fetcher: returns canned HTML or a canned
// failure synchronously, so the tests are hermetic.
class CannedFetcher : public EmbedFetcher
{
public:
    bool succeed = true;
    QString html;
    int calls = 0;
    void fetch(const QString &, std::function<void(bool, const QString &)> done) override
    {
        ++calls;
        done(succeed, html);
    }
};

class TestEmbedMetadata : public QObject
{
    Q_OBJECT

private slots:
    void testIsEmbedUrl_data();
    void testIsEmbedUrl();
    void testIsVideoHost();
    void testParseOpenGraph();
    void testParseOpenGraphFallback();
    void testRequestFetchesParsesAndCaches();
    void testCacheHitDoesNotRefetch();
    void testFailedFetchIsFallback();
    void testFailedFetchIsForgottenAfterItsWindow();
    void testUntimestampedFailureExpiresOnSight();
    void testUnapprovedOriginIsNotFetched();
};

void TestEmbedMetadata::testIsEmbedUrl_data()
{
    QTest::addColumn<QString>("url");
    QTest::addColumn<bool>("embed");
    QTest::newRow("web page") << "https://example.com/article" << true;
    QTest::newRow("bare domain") << "https://example.com" << true;
    QTest::newRow("youtube") << "https://youtube.com/watch?v=abc" << true;
    QTest::newRow("http") << "http://example.org" << true;
    QTest::newRow("remote image") << "https://x.com/pic.png" << false;
    QTest::newRow("remote video file") << "https://x.com/clip.mp4" << false;
    QTest::newRow("local path") << "assets/pic.png" << false;
    QTest::newRow("not a url") << "just text" << false;
}

void TestEmbedMetadata::testIsEmbedUrl()
{
    QFETCH(QString, url);
    QFETCH(bool, embed);
    QCOMPARE(ImageAssets::isEmbedUrl(url), embed);
}

void TestEmbedMetadata::testIsVideoHost()
{
    QVERIFY(EmbedMetadata::isVideoHost("https://youtube.com/watch?v=x"));
    QVERIFY(EmbedMetadata::isVideoHost("https://youtu.be/x"));
    QVERIFY(EmbedMetadata::isVideoHost("https://vimeo.com/123"));
    QVERIFY(!EmbedMetadata::isVideoHost("https://example.com/page"));
}

void TestEmbedMetadata::testParseOpenGraph()
{
    const QString html =
        "<html><head>"
        "<meta property=\"og:title\" content=\"The Title\">"
        "<meta property=\"og:description\" content=\"A description.\">"
        "<meta property=\"og:image\" content=\"https://x.com/thumb.jpg\">"
        "</head></html>";
    const QVariantMap m = EmbedMetadata::parseOpenGraph(html, "https://x.com/p");
    QCOMPARE(m.value("title").toString(), QString("The Title"));
    QCOMPARE(m.value("description").toString(), QString("A description."));
    QCOMPARE(m.value("image").toString(), QString("https://x.com/thumb.jpg"));
    QCOMPARE(m.value("ok").toBool(), true);
    // Favicon derived from the host.
    QVERIFY(m.value("favicon").toString().contains("x.com/favicon.ico"));
}

void TestEmbedMetadata::testParseOpenGraphFallback()
{
    // No OpenGraph tags: title falls back to the host, ok is false, but the
    // card can still name the URL.
    const QVariantMap m = EmbedMetadata::parseOpenGraph("<html></html>",
                                                        "https://nowhere.test/x");
    QCOMPARE(m.value("ok").toBool(), false);
    QCOMPARE(m.value("title").toString(), QString("nowhere.test"));
}

void TestEmbedMetadata::testRequestFetchesParsesAndCaches()
{
    QTemporaryDir dir;
    NoteCollection coll;
    QVERIFY(coll.openRoot(dir.path()));
    CannedFetcher fetcher;
    fetcher.html = "<meta property=\"og:title\" content=\"Cached Title\">";

    // Fetching requires the reader to have approved the origin; these tests
    // are about what happens once they have.
    EgressPolicy policy;
    policy.allowOrigin("https://example.com/page");

    EmbedMetadata em;
    em.setCollection(&coll);
    em.setFetcher(&fetcher);
    em.setPolicy(&policy);
    QSignalSpy spy(&em, &EmbedMetadata::metadataReady);

    const QString url = "https://example.com/page";
    em.requestMetadata(url);
    QCOMPARE(spy.count(), 1);
    QCOMPARE(fetcher.calls, 1);
    const QVariantMap m = em.cachedMetadata(url);
    QCOMPARE(m.value("title").toString(), QString("Cached Title"));
    // The cache file exists under .kvit/cache/embedcache.
    QVERIFY(QDir(dir.path()).exists(".kvit/cache/embedcache"));
}

void TestEmbedMetadata::testCacheHitDoesNotRefetch()
{
    QTemporaryDir dir;
    NoteCollection coll;
    QVERIFY(coll.openRoot(dir.path()));
    CannedFetcher fetcher;
    fetcher.html = "<meta property=\"og:title\" content=\"First\">";
    EgressPolicy policy;
    policy.allowOrigin("https://example.com/page");
    EmbedMetadata em;
    em.setCollection(&coll);
    em.setFetcher(&fetcher);
    em.setPolicy(&policy);

    const QString url = "https://example.com/page";
    em.requestMetadata(url);
    QCOMPARE(fetcher.calls, 1);
    // Second request hits the cache — no re-fetch.
    em.requestMetadata(url);
    QCOMPARE(fetcher.calls, 1);
}

void TestEmbedMetadata::testFailedFetchIsFallback()
{
    QTemporaryDir dir;
    NoteCollection coll;
    QVERIFY(coll.openRoot(dir.path()));
    CannedFetcher fetcher;
    fetcher.succeed = false;
    EgressPolicy policy;
    policy.allowOrigin("https://unreachable.test/x");
    EmbedMetadata em;
    em.setCollection(&coll);
    em.setFetcher(&fetcher);
    em.setPolicy(&policy);
    QSignalSpy spy(&em, &EmbedMetadata::metadataReady);

    const QString url = "https://unreachable.test/x";
    em.requestMetadata(url);
    QCOMPARE(spy.count(), 1);
    const QVariantMap m = em.cachedMetadata(url);
    QCOMPARE(m.value("ok").toBool(), false);        // the fallback card
    QCOMPARE(m.value("title").toString(), QString("unreachable.test"));

    // A failure is remembered, so a note full of dead links does not
    // re-request every one of them each time it is opened.
    em.requestMetadata(url);
    QCOMPARE(fetcher.calls, 1);

    // But only for a while, and the reader can cut that short. Try again
    // drops the remembered failure and asks once more; here the second
    // attempt succeeds, which is the case the button exists for.
    fetcher.succeed = true;
    fetcher.html = "<meta property=\"og:title\" content=\"Back Up\">";
    em.retryMetadata(url);
    QCOMPARE(fetcher.calls, 2);
    QCOMPARE(em.cachedMetadata(url).value("title").toString(), QString("Back Up"));
    QCOMPARE(em.cachedMetadata(url).value("ok").toBool(), true);
}

// The window on a remembered failure. A cache entry is rewritten with an old
// timestamp -- the same thing the clock does an hour later -- and the card
// then reads as having no entry at all, so it asks again. A successful entry
// with the same age is still served from the cache.
void TestEmbedMetadata::testFailedFetchIsForgottenAfterItsWindow()
{
    QTemporaryDir dir;
    NoteCollection coll;
    QVERIFY(coll.openRoot(dir.path()));
    CannedFetcher fetcher;
    fetcher.succeed = false;
    EgressPolicy policy;
    policy.allowOrigin("https://unreachable.test/x");
    EmbedMetadata em;
    em.setCollection(&coll);
    em.setFetcher(&fetcher);
    em.setPolicy(&policy);

    const QString url = "https://unreachable.test/x";
    em.requestMetadata(url);
    QCOMPARE(fetcher.calls, 1);
    QVERIFY(!em.cachedMetadata(url).isEmpty());

    const QString path = QDir(dir.path())
        .filePath(".kvit/cache/embedcache/"
                  + QString::fromLatin1(
                        QCryptographicHash::hash(url.toUtf8(),
                                                 QCryptographicHash::Sha1).toHex())
                  + ".json");
    const auto restamp = [&path](qint64 fetchedAt) {
        QFile f(path);
        QVERIFY(f.open(QIODevice::ReadWrite));
        QJsonObject obj = QJsonDocument::fromJson(f.readAll()).object();
        obj["fetchedAt"] = fetchedAt;
        f.resize(0);
        f.write(QJsonDocument(obj).toJson());
    };

    const qint64 stale = QDateTime::currentSecsSinceEpoch()
                       - EmbedMetadata::FailedRetryAfterSecs - 60;
    restamp(stale);
    QVERIFY2(em.cachedMetadata(url).isEmpty(),
             "a failure older than its window is still being served");
    fetcher.succeed = true;
    fetcher.html = "<meta property=\"og:title\" content=\"Back Up\">";
    em.requestMetadata(url);
    QCOMPARE(fetcher.calls, 2);

    // The successful entry that replaced it is kept regardless of age.
    restamp(stale);
    QCOMPARE(em.cachedMetadata(url).value("title").toString(), QString("Back Up"));
    em.requestMetadata(url);
    QCOMPARE(fetcher.calls, 2);
}

// An entry written before fetch times were recorded carries no timestamp. A
// failure among those has been in place at least as long as the upgrade, so
// it must not outlive it.
void TestEmbedMetadata::testUntimestampedFailureExpiresOnSight()
{
    QTemporaryDir dir;
    NoteCollection coll;
    QVERIFY(coll.openRoot(dir.path()));
    const QString url = "https://cnn.com";
    const QString cacheDir = QDir(dir.path()).filePath(".kvit/cache/embedcache");
    QVERIFY(QDir().mkpath(cacheDir));
    QFile f(QDir(cacheDir).filePath(
        QString::fromLatin1(QCryptographicHash::hash(url.toUtf8(),
                                                     QCryptographicHash::Sha1).toHex())
        + ".json"));
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write("{\"ok\": false, \"title\": \"cnn.com\", \"url\": \"https://cnn.com\"}");
    f.close();

    CannedFetcher fetcher;
    fetcher.html = "<meta property=\"og:title\" content=\"CNN\">";
    EgressPolicy policy;
    policy.allowOrigin(url);
    EmbedMetadata em;
    em.setCollection(&coll);
    em.setFetcher(&fetcher);
    em.setPolicy(&policy);

    QVERIFY(em.cachedMetadata(url).isEmpty());
    em.requestMetadata(url);
    QCOMPARE(fetcher.calls, 1);
    QCOMPARE(em.cachedMetadata(url).value("title").toString(), QString("CNN"));
}

// Opening a note names URLs; it does not approve them. Without an approval
// the fetcher is never called, so the host never learns the note was opened.
void TestEmbedMetadata::testUnapprovedOriginIsNotFetched()
{
    QTemporaryDir dir;
    NoteCollection coll;
    QVERIFY(coll.openRoot(dir.path()));
    CannedFetcher fetcher;
    fetcher.html = "<meta property=\"og:title\" content=\"Should not load\">";

    EgressPolicy policy;      // nothing approved
    EmbedMetadata em;
    em.setCollection(&coll);
    em.setFetcher(&fetcher);
    em.setPolicy(&policy);
    QSignalSpy ready(&em, &EmbedMetadata::metadataReady);
    QSignalSpy consent(&em, &EmbedMetadata::consentRequired);

    const QString url = "https://example.com/page";
    em.requestMetadata(url);
    QCOMPARE(fetcher.calls, 0);
    QCOMPARE(ready.count(), 0);
    QCOMPARE(consent.count(), 1);
    QVERIFY(em.needsConsent(url));
    QVERIFY(em.cachedMetadata(url).isEmpty());

    // A build that never wired a policy fetches nothing either, rather than
    // falling back to the old fetch-on-sight behavior.
    EmbedMetadata unwired;
    unwired.setCollection(&coll);
    unwired.setFetcher(&fetcher);
    unwired.requestMetadata(url);
    QCOMPARE(fetcher.calls, 0);
}

QTEST_MAIN(TestEmbedMetadata)
#include "test_embedmetadata.moc"
