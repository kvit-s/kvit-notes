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
    void testTruncatedWriteIsNotCountedOrLeftBehind();

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

QTEST_MAIN(TestDocumentImporter)
#include "test_documentimporter.moc"
