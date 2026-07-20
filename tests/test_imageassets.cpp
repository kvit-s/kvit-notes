// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
#include <QtTest>
#include <QTemporaryDir>
#include <QUrl>
#include <QImage>
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
            QFile f(p); f.open(QIODevice::WriteOnly); f.write("x"); f.close();
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

    // ---- Asset ingestion ----

    void uniqueNameCollisionSuffix()
    {
        QTemporaryDir dir;
        const QString d = dir.path();
        const QString n1 = ImageAssets::uniqueAssetName(d, "note", "20260708-120000", "png");
        QCOMPARE(n1, QStringLiteral("note-20260708-120000.png"));
        // Create it, then the next name gets a -1 suffix.
        QFile f(QDir(d).filePath(n1)); f.open(QIODevice::WriteOnly); f.close();
        const QString n2 = ImageAssets::uniqueAssetName(d, "note", "20260708-120000", "png");
        QCOMPARE(n2, QStringLiteral("note-20260708-120000-1.png"));
    }

    void ingestImageWritesToAssetsAndReturnsRootRelative()
    {
        QTemporaryDir dir;
        const QString root = dir.path();
        QDir(root).mkpath("folder");
        const QString noteDir = root + "/folder";
        ImageAssets ia;

        QImage img(10, 10, QImage::Format_ARGB32);
        img.fill(Qt::red);
        const QString stored = ia.ingestImage(img, "my-note", root, noteDir);
        // Root-relative path so a note move never breaks it.
        QVERIFY2(stored.startsWith("assets/"), qPrintable(stored));
        QVERIFY(stored.endsWith(".png"));
        QVERIFY(stored.contains("my-note-"));
        // The file exists under the root's assets dir, and resolves back.
        QVERIFY(QFileInfo(QDir(root).filePath(stored)).exists());
        QCOMPARE(ImageAssets::resolveSource(stored, noteDir, root),
                 QUrl::fromLocalFile(QDir(root).filePath(stored)).toString());
    }

    void ingestFileLinksInPlaceUnderRoot()
    {
        QTemporaryDir dir;
        const QString root = dir.path();
        QDir(root).mkpath("pics");
        QDir(root).mkpath("folder");
        const QString src = root + "/pics/cat.jpg";
        { QFile f(src); f.open(QIODevice::WriteOnly); f.write("x"); f.close(); }
        ImageAssets ia;

        // A file already under the root is linked in place (root-relative,
        // no copy into assets/).
        const QString stored = ia.ingestFile(src, "note", root, root + "/folder");
        QCOMPARE(stored, QStringLiteral("pics/cat.jpg"));
        QVERIFY(!QDir(root).exists("assets"));   // nothing copied
    }

    void ingestFileCopiesOutsideRoot()
    {
        QTemporaryDir extern_;
        QTemporaryDir dir;
        const QString root = dir.path();
        const QString src = extern_.path() + "/outside.png";
        { QFile f(src); f.open(QIODevice::WriteOnly); f.write("x"); f.close(); }
        ImageAssets ia;

        const QString stored = ia.ingestFile(src, "note", root, root);
        QVERIFY2(stored.startsWith("assets/"), qPrintable(stored));
        QVERIFY(stored.endsWith(".png"));
        QVERIFY(QFileInfo(QDir(root).filePath(stored)).exists());
    }
};

QTEST_MAIN(TestImageAssets)
#include "test_imageassets.moc"
