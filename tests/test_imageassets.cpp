// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
#include <QtTest>
#include <QTemporaryDir>
#include <QUrl>
#include <QDateTime>
#include <QImage>
#include <QImageReader>
#include <QFile>
#include <QDir>

#include "imageassets.h"

using Kind = ImageAssets::Kind;

// Corpus for the image/media markdown parse-build-classify-resolve core.
// Pure functions are tested without a filesystem; resolveSource runs against
// a real temp directory.
class TestImageAssets : public QObject
{
    Q_OBJECT

private slots:
    void parseFullExpression()
    {
        const auto p = ImageAssets::parseLine(
            QStringLiteral("![a cat|300](cats/tom.png \"My cat\")"));
        QVERIFY(p.valid);
        QCOMPARE(p.alt, QStringLiteral("a cat"));
        QCOMPARE(p.width, 300);
        QCOMPARE(p.path, QStringLiteral("cats/tom.png"));
        QCOMPARE(p.caption, QStringLiteral("My cat"));
        QCOMPARE(p.kind, Kind::Image);
    }

    void parseMinimal()
    {
        const auto p = ImageAssets::parseLine(QStringLiteral("![](x.jpg)"));
        QVERIFY(p.valid);
        QCOMPARE(p.alt, QString());
        QCOMPARE(p.width, 0);
        QCOMPARE(p.path, QStringLiteral("x.jpg"));
        QCOMPARE(p.caption, QString());
    }

    void parseWidthAndHeightSuffix()
    {
        // Obsidian's |WxH keeps the width; height derives from aspect.
        const auto p = ImageAssets::parseLine(QStringLiteral("![alt|200x100](x.png)"));
        QVERIFY(p.valid);
        QCOMPARE(p.width, 200);
        QCOMPARE(p.alt, QStringLiteral("alt"));
    }

    void barInAltThatIsNotAWidthStays()
    {
        // A trailing |token that is not a number is part of the alt text.
        const auto p = ImageAssets::parseLine(QStringLiteral("![a|b](x.png)"));
        QVERIFY(p.valid);
        QCOMPARE(p.alt, QStringLiteral("a|b"));
        QCOMPARE(p.width, 0);
    }

    void notAnImageExpression()
    {
        // Leading/trailing text fails the whole-line rule.
        QVERIFY(!ImageAssets::parseLine(QStringLiteral("see ![a](x.png)")).valid);
        QVERIFY(!ImageAssets::parseLine(QStringLiteral("![a](x.png) here")).valid);
        QVERIFY(!ImageAssets::parseLine(QStringLiteral("[a](x.png)")).valid); // link, not image
        QVERIFY(!ImageAssets::parseLine(QStringLiteral("![a]()")).valid);     // no path
        QVERIFY(!ImageAssets::parseLine(QStringLiteral("plain text")).valid);
    }

    void kindByExtension()
    {
        QCOMPARE(ImageAssets::kindForExtension("a.PNG"), Kind::Image);
        QCOMPARE(ImageAssets::kindForExtension("a.webp"), Kind::Image);
        QCOMPARE(ImageAssets::kindForExtension("a.svg"), Kind::Image);
        QCOMPARE(ImageAssets::kindForExtension("a.mp4"), Kind::Media);
        QCOMPARE(ImageAssets::kindForExtension("a.mp3"), Kind::Media);
        QCOMPARE(ImageAssets::kindForExtension("a.txt"), Kind::None);
        // URL with a query string still classifies by extension.
        QCOMPARE(ImageAssets::kindForExtension("http://h/a.jpg?v=2"), Kind::Image);
    }

    void classifyByExtension()
    {
        QCOMPARE(ImageAssets::classifyLine("![a](x.png)").kind, Kind::Image);
        QCOMPARE(ImageAssets::classifyLine("![a](song.mp3)").kind, Kind::Media);
        // A recognized expression whose target is not media/image is NOT a
        // block (stays a paragraph): e.g. a .txt.
        QVERIFY(!ImageAssets::classifyLine("![a](notes.txt)").valid);
    }

    void buildRoundTrips_data()
    {
        QTest::addColumn<QString>("markdown");
        QTest::newRow("full") << "![a cat|300](cats/tom.png \"My cat\")";
        QTest::newRow("no-width") << "![alt](x.jpg \"cap\")";
        QTest::newRow("no-caption") << "![alt|120](x.jpg)";
        QTest::newRow("minimal") << "![](x.png)";
    }
    void buildRoundTrips()
    {
        QFETCH(QString, markdown);
        const auto p = ImageAssets::parseLine(markdown);
        QVERIFY(p.valid);
        QCOMPARE(ImageAssets::buildMarkdown(p.path, p.alt, p.caption, p.width),
                 markdown);
    }

    // M9: fields carrying the delimiter characters must survive a build ->
    // parse round trip rather than changing the expression's structure.
    void buildParseRoundTripsHostileFields_data()
    {
        QTest::addColumn<QString>("path");
        QTest::addColumn<QString>("alt");
        QTest::addColumn<QString>("caption");
        QTest::addColumn<int>("width");
        QTest::newRow("plain")          << "x.png" << "alt" << "cap" << 0;
        QTest::newRow("bracket in alt") << "x.png" << "a [b] c" << "" << 0;
        QTest::newRow("close bracket")  << "x.png" << "a] c" << "" << 0;
        QTest::newRow("quote caption")  << "x.png" << "alt" << "he said \"hi\"" << 0;
        QTest::newRow("paren path")     << "a_(b).png" << "alt" << "" << 0;
        QTest::newRow("space path")     << "my pic.png" << "alt" << "" << 0;
        QTest::newRow("all at once")    << "a_(b) c.png" << "x]y" << "q\"r" << 300;
        QTest::newRow("bar in alt")     << "x.png" << "a|b" << "" << 0;
        // A caption can hold a line break; the expression is one line,
        // so it rides the same escape as the delimiters.
        QTest::newRow("caption break")  << "x.png" << "alt"
                                        << "first line\nsecond line" << 0;
        QTest::newRow("break and quote")<< "x.png" << "alt"
                                        << "he said\n\"hi\"" << 0;
        QTest::newRow("backslash n")    << "x.png" << "alt"
                                        << "a literal \\n stays" << 0;
        QTest::newRow("break in alt")   << "x.png" << "a\nb" << "" << 0;
    }
    void buildParseRoundTripsHostileFields()
    {
        QFETCH(QString, path);
        QFETCH(QString, alt);
        QFETCH(QString, caption);
        QFETCH(int, width);
        const QString md =
            ImageAssets::buildMarkdown(path, alt, caption, width);
        const auto p = ImageAssets::parseLine(md);
        QVERIFY2(p.valid, qPrintable("did not parse: " + md));
        QCOMPARE(p.path, path);
        QCOMPARE(p.alt, alt);
        QCOMPARE(p.caption, caption);
        QCOMPARE(p.width, width);
    }

    void resolveOrder()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString root = dir.path();
        QDir(root).mkpath("folder/assets");
        QDir(root).mkpath("assets");
        // A file under the note folder AND under the root; note-relative wins.
        auto writeFile = [](const QString &p) {
            QFile f(p); QVERIFY(f.open(QIODevice::WriteOnly)); f.write("x"); f.close();
        };
        writeFile(root + "/folder/assets/pic.png");
        writeFile(root + "/assets/pic.png");
        const QString noteDir = root + "/folder";

        const QString src = ImageAssets::resolveSource("assets/pic.png", noteDir, root);
        QCOMPARE(src, QUrl::fromLocalFile(root + "/folder/assets/pic.png").toString());

        // Root-relative when the note folder has none.
        const QString src2 = ImageAssets::resolveSource(
            "assets/pic.png", root + "/empty", root);
        QCOMPARE(src2, QUrl::fromLocalFile(root + "/assets/pic.png").toString());

        // http(s) URL passes through verbatim.
        QCOMPARE(ImageAssets::resolveSource("https://h/a.png", noteDir, root),
                 QStringLiteral("https://h/a.png"));

        // Unresolved → empty (the placeholder).
        QVERIFY(ImageAssets::resolveSource("missing.png", noteDir, root).isEmpty());
    }

    // A website names a file at its own root with a leading "/": Hugo serves
    // `/images/a.png` from `static/images/a.png`. Such a path used to be
    // tried only as an absolute path on this machine, where it names nothing.
    void siteRootPathResolvesUnderSiteFolder()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString root = dir.path();
        QDir(root).mkpath("static/images");
        QDir(root).mkpath("images");
        QDir(root).mkpath("content/posts");
        auto writeFile = [](const QString &p) {
            QFile f(p); QVERIFY(f.open(QIODevice::WriteOnly)); f.write("x"); f.close();
        };
        writeFile(root + "/static/images/a.png");
        writeFile(root + "/images/b.png");
        const QString noteDir = root + "/content/posts";

        // Under the site folder when one is given.
        QCOMPARE(ImageAssets::resolveSource("/images/a.png", noteDir, root,
                                            root + "/static"),
                 QUrl::fromLocalFile(root + "/static/images/a.png").toString());
        // Under the vault root when none is.
        QCOMPARE(ImageAssets::resolveSource("/images/b.png", noteDir, root),
                 QUrl::fromLocalFile(root + "/images/b.png").toString());
        // The site folder is where "/" points, not an extra search path.
        QVERIFY(ImageAssets::resolveSource("/images/b.png", noteDir, root,
                                           root + "/static").isEmpty());
        // A real absolute path still resolves as itself.
        QCOMPARE(ImageAssets::resolveSource(root + "/images/b.png", noteDir,
                                            root, root + "/static"),
                 QUrl::fromLocalFile(root + "/images/b.png").toString());
        // "//host/x" is a protocol-relative URL, never a site path.
        QVERIFY(ImageAssets::resolveSource("//images/b.png", noteDir, root)
                    .isEmpty());
    }

    // URL schemes are case-insensitive, so every stage has to read them the
    // same way. Classification always did; resolution compared literal
    // lowercase prefixes, so `HTTPS://…` was accepted as an image and then
    // hunted for on disk, where it resolved to nothing and rendered as the
    // broken-path placeholder.
    void uppercaseSchemeResolvesAsRemote_data()
    {
        QTest::addColumn<QString>("url");
        QTest::newRow("lowercase") << QStringLiteral("https://h/a.png");
        QTest::newRow("uppercase") << QStringLiteral("HTTPS://h/a.png");
        QTest::newRow("mixed") << QStringLiteral("HtTp://h/a.png");
    }

    void uppercaseSchemeResolvesAsRemote()
    {
        QFETCH(QString, url);
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        QCOMPARE(ImageAssets::resolveSource(url, dir.path(), dir.path()), url);

        // And the same URL with no media extension is still an embed, so the
        // two classifications stay consistent with each other.
        const QString page = url.left(url.size() - 5);   // drop "a.png"
        QVERIFY(ImageAssets::isEmbedUrl(page));
    }

    // What the embed dialog accepts. A reader typing a host the way they say
    // it out loud ("cnn.com") must reach the embed card: without a scheme the
    // URL is not remote, so the block would land as an image and show the
    // broken-image placeholder.
    void normalizeEmbedUrl_data()
    {
        QTest::addColumn<QString>("typed");
        QTest::addColumn<QString>("expected");
        QTest::newRow("bare host")        << "cnn.com" << "https://cnn.com";
        QTest::newRow("host and path")    << "cnn.com/world"
                                          << "https://cnn.com/world";
        QTest::newRow("www")              << "www.cnn.com" << "https://www.cnn.com";
        QTest::newRow("surrounding space")<< "  cnn.com \t" << "https://cnn.com";
        QTest::newRow("protocol relative")<< "//cnn.com/x" << "https://cnn.com/x";
        QTest::newRow("host and port")    << "localhost:8080/wiki"
                                          << "https://localhost:8080/wiki";
        QTest::newRow("intranet host")    << "wiki" << "https://wiki";
        // An address that already names how to fetch it is left alone,
        // including the uppercase spelling of the scheme.
        QTest::newRow("https")            << "https://cnn.com/a"
                                          << "https://cnn.com/a";
        QTest::newRow("http kept")        << "http://cnn.com" << "http://cnn.com";
        QTest::newRow("uppercase scheme") << "HTTPS://cnn.com" << "HTTPS://cnn.com";
        // Nothing an embed card could fetch: the dialog leaves OK disabled
        // rather than storing a block that can only report itself broken.
        QTest::newRow("empty")            << "" << "";
        QTest::newRow("blank")            << "   " << "";
        QTest::newRow("prose")            << "see the cnn.com story" << "";
        QTest::newRow("mailto")           << "mailto:a@b.com" << "";
        QTest::newRow("file")             << "file:///home/a/pic.png" << "";
    }
    void normalizeEmbedUrl()
    {
        QFETCH(QString, typed);
        QFETCH(QString, expected);
        QCOMPARE(ImageAssets::normalizeEmbedUrl(typed), expected);
    }

    // The normalizer feeds the classifier, so what the dialog stores has to
    // reach the embed delegate rather than the image one.
    void normalizedBareHostIsAnEmbed()
    {
        const QString url = ImageAssets::normalizeEmbedUrl("cnn.com");
        QVERIFY(ImageAssets::isEmbedUrl(url));
        const auto p = ImageAssets::parseLine("![](" + url + ")");
        QVERIFY(p.valid);
        QCOMPARE(p.path, url);
        QVERIFY(ImageAssets::isEmbedUrl(p.path));
        // A typed host that does name an image file stays an image.
        QVERIFY(!ImageAssets::isEmbedUrl(
            ImageAssets::normalizeEmbedUrl("cnn.com/logo.png")));
    }

    // The file picker's filters and the classifier read one list, so the
    // property to hold is that every extension a filter offers classifies as
    // the kind that filter was asked for. Asserting the literal filter string
    // instead would pass while the picker offered a file the app rejects.
    void nameFiltersMatchTheClassifier()
    {
        ImageAssets assets;
        for (const QString &kind : {QStringLiteral("image"),
                                    QStringLiteral("media")}) {
            const QStringList filters = assets.nameFilters(kind);
            QCOMPARE(filters.size(), 2);
            QCOMPARE(filters.last(), QStringLiteral("All files (*)"));

            const int open = filters.first().indexOf(QLatin1Char('('));
            const QStringList patterns =
                filters.first().mid(open + 1).chopped(1).split(QLatin1Char(' '));
            QVERIFY(!patterns.isEmpty());
            const Kind expected =
                kind == QLatin1String("media") ? Kind::Media : Kind::Image;
            for (const QString &pattern : patterns) {
                QVERIFY2(pattern.startsWith(QLatin1String("*.")),
                         qPrintable(pattern));
                QCOMPARE(ImageAssets::kindForExtension(
                             QStringLiteral("file") + pattern.mid(1)),
                         expected);
            }
        }
        // The two kinds name different files: an audio picker that offered
        // PNGs would be the bug this guards.
        QVERIFY(assets.nameFilters(QStringLiteral("media")).first()
                    .contains(QStringLiteral("*.mp3")));
        QVERIFY(!assets.nameFilters(QStringLiteral("media")).first()
                     .contains(QStringLiteral("*.png")));
        QVERIFY(assets.nameFilters(QStringLiteral("image")).first()
                    .contains(QStringLiteral("*.png")));
        // Anything that is not "media" is the image picker, which is what the
        // dialog falls back to when a caller names no kind.
        QCOMPARE(assets.nameFilters(QString()),
                 assets.nameFilters(QStringLiteral("image")));
    }

    // ---- measuring a file ----
    //
    // A block whose markdown carries no width draws the picture at the
    // picture's own width, and asks the decoder for that many pixels. Both
    // answers come from these measurements; reading them back out of the
    // loaded image instead made each depend on the other, which QML reports
    // as a binding loop.

    void naturalSizeReadsThePicturesOwnSize()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        QImage picture(360, 240, QImage::Format_RGB32);
        picture.fill(Qt::darkCyan);
        QVERIFY(picture.save(dir.filePath(QStringLiteral("chart.png"))));

        ImageAssets assets;
        const QString url = ImageAssets::resolveSource(
            QStringLiteral("chart.png"), dir.path(), QString());
        QCOMPARE(assets.naturalSize(url), QSize(360, 240));
        // Asked again, it answers from what it remembered: a binding asks
        // this once per row and again every time a row is recycled.
        QCOMPARE(assets.naturalSize(url), QSize(360, 240));
    }

    void naturalSizeMeasuresAgainWhenTheFileChanges()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString file = dir.filePath(QStringLiteral("chart.png"));
        QImage first(360, 240, QImage::Format_RGB32);
        first.fill(Qt::darkCyan);
        QVERIFY(first.save(file));

        ImageAssets assets;
        const QString url = ImageAssets::resolveSource(
            QStringLiteral("chart.png"), dir.path(), QString());
        QCOMPARE(assets.naturalSize(url), QSize(360, 240));

        // The same path, a different picture — what a reader who replaces an
        // asset leaves behind.
        QImage second(120, 90, QImage::Format_RGB32);
        second.fill(Qt::magenta);
        QVERIFY(second.save(file));
        // Two files written in the same second can share a timestamp, so the
        // length is what the remembered size is checked against here; moving
        // the timestamp as well covers the case where the length matches too.
        QFile touched(file);
        QVERIFY(touched.open(QIODevice::ReadWrite));
        QVERIFY(touched.setFileTime(QDateTime::currentDateTime().addSecs(5),
                                    QFileDevice::FileModificationTime));
        touched.close();

        QCOMPARE(assets.naturalSize(url), QSize(120, 90));
    }

    void naturalSizeAnswersNothingForWhatItCannotOpen()
    {
        ImageAssets assets;
        // Nothing resolved, a remote picture, and the provider URL a remote
        // picture is actually loaded through: none of them is a file to read,
        // and the delegate falls back to the size the image reports once it
        // has arrived.
        QVERIFY(!assets.naturalSize(QString()).isValid());
        QVERIFY(!assets.naturalSize(
                     QStringLiteral("https://pictures.invalid/a.png")).isValid());
        QVERIFY(!assets.naturalSize(
                     QStringLiteral("image://remote/https%3A%2F%2Fh%2Fa.png"))
                     .isValid());

        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        QFile notAPicture(dir.filePath(QStringLiteral("chart.png")));
        QVERIFY(notAPicture.open(QIODevice::WriteOnly));
        notAPicture.write("this is text, whatever the extension says");
        notAPicture.close();
        const QString url = ImageAssets::resolveSource(
            QStringLiteral("chart.png"), dir.path(), QString());
        QVERIFY(!assets.naturalSize(url).isValid());
        // Remembered as unmeasurable rather than opened again by every
        // binding that asks.
        QVERIFY(!assets.naturalSize(url).isValid());
    }

    void naturalSizeAnswersAScalablePictureItsOwnSize()
    {
        if (!QImageReader::supportedImageFormats().contains("svg"))
            QSKIP("this Qt build has no SVG reader");
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        QFile svg(dir.filePath(QStringLiteral("logo.svg")));
        QVERIFY(svg.open(QIODevice::WriteOnly));
        svg.write("<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"240\" "
                  "height=\"120\"><rect width=\"240\" height=\"120\"/></svg>");
        svg.close();

        ImageAssets assets;
        // The size the file itself gives, not the size something asked for.
        // A scalable picture renders at whatever size it is asked for, so
        // this is the one kind whose drawn width the decode request would
        // otherwise decide.
        QCOMPARE(assets.naturalSize(ImageAssets::resolveSource(
                     QStringLiteral("logo.svg"), dir.path(), QString())),
                 QSize(240, 120));
    }

    void uppercaseSchemeInAnImageExpression()
    {
        const auto p = ImageAssets::parseLine(
            QStringLiteral("![alt](HTTPS://h/pic.png)"));
        QVERIFY(p.valid);
        QCOMPARE(p.kind, Kind::Image);
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        QCOMPARE(ImageAssets::resolveSource(p.path, dir.path(), dir.path()),
                 QStringLiteral("HTTPS://h/pic.png"));
    }
};

QTEST_MAIN(TestImageAssets)
#include "test_imageassets.moc"
