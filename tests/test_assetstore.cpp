// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
#include <QtTest>
#include <QTemporaryDir>
#include <QUrl>
#include <QImage>
#include <QFile>
#include <QDir>

#include "assetstore.h"
#include "imageassets.h"

// Bringing a pasted, dropped or chosen file into the vault's assets
// directory. The markdown expression these produce is ImageAssets' half and
// is covered by ImageAssetsTests; what is checked here is where the bytes
// land and what the note ends up storing as the path.
class TestAssetStore : public QObject
{
    Q_OBJECT

private slots:
    void uniqueNameCollisionSuffix()
    {
        QTemporaryDir dir;
        const QString d = dir.path();
        const QString n1 = AssetStore::uniqueAssetName(d, "note", "20260708-120000", "png");
        QCOMPARE(n1, QStringLiteral("note-20260708-120000.png"));
        // Create it, then the next name gets a -1 suffix.
        QFile f(QDir(d).filePath(n1)); f.open(QIODevice::WriteOnly); f.close();
        const QString n2 = AssetStore::uniqueAssetName(d, "note", "20260708-120000", "png");
        QCOMPARE(n2, QStringLiteral("note-20260708-120000-1.png"));
    }

    void ingestImageWritesToAssetsAndReturnsRootRelative()
    {
        QTemporaryDir dir;
        const QString root = dir.path();
        QDir(root).mkpath("folder");
        const QString noteDir = root + "/folder";
        AssetStore store;

        QImage img(10, 10, QImage::Format_ARGB32);
        img.fill(Qt::red);
        const QString stored = store.ingestImage(img, "my-note", root, noteDir);
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
        AssetStore store;

        // A file already under the root is linked in place (root-relative,
        // no copy into assets/).
        const QString stored = store.ingestFile(src, "note", root, root + "/folder");
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
        AssetStore store;

        const QString stored = store.ingestFile(src, "note", root, root);
        QVERIFY2(stored.startsWith("assets/"), qPrintable(stored));
        QVERIFY(stored.endsWith(".png"));
        QVERIFY(QFileInfo(QDir(root).filePath(stored)).exists());
    }

    // The drop handler in main.qml strips the file:// scheme with a string
    // replace and hands the remainder to ingestLocalFile. QML renders a QUrl
    // with QUrl::toString(), which leaves a space literal but keeps the
    // characters that are URL delimiters percent-encoded — '#' becomes %23,
    // '%' becomes %25. Those survive the hand-strip and name a file that does
    // not exist, so the drop is silently ignored. Passing the URL through
    // intact ingests the file the user actually dropped.
    void ingestLocalFilePercentDecodesUrls()
    {
        QTemporaryDir extern_;
        QTemporaryDir dir;
        const QString root = dir.path();
        const QString src = extern_.path() + "/photo #2.png";
        { QFile f(src); f.open(QIODevice::WriteOnly); f.write("x"); f.close(); }
        QVERIFY(QFileInfo::exists(src));
        AssetStore store;

        const QUrl url = QUrl::fromLocalFile(src);
        const QString asQmlSeesIt = url.toString();
        QVERIFY2(asQmlSeesIt.contains(QLatin1String("%23")),
                 qPrintable(asQmlSeesIt));

        // A whole file:// URL ingests: ingestLocalFile decodes it via
        // QUrl::toLocalFile().
        const QString stored = store.ingestLocalFile(asQmlSeesIt, "note",
                                                  root, root);
        QVERIFY2(!stored.isEmpty(), "a dropped file:// URL must ingest");
        QVERIFY(QFileInfo(QDir(root).filePath(stored)).exists());

        // The scheme-stripped form the QML builds names no real file, which
        // is exactly why the QML must not construct it.
        const QString handStripped =
            asQmlSeesIt.mid(QStringLiteral("file://").size());
        QVERIFY(handStripped.contains(QLatin1String("%23")));
        QVERIFY2(!QFileInfo::exists(handStripped),
                 "the percent-encoded path must not resolve");
        QVERIFY2(store.ingestLocalFile(handStripped, "note", root, root).isEmpty(),
                 "ingesting the hand-stripped path must fail, which is the "
                 "silently-dropped file the user sees");
    }

    // A slug is a note title the caller passed through and an extension comes
    // off a dropped file's name. Neither may steer the write out of the
    // assets directory.
    void assetNameCannotEscapeTheAssetsDirectory()
    {
        QTemporaryDir dir;
        const QString d = dir.path();
        const QString name = AssetStore::uniqueAssetName(
            d, QStringLiteral("../../etc/passwd"),
            QStringLiteral("20260720-101500"), QStringLiteral("../png"));
        QVERIFY2(!name.contains(QLatin1Char('/')), qPrintable(name));
        QVERIFY2(!name.contains(QLatin1String("..")), qPrintable(name));
        QCOMPARE(QFileInfo(QDir(d).filePath(name)).absolutePath(),
                 QFileInfo(d).absoluteFilePath());
    }

    void ingestKeepsATraversalSlugInsideAssets()
    {
        QTemporaryDir dir;
        const QString root = dir.path();
        AssetStore store;
        QImage img(4, 4, QImage::Format_ARGB32);
        img.fill(Qt::blue);

        const QString stored = store.ingestImage(img, QStringLiteral("../../pwn"),
                                                 root, root);
        QVERIFY2(stored.startsWith(QStringLiteral("assets/")), qPrintable(stored));
        QVERIFY2(!stored.contains(QLatin1String("..")), qPrintable(stored));
        QVERIFY(QFileInfo(QDir(root).filePath(stored)).exists());
    }
};

QTEST_MAIN(TestAssetStore)
#include "test_assetstore.moc"
