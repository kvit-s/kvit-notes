// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
#include <QtTest/QtTest>
#include "embedmetadata.h"
#include "notecollection.h"

#include <QSignalSpy>
#include <QTemporaryDir>

// A fake fetcher (phase11 decision 11): returns canned HTML or a canned
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
    QCOMPARE(EmbedMetadata::isEmbedUrl(url), embed);
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

    EmbedMetadata em;
    em.setCollection(&coll);
    em.setFetcher(&fetcher);
    QSignalSpy spy(&em, &EmbedMetadata::metadataReady);

    const QString url = "https://example.com/page";
    em.requestMetadata(url);
    QCOMPARE(spy.count(), 1);
    QCOMPARE(fetcher.calls, 1);
    const QVariantMap m = em.cachedMetadata(url);
    QCOMPARE(m.value("title").toString(), QString("Cached Title"));
    // The cache file exists under .kvit/embedcache.
    QVERIFY(QDir(dir.path()).exists(".kvit/embedcache"));
}

void TestEmbedMetadata::testCacheHitDoesNotRefetch()
{
    QTemporaryDir dir;
    NoteCollection coll;
    QVERIFY(coll.openRoot(dir.path()));
    CannedFetcher fetcher;
    fetcher.html = "<meta property=\"og:title\" content=\"First\">";
    EmbedMetadata em;
    em.setCollection(&coll);
    em.setFetcher(&fetcher);

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
    EmbedMetadata em;
    em.setCollection(&coll);
    em.setFetcher(&fetcher);
    QSignalSpy spy(&em, &EmbedMetadata::metadataReady);

    const QString url = "https://unreachable.test/x";
    em.requestMetadata(url);
    QCOMPARE(spy.count(), 1);
    const QVariantMap m = em.cachedMetadata(url);
    QCOMPARE(m.value("ok").toBool(), false);        // the fallback card
    QCOMPARE(m.value("title").toString(), QString("unreachable.test"));
}

QTEST_MAIN(TestEmbedMetadata)
#include "test_embedmetadata.moc"
