// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
#include <QtTest/QtTest>
#include "documentimporter.h"
#include "notecollection.h"

#include <QTemporaryDir>
#include <QDir>
#include <QFile>
#include <QTextStream>

#include "faultinjection.h"

// Import into the collection: the importable-file test,
// single/batch/folder import with structure preserved, collision suffixing,
// front-matter survival (the Obsidian-vault case), and the dry-run counts.
class TestDocumentImporter : public QObject
{
    Q_OBJECT

private slots:
    void init();
    void cleanup();

    void testIsImportable();
    void testImportSingleFilePreservesContent();
    void testImportBatch();
    void testCollisionSuffixing();
    void testImportFolderPreservesTree();
    void testObsidianFrontMatterSurvives();
    void testDryRunFiles();
    void testDryRunFolder();
    void testNonImportableSkipped();
    void testTraversalAndAbsoluteTargetsAreRejected();
    void testTruncatedWriteIsNotCountedOrLeftBehind();
    void testUnreadableSourceIsNotCountedAsImported();
    void testOversizedSourceIsSkipped();
    void testCancelledImportKeepsWhatItCommitted();
    void testSteppedFolderImportYieldsAndPreservesTheTree();
    void testSteppedFolderImportStopsWhenCancelled();
    void testSkippedCountIsReadableAndAnnouncedFromQml();

private:
    QTemporaryDir *m_root = nullptr;   // the collection
    QTemporaryDir *m_src = nullptr;    // external source files
    NoteCollection *m_collection = nullptr;
    DocumentImporter *m_importer = nullptr;

    QString writeSource(const QString &relPath, const QString &content)
    {
        const QString abs = QDir(m_src->path()).filePath(relPath);
        QDir().mkpath(QFileInfo(abs).absolutePath());
        QFile f(abs);
        f.open(QIODevice::WriteOnly | QIODevice::Text);
        QTextStream(&f) << content;
        f.close();
        return abs;
    }
    QString readNote(const QString &relPath)
    {
        QFile f(m_collection->absolutePath(relPath));
        if (!f.open(QIODevice::ReadOnly | QIODevice::Text))
            return QString();
        return QString::fromUtf8(f.readAll());
    }
};

void TestDocumentImporter::init()
{
    m_root = new QTemporaryDir();
    m_src = new QTemporaryDir();
    m_collection = new NoteCollection(this);
    QVERIFY(m_collection->openRoot(m_root->path()));
    m_importer = new DocumentImporter(this);
    m_importer->setCollection(m_collection);
}

void TestDocumentImporter::cleanup()
{
    delete m_importer; m_importer = nullptr;
    delete m_collection; m_collection = nullptr;
    delete m_root; m_root = nullptr;
    delete m_src; m_src = nullptr;
}

void TestDocumentImporter::testIsImportable()
{
    QVERIFY(DocumentImporter::isImportable("a.md"));
    QVERIFY(DocumentImporter::isImportable("A.MD"));
    QVERIFY(DocumentImporter::isImportable("notes.txt"));
    QVERIFY(DocumentImporter::isImportable("x.markdown"));
    QVERIFY(!DocumentImporter::isImportable("image.png"));
    QVERIFY(!DocumentImporter::isImportable("data.json"));
}

void TestDocumentImporter::testImportSingleFilePreservesContent()
{
    const QString src = writeSource("Note.md", "# Hi\n\nbody\n");
    QCOMPARE(m_importer->importFiles({src}, QString()), 1);
    QCOMPARE(readNote("Note.md"), QString("# Hi\n\nbody\n"));
    // The collection sees it as a native note.
    QVERIFY(m_collection->noteRelPaths().contains("Note.md"));
}

void TestDocumentImporter::testImportBatch()
{
    const QString a = writeSource("A.md", "alpha");
    const QString b = writeSource("B.md", "beta");
    QCOMPARE(m_importer->importFiles({a, b}, QString()), 2);
    QVERIFY(m_collection->noteRelPaths().contains("A.md"));
    QVERIFY(m_collection->noteRelPaths().contains("B.md"));
}

void TestDocumentImporter::testCollisionSuffixing()
{
    m_collection->createNote("", "Dup"); // Dup.md exists
    const QString src = writeSource("Dup.md", "imported body");
    QCOMPARE(m_importer->importFiles({src}, QString()), 1);
    // The import lands beside the existing note as "Dup 2.md".
    QVERIFY(m_collection->noteRelPaths().contains("Dup 2.md"));
    QCOMPARE(readNote("Dup 2.md"), QString("imported body"));
}

void TestDocumentImporter::testImportFolderPreservesTree()
{
    writeSource("top.md", "t");
    writeSource("sub/child.md", "c");
    writeSource("sub/deep/leaf.md", "l");
    writeSource("sub/ignore.png", "x"); // not importable

    QCOMPARE(m_importer->importFolder(m_src->path(), "Vault"), 3);
    QVERIFY(m_collection->noteRelPaths().contains("Vault/top.md"));
    QVERIFY(m_collection->noteRelPaths().contains("Vault/sub/child.md"));
    QVERIFY(m_collection->noteRelPaths().contains("Vault/sub/deep/leaf.md"));
    QCOMPARE(readNote("Vault/sub/deep/leaf.md"), QString("l"));
}

void TestDocumentImporter::testObsidianFrontMatterSurvives()
{
    // A note with Obsidian-style front-matter, including a foreign key, must
    // survive byte-for-byte (the front-matter tolerance rule).
    const QString content =
        "---\ntags: [research]\naliases: [foo, bar]\ncssclass: wide\n---\n"
        "# Vault Note\n\nContent.\n";
    const QString src = writeSource("Vault Note.md", content);
    QCOMPARE(m_importer->importFiles({src}, QString()), 1);
    QCOMPARE(readNote("Vault Note.md"), content);
}

void TestDocumentImporter::testDryRunFiles()
{
    m_collection->createNote("", "Exists");
    const QString a = writeSource("Exists.md", "x"); // collides
    const QString b = writeSource("New.md", "y");
    const QString c = writeSource("pic.png", "z");   // skipped
    const QVariantMap dry = m_importer->dryRunFiles({a, b, c}, QString());
    QCOMPARE(dry.value("files").toInt(), 2);      // md files only
    QCOMPARE(dry.value("collisions").toInt(), 1); // Exists.md
}

void TestDocumentImporter::testDryRunFolder()
{
    writeSource("one.md", "1");
    writeSource("sub/two.md", "2");
    writeSource("sub/three.txt", "3");
    const QVariantMap dry = m_importer->dryRunFolder(m_src->path(), "Into");
    QCOMPARE(dry.value("files").toInt(), 3);
    QCOMPARE(dry.value("folders").toInt(), 1); // "sub"
    QCOMPARE(dry.value("collisions").toInt(), 0);
}

void TestDocumentImporter::testNonImportableSkipped()
{
    const QString png = writeSource("image.png", "notmarkdown");
    QCOMPARE(m_importer->importFiles({png}, QString()), 0);
}

void TestDocumentImporter::testTraversalAndAbsoluteTargetsAreRejected()
{
    const QString src = writeSource("Escape.md", "must stay in the vault");
    const QString outside = m_root->path() + QStringLiteral("-outside");
    const QString outsideName = QFileInfo(outside).fileName();

    QCOMPARE(m_importer->importFiles({src}, QStringLiteral("../") + outsideName), 0);
    QVERIFY(!QFileInfo::exists(outside));

    QCOMPARE(m_importer->importFolder(m_src->path(), outside), 0);
    QVERIFY(!QFileInfo::exists(QDir(outside).filePath("Escape.md")));
}

void TestDocumentImporter::testTruncatedWriteIsNotCountedOrLeftBehind()
{
    // A write that cannot store every byte must not be reported as an
    // import. The size cap makes the copy fail partway — the truncation
    // shape a full disk or a quota produces, without needing either.
    QByteArray payload;
    payload.fill('x', 64 * 1024);
    payload.prepend("# Long note\n\n");
    const QString src = writeSource("Long.md", QString::fromLatin1(payload));

    int imported = 0;
    {
        FaultInjection::FileSizeLimit capped(4096);
        if (!capped.supported())
            QSKIP(qPrintable(capped.skipReason()));
        imported = m_importer->importFiles({src}, QString());
    }

    // A note that could not be written whole is not an import...
    QCOMPARE(imported, 0);
    // ...and leaves no truncated file for the collection to index.
    QVERIFY(!QFile::exists(m_collection->absolutePath("Long.md")));
    QVERIFY(!m_collection->noteRelPaths().contains("Long.md"));
}

// The import reader had the same defect as the collection's: it opened the
// source, called readAll(), and reported success without asking whether the
// read had finished. A source that fails part way through therefore became a
// truncated note counted as a successful import, with nothing left to say
// what had been lost.
void TestDocumentImporter::testUnreadableSourceIsNotCountedAsImported()
{
#ifndef Q_OS_LINUX
    QSKIP("no file on this platform that opens and then fails to read");
#else
    // /proc/self/mem opens for this process and returns EIO on read, which is
    // what a failing disk or a dropped network mount looks like.
    const QString unreadable = QStringLiteral("/proc/self/mem");
    if (!QFileInfo::exists(unreadable))
        QSKIP("no file on this platform that opens and then fails to read");
    const QString src = QDir(m_src->path()).filePath(QStringLiteral("Broken.md"));
    if (!QFile::link(unreadable, src))
        QSKIP("real symbolic links require elevation on this platform");

    QCOMPARE(m_importer->importFiles({src}, QString()), 0);
    QVERIFY(!QFile::exists(m_collection->absolutePath("Broken.md")));
    QCOMPARE(m_importer->lastSkippedCount(), 1);
#endif
}

// A note is prose. Reading whatever the file dialog was pointed at into
// memory turns one wrong selection — a disk image, a log — into a freeze or
// an out-of-memory failure, so anything past the cap is skipped and counted.
void TestDocumentImporter::testOversizedSourceIsSkipped()
{
    const QString withinCap = writeSource("Small.md", QStringLiteral("# small\n"));
    const QString large =
        writeSource("Large.md", QString(64 * 1024, QLatin1Char('x')));

    const qint64 previous = DocumentImporter::maxFileBytes();
    DocumentImporter::setMaxFileBytes(1024);
    const int imported = m_importer->importFiles({withinCap, large}, QString());
    const int skipped = m_importer->lastSkippedCount();
    DocumentImporter::setMaxFileBytes(previous);

    QCOMPARE(imported, 1);
    QCOMPARE(skipped, 1);
    QVERIFY(QFile::exists(m_collection->absolutePath("Small.md")));
    QVERIFY(!QFile::exists(m_collection->absolutePath("Large.md")));
}

// Import runs until it is done, however many files were selected. A caller
// driving it from a worker thread needs a way to stop it, and what it has
// already committed stays committed: each file is one atomic copy.
void TestDocumentImporter::testCancelledImportKeepsWhatItCommitted()
{
    QStringList sources;
    for (int i = 0; i < 6; ++i) {
        sources.append(writeSource(QStringLiteral("N%1.md").arg(i),
                                   QStringLiteral("# note %1\n").arg(i)));
    }

    // Cancelled after the second file lands, from the progress report the
    // asynchronous caller will be listening to anyway.
    int seen = 0;
    connect(m_importer, &DocumentImporter::importProgress, this,
            [&](int done, int total) {
                QCOMPARE(total, 6);
                seen = done;
                if (done == 2)
                    m_importer->requestCancel();
            });

    const int imported = m_importer->importFiles(sources, QString());
    QCOMPARE(seen, 2);
    QCOMPARE(imported, 2);
    QVERIFY(QFile::exists(m_collection->absolutePath("N0.md")));
    QVERIFY(QFile::exists(m_collection->absolutePath("N1.md")));
    QVERIFY(!QFile::exists(m_collection->absolutePath("N2.md")));

    // A second run is not affected by the cancel that stopped the first.
    disconnect(m_importer, &DocumentImporter::importProgress, this, nullptr);
    QCOMPARE(m_importer->importFiles({sources.at(2)}, QString()), 1);
}

// Folder import copied every file before returning, so nothing else ran while
// it did: the progress label could not repaint and the cancel button could not
// be pressed, on exactly the imports big enough to need them. The stepped
// import returns at once and copies between event-loop turns instead, with
// the subfolder tree mirrored exactly as the blocking one does.
void TestDocumentImporter::testSteppedFolderImportYieldsAndPreservesTheTree()
{
    writeSource("top.md", QStringLiteral("# top\n"));
    writeSource("sub/one.md", QStringLiteral("# one\n"));
    writeSource("sub/deeper/two.md", QStringLiteral("# two\n"));
    writeSource("sub/notes.txt", QStringLiteral("plain text\n"));
    writeSource("ignored.png", QStringLiteral("not importable\n"));

    // What the dialog sizes its progress bar with.
    QCOMPARE(m_importer->listImportableFiles(m_src->path()).size(), 4);

    QSignalSpy progress(m_importer, &DocumentImporter::importProgress);
    QSignalSpy finished(m_importer, &DocumentImporter::importFinished);

    m_importer->startImportFolder(m_src->path(), QStringLiteral("In"));
    // The call returned before doing any copying: the event loop has not run
    // yet, so nothing can have been imported.
    QVERIFY(m_importer->importInProgress());
    QCOMPARE(progress.count(), 0);
    QVERIFY(!QFile::exists(m_collection->absolutePath("In/top.md")));

    QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 1, 15000);
    QVERIFY(!m_importer->importInProgress());
    QCOMPARE(finished.first().at(0).toInt(), 4);   // imported
    QCOMPARE(finished.first().at(1).toInt(), 0);   // skipped
    QCOMPARE(finished.first().at(2).toBool(), false); // cancelled

    // The progress reports carry the whole folder's totals, not a chunk's.
    QCOMPARE(progress.count(), 4);
    QCOMPARE(progress.last().at(0).toInt(), 4);
    QCOMPARE(progress.last().at(1).toInt(), 4);

    // Same destinations as the blocking import: the subfolder tree is
    // recreated under the target folder.
    QCOMPARE(readNote("In/top.md"), QStringLiteral("# top\n"));
    QCOMPARE(readNote("In/sub/one.md"), QStringLiteral("# one\n"));
    QCOMPARE(readNote("In/sub/deeper/two.md"), QStringLiteral("# two\n"));
    QVERIFY(QFile::exists(m_collection->absolutePath("In/sub/notes.md")));
    // The index was refreshed once, at the end.
    QVERIFY(m_collection->noteRelPaths().contains("In/sub/deeper/two.md"));
}

void TestDocumentImporter::testSteppedFolderImportStopsWhenCancelled()
{
    for (int i = 0; i < 40; ++i) {
        writeSource(QStringLiteral("N%1.md").arg(i, 2, 10, QLatin1Char('0')),
                    QStringLiteral("# note %1\n").arg(i));
    }

    QSignalSpy finished(m_importer, &DocumentImporter::importFinished);
    int cancelledAfter = 0;
    connect(m_importer, &DocumentImporter::importProgress, this,
            [&](int done, int total) {
                QCOMPARE(total, 40);
                if (done == 1 && cancelledAfter == 0) {
                    cancelledAfter = done;
                    m_importer->requestCancel();
                }
            });

    m_importer->startImportFolder(m_src->path(), QString());
    QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 1, 15000);

    QCOMPARE(cancelledAfter, 1);
    QCOMPARE(finished.first().at(2).toBool(), true);
    const int imported = finished.first().at(0).toInt();
    // Whatever it had already committed stays; the rest was never started.
    QVERIFY2(imported >= 1 && imported < 40,
             qPrintable(QStringLiteral("a cancelled import copied %1 of 40 files")
                            .arg(imported)));
    QCOMPARE(m_collection->noteRelPaths().size(), imported);

    // The next run is not affected by the cancel that stopped this one.
    disconnect(m_importer, &DocumentImporter::importProgress, this, nullptr);
    QSignalSpy second(m_importer, &DocumentImporter::importFinished);
    m_importer->startImportFolder(m_src->path(), QStringLiteral("Again"));
    QTRY_COMPARE_WITH_TIMEOUT(second.count(), 1, 15000);
    QCOMPARE(second.first().at(0).toInt(), 40);
    QCOMPARE(second.first().at(2).toBool(), false);
}

// The dialog reports how many files were declined. QML can only read a
// property or an invokable, and the count climbs as the run proceeds, so it
// is a property with a change signal rather than a plain method.
void TestDocumentImporter::testSkippedCountIsReadableAndAnnouncedFromQml()
{
    writeSource("Small.md", QStringLiteral("# small\n"));
    writeSource("Large.md", QString(64 * 1024, QLatin1Char('x')));

    QSignalSpy skippedChanged(m_importer,
                              &DocumentImporter::lastSkippedCountChanged);
    QSignalSpy finished(m_importer, &DocumentImporter::importFinished);

    const qint64 previous = DocumentImporter::maxFileBytes();
    DocumentImporter::setMaxFileBytes(1024);
    m_importer->startImportFolder(m_src->path(), QString());
    QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 1, 15000);
    DocumentImporter::setMaxFileBytes(previous);

    QCOMPARE(finished.first().at(0).toInt(), 1);   // imported
    QCOMPARE(finished.first().at(1).toInt(), 1);   // skipped
    // Readable through the metaobject, which is what QML does.
    QCOMPARE(m_importer->property("lastSkippedCount").toInt(), 1);
    QVERIFY2(skippedChanged.count() >= 1,
             "the skipped count changed without announcing it");
    // And it is announced during the run rather than only at the end.
    QVERIFY(m_importer->property("importInProgress").toBool() == false);
}

QTEST_MAIN(TestDocumentImporter)
#include "test_documentimporter.moc"
