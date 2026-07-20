// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
#include <QtTest/QtTest>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QFile>
#include <QDir>

#include "notecollection.h"
#include "timingbudget.h"

#include "faultinjection.h"

// Unit suite for the collection object, over real temporary
// directories. The contracts under test: the scan reads and
// never writes; every operation is reflected on disk and in the index;
// destructive operations land in .kvit/trash; metadata rewrites preserve
// body bytes and the file's modification time; collection.json state
// survives a close/reopen; revision bumps exactly on observable changes.
class TestNoteCollection : public QObject
{
    Q_OBJECT

private slots:
    void init();
    void cleanup();

    // Scan and index
    void testScanFindsNestedStructure();
    void testScanSkipsKvitAndDotEntries();
    void testScanNeverWrites();

    // Vault containment: nothing outside the selected root is reachable,
    // by scan or by mutation.
    void testScanExcludesSymlinkedNoteOutsideRoot();
    void testScanExcludesSymlinkedDirectoryOutsideRoot();
    void testScanTerminatesOnSymlinkCycle();
    void testRefreshPathsExcludesSymlinkedNote();
    void testMutationsRejectPathsEscapingRoot();
    void testCanonicalContainmentRejectsLinksOutOfTheRoot();
    void testDeleteCannotReachOutsideThroughSymlink();
    void testIndexFields();
    void testPersistentIndexWarmStartSkipsUnchangedParse();
    void testCorruptPersistentIndexFallsBackToFullParse();
    void testRefreshPathsReindexesOnlyChangedNote();
    void testRefreshPathsDirectoryScopesToSubtree();
    void testRefreshPathsDeletedDirectoryRemovesSubtree();
    void testAsyncOpenRootReturnsBeforeBodyParse();
    void testCreatedPrefersFrontMatter();
    void testInitializeIfEmpty();

    // Note operations
    void testCreateNote();
    void testCreateNoteUniqueUntitled();
    void testCreateNoteCollision();
    void testCreateNoteInvalidNames();
    void testRenameNote();
    void testRenameNoteCollision();
    void testMoveNote();
    void testMoveNoteCollision();
    void testDeleteNoteGoesToTrash();

    // Folder operations
    void testCreateFolder();
    void testRenameFolderRebindsContents();
    void testDeleteFolderRecursive();
    void testFolderStatePersists();

    // Metadata
    void testSetMetadataWritesFrontMatterLazily();
    void testSetGoalRoundTrips();
    void testMetadataRewritePreservesBodyAndMtime();
    void testClearingMetadataRemovesBlock();
    void testForeignKeysSurviveMetadataWrite();
    void testFrontMatterFor();

    // Tags
    void testTagRegistry();
    void testRenameTagRewritesAffectedNotesOnly();
    void testMergeTagUnions();
    void testDeleteTag();
    void testTagColorsPersist();
    void testTagAutoColorAssignment();
    void testTagListing();

    // Manual order
    void testManualOrderReconciliation();
    void testSetManualPositionPersists();

    // Workspace state
    void testLastOpenNotePersists();
    void testCollectionFileCorruptionTolerated();

    // Backups
    void testBackupRotationFloorAndPrune();
    void testBackupsListingAndBody();

    // Crash recovery
    void testRecoveryLifecycle();
    void testRecoveryRecreatesDeletedFolder();
    void testFailedIndexWriteKeepsIndexDirty();
    void testFailedCollectionWriteIsReported();
    void testRefreshDoesNotIngestLiveJournals();

    // Wiki-links
    void testExtractWikiLinks();
    void testResolveWikiTarget();
    void testBacklinks();
    void testWikiLinksReindexOnRefresh();
    void testRenameRewritesReferringLinks();
    void testRenamePlanCancelAndConflictSkip();
    void testFolderRenameRewritesQualifiedLinksOnly();
    void testMoveKeepsBareLinksUntouched();
    void testRewriteWikiTargetsInTextSurgical();

    // Seams
    void testNoteSavedRefreshesEntry();
    void testNoteSavedCanUseProvidedText();
    void testRevisionContract();

    // The adopted performance target — opening a 500-note collection
    // in under 1 second, measured.
    void testBenchmark500NoteOpen();

private:
    QString abs(const QString &relPath) const;
    void writeNote(const QString &relPath, const QString &content);
    QString readNote(const QString &relPath) const;
    void makeFixture();

    QTemporaryDir *m_dir = nullptr;
    NoteCollection *m_collection = nullptr;
};

void TestNoteCollection::init()
{
    m_dir = new QTemporaryDir();
    QVERIFY(m_dir->isValid());
    m_collection = new NoteCollection();
}

void TestNoteCollection::cleanup()
{
    delete m_collection;
    delete m_dir;
    m_collection = nullptr;
    m_dir = nullptr;
}

QString TestNoteCollection::abs(const QString &relPath) const
{
    return m_dir->filePath(relPath);
}

void TestNoteCollection::writeNote(const QString &relPath, const QString &content)
{
    QFileInfo info(abs(relPath));
    QVERIFY(QDir().mkpath(info.absolutePath()));
    QFile file(abs(relPath));
    QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Text));
    file.write(content.toUtf8());
}

QString TestNoteCollection::readNote(const QString &relPath) const
{
    QFile file(abs(relPath));
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        return QString();
    return QString::fromUtf8(file.readAll());
}

// Root note, one folder with two notes, one nested folder with one note.
void TestNoteCollection::makeFixture()
{
    writeNote("Welcome.md", "# Welcome\n\nRoot note\n");
    writeNote("Ideas/Reading.md",
              "---\ntags: [books]\n---\nA **bold** reading list\n");
    writeNote("Ideas/Plans.md", "Some plans\n");
    writeNote("Ideas/Projects/Kvit.md",
              "---\ntags: [work, kvit]\npinned: true\n---\nThe editor\n");
    QVERIFY(m_collection->openRoot(m_dir->path()));
}

// ------------------------------------------------------ scan and index

void TestNoteCollection::testScanFindsNestedStructure()
{
    makeFixture();

    QCOMPARE(m_collection->noteRelPaths(),
             (QStringList() << "Ideas/Plans.md" << "Ideas/Projects/Kvit.md"
                            << "Ideas/Reading.md" << "Welcome.md"));
    QCOMPARE(m_collection->folderRelPaths(),
             (QStringList() << "Ideas" << "Ideas/Projects"));
    QCOMPARE(m_collection->noteCount(), 4);
    QCOMPARE(m_collection->noteCountInFolder("Ideas", false), 2);
    QCOMPARE(m_collection->noteCountInFolder("Ideas", true), 3);
    QCOMPARE(m_collection->noteCountInFolder(QString(), true), 4);
}

void TestNoteCollection::testScanSkipsKvitAndDotEntries()
{
    writeNote("Note.md", "content\n");
    writeNote(".kvit/trash/old.md", "trashed\n");
    writeNote(".hidden/secret.md", "hidden\n");
    writeNote("readme.txt", "not markdown\n");
    QVERIFY(m_collection->openRoot(m_dir->path()));

    QCOMPARE(m_collection->noteRelPaths(), QStringList{QStringLiteral("Note.md")});
    QCOMPARE(m_collection->folderRelPaths(), QStringList());
}

void TestNoteCollection::testScanNeverWrites()
{
    const QString pristine =
        QStringLiteral("---\nweird: value\n---\nBody without our keys\n");
    writeNote("Pristine.md", pristine);
    writeNote("Plain.md", "No metadata at all\n");

    QVERIFY(m_collection->openRoot(m_dir->path()));

    QCOMPARE(readNote("Pristine.md"), pristine);
    QCOMPARE(readNote("Plain.md"), QStringLiteral("No metadata at all\n"));
    // Opening may write only the performance index sidecar; user collection
    // state and note files remain untouched until state changes.
    QVERIFY(QFileInfo::exists(abs(".kvit/index.json")));
    QVERIFY(!QFileInfo::exists(abs(".kvit/collection.json")));
}

// A vault is the subtree the user selected, and nothing else. A symlink is
// the one filesystem construct that lets a relative path inside the root
// name a file outside it, so the scan must not follow one and no mutation
// may resolve through one. These tests need real symbolic links: Windows
// grants that privilege only to elevated processes, and QFile::link there
// writes a .lnk shortcut instead, so they are skipped on that platform and
// the containment code is exercised there through the escaping-path test.
#ifdef Q_OS_WIN
#  define SKIP_WITHOUT_SYMLINKS() \
      QSKIP("real symbolic links require elevation on Windows")
#else
#  define SKIP_WITHOUT_SYMLINKS() do {} while (false)
#endif

void TestNoteCollection::testScanExcludesSymlinkedNoteOutsideRoot()
{
    SKIP_WITHOUT_SYMLINKS();
    QTemporaryDir outside;
    QVERIFY(outside.isValid());
    QFile secret(outside.filePath("Secret.md"));
    QVERIFY(secret.open(QIODevice::WriteOnly));
    secret.write("private notes that live outside the vault\n");
    secret.close();

    writeNote("Welcome.md", "root note\n");
    QVERIFY(QFile::link(outside.filePath("Secret.md"), abs("Alias.md")));

    QVERIFY(m_collection->openRoot(m_dir->path()));

    // The alias must not appear as a note under any name.
    QVERIFY(!m_collection->note("Alias.md"));
    QCOMPARE(m_collection->noteRelPaths(), QStringList{"Welcome.md"});
}

void TestNoteCollection::testScanExcludesSymlinkedDirectoryOutsideRoot()
{
    SKIP_WITHOUT_SYMLINKS();
    QTemporaryDir outside;
    QVERIFY(outside.isValid());
    QVERIFY(QDir(outside.path()).mkpath("Private"));
    QFile secret(outside.filePath("Private/Secret.md"));
    QVERIFY(secret.open(QIODevice::WriteOnly));
    secret.write("outside\n");
    secret.close();

    writeNote("Welcome.md", "root note\n");
    QVERIFY(QFile::link(outside.filePath("Private"), abs("Linked")));

    QVERIFY(m_collection->openRoot(m_dir->path()));

    QVERIFY(!m_collection->folderRelPaths().contains("Linked"));
    QVERIFY(!m_collection->note("Linked/Secret.md"));
    QCOMPARE(m_collection->noteRelPaths(), QStringList{"Welcome.md"});
}

void TestNoteCollection::testScanTerminatesOnSymlinkCycle()
{
    SKIP_WITHOUT_SYMLINKS();
    // A link back to an ancestor: following it re-enters the same directories
    // forever, so the scan must either not follow it or notice the revisit.
    writeNote("Welcome.md", "root note\n");
    writeNote("Sub/Nested.md", "nested\n");
    QVERIFY(QFile::link(m_dir->path(), abs("Sub/Loop")));

    QElapsedTimer timer;
    timer.start();
    QVERIFY(m_collection->openRoot(m_dir->path()));
    const qint64 elapsed = timer.elapsed();

    QCOMPARE(m_collection->noteRelPaths(),
             QStringList({"Sub/Nested.md", "Welcome.md"}));
    QVERIFY2(elapsed < 5000,
             qPrintable(QStringLiteral("scan took %1 ms").arg(elapsed)));
}

void TestNoteCollection::testRefreshPathsExcludesSymlinkedNote()
{
    SKIP_WITHOUT_SYMLINKS();
    // The incremental walker must apply the same rule as the full scan;
    // otherwise a file-watcher refresh reintroduces what the scan excluded.
    QTemporaryDir outside;
    QVERIFY(outside.isValid());
    QVERIFY(QDir(outside.path()).mkpath("Private"));
    QFile secret(outside.filePath("Private/Secret.md"));
    QVERIFY(secret.open(QIODevice::WriteOnly));
    secret.write("outside\n");
    secret.close();

    writeNote("Welcome.md", "root note\n");
    QVERIFY(m_collection->openRoot(m_dir->path()));
    QVERIFY(QFile::link(outside.filePath("Private"), abs("Linked")));

    m_collection->refreshPaths({m_dir->path()});
    QTRY_VERIFY_WITH_TIMEOUT(!m_collection->scanInProgress(), 5000);

    QVERIFY(!m_collection->note("Linked/Secret.md"));
    QVERIFY(!m_collection->folderRelPaths().contains("Linked"));
}

// Canonical containment, exercised directly.
//
// A mutation audit found that the other containment tests in this file pass
// with the containment machinery disabled: the scan excludes symlinks through
// QDir::NoSymLinks, and the mutating entry points refuse an escaping relative
// path at name validation or at note/folder lookup, both of which run before
// ensureWithinRoot is consulted. Those tests pin the outcome, which is worth
// having, but nothing reached isWithinCanonicalRoot.
//
// relativePath() is the one public surface where containment is the only
// thing deciding, so it is where the check can actually be verified. The
// discriminating case is the second one: a path that is textually under the
// root and resolves outside it. A prefix comparison accepts that path; only
// canonicalizing before comparing rejects it.
void TestNoteCollection::testCanonicalContainmentRejectsLinksOutOfTheRoot()
{
    makeFixture();

    QTemporaryDir outside;
    QVERIFY(outside.isValid());
    QFile victim(outside.filePath(QStringLiteral("Victim.md")));
    QVERIFY(victim.open(QIODevice::WriteOnly));
    victim.write("someone else's file\n");
    victim.close();

    // A note genuinely inside the root resolves, so the cases below fail for
    // containment reasons rather than because everything returns empty.
    QCOMPARE(m_collection->relativePath(
                 QDir(m_dir->path()).filePath(QStringLiteral("Welcome.md"))),
             QStringLiteral("Welcome.md"));

    // Plainly outside.
    QVERIFY2(m_collection->relativePath(outside.filePath(QStringLiteral("Victim.md")))
                 .isEmpty(),
             "a path outside the root was accepted as a note");

    // Textually inside, actually outside. This is the case the canonical
    // comparison exists for.
    const QString link = QDir(m_dir->path()).filePath(QStringLiteral("Link.md"));
    if (!QFile::link(outside.filePath(QStringLiteral("Victim.md")), link))
        QSKIP("this filesystem does not support symbolic links");
    QVERIFY2(m_collection->relativePath(link).isEmpty(),
             "a symlink under the root that resolves outside it was accepted "
             "as a note, so containment is comparing text rather than "
             "canonical paths");

    // Same for a directory link, which is how a whole tree gets pulled in.
    const QString dirLink = QDir(m_dir->path()).filePath(QStringLiteral("Linked"));
    QVERIFY(QFile::link(outside.path(), dirLink));
    QVERIFY2(m_collection->relativePath(
                 QDir(dirLink).filePath(QStringLiteral("Victim.md"))).isEmpty(),
             "a path through a directory symlink escaped the root");
}

void TestNoteCollection::testMutationsRejectPathsEscapingRoot()
{
    // Relative paths are the collection's whole addressing scheme, so one
    // that climbs out of the root must be refused rather than resolved.
    QTemporaryDir outside;
    QVERIFY(outside.isValid());
    QFile victim(outside.filePath("Victim.md"));
    QVERIFY(victim.open(QIODevice::WriteOnly));
    victim.write("someone else's file\n");
    victim.close();

    makeFixture();
    const QString escaping =
        QDir(m_dir->path()).relativeFilePath(outside.filePath("Victim.md"));
    QVERIFY(escaping.startsWith(QLatin1String("..")));

    QSignalSpy failSpy(m_collection, &NoteCollection::operationFailed);

    QVERIFY(!m_collection->deleteNote(escaping));
    QVERIFY(!m_collection->renameNote(escaping, "Renamed"));
    QVERIFY(!m_collection->moveNote(escaping, QString()));
    QVERIFY(!m_collection->setTags(escaping, {"tag"}));
    QVERIFY(!m_collection->setPinned(escaping, true));
    QVERIFY(m_collection->createNote(QStringLiteral(".."), "Sneak").isEmpty());
    QVERIFY(m_collection->createFolder(QStringLiteral(".."), "Sneak").isEmpty());
    QVERIFY(!m_collection->deleteFolder(QStringLiteral("..")));
    QVERIFY(failSpy.count() > 0);

    // The outside file is untouched, and nothing was created beside it.
    QVERIFY(QFileInfo::exists(outside.filePath("Victim.md")));
    QCOMPARE(QDir(outside.path()).entryList(QDir::Files | QDir::Dirs
                                            | QDir::NoDotAndDotDot),
             QStringList{"Victim.md"});
}

void TestNoteCollection::testDeleteCannotReachOutsideThroughSymlink()
{
    SKIP_WITHOUT_SYMLINKS();
    // The damaging consequence of following a link: once the alias is in the
    // index it is an ordinary note to every operation, so a delete the user
    // believes is scoped to their vault moves someone else's file to the
    // vault's trash.
    QTemporaryDir outside;
    QVERIFY(outside.isValid());
    QVERIFY(QDir(outside.path()).mkpath("Private"));
    QFile secret(outside.filePath("Private/Secret.md"));
    QVERIFY(secret.open(QIODevice::WriteOnly));
    secret.write("someone else's file\n");
    secret.close();

    writeNote("Welcome.md", "root note\n");
    QVERIFY(QFile::link(outside.filePath("Private"), abs("Linked")));
    QVERIFY(m_collection->openRoot(m_dir->path()));

    const bool deleted = m_collection->deleteNote("Linked/Secret.md");
    QVERIFY2(QFileInfo::exists(outside.filePath("Private/Secret.md")),
             "a delete inside the vault removed a file outside it");
    QVERIFY(!deleted);
}

void TestNoteCollection::testIndexFields()
{
    makeFixture();

    const NoteCollection::NoteEntry *entry =
        m_collection->note("Ideas/Reading.md");
    QVERIFY(entry);
    QCOMPARE(entry->title, QStringLiteral("Reading"));
    QCOMPARE(entry->folder, QStringLiteral("Ideas"));
    QCOMPARE(entry->meta.tags, QStringList{QStringLiteral("books")});
    QCOMPARE(entry->meta.pinned, false);
    // Snippet and word count come from display text: markers stripped.
    QCOMPARE(entry->snippet, QStringLiteral("A bold reading list"));
    QCOMPARE(entry->wordCount, 4);
    QVERIFY(entry->modified.isValid());
    QVERIFY(entry->created.isValid());
    // Bodies are no longer resident; noteInfo reads the saved
    // body on demand.
    QCOMPARE(m_collection->noteInfo("Ideas/Reading.md").value("body").toString(),
             QStringLiteral("A **bold** reading list\n"));

    const NoteCollection::NoteEntry *pinned =
        m_collection->note("Ideas/Projects/Kvit.md");
    QVERIFY(pinned);
    QCOMPARE(pinned->meta.pinned, true);
    QCOMPARE(pinned->meta.tags,
             (QStringList() << QStringLiteral("work") << QStringLiteral("kvit")));
}

void TestNoteCollection::testPersistentIndexWarmStartSkipsUnchangedParse()
{
    writeNote("A.md", "one two\n");
    writeNote("Folder/B.md", "---\ntags: [cache]\n---\nthree **four** five\n");

    int firstParseCount = 0;
    NoteCollection first;
    first.setIndexParseObserverForTesting(
        [&firstParseCount](const QString &) { ++firstParseCount; });
    QVERIFY(first.openRoot(m_dir->path()));
    QCOMPARE(firstParseCount, 2);
    QVERIFY(QFileInfo::exists(abs(".kvit/index.json")));

    int warmParseCount = 0;
    NoteCollection warm;
    warm.setIndexParseObserverForTesting(
        [&warmParseCount](const QString &) { ++warmParseCount; });
    QVERIFY(warm.openRoot(m_dir->path()));
    QCOMPARE(warmParseCount, 0);
    QCOMPARE(warm.noteCount(), 2);
    QCOMPARE(warm.note("Folder/B.md")->wordCount, 3);
    QCOMPARE(warm.note("Folder/B.md")->meta.tags,
             QStringList{QStringLiteral("cache")});

    writeNote("Folder/B.md", "---\ntags: [cache]\n---\nchanged body with six words\n");
    QStringList parsed;
    NoteCollection changed;
    changed.setIndexParseObserverForTesting(
        [&parsed](const QString &relPath) { parsed.append(relPath); });
    QVERIFY(changed.openRoot(m_dir->path()));
    QCOMPARE(parsed, QStringList{QStringLiteral("Folder/B.md")});
    QCOMPARE(changed.note("Folder/B.md")->wordCount, 5);
}

void TestNoteCollection::testCorruptPersistentIndexFallsBackToFullParse()
{
    writeNote("A.md", "one two\n");
    writeNote("B.md", "three four\n");

    NoteCollection first;
    QVERIFY(first.openRoot(m_dir->path()));
    writeNote(".kvit/index.json", "{not json");

    QStringList parsed;
    NoteCollection reopened;
    reopened.setIndexParseObserverForTesting(
        [&parsed](const QString &relPath) { parsed.append(relPath); });
    QVERIFY(reopened.openRoot(m_dir->path()));
    parsed.sort();
    QCOMPARE(parsed, (QStringList() << "A.md" << "B.md"));
    QCOMPARE(reopened.noteCount(), 2);
    QCOMPARE(reopened.note("A.md")->wordCount, 2);
}

void TestNoteCollection::testRefreshPathsReindexesOnlyChangedNote()
{
    makeFixture();
    QStringList parsed;
    m_collection->setIndexParseObserverForTesting(
        [&parsed](const QString &relPath) { parsed.append(relPath); });
    QSignalSpy revisionSpy(m_collection, &NoteCollection::revisionChanged);

    writeNote("Ideas/Plans.md", "External change with more words today\n");
    m_collection->refreshPaths(QStringList{abs("Ideas/Plans.md")});

    QCOMPARE(parsed, QStringList{QStringLiteral("Ideas/Plans.md")});
    QCOMPARE(revisionSpy.count(), 1);
    QCOMPARE(m_collection->noteCount(), 4);
    const NoteCollection::NoteEntry *entry =
        m_collection->note("Ideas/Plans.md");
    QVERIFY(entry);
    QCOMPARE(entry->wordCount, 6);
    QCOMPARE(entry->snippet,
             QStringLiteral("External change with more words today"));
}

void TestNoteCollection::testRefreshPathsDirectoryScopesToSubtree()
{
    makeFixture();
    QStringList parsed;
    m_collection->setIndexParseObserverForTesting(
        [&parsed](const QString &relPath) { parsed.append(relPath); });
    QSignalSpy revisionSpy(m_collection, &NoteCollection::revisionChanged);

    writeNote("Ideas/New.md", "fresh external note words\n");
    m_collection->refreshPaths(QStringList{abs("Ideas")});

    QCOMPARE(parsed, QStringList());
    QCOMPARE(revisionSpy.count(), 0);
    QVERIFY(revisionSpy.wait(5000));
    QCOMPARE(parsed, QStringList{QStringLiteral("Ideas/New.md")});
    QCOMPARE(revisionSpy.count(), 1);
    QCOMPARE(m_collection->noteCount(), 5);
    QVERIFY(m_collection->note("Ideas/New.md"));
    QCOMPARE(m_collection->note("Ideas/Plans.md")->wordCount, 2);
    QCOMPARE(m_collection->noteCountInFolder("Ideas", false), 3);
    QCOMPARE(m_collection->noteCountInFolder("Ideas", true), 4);
}

void TestNoteCollection::testRefreshPathsDeletedDirectoryRemovesSubtree()
{
    makeFixture();
    QStringList parsed;
    m_collection->setIndexParseObserverForTesting(
        [&parsed](const QString &relPath) { parsed.append(relPath); });
    QSignalSpy revisionSpy(m_collection, &NoteCollection::revisionChanged);
    QSignalSpy removedSpy(m_collection, &NoteCollection::noteRemoved);

    QDir doomed(abs("Ideas/Projects"));
    QVERIFY(doomed.removeRecursively());
    m_collection->refreshPaths(QStringList{abs("Ideas/Projects")});

    QCOMPARE(parsed, QStringList());
    QCOMPARE(revisionSpy.count(), 0);
    QCOMPARE(removedSpy.count(), 0);
    QVERIFY(revisionSpy.wait(5000));
    QCOMPARE(parsed, QStringList());
    QCOMPARE(revisionSpy.count(), 1);
    QCOMPARE(removedSpy.count(), 1);
    QVERIFY(!m_collection->folder("Ideas/Projects"));
    QVERIFY(!m_collection->note("Ideas/Projects/Kvit.md"));
    QCOMPARE(m_collection->folderRelPaths(), QStringList{QStringLiteral("Ideas")});
    QCOMPARE(m_collection->noteCount(), 3);
    QCOMPARE(m_collection->noteCountInFolder("Ideas", true), 2);
}

void TestNoteCollection::testAsyncOpenRootReturnsBeforeBodyParse()
{
    writeNote("Huge.md",
              "# Huge\n\n"
              "alpha beta gamma delta epsilon zeta eta theta iota kappa\n");

    QStringList parsed;
    NoteCollection collection;
    collection.setIndexParseObserverForTesting(
        [&parsed](const QString &relPath) { parsed.append(relPath); });
    QSignalSpy revisionSpy(&collection, &NoteCollection::revisionChanged);
    QSignalSpy finishedSpy(&collection, &NoteCollection::scanFinished);

    QVERIFY(collection.openRootAsync(m_dir->path()));
    QCOMPARE(revisionSpy.count(), 1);
    QCOMPARE(collection.noteCount(), 0);
    QCOMPARE(parsed.size(), 0);
    QVERIFY(collection.scanInProgress());

    QVERIFY(revisionSpy.wait(5000));
    QCOMPARE(collection.noteCount(), 1);
    const NoteCollection::NoteEntry *initial = collection.note("Huge.md");
    QVERIFY(initial);
    QCOMPARE(initial->title, QStringLiteral("Huge"));
    QCOMPARE(initial->wordCount, 0);
    QCOMPARE(initial->fileSize, qint64(-1));
    QCOMPARE(parsed.size(), 0);

    QVERIFY(finishedSpy.wait(5000));
    QTRY_VERIFY_WITH_TIMEOUT(!collection.scanInProgress(), 5000);
    QTRY_VERIFY_WITH_TIMEOUT(parsed.contains(QStringLiteral("Huge.md")), 5000);
    const NoteCollection::NoteEntry *indexed = collection.note("Huge.md");
    QVERIFY(indexed);
    QVERIFY(indexed->wordCount > 0);
    QVERIFY(QFileInfo::exists(abs(".kvit/index.json")));
}

void TestNoteCollection::testCreatedPrefersFrontMatter()
{
    writeNote("Dated.md", "---\ncreated: 2020-01-02T03:04:05\n---\nBody\n");
    writeNote("Undated.md", "Body\n");
    QVERIFY(m_collection->openRoot(m_dir->path()));

    QCOMPARE(m_collection->note("Dated.md")->created,
             QDateTime(QDate(2020, 1, 2), QTime(3, 4, 5)));
    // Fallback is a file time — recent, certainly not year 2020.
    QVERIFY(m_collection->note("Undated.md")->created.date().year() >= 2026);
}

void TestNoteCollection::testInitializeIfEmpty()
{
    QVERIFY(m_collection->openRoot(m_dir->path()));
    QCOMPARE(m_collection->noteCount(), 0);

    m_collection->initializeIfEmpty();
    QCOMPARE(m_collection->noteRelPaths(), QStringList{QStringLiteral("Welcome.md")});
    QVERIFY(readNote("Welcome.md").startsWith(QStringLiteral("# Welcome")));

    // Idempotent, and never fires on a non-empty collection.
    m_collection->initializeIfEmpty();
    QCOMPARE(m_collection->noteCount(), 1);
}

// ------------------------------------------------------ note operations

void TestNoteCollection::testCreateNote()
{
    makeFixture();

    const QString relPath = m_collection->createNote("Ideas", "Fresh");
    QCOMPARE(relPath, QStringLiteral("Ideas/Fresh.md"));
    QVERIFY(QFileInfo::exists(abs(relPath)));
    QCOMPARE(readNote(relPath), QString());
    QVERIFY(m_collection->note(relPath));
    QCOMPARE(m_collection->note(relPath)->title, QStringLiteral("Fresh"));

    // New notes reconcile to the end of the folder's order.
    QCOMPARE(m_collection->notesInFolder("Ideas").last(), relPath);
}

void TestNoteCollection::testCreateNoteUniqueUntitled()
{
    QVERIFY(m_collection->openRoot(m_dir->path()));

    QCOMPARE(m_collection->createNote(QString()), QStringLiteral("Untitled.md"));
    QCOMPARE(m_collection->createNote(QString()), QStringLiteral("Untitled 2.md"));
    QCOMPARE(m_collection->createNote(QString()), QStringLiteral("Untitled 3.md"));
}

void TestNoteCollection::testCreateNoteCollision()
{
    makeFixture();
    QSignalSpy failSpy(m_collection, &NoteCollection::operationFailed);

    QCOMPARE(m_collection->createNote("Ideas", "Plans"), QString());
    QCOMPARE(failSpy.count(), 1);
    QCOMPARE(m_collection->createNote("Nowhere", "X"), QString());
    QCOMPARE(failSpy.count(), 2);
}

void TestNoteCollection::testCreateNoteInvalidNames()
{
    QVERIFY(m_collection->openRoot(m_dir->path()));
    QSignalSpy failSpy(m_collection, &NoteCollection::operationFailed);

    QCOMPARE(m_collection->createNote(QString(), "a/b"), QString());
    QCOMPARE(m_collection->createNote(QString(), ".dotfile"), QString());
    QCOMPARE(failSpy.count(), 2);
}

void TestNoteCollection::testRenameNote()
{
    makeFixture();
    QSignalSpy movedSpy(m_collection, &NoteCollection::noteMoved);

    QVERIFY(m_collection->renameNote("Ideas/Plans.md", "Better Plans"));
    QVERIFY(!QFileInfo::exists(abs("Ideas/Plans.md")));
    QCOMPARE(readNote("Ideas/Better Plans.md"), QStringLiteral("Some plans\n"));
    QVERIFY(!m_collection->note("Ideas/Plans.md"));
    QCOMPARE(m_collection->note("Ideas/Better Plans.md")->title,
             QStringLiteral("Better Plans"));

    QCOMPARE(movedSpy.count(), 1);
    QCOMPARE(movedSpy.at(0).at(0).toString(), QStringLiteral("Ideas/Plans.md"));
    QCOMPARE(movedSpy.at(0).at(1).toString(),
             QStringLiteral("Ideas/Better Plans.md"));

    // Renaming to the current name is a no-op, not an error.
    QSignalSpy failSpy(m_collection, &NoteCollection::operationFailed);
    QVERIFY(m_collection->renameNote("Ideas/Better Plans.md", "Better Plans"));
    QCOMPARE(failSpy.count(), 0);
}

void TestNoteCollection::testRenameNoteCollision()
{
    makeFixture();
    QSignalSpy failSpy(m_collection, &NoteCollection::operationFailed);

    QVERIFY(!m_collection->renameNote("Ideas/Plans.md", "Reading"));
    QCOMPARE(failSpy.count(), 1);
    // Nothing moved.
    QVERIFY(QFileInfo::exists(abs("Ideas/Plans.md")));
    QVERIFY(m_collection->note("Ideas/Plans.md"));
}

void TestNoteCollection::testMoveNote()
{
    makeFixture();
    QSignalSpy movedSpy(m_collection, &NoteCollection::noteMoved);

    QVERIFY(m_collection->moveNote("Welcome.md", "Ideas/Projects"));
    QCOMPARE(readNote("Ideas/Projects/Welcome.md"),
             QStringLiteral("# Welcome\n\nRoot note\n"));
    QVERIFY(!m_collection->note("Welcome.md"));
    QCOMPARE(m_collection->note("Ideas/Projects/Welcome.md")->folder,
             QStringLiteral("Ideas/Projects"));
    QCOMPARE(movedSpy.count(), 1);

    // Move to the root ("" folder).
    QVERIFY(m_collection->moveNote("Ideas/Projects/Welcome.md", QString()));
    QVERIFY(m_collection->note("Welcome.md"));
}

void TestNoteCollection::testMoveNoteCollision()
{
    makeFixture();
    writeNote("Plans.md", "root plans\n");
    m_collection->refresh();

    QSignalSpy failSpy(m_collection, &NoteCollection::operationFailed);
    QVERIFY(!m_collection->moveNote("Ideas/Plans.md", QString()));
    QCOMPARE(failSpy.count(), 1);
    QCOMPARE(readNote("Plans.md"), QStringLiteral("root plans\n"));
    QVERIFY(m_collection->note("Ideas/Plans.md"));
}

void TestNoteCollection::testDeleteNoteGoesToTrash()
{
    makeFixture();
    QSignalSpy removedSpy(m_collection, &NoteCollection::noteRemoved);

    QVERIFY(m_collection->deleteNote("Ideas/Reading.md"));
    QVERIFY(!QFileInfo::exists(abs("Ideas/Reading.md")));
    QVERIFY(!m_collection->note("Ideas/Reading.md"));
    QCOMPARE(removedSpy.count(), 1);

    // The body survives, byte-identical, in .kvit/trash.
    QDir trash(abs(".kvit/trash"));
    const QStringList entries = trash.entryList(QDir::Files);
    QCOMPARE(entries.size(), 1);
    QVERIFY(entries.first().endsWith(QStringLiteral("-Reading.md")));
    QCOMPARE(readNote(QStringLiteral(".kvit/trash/") + entries.first()),
             QStringLiteral("---\ntags: [books]\n---\nA **bold** reading list\n"));
}

// ---------------------------------------------------- folder operations

void TestNoteCollection::testCreateFolder()
{
    makeFixture();

    QCOMPARE(m_collection->createFolder(QString(), "Archive"),
             QStringLiteral("Archive"));
    QVERIFY(QFileInfo(abs("Archive")).isDir());
    QVERIFY(m_collection->folder("Archive"));

    QCOMPARE(m_collection->createFolder("Archive", "2026"),
             QStringLiteral("Archive/2026"));
    QVERIFY(m_collection->folder("Archive/2026"));

    QSignalSpy failSpy(m_collection, &NoteCollection::operationFailed);
    QCOMPARE(m_collection->createFolder(QString(), "Archive"), QString());
    QCOMPARE(failSpy.count(), 1);
}

void TestNoteCollection::testRenameFolderRebindsContents()
{
    makeFixture();
    QSignalSpy movedSpy(m_collection, &NoteCollection::noteMoved);

    QVERIFY(m_collection->renameFolder("Ideas", "Thoughts"));
    QVERIFY(QFileInfo(abs("Thoughts")).isDir());
    QVERIFY(!QFileInfo::exists(abs("Ideas")));

    // Folders, subfolders, and notes all rebound.
    QCOMPARE(m_collection->folderRelPaths(),
             (QStringList() << "Thoughts" << "Thoughts/Projects"));
    QVERIFY(m_collection->note("Thoughts/Reading.md"));
    QVERIFY(m_collection->note("Thoughts/Projects/Kvit.md"));
    QCOMPARE(m_collection->note("Thoughts/Projects/Kvit.md")->folder,
             QStringLiteral("Thoughts/Projects"));

    // Every contained note announced its new identity.
    QCOMPARE(movedSpy.count(), 3);
}

void TestNoteCollection::testDeleteFolderRecursive()
{
    makeFixture();
    QSignalSpy removedSpy(m_collection, &NoteCollection::noteRemoved);

    QVERIFY(m_collection->deleteFolder("Ideas"));
    QVERIFY(!QFileInfo::exists(abs("Ideas")));
    QCOMPARE(m_collection->folderRelPaths(), QStringList());
    QCOMPARE(m_collection->noteRelPaths(), QStringList{QStringLiteral("Welcome.md")});
    QCOMPARE(removedSpy.count(), 3);

    // The whole tree sits in the trash, contents intact.
    QDir trash(abs(".kvit/trash"));
    const QStringList entries = trash.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
    QCOMPARE(entries.size(), 1);
    QVERIFY(QFileInfo::exists(abs(QStringLiteral(".kvit/trash/") + entries.first()
                                  + QStringLiteral("/Projects/Kvit.md"))));
}

void TestNoteCollection::testFolderStatePersists()
{
    makeFixture();

    m_collection->setFolderExpanded("Ideas", false);
    m_collection->setFolderColor("Ideas", QStringLiteral("#4090e0"));

    NoteCollection reopened;
    QVERIFY(reopened.openRoot(m_dir->path()));
    QCOMPARE(reopened.folder("Ideas")->expanded, false);
    QCOMPARE(reopened.folder("Ideas")->color, QStringLiteral("#4090e0"));
    QCOMPARE(reopened.folder("Ideas/Projects")->expanded, true);
}

// ------------------------------------------------------------ metadata

void TestNoteCollection::testSetMetadataWritesFrontMatterLazily()
{
    makeFixture();
    // Plans.md has no front-matter; setting a flag creates exactly the
    // canonical block.
    QVERIFY(m_collection->setPinned("Ideas/Plans.md", true));
    QCOMPARE(readNote("Ideas/Plans.md"),
             QStringLiteral("---\npinned: true\n---\nSome plans\n"));
    QCOMPARE(m_collection->note("Ideas/Plans.md")->meta.pinned, true);
}

void TestNoteCollection::testSetGoalRoundTrips()
{
    makeFixture();
    // A writing goal writes to front-matter and reads back; 0 clears it.
    QVERIFY(m_collection->setGoal("Ideas/Plans.md", 800));
    QCOMPARE(m_collection->goalFor("Ideas/Plans.md"), 800);
    QVERIFY(readNote("Ideas/Plans.md").contains(QStringLiteral("goal: 800")));
    QVERIFY(m_collection->setGoal("Ideas/Plans.md", 0));
    QCOMPARE(m_collection->goalFor("Ideas/Plans.md"), 0);
    QVERIFY(!readNote("Ideas/Plans.md").contains(QStringLiteral("goal")));
}

void TestNoteCollection::testMetadataRewritePreservesBodyAndMtime()
{
    makeFixture();
    const QDateTime before =
        QFileInfo(abs("Ideas/Reading.md")).lastModified();

    QVERIFY(m_collection->setFavorite("Ideas/Reading.md", true));
    QCOMPARE(readNote("Ideas/Reading.md"),
             QStringLiteral("---\ntags: [books]\nfavorite: true\n---\n"
                            "A **bold** reading list\n"));
    // A metadata edit is not a content edit: mtime restored.
    QCOMPARE(QFileInfo(abs("Ideas/Reading.md")).lastModified(), before);
    QCOMPARE(m_collection->note("Ideas/Reading.md")->modified, before);
}

void TestNoteCollection::testClearingMetadataRemovesBlock()
{
    makeFixture();
    QVERIFY(m_collection->setTags("Ideas/Reading.md", QStringList()));
    QCOMPARE(readNote("Ideas/Reading.md"),
             QStringLiteral("A **bold** reading list\n"));
}

void TestNoteCollection::testForeignKeysSurviveMetadataWrite()
{
    writeNote("Foreign.md",
              "---\nlayout: post\naliases:\n  - f\n---\nBody\n");
    QVERIFY(m_collection->openRoot(m_dir->path()));

    QVERIFY(m_collection->setPinned("Foreign.md", true));
    QCOMPARE(readNote("Foreign.md"),
             QStringLiteral("---\npinned: true\nlayout: post\naliases:\n"
                            "  - f\n---\nBody\n"));
}

void TestNoteCollection::testFrontMatterFor()
{
    makeFixture();
    QCOMPARE(m_collection->frontMatterFor("Ideas/Projects/Kvit.md"),
             QStringLiteral("---\ntags: [work, kvit]\npinned: true\n---\n"));
    QCOMPARE(m_collection->frontMatterFor("Ideas/Plans.md"), QString());
    QCOMPARE(m_collection->frontMatterFor("missing.md"), QString());
}

// ----------------------------------------------------------------- tags

void TestNoteCollection::testTagRegistry()
{
    makeFixture();
    QCOMPARE(m_collection->allTags(),
             (QStringList() << "books" << "kvit" << "work"));
    QCOMPARE(m_collection->tagCount("books"), 1);
    QCOMPARE(m_collection->tagCount("nope"), 0);

    QVERIFY(m_collection->addTag("Ideas/Plans.md", "books"));
    QCOMPARE(m_collection->tagCount("books"), 2);
    QVERIFY(m_collection->removeTag("Ideas/Plans.md", "books"));
    QCOMPARE(m_collection->tagCount("books"), 1);
}

void TestNoteCollection::testRenameTagRewritesAffectedNotesOnly()
{
    makeFixture();
    const QString untouched = readNote("Ideas/Plans.md");

    QVERIFY(m_collection->renameTag("books", "reading"));
    QCOMPARE(m_collection->allTags(),
             (QStringList() << "kvit" << "reading" << "work"));
    QVERIFY(readNote("Ideas/Reading.md")
                .contains(QStringLiteral("tags: [reading]")));
    // A note without the tag keeps its exact bytes.
    QCOMPARE(readNote("Ideas/Plans.md"), untouched);
}

void TestNoteCollection::testMergeTagUnions()
{
    makeFixture();
    // "work" exists on Kvit.md alongside "kvit"; renaming "kvit" onto
    // "work" must dedupe, not duplicate.
    QVERIFY(m_collection->renameTag("kvit", "work"));
    QCOMPARE(m_collection->note("Ideas/Projects/Kvit.md")->meta.tags,
             QStringList{QStringLiteral("work")});
    QCOMPARE(m_collection->allTags(), (QStringList() << "books" << "work"));
}

void TestNoteCollection::testDeleteTag()
{
    makeFixture();
    QVERIFY(m_collection->deleteTag("work"));
    QCOMPARE(m_collection->note("Ideas/Projects/Kvit.md")->meta.tags,
             QStringList{QStringLiteral("kvit")});
    QCOMPARE(m_collection->allTags(), (QStringList() << "books" << "kvit"));
}

void TestNoteCollection::testTagColorsPersist()
{
    makeFixture();
    m_collection->setTagColor("books", QStringLiteral("#e07030"));

    NoteCollection reopened;
    QVERIFY(reopened.openRoot(m_dir->path()));
    QCOMPARE(reopened.tagColor("books"), QStringLiteral("#e07030"));
    QCOMPARE(reopened.tagColor("work"), QString());

    // Rename carries the color.
    QVERIFY(reopened.renameTag("books", "reading"));
    QCOMPARE(reopened.tagColor("reading"), QStringLiteral("#e07030"));
    QCOMPARE(reopened.tagColor("books"), QString());
}

void TestNoteCollection::testTagAutoColorAssignment()
{
    makeFixture();

    // A tag entering the registry through a metadata edit gets a palette
    // color automatically (§8.2); a manually colored tag keeps its color.
    m_collection->setTagColor("books", QStringLiteral("#123456"));
    QVERIFY(m_collection->addTag("Ideas/Plans.md", "fresh"));
    QVERIFY(!m_collection->tagColor("fresh").isEmpty());
    QCOMPARE(m_collection->tagColor("books"), QStringLiteral("#123456"));

    // A second new tag gets a (different) color too.
    QVERIFY(m_collection->addTag("Ideas/Plans.md", "another"));
    QVERIFY(!m_collection->tagColor("another").isEmpty());
    QVERIFY(m_collection->tagColor("another") != m_collection->tagColor("fresh"));
}

void TestNoteCollection::testTagListing()
{
    makeFixture();
    m_collection->setTagColor("books", QStringLiteral("#123456"));

    const QVariantList listing = m_collection->tagListing();
    QCOMPARE(listing.size(), 3);
    const QVariantMap first = listing.at(0).toMap();
    QCOMPARE(first.value("name").toString(), QStringLiteral("books"));
    QCOMPARE(first.value("count").toInt(), 1);
    QCOMPARE(first.value("color").toString(), QStringLiteral("#123456"));
    QCOMPARE(listing.at(1).toMap().value("name").toString(),
             QStringLiteral("kvit"));
    QCOMPARE(listing.at(2).toMap().value("name").toString(),
             QStringLiteral("work"));
}

// --------------------------------------------------------- manual order

void TestNoteCollection::testManualOrderReconciliation()
{
    makeFixture();
    // No stored order: notes come back oldest-created first; the fixture
    // files were written moments apart, so just assert the set and that
    // a new note appends at the end.
    QStringList order = m_collection->notesInFolder("Ideas");
    QCOMPARE(order.size(), 2);
    QVERIFY(order.contains(QStringLiteral("Ideas/Reading.md")));
    QVERIFY(order.contains(QStringLiteral("Ideas/Plans.md")));

    const QString fresh = m_collection->createNote("Ideas", "Zebra");
    QCOMPARE(m_collection->notesInFolder("Ideas").last(), fresh);

    // Deleted notes prune from a stored order.
    QVERIFY(m_collection->setManualPosition(fresh, 0));
    QVERIFY(m_collection->deleteNote(fresh));
    QVERIFY(!m_collection->notesInFolder("Ideas").contains(fresh));
}

void TestNoteCollection::testSetManualPositionPersists()
{
    makeFixture();
    QStringList before = m_collection->notesInFolder("Ideas");
    const QString last = before.last();

    QVERIFY(m_collection->setManualPosition(last, 0));
    QCOMPARE(m_collection->notesInFolder("Ideas").first(), last);

    NoteCollection reopened;
    QVERIFY(reopened.openRoot(m_dir->path()));
    QCOMPARE(reopened.notesInFolder("Ideas").first(), last);
}

// ------------------------------------------------------ workspace state

void TestNoteCollection::testLastOpenNotePersists()
{
    makeFixture();
    m_collection->setLastOpenNote("Ideas/Reading.md");

    NoteCollection reopened;
    QVERIFY(reopened.openRoot(m_dir->path()));
    QCOMPARE(reopened.lastOpenNote(), QStringLiteral("Ideas/Reading.md"));

    // A last-open note that no longer exists is dropped on load.
    QVERIFY(reopened.deleteNote("Ideas/Reading.md"));
    NoteCollection again;
    QVERIFY(again.openRoot(m_dir->path()));
    QCOMPARE(again.lastOpenNote(), QString());
}

void TestNoteCollection::testCollectionFileCorruptionTolerated()
{
    makeFixture();
    m_collection->setFolderColor("Ideas", QStringLiteral("#4090e0"));

    writeNote(".kvit/collection.json", "{not json at all");

    NoteCollection reopened;
    QVERIFY(reopened.openRoot(m_dir->path()));
    // State degrades to defaults; the notes themselves are untouched.
    QCOMPARE(reopened.noteCount(), 4);
    QCOMPARE(reopened.folder("Ideas")->color, QString());
    QCOMPARE(readNote("Ideas/Plans.md"), QStringLiteral("Some plans\n"));
}

// -------------------------------------------------------------- backups

void TestNoteCollection::testBackupRotationFloorAndPrune()
{
    makeFixture();
    const QString absPath = abs("Ideas/Plans.md");

    // An injectable clock drives the rotation floor.
    QDateTime now(QDate(2026, 7, 7), QTime(10, 0, 0));
    m_collection->setClockForTesting([&now]() { return now; });

    // First save of the window: one backup of the CURRENT file.
    m_collection->backupBeforeOverwrite(absPath);
    QDir backupDir(abs(".kvit/backups/Ideas/Plans.md"));
    QTRY_COMPARE_WITH_TIMEOUT(backupDir.entryList(QDir::Files).size(), 1, 5000);
    QCOMPARE(readNote(".kvit/backups/Ideas/Plans.md/"
                      + backupDir.entryList(QDir::Files).first()),
             QStringLiteral("Some plans\n"));

    // Within the 10-minute floor: no new copy, whatever the cadence.
    now = now.addSecs(5 * 60);
    m_collection->backupBeforeOverwrite(absPath);
    QCOMPARE(backupDir.entryList(QDir::Files).size(), 1);

    // Past the floor: a second copy.
    now = now.addSecs(6 * 60);
    m_collection->backupBeforeOverwrite(absPath);
    QTRY_COMPARE_WITH_TIMEOUT(backupDir.entryList(QDir::Files).size(), 2, 5000);

    // The cap prunes the oldest beyond 10.
    for (int i = 0; i < 12; ++i) {
        now = now.addSecs(11 * 60);
        m_collection->backupBeforeOverwrite(absPath);
    }
    QTRY_COMPARE_WITH_TIMEOUT(backupDir.entryList(QDir::Files, QDir::Name).size(),
                              10, 5000);
    // The very first stamp (10:00:00) was pruned.
    QVERIFY(!backupDir.entryList(QDir::Files, QDir::Name)
                 .first()
                 .startsWith(QStringLiteral("20260707-1000")));

    // Paths outside the root are ignored.
    m_collection->backupBeforeOverwrite(QStringLiteral("/tmp/elsewhere.md"));
}

void TestNoteCollection::testBackupsListingAndBody()
{
    makeFixture();
    const QString absPath = abs("Ideas/Reading.md");
    QDateTime now(QDate(2026, 7, 7), QTime(9, 0, 0));
    m_collection->setClockForTesting([&now]() { return now; });

    m_collection->backupBeforeOverwrite(absPath);
    writeNote("Ideas/Reading.md",
              "---\ntags: [books]\n---\nA changed reading list\n");
    now = now.addSecs(20 * 60);
    m_collection->backupBeforeOverwrite(absPath);

    QTRY_COMPARE_WITH_TIMEOUT(
        QDir(abs(".kvit/backups/Ideas/Reading.md")).entryList(QDir::Files).size(),
        2, 5000);
    const QVariantList listing = m_collection->backupsFor("Ideas/Reading.md");
    QCOMPARE(listing.size(), 2);
    // Newest first, with the front-matter-free preview.
    const QVariantMap newest = listing.at(0).toMap();
    QCOMPARE(newest.value("timestamp").toDateTime(),
             QDateTime(QDate(2026, 7, 7), QTime(9, 20, 0)));
    QCOMPARE(newest.value("preview").toString(),
             QStringLiteral("A changed reading list"));
    const QVariantMap oldest = listing.at(1).toMap();
    QCOMPARE(oldest.value("preview").toString(),
             QStringLiteral("A bold reading list"));

    // backupBody returns the body only, front-matter stripped.
    QCOMPARE(m_collection->backupBody("Ideas/Reading.md",
                                      oldest.value("fileName").toString()),
             QStringLiteral("A **bold** reading list\n"));
    QCOMPARE(m_collection->backupBody("Ideas/Reading.md", "no-such.md"),
             QString());
}

// ------------------------------------------------------- crash recovery

void TestNoteCollection::testRecoveryLifecycle()
{
    makeFixture();

    // Fabricate crash evidence: a journal left behind by a dead session.
    const QString journal = m_collection->journalPathFor("Ideas/Plans.md");
    QVERIFY(!journal.isEmpty());
    {
        QFile file(journal);
        QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Text));
        file.write("Unsaved plans content\n");
    }

    // Journals count as evidence only when found at openRoot.
    QCOMPARE(m_collection->recoveryEntries().size(), 0);
    NoteCollection reopened;
    QVERIFY(reopened.openRoot(m_dir->path()));
    const QVariantList entries = reopened.recoveryEntries();
    QCOMPARE(entries.size(), 1);
    const QVariantMap entry = entries.at(0).toMap();
    QCOMPARE(entry.value("relPath").toString(),
             QStringLiteral("Ideas/Plans.md"));
    QCOMPARE(entry.value("title").toString(), QStringLiteral("Plans"));
    QCOMPARE(entry.value("preview").toString(),
             QStringLiteral("Unsaved plans content"));

    // Restore writes the journal image over the note and reindexes.
    QVERIFY(reopened.restoreRecovery("Ideas/Plans.md"));
    QCOMPARE(readNote("Ideas/Plans.md"),
             QStringLiteral("Unsaved plans content\n"));
    QCOMPARE(reopened.note("Ideas/Plans.md")->snippet,
             QStringLiteral("Unsaved plans content"));
    QCOMPARE(reopened.recoveryEntries().size(), 0);
    QVERIFY(!QFileInfo::exists(journal));

    // Discard removes the journal and keeps the disk file.
    const QString journal2 = reopened.journalPathFor("Welcome.md");
    {
        QFile file(journal2);
        QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Text));
        file.write("Ghost edits\n");
    }
    NoteCollection again;
    QVERIFY(again.openRoot(m_dir->path()));
    QCOMPARE(again.recoveryEntries().size(), 1);
    again.discardRecovery("Welcome.md");
    QCOMPARE(again.recoveryEntries().size(), 0);
    QVERIFY(!QFileInfo::exists(journal2));
    QCOMPARE(readNote("Welcome.md"), QStringLiteral("# Welcome\n\nRoot note\n"));
}

void TestNoteCollection::testRecoveryRecreatesDeletedFolder()
{
    makeFixture();
    const QString journal =
        m_collection->journalPathFor("Ideas/Projects/Kvit.md");
    {
        QFile file(journal);
        QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Text));
        file.write("Recovered editor notes\n");
    }
    // The whole folder vanished since the crash.
    QVERIFY(m_collection->deleteFolder("Ideas"));

    NoteCollection reopened;
    QVERIFY(reopened.openRoot(m_dir->path()));
    QCOMPARE(reopened.recoveryEntries().size(), 1);
    QVERIFY(reopened.restoreRecovery("Ideas/Projects/Kvit.md"));
    QCOMPARE(readNote("Ideas/Projects/Kvit.md"),
             QStringLiteral("Recovered editor notes\n"));
    QVERIFY(reopened.folder("Ideas"));
    QVERIFY(reopened.folder("Ideas/Projects"));
    QVERIFY(reopened.note("Ideas/Projects/Kvit.md"));
}

void TestNoteCollection::testRefreshDoesNotIngestLiveJournals()
{
    makeFixture();
    // A live journal appears mid-session (the open, dirty note).
    const QString journal = m_collection->journalPathFor("Welcome.md");
    {
        QFile file(journal);
        QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Text));
        file.write("In-flight edits\n");
    }
    m_collection->refresh();
    QCOMPARE(m_collection->recoveryEntries().size(), 0);
}

// ----------------------------------------------------------- wiki-links

void TestNoteCollection::testExtractWikiLinks()
{
    // Grammar mirror of the formatter's matcher: targets keep their heading
    // anchor, aliases are stripped, duplicates are kept in order.
    QCOMPARE(NoteCollection::extractWikiLinks(
                 "See [[A]] then [[b/C|alias]] and [[A]].\n[[D#H]]"),
             (QStringList() << "A" << "b/C" << "A" << "D#H"));

    // Code fences are excluded; inline text around them still scans.
    QCOMPARE(NoteCollection::extractWikiLinks(
                 "[[before]]\n```\n[[inside]]\n```\n[[after]]"),
             (QStringList() << "before" << "after"));

    // Malformed or same-note forms are not outgoing links.
    QVERIFY(NoteCollection::extractWikiLinks(
                "[[]] [[a|]] [[a#]] [[#local]] \\[[escaped]]").isEmpty());

    // The collection scanner and editor agree that verbatim inline regions
    // are opaque. Variable backtick lengths and both math forms are covered.
    QCOMPARE(NoteCollection::extractWikiLinks(
                 "[[yes]] `[[code]]` ``x ` [[code2]] x`` "
                 "$x + [[math]]$ $$\n[[display]]\n$$ \\$ [[after]]"),
             (QStringList() << "yes" << "after"));

    // Escaped dollars do not open math, and a longer fence is closed only by
    // a matching character/run.
    QCOMPARE(NoteCollection::extractWikiLinks(
                 "\\$[[literal]]$\n````lang\n[[fenced]]\n```\n"
                 "still [[fenced2]]\n````\n[[outside]]"),
             (QStringList() << "literal" << "outside"));
}

void TestNoteCollection::testResolveWikiTarget()
{
    makeFixture();

    // Bare basename, case-insensitive, ".md" implied.
    QCOMPARE(m_collection->resolveWikiTarget("Kvit"), QString("Ideas/Projects/Kvit.md"));
    QCOMPARE(m_collection->resolveWikiTarget("kvit"), QString("Ideas/Projects/Kvit.md"));
    QCOMPARE(m_collection->resolveWikiTarget("Kvit.md"), QString("Ideas/Projects/Kvit.md"));
    QCOMPARE(m_collection->resolveWikiTarget("Welcome"), QString("Welcome.md"));

    // Suffix with more path; heading anchors are ignored for resolution.
    QCOMPARE(m_collection->resolveWikiTarget("Projects/Kvit"),
             QString("Ideas/Projects/Kvit.md"));
    QCOMPARE(m_collection->resolveWikiTarget("Kvit#Heading"),
             QString("Ideas/Projects/Kvit.md"));

    // No match.
    QCOMPARE(m_collection->resolveWikiTarget("Nope"), QString());
    QCOMPARE(m_collection->resolveWikiTarget("Other/Kvit"), QString());

    // Ambiguity never picks an arbitrary candidate; more path disambiguates.
    writeNote("Archive/Welcome.md", "old welcome\n");
    m_collection->refresh();
    QCOMPARE(m_collection->resolveWikiTarget("Welcome"), QString());
    const QVariantMap ambiguous =
        m_collection->wikiTargetResolution("WELCOME.md");
    QCOMPARE(ambiguous.value("status").toString(), QString("ambiguous"));
    QCOMPARE(ambiguous.value("candidates").toStringList().size(), 2);
    QCOMPARE(m_collection->resolveWikiTarget("Archive/Welcome"),
             QString("Archive/Welcome.md"));
}

void TestNoteCollection::testBacklinks()
{
    writeNote("A.md", "Links to [[B]] twice: [[b#Heading|alias]]\n");
    writeNote("B.md", "No outgoing links\n");
    writeNote("C.md", "```\n[[B]]\n```\nOnly a fenced mention\n");
    QVERIFY(m_collection->openRoot(m_dir->path()));

    QCOMPARE(m_collection->linksFrom("A.md"),
             (QStringList() << "B" << "b#Heading"));

    const QVariantList backs = m_collection->backlinksTo("B.md");
    QCOMPARE(backs.size(), 1);
    const QVariantMap row = backs.first().toMap();
    QCOMPARE(row.value("relPath").toString(), QString("A.md"));
    QCOMPARE(row.value("count").toInt(), 2);
    const QStringList contexts = row.value("contexts").toStringList();
    QCOMPARE(contexts.size(), 1); // both links share one line
    QVERIFY(contexts.first().contains("[[B]]"));

    QVERIFY(m_collection->backlinksTo("A.md").isEmpty());
}

void TestNoteCollection::testWikiLinksReindexOnRefresh()
{
    makeFixture();
    QVERIFY(m_collection->backlinksTo("Welcome.md").isEmpty());

    // An external edit adds a link; refreshPaths re-extracts and the
    // resolution index follows the revision bump.
    writeNote("Ideas/Plans.md", "Some plans, see [[Welcome]]\n");
    m_collection->refreshPaths({abs("Ideas/Plans.md")});
    const QVariantList backs = m_collection->backlinksTo("Welcome.md");
    QCOMPARE(backs.size(), 1);
    QCOMPARE(backs.first().toMap().value("relPath").toString(),
             QString("Ideas/Plans.md"));

    // A new note flips resolution for a previously dangling target.
    QCOMPARE(m_collection->resolveWikiTarget("Fresh"), QString());
    writeNote("Fresh.md", "now exists\n");
    m_collection->refreshPaths({abs("Fresh.md")});
    QCOMPARE(m_collection->resolveWikiTarget("Fresh"), QString("Fresh.md"));
}

void TestNoteCollection::testRenameRewritesReferringLinks()
{
    writeNote("Target.md", "content, plus a self link [[Target]]\n");
    writeNote("Ref.md",
              "One [[Target]] and [[target#Head|alias]] here.\n"
              "```\n[[Target]] stays untouched in a fence\n```\n");
    writeNote("Unrelated.md", "No links\n");
    QVERIFY(m_collection->openRoot(m_dir->path()));

    QSignalSpy spy(m_collection, &NoteCollection::wikiLinksRewritten);
    const QVariantMap plan = m_collection->planNoteRename("Target.md", "Renamed");
    QVERIFY(plan.value("ok").toBool());
    QCOMPARE(plan.value("linkCount").toInt(), 3);
    const QVariantMap result = m_collection->applyRenamePlan(
        plan.value("id").toString(), true);
    QVERIFY(result.value("ok").toBool());

    // Referrer rewritten: target text swapped, alias and anchor kept,
    // fenced occurrence untouched.
    QCOMPARE(readNote("Ref.md"),
             QString("One [[Renamed]] and [[Renamed#Head|alias]] here.\n"
                     "```\n[[Target]] stays untouched in a fence\n```\n"));
    // The self link in the renamed note follows too.
    QCOMPARE(readNote("Renamed.md"),
             QString("content, plus a self link [[Renamed]]\n"));
    QCOMPARE(readNote("Unrelated.md"), QString("No links\n"));

    QCOMPARE(spy.count(), 1);
    QCOMPARE(spy.first().at(0).toInt(), 3); // links
    QCOMPARE(spy.first().at(1).toInt(), 2); // notes

    // The index followed the rewrite: backlinks resolve to the new path.
    const QVariantList backs = m_collection->backlinksTo("Renamed.md");
    QCOMPARE(backs.size(), 1);
    QCOMPARE(backs.first().toMap().value("relPath").toString(),
             QString("Ref.md"));
}

void TestNoteCollection::testRenamePlanCancelAndConflictSkip()
{
    writeNote("Target.md", "target\n");
    writeNote("Ref.md", "before [[Target]] after\n");
    QVERIFY(m_collection->openRoot(m_dir->path()));

    // Rename-only is the explicit cancellation of link edits.
    QVariantMap plan = m_collection->planNoteRename("Target.md", "First");
    QCOMPARE(plan.value("linkCount").toInt(), 1);
    QVariantMap result = m_collection->applyRenamePlan(
        plan.value("id").toString(), false);
    QVERIFY(result.value("ok").toBool());
    QCOMPARE(readNote("Ref.md"), QString("before [[Target]] after\n"));

    // A fresh plan snapshots bytes. An external edit after planning is never
    // overwritten; it is named in the partial result.
    writeNote("Ref.md", "before [[First]] after\n");
    m_collection->refreshPaths({abs("Ref.md")});
    plan = m_collection->planNoteRename("First.md", "Second");
    QCOMPARE(plan.value("linkCount").toInt(), 1);
    writeNote("Ref.md", "newer external text with [[First]]\n");
    result = m_collection->applyRenamePlan(plan.value("id").toString(), true);
    QVERIFY(result.value("ok").toBool());
    QCOMPARE(result.value("skipped").toStringList(),
             QStringList{QStringLiteral("Ref.md")});
    QCOMPARE(readNote("Ref.md"),
             QString("newer external text with [[First]]\n"));

    // Retry uses the new snapshot and only changes targets still pointing at
    // the old note, so repeating after a partial result is safe.
    result = m_collection->applyRenamePlan(plan.value("id").toString(), true);
    QVERIFY(result.value("ok").toBool());
    QCOMPARE(readNote("Ref.md"),
             QString("newer external text with [[Second]]\n"));
}

void TestNoteCollection::testFolderRenameRewritesQualifiedLinksOnly()
{
    writeNote("Ideas/Target.md", "target\n");
    writeNote("Ref.md",
              "bare [[Target]] qualified [[Ideas/Target#H|alias]]\n"
              "`[[Ideas/Target]]` $[[Ideas/Target]]$\n");
    QVERIFY(m_collection->openRoot(m_dir->path()));

    const QVariantMap plan = m_collection->planFolderRename("Ideas", "Thoughts");
    QCOMPARE(plan.value("linkCount").toInt(), 1);
    const QVariantMap result = m_collection->applyRenamePlan(
        plan.value("id").toString(), true);
    QVERIFY(result.value("ok").toBool());
    QCOMPARE(readNote("Ref.md"),
             QString("bare [[Target]] qualified [[Thoughts/Target#H|alias]]\n"
                     "`[[Ideas/Target]]` $[[Ideas/Target]]$\n"));
    QCOMPARE(m_collection->resolveWikiTarget("Target"),
             QString("Thoughts/Target.md"));
}

void TestNoteCollection::testMoveKeepsBareLinksUntouched()
{
    writeNote("Target.md", "content\n");
    writeNote("RefBare.md", "A bare [[target]] link\n");
    writeNote("RefPath.md", "A qualified [[/Target|t]] link\n");
    QVERIFY(m_collection->openRoot(m_dir->path()));
    QVERIFY(m_collection->createFolder("", "Sub") != QString());

    QVERIFY(m_collection->moveNote("Target.md", "Sub"));

    // The bare name still resolves after the move — the file must not be
    // churned (user casing preserved verbatim).
    QCOMPARE(readNote("RefBare.md"), QString("A bare [[target]] link\n"));
    QCOMPARE(m_collection->resolveWikiTarget("target"),
             QString("Sub/Target.md"));
}

void TestNoteCollection::testRewriteWikiTargetsInTextSurgical()
{
    // Alias, anchor, and inner spacing all survive; only the note part
    // moves. Keys are lowercased, ".md"-stripped.
    QString text = QStringLiteral(
        "a [[ Old ]] b [[old#H|x]] c [[Old.md]] d [[other]]\n");
    const int n = NoteCollection::rewriteWikiTargetsInText(
        &text, {QStringLiteral("old")}, QStringLiteral("New"));
    QCOMPARE(n, 3);
    QCOMPARE(text, QString("a [[ New ]] b [[New#H|x]] c [[New]] d [[other]]\n"));

    QString opaque = QStringLiteral(
        "🙂 [[old]] `[[old]]` $[[old]]$ $$[[old]]$$ [[ old #H | a ]]\n");
    QCOMPARE(NoteCollection::rewriteWikiTargetsInText(
                 &opaque, {QStringLiteral("old")}, QStringLiteral("New")), 2);
    QCOMPARE(opaque, QString("🙂 [[New]] `[[old]]` $[[old]]$ "
                             "$$[[old]]$$ [[ New #H | a ]]\n"));
}

// ---------------------------------------------------------------- seams

void TestNoteCollection::testNoteSavedRefreshesEntry()
{
    makeFixture();
    QCOMPARE(m_collection->note("Ideas/Plans.md")->wordCount, 2);

    // Simulate DocumentManager saving new content.
    writeNote("Ideas/Plans.md", "Some plans got much **longer** today\n");
    m_collection->noteSaved(abs("Ideas/Plans.md"));

    QTRY_COMPARE(m_collection->note("Ideas/Plans.md")->wordCount, 6);
    const NoteCollection::NoteEntry *entry = m_collection->note("Ideas/Plans.md");
    QCOMPARE(entry->wordCount, 6);
    QCOMPARE(entry->snippet, QStringLiteral("Some plans got much longer today"));
    QCOMPARE(m_collection->noteInfo("Ideas/Plans.md").value("body").toString(),
             QStringLiteral("Some plans got much **longer** today\n"));

    // Paths outside the root are ignored.
    m_collection->noteSaved(QStringLiteral("/tmp/elsewhere.md"));
}

void TestNoteCollection::testNoteSavedCanUseProvidedText()
{
    makeFixture();
    QStringList parsed;
    m_collection->setIndexParseObserverForTesting(
        [&parsed](const QString &relPath) { parsed.append(relPath); });

    m_collection->noteSaved(abs("Ideas/Plans.md"),
                            QStringLiteral("Memory **only** text\n"));

    QTRY_COMPARE(parsed, QStringList{QStringLiteral("Ideas/Plans.md")});
    const NoteCollection::NoteEntry *entry =
        m_collection->note("Ideas/Plans.md");
    QVERIFY(entry);
    QCOMPARE(entry->wordCount, 3);
    QCOMPARE(entry->snippet, QStringLiteral("Memory only text"));
    // wordCount and snippet prove the supplied snapshot drove the reindex; the
    // body is no longer cached, and the fixture's disk bytes were untouched.
    QCOMPARE(readNote("Ideas/Plans.md"), QStringLiteral("Some plans\n"));
}

void TestNoteCollection::testRevisionContract()
{
    makeFixture();
    int revision = m_collection->revision();
    QSignalSpy revisionSpy(m_collection, &NoteCollection::revisionChanged);

    // Observable changes bump exactly once.
    QVERIFY(m_collection->setPinned("Ideas/Plans.md", true));
    QCOMPARE(m_collection->revision(), revision + 1);
    QCOMPARE(revisionSpy.count(), 1);

    // No-op operations do not bump.
    QVERIFY(m_collection->setPinned("Ideas/Plans.md", true));
    QVERIFY(m_collection->renameNote("Ideas/Plans.md", "Plans"));
    QVERIFY(m_collection->moveNote("Ideas/Plans.md", "Ideas"));
    m_collection->setFolderExpanded("Ideas", true); // already expanded
    QCOMPARE(revisionSpy.count(), 1);

    // Failed operations do not bump.
    QVERIFY(!m_collection->renameNote("Ideas/Plans.md", "Reading"));
    QVERIFY(!m_collection->deleteNote("missing.md"));
    QCOMPARE(revisionSpy.count(), 1);

    // Every successful mutation bumps.
    QVERIFY(!m_collection->createNote("Ideas", "Note A").isEmpty());
    QVERIFY(m_collection->renameNote("Ideas/Note A.md", "Note B"));
    QVERIFY(m_collection->moveNote("Ideas/Note B.md", QString()));
    QVERIFY(m_collection->deleteNote("Note B.md"));
    QCOMPARE(revisionSpy.count(), 5);
}

void TestNoteCollection::testBenchmark500NoteOpen()
{
    // 500 notes — an order of magnitude beyond a dogfooding collection —
    // in 20 folders, each with front-matter and a mixed-type body the
    // scan must fully parse (display texts, word counts, snippets).
    for (int folder = 0; folder < 20; ++folder)
        QVERIFY(QDir().mkpath(abs(QStringLiteral("Folder%1").arg(folder))));
    for (int i = 0; i < 500; ++i) {
        QString body;
        for (int line = 0; line < 8 + (i % 12); ++line) {
            body += QStringLiteral(
                        "Paragraph %1 of note %2 with **bold** and *italic* "
                        "text that the scanner parses\n\n")
                        .arg(line)
                        .arg(i);
        }
        writeNote(QStringLiteral("Folder%1/Note %2.md").arg(i % 20).arg(i),
                  QStringLiteral("---\ntags: [tag%1, shared]\npinned: %2\n"
                                 "---\n%3")
                      .arg(i % 7)
                      .arg(i % 9 == 0 ? "true" : "false")
                      .arg(body));
    }

    NoteCollection collection;
    KvitOpTimer timer;
    QVERIFY(collection.openRoot(m_dir->path()));

    QCOMPARE(collection.noteCount(), 500);
    QVERIFY(collection.note("Folder3/Note 3.md"));

    // Budgeted in CPU time rather than wall-clock: the open is
    // single-threaded and CPU-bound, so its CPU cost tracks the scanning and
    // parsing work while wall-clock tracks how busy the machine is. The two
    // thresholds and the numbers behind them are explained in
    // tests/timingbudget.h; measured on this collection, unchanged code
    // costs ~190 ms of CPU idle and ~450 ms under heavy load, and a doubling
    // of the per-note parse costs ~372 ms and ~820 ms respectively.
    // Measured on the Linux dev machine (GCC, warm page cache): 875-893 ms
    // CPU across five runs, and 892 ms CPU against 897 ms wall - this
    // operation is CPU-bound, so its CPU time is essentially its wall time
    // rather than a fraction of it. The tight budget keeps the ~13% headroom
    // the historical 1000 ms wall-clock gate had; the ceiling is the point
    // past which no amount of contention explains the result.
    KVIT_ASSERT_CPU_BUDGET(timer, "collection.open 500-note", 1000.0, 2000.0);
}

// A read-only .kvit directory stands in for a full disk or a read-only
// mount: the sidecar writes fail while the notes themselves stay writable.
// FaultInjection::DeniedWrites restores the permissions on scope exit, so an
// assertion that fails below cannot leave the directory unwritable for the
// next test, and it skips rather than passing vacuously under a user that
// bypasses the permission bits.

void TestNoteCollection::testFailedIndexWriteKeepsIndexDirty()
{
    writeNote("A.md", "alpha\n");
    QVERIFY(m_collection->openRoot(m_dir->path()));
    const QString kvitDir = m_dir->filePath(".kvit");
    QVERIFY(QDir().mkpath(kvitDir));

    bool stillDirty = false;
    {
        FaultInjection::DeniedWrites denied(kvitDir);
        if (!denied.supported())
            QSKIP(qPrintable(denied.skipReason()));
        // The note itself is written; only the index sidecar fails.
        m_collection->setTags("A.md", {"one"});
        stillDirty = m_collection->indexDirtyForTesting();
    }

    QVERIFY2(stillDirty,
             "a failed index write cleared the dirty flag, so the change "
             "will never be retried");
}

void TestNoteCollection::testFailedCollectionWriteIsReported()
{
    writeNote("A.md", "alpha\n");
    QVERIFY(m_collection->openRoot(m_dir->path()));
    const QString kvitDir = m_dir->filePath(".kvit");
    QVERIFY(QDir().mkpath(kvitDir));

    QSignalSpy failedSpy(m_collection, &NoteCollection::operationFailed);
    int failures = 0;
    {
        FaultInjection::DeniedWrites denied(kvitDir);
        if (!denied.supported())
            QSKIP(qPrintable(denied.skipReason()));
        m_collection->setTagColor("one", "#ff0000"); // writes collection.json
        failures = failedSpy.count();
    }

    QVERIFY2(failures > 0,
             "a failed collection-state write was not surfaced to the user");
}

QTEST_MAIN(TestNoteCollection)
#include "test_notecollection.moc"
