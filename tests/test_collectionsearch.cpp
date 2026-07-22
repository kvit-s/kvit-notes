// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
#include <QtTest/QtTest>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QFile>
#include <QDir>
#include <QDirIterator>

#include "faultinjection.h"

#include "appcontext.h"
#include "notecollection.h"
#include "collectionsearch.h"
#include "collectionsearchindex.h"

#include <QElapsedTimer>
#include <QtSql/QSqlDatabase>
#include <QtSql/QSqlQuery>

namespace {

// Reach the index database the way a second process would: a connection of the
// test's own, outside everything the coordinator owns.
bool runRawSqlOn(const QString &dbPath, const QString &statement)
{
    static int counter = 0;
    const QString name =
        QStringLiteral("kvit_collectionsearch_raw_%1").arg(counter++);
    bool ok = false;
    {
        QSqlDatabase db =
            QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), name);
        db.setDatabaseName(dbPath);
        if (db.open()) {
            QSqlQuery query(db);
            ok = query.exec(statement);
            db.close();
        }
    }
    QSqlDatabase::removeDatabase(name);
    return ok;
}

// The change token the index has stored for one note, read the same way.
// -1 when the row is missing or the database cannot be opened.
qint64 storedChangeToken(const QString &dbPath, const QString &relPath)
{
    static int counter = 0;
    const QString name =
        QStringLiteral("kvit_collectionsearch_token_%1").arg(counter++);
    qint64 token = -1;
    {
        QSqlDatabase db =
            QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), name);
        db.setDatabaseName(dbPath);
        if (db.open()) {
            QSqlQuery query(db);
            query.prepare(QStringLiteral(
                "SELECT change_token FROM search_notes WHERE rel_path=?"));
            query.addBindValue(relPath);
            if (query.exec() && query.next())
                token = query.value(0).toLongLong();
            db.close();
        }
    }
    QSqlDatabase::removeDatabase(name);
    return token;
}

} // namespace

// Facade suite for global search. The query engine and its semantics are
// covered exhaustively by test_searchindexdb; this suite checks the
// QML-facing CollectionSearch: it wires the collection and the disk-backed
// index, runs queries off the GUI thread, keeps only the latest generation,
// exposes the same result shape and filters, updates live on saves, and maps a
// clicked result back to a Markdown cursor position. Queries are asynchronous,
// so results are awaited with QTRY_*.
class TestCollectionSearch : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void init();
    void cleanup();

    void testEmptyQueryIsInert();
    void testMatchesTitlesAndBodies();
    void testResultShapeAndOrder();
    void testWholeWordShortQuery();
    void testSnippetWindows();
    void testCodeBlockContentMatches();
    void testFolderScopeIsRecursive();
    void testTagFilter();
    void testDatePreset();
    void testFiltersCompose();
    void testLiveUpdateOnSave();
    void testRowCapIsVisibleNeverSilent();
    void testMarkdownPosition();
    void testRevisionContract();
    void testClearedQueryDropsInFlightResults();
    void testSupersededQueryNeverShowsStaleSnapshot();
    void testEqualLengthTagRenameReachesSearchIndex();
    void testRestoredRecoveryReachesSearchIndex();

    // SEARCH-1: two vaults open at once must not share connections.
    void testTwoIndexesOnTwoRootsStayApart();
    // SEARCH-4: an equal-size rewrite that kept its timestamp is reindexed.
    void testEqualSizeSameMtimeRewriteIsReindexed();
    // SEARCH-4, cost: a warm reconcile must not read a vault it has already
    // indexed, and must still catch the rewrite above.
    void testUnchangedNotesAreNotReadOnReconcile();
    void testRewriteImmediatelyAfterIndexingIsStillSeen();
    void testChangeTokenThatDoesNotMoveIsNotRecorded();
    void testWarmReconcileCostWithAndWithoutTheStamp();
    // SEARCH-5: a write the database refused is reported, not swallowed.
    void testFailedWriteMarksTheIndexDegraded();
    // SEARCH-7: the indexing flag describes the queue, not the worker's echo.
    void testIndexingIsTrueTheMomentReconcileIsQueued();
    void testQueuedReconcilesDoNotFlickerTheIndexingFlag();
    // SEARCH-5, facade half: a query the engine could not run must not be
    // shown as "no matches", the degraded state must reach QML, and the
    // rebuild must be reachable.
    void testFailedQueryKeepsThePreviousResults();
    void testDegradedStateIsVisibleToTheView();
    void testRebuildIndexRefillsFromDisk();
    // ARCH-4: switching vaults through the real composition must not park the
    // GUI thread behind the previous vault's reconcile and queries.
    void testVaultSwitchReleasesTheOldIndexWithoutBlocking();

private:
    void writeNote(const QString &relPath, const QString &content);
    QList<ReconcileEntry> listingFor(const QString &rootPath) const;
    // Reconcile the whole vault and return how long the pass took, measured
    // from the signal the coordinator emits when the last queued job finishes
    // rather than from a polling loop.
    qint64 timedReconcileUs(const QList<ReconcileEntry> &listing);
    // Wait until a query for `q` settles to the expected note count.
    void expectNoteCount(const QString &q, int expected);

    QTemporaryDir *m_dir = nullptr;
    NoteCollection *m_collection = nullptr;
    CollectionSearchIndex *m_index = nullptr;
    CollectionSearch *m_search = nullptr;
};

void TestCollectionSearch::initTestCase()
{
    QVERIFY2(CollectionSearchIndex::capabilityAvailable(),
             "SQLite FTS5 with the trigram tokenizer must be available");
}

void TestCollectionSearch::init()
{
    m_dir = new QTemporaryDir();
    QVERIFY(m_dir->isValid());

    writeNote("Fox notes.md",
              "The quick **brown fox** jumps\n\nA second fox block\n");
    writeNote("Recipes/Bread.md",
              "---\ntags: [cooking]\n---\nKnead the dough well\n\n"
              "Loaf recipe here\n");
    writeNote("Recipes/Soup/Stock.md",
              "Simmer bones for stock\n\nStock recipe notes\n\n"
              "```\nfox in a code block\n```\n");
    writeNote("Plain.md", "Nothing interesting here\n");

    m_collection = new NoteCollection();
    m_index = new CollectionSearchIndex();
    m_collection->setSearchIndex(m_index);
    QVERIFY(m_collection->openRoot(m_dir->path()));
    m_search = new CollectionSearch();
    m_search->setSearchIndex(m_index);
    m_search->setCollection(m_collection);
    // Let the cold reconcile finish before the assertions run.
    QTRY_VERIFY(!m_search->indexing());
}

void TestCollectionSearch::cleanup()
{
    delete m_search;
    delete m_collection; // closeRoot tears the index down
    delete m_index;
    delete m_dir;
    m_search = nullptr;
    m_collection = nullptr;
    m_index = nullptr;
    m_dir = nullptr;
}

void TestCollectionSearch::writeNote(const QString &relPath,
                                     const QString &content)
{
    QFileInfo info(m_dir->filePath(relPath));
    QVERIFY(QDir().mkpath(info.absolutePath()));
    QFile file(m_dir->filePath(relPath));
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write(content.toUtf8());
}

QList<ReconcileEntry> TestCollectionSearch::listingFor(
    const QString &rootPath) const
{
    // What the repository hands the index: one entry per note, with the
    // metadata a reconcile pass starts from.
    QList<ReconcileEntry> listing;
    QDirIterator it(rootPath, QStringList{QStringLiteral("*.md")}, QDir::Files,
                    QDirIterator::Subdirectories);
    const QDir root(rootPath);
    while (it.hasNext()) {
        const QFileInfo info(it.next());
        ReconcileEntry entry;
        entry.relPath = root.relativeFilePath(info.absoluteFilePath());
        entry.absPath = info.absoluteFilePath();
        entry.fileSize = info.size();
        entry.modifiedMs = info.lastModified().toMSecsSinceEpoch();
        listing.append(entry);
    }
    return listing;
}

qint64 TestCollectionSearch::timedReconcileUs(
    const QList<ReconcileEntry> &listing)
{
    QElapsedTimer timer;
    qint64 elapsedUs = -1;
    const auto connection =
        connect(m_index, &CollectionSearchIndex::indexingChanged, this,
                [this, &timer, &elapsedUs]() {
                    if (!m_index->isIndexing() && elapsedUs < 0)
                        elapsedUs = timer.nsecsElapsed() / 1000;
                });
    timer.start();
    m_index->reconcile(listing);
    QTest::qWait(0);
    while (m_index->isIndexing())
        QTest::qWait(1);
    disconnect(connection);
    return elapsedUs;
}

void TestCollectionSearch::expectNoteCount(const QString &q, int expected)
{
    m_search->setQuery(q);
    QTRY_COMPARE(m_search->noteCount(), expected);
}

void TestCollectionSearch::testEmptyQueryIsInert()
{
    QCOMPARE(m_search->noteCount(), 0);
    QCOMPARE(m_search->matchCount(), 0);
    QCOMPARE(m_search->results(), QVariantList());
}

void TestCollectionSearch::testMatchesTitlesAndBodies()
{
    m_search->setQuery(QStringLiteral("fox"));
    // "Fox notes" matches by title AND body; Stock.md by code content.
    QTRY_COMPARE(m_search->noteCount(), 2);

    const QVariantMap first = m_search->results().at(0).toMap();
    QCOMPARE(first.value("relPath").toString(), QStringLiteral("Fox notes.md"));
    QCOMPARE(first.value("titleMatched").toBool(), true);
    QCOMPARE(first.value("matches").toList().size(), 2);

    // Title-only match: still a result group, with no body matches.
    m_search->setQuery(QStringLiteral("bread"));
    QTRY_COMPARE(m_search->noteCount(), 1);
    const QVariantMap bread = m_search->results().at(0).toMap();
    QCOMPARE(bread.value("titleMatched").toBool(), true);
    QCOMPARE(bread.value("matches").toList().size(), 0);
    QCOMPARE(m_search->matchCount(), 0);
}

void TestCollectionSearch::testResultShapeAndOrder()
{
    m_search->setQuery(QStringLiteral("fox"));
    QTRY_COMPARE(m_search->noteCount(), 2);
    const QVariantMap group = m_search->results().at(0).toMap();
    // The QML-facing shape is preserved.
    for (const char *key : {"relPath", "title", "titleMatched", "matchCount",
                            "moreMatches", "matches"})
        QVERIFY(group.contains(QLatin1String(key)));
    const QVariantList matches = group.value("matches").toList();
    QCOMPARE(matches.size(), 2);
    const QVariantMap m0 = matches.at(0).toMap();
    for (const char *key : {"blockIndex", "start", "length", "snippet",
                            "snippetStart", "snippetLength"})
        QVERIFY(m0.contains(QLatin1String(key)));
    // Document order and display coordinates ("fox" at 16 with markers hidden).
    QCOMPARE(m0.value("blockIndex").toInt(), 0);
    QCOMPARE(m0.value("start").toInt(), 16);
    QCOMPARE(m0.value("length").toInt(), 3);
    QCOMPARE(matches.at(1).toMap().value("blockIndex").toInt(), 1);
    QCOMPARE(matches.at(1).toMap().value("start").toInt(), 9);
}

void TestCollectionSearch::testWholeWordShortQuery()
{
    // A two-character query is whole-word: "an" matches standalone words, not
    // substrings.
    writeNote("Words.md", "an ant and analysis\n");
    m_collection->refresh();
    m_search->setQuery(QStringLiteral("an"));
    QTRY_VERIFY(m_search->noteCount() >= 1);
    bool found = false;
    int matchesInWords = -1;
    for (const QVariant &g : m_search->results()) {
        const QVariantMap group = g.toMap();
        if (group.value("relPath").toString() == QStringLiteral("Words.md")) {
            found = true;
            matchesInWords = group.value("matchCount").toInt();
        }
    }
    QVERIFY(found);
    // Only the standalone "an" — not "ant", "and", or "analysis".
    QCOMPARE(matchesInWords, 1);
}

void TestCollectionSearch::testSnippetWindows()
{
    m_search->setQuery(QStringLiteral("fox"));
    QTRY_COMPARE(m_search->noteCount(), 2);
    const QVariantMap group = m_search->results().at(0).toMap();
    const QVariantMap m0 = group.value("matches").toList().at(0).toMap();
    QCOMPARE(m0.value("snippet").toString(),
             QStringLiteral("The quick brown fox jumps"));
    QCOMPARE(m0.value("snippetStart").toInt(), 16);
    QCOMPARE(m0.value("snippetLength").toInt(), 3);

    // A long line truncates around the match with ellipses.
    QString longLine;
    for (int i = 0; i < 30; ++i)
        longLine += QStringLiteral("word%1 ").arg(i);
    longLine += QStringLiteral("needle end of the line goes on and on");
    writeNote("Long.md", longLine + QStringLiteral("\n"));
    m_collection->refresh();
    m_search->setQuery(QStringLiteral("needle"));
    // QTRY_COMPARE on the new count (1), not >=1, so a stale prior-query
    // snapshot cannot satisfy the wait before this query lands.
    QTRY_COMPARE(m_search->noteCount(), 1);
    const QVariantMap longGroup = m_search->results().at(0).toMap();
    const QVariantMap match = longGroup.value("matches").toList().at(0).toMap();
    const QString snippet = match.value("snippet").toString();
    QVERIFY(snippet.startsWith(QStringLiteral("…")));
    QVERIFY(snippet.contains(QStringLiteral("needle")));
    QCOMPARE(snippet.mid(match.value("snippetStart").toInt(), 6),
             QStringLiteral("needle"));
}

void TestCollectionSearch::testCodeBlockContentMatches()
{
    m_search->setQuery(QStringLiteral("fox in a code"));
    QTRY_COMPARE(m_search->noteCount(), 1);
    const QVariantMap group = m_search->results().at(0).toMap();
    QCOMPARE(group.value("relPath").toString(),
             QStringLiteral("Recipes/Soup/Stock.md"));
    QCOMPARE(group.value("matches").toList().at(0).toMap()
                 .value("blockIndex").toInt(), 2);
}

void TestCollectionSearch::testFolderScopeIsRecursive()
{
    // "recipe" is in both Recipes notes (Bread and nested Stock).
    m_search->setQuery(QStringLiteral("recipe"));
    QTRY_COMPARE(m_search->noteCount(), 2);

    m_search->setFolderScope(QStringLiteral("Recipes"));
    QTRY_COMPARE(m_search->noteCount(), 2); // Bread + nested Stock

    m_search->setFolderScope(QStringLiteral("Recipes/Soup"));
    QTRY_COMPARE(m_search->noteCount(), 1);

    m_search->setFolderScope(QString());
    QTRY_COMPARE(m_search->noteCount(), 2);
}

void TestCollectionSearch::testTagFilter()
{
    m_search->setQuery(QStringLiteral("recipe"));
    m_search->setTagFilter(QStringLiteral("cooking"));
    QTRY_COMPARE(m_search->noteCount(), 1);
    QCOMPARE(m_search->results().at(0).toMap().value("relPath").toString(),
             QStringLiteral("Recipes/Bread.md"));
    m_search->setTagFilter(QString());
}

void TestCollectionSearch::testDatePreset()
{
    {
        QFile file(m_dir->filePath("Plain.md"));
        QVERIFY(file.open(QIODevice::ReadWrite));
        file.setFileTime(QDateTime::currentDateTime().addDays(-90),
                         QFileDevice::FileModificationTime);
    }
    m_collection->refresh();
    QTRY_VERIFY(!m_search->indexing());

    m_search->setQuery(QStringLiteral("interesting"));
    QTRY_COMPARE(m_search->noteCount(), 1); // "any"

    m_search->setDatePreset(QStringLiteral("month"));
    QTRY_COMPARE(m_search->noteCount(), 0); // 90 days old

    m_search->setDatePreset(QStringLiteral("year"));
    QTRY_COMPARE(m_search->noteCount(), 1);

    m_search->setDatePreset(QStringLiteral("today"));
    QTRY_COMPARE(m_search->noteCount(), 0);
    m_search->setDatePreset(QStringLiteral("any"));
}

void TestCollectionSearch::testFiltersCompose()
{
    m_search->setQuery(QStringLiteral("recipe"));
    m_search->setFolderScope(QStringLiteral("Recipes"));
    m_search->setTagFilter(QStringLiteral("cooking"));
    QTRY_COMPARE(m_search->noteCount(), 1);

    m_search->setFolderScope(QStringLiteral("Recipes/Soup"));
    QTRY_COMPARE(m_search->noteCount(), 0); // tag lives outside this folder
    m_search->setFolderScope(QString());
    m_search->setTagFilter(QString());
}

void TestCollectionSearch::testLiveUpdateOnSave()
{
    m_search->setQuery(QStringLiteral("dough"));
    QTRY_COMPARE(m_search->matchCount(), 1);

    // A save-path change (noteSaved) updates results live.
    writeNote("Recipes/Bread.md",
              "---\ntags: [cooking]\n---\nNo more kneading\n");
    m_collection->noteSaved(m_dir->filePath("Recipes/Bread.md"));
    QTRY_COMPARE(m_search->matchCount(), 0);

    // New notes join the corpus after a rescan.
    writeNote("Dough.md", "dough dough\n");
    m_collection->refresh();
    QTRY_COMPARE(m_search->matchCount(), 2);
}

void TestCollectionSearch::testRowCapIsVisibleNeverSilent()
{
    QString body;
    for (int i = 0; i < 15; ++i)
        body += QStringLiteral("line %1 has a needle in it\n\n").arg(i);
    writeNote("Haystack.md", body);
    m_collection->refresh();

    m_search->setQuery(QStringLiteral("needle"));
    QTRY_COMPARE(m_search->matchCount(), 15);
    const QVariantMap group = m_search->results().at(0).toMap();
    QCOMPARE(group.value("matchCount").toInt(), 15);
    QCOMPARE(group.value("matches").toList().size(), 10);
    QCOMPARE(group.value("moreMatches").toInt(), 5);
}

void TestCollectionSearch::testMarkdownPosition()
{
    // Display position 16 ("fox" after "The quick brown ") maps into the
    // markdown, which carries the hidden "**" before it. markdownPosition
    // reads and parses the note file directly.
    QCOMPARE(m_search->markdownPosition(QStringLiteral("Fox notes.md"), 0, 16),
             18);
    QCOMPARE(m_search->markdownPosition(QStringLiteral("Fox notes.md"), 1, 9),
             9);
    // Code blocks are verbatim: identity.
    QCOMPARE(m_search->markdownPosition(
                 QStringLiteral("Recipes/Soup/Stock.md"), 2, 4), 4);
    // Unknown notes and out-of-range blocks degrade to 0.
    QCOMPARE(m_search->markdownPosition(QStringLiteral("missing.md"), 0, 5), 0);
    QCOMPARE(m_search->markdownPosition(QStringLiteral("Plain.md"), 9, 5), 0);
}

void TestCollectionSearch::testRevisionContract()
{
    QSignalSpy revisionSpy(m_search, &CollectionSearch::revisionChanged);

    m_search->setQuery(QStringLiteral("fox"));
    QTRY_COMPARE(revisionSpy.count(), 1);

    // Same query again: setQuery short-circuits, no new work, no bump.
    m_search->setQuery(QStringLiteral("fox"));
    QTest::qWait(250);
    QCOMPARE(revisionSpy.count(), 1);

    // A collection change that does not affect the results: recompute runs but
    // produces the same snapshot, so no bump.
    m_collection->setFolderExpanded(QStringLiteral("Recipes"), false);
    QTest::qWait(250);
    QCOMPARE(revisionSpy.count(), 1);

    // Clearing the query empties the results: bump.
    m_search->setQuery(QString());
    QTRY_COMPARE(revisionSpy.count(), 2);
}

void TestCollectionSearch::testClearedQueryDropsInFlightResults()
{
    // Warm the index first: a query submitted before the cold reconcile
    // lands answers from an empty database and could not repopulate
    // anything, which would make this test pass for the wrong reason.
    m_search->setQuery(QStringLiteral("fox"));
    QTRY_COMPARE(m_search->noteCount(), 2);
    m_search->setQuery(QString());
    QTRY_COMPARE(m_search->noteCount(), 0);

    // Record every snapshot the view is told to render, so a result that
    // appears and is then cleared again is still caught.
    QList<int> published;
    connect(m_search, &CollectionSearch::revisionChanged, this,
            [this, &published]() { published << m_search->noteCount(); });

    // Submit, then clear the input before the reply can be delivered: the
    // reply belongs to an input that no longer exists.
    m_search->setQuery(QStringLiteral("fox"));
    m_search->submitNow(); // queued to the read thread; no reply yet
    m_search->setQuery(QString());
    QCOMPARE(m_search->noteCount(), 0);

    // Give the in-flight reply every chance to land.
    QTest::qWait(300);
    QVERIFY2(!published.contains(2),
             "the cleared query was repopulated by an in-flight reply");
    QCOMPARE(m_search->noteCount(), 0);
    QCOMPARE(m_search->matchCount(), 0);
    QCOMPARE(m_search->results(), QVariantList());
}

void TestCollectionSearch::testSupersededQueryNeverShowsStaleSnapshot()
{
    // Warm the index so the superseded reply carries real results.
    m_search->setQuery(QStringLiteral("fox"));
    QTRY_COMPARE(m_search->noteCount(), 2);
    m_search->setQuery(QString());
    QTRY_COMPARE(m_search->noteCount(), 0);

    // Every snapshot the view is told to render, in order.
    QStringList seen;
    connect(m_search, &CollectionSearch::revisionChanged, this, [this, &seen]() {
        const QVariantList groups = m_search->results();
        for (const QVariant &group : groups)
            seen << group.toMap().value(QStringLiteral("relPath")).toString();
    });

    m_search->setQuery(QStringLiteral("fox"));
    m_search->submitNow();                       // "fox" is in flight
    m_search->setQuery(QStringLiteral("bread")); // superseded before it lands

    QTRY_COMPARE(m_search->noteCount(), 1);
    QCOMPARE(m_search->results().at(0).toMap().value("relPath").toString(),
             QStringLiteral("Recipes/Bread.md"));
    QVERIFY2(!seen.contains(QStringLiteral("Fox notes.md")),
             "a superseded query's results were displayed");
}

void TestCollectionSearch::testEqualLengthTagRenameReachesSearchIndex()
{
    // A same-length rename leaves the file size unchanged, and the
    // front-matter rewrite deliberately restores the mtime, so neither
    // freshness key moves. The search index must still be told.
    writeNote(QStringLiteral("Library.md"),
              QStringLiteral("---\ntags: [books]\n---\nShelf contents here\n"));
    QVERIFY(m_collection->openRoot(m_dir->path()));
    QTRY_VERIFY(!m_search->indexing());

    m_search->setTagFilter(QStringLiteral("books"));
    m_search->setQuery(QStringLiteral("shelf"));
    QTRY_COMPARE(m_search->noteCount(), 1);

    QVERIFY(m_collection->renameTag(QStringLiteral("books"),
                                    QStringLiteral("draft")));
    QTRY_VERIFY(!m_search->indexing());

    // The new tag matches.
    m_search->setTagFilter(QStringLiteral("draft"));
    m_search->setQuery(QStringLiteral("shelf"));
    QTRY_COMPARE(m_search->noteCount(), 1);

    // The old tag does not.
    m_search->setTagFilter(QStringLiteral("books"));
    m_search->setQuery(QStringLiteral("shelf"));
    QTest::qWait(300);
    QCOMPARE(m_search->noteCount(), 0);
}

void TestCollectionSearch::testRestoredRecoveryReachesSearchIndex()
{
    // Restoring a crash journal rewrites the note on disk; global search
    // must see the restored content, not the pre-crash text.
    const QString journal = m_collection->journalPathFor(
        QStringLiteral("Plain.md"));
    QVERIFY(!journal.isEmpty());
    {
        QFile file(journal);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write("Recovered zarfblat content\n");
    }
    // Pending recovery is detected when the collection opens.
    QVERIFY(m_collection->openRoot(m_dir->path()));
    QTRY_VERIFY(!m_search->indexing());

    QVERIFY(m_collection->restoreRecovery(QStringLiteral("Plain.md")));
    QTRY_VERIFY(!m_search->indexing());

    m_search->setQuery(QStringLiteral("zarfblat"));
    QTRY_COMPARE(m_search->noteCount(), 1);
    QCOMPARE(m_search->results().at(0).toMap().value("relPath").toString(),
             QStringLiteral("Plain.md"));
}

void TestCollectionSearch::testTwoIndexesOnTwoRootsStayApart()
{
    // Two vaults open at once. The connection names used to be fixed strings,
    // so the second index replaced the first one's entry in Qt's global
    // registry: the two coordinators then addressed one database between them,
    // and closing either one unregistered the connection the other was using.
    QTemporaryDir other;
    QVERIFY(other.isValid());
    {
        QFile file(other.filePath(QStringLiteral("Other.md")));
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write("zarfblat lives only here\n");
    }

    CollectionSearchIndex secondIndex;
    secondIndex.openForRoot(other.path());
    QVERIFY(secondIndex.isUsable());
    QVERIFY(m_index->isUsable());

    QSignalSpy secondIndexed(&secondIndex,
                             &CollectionSearchIndex::indexingChanged);
    secondIndex.reconcile(listingFor(other.path()));
    QTRY_VERIFY(!secondIndex.isIndexing());

    // The first vault still answers, and answers about its own notes.
    m_search->setQuery(QStringLiteral("fox"));
    QTRY_COMPARE(m_search->noteCount(), 2);
    m_search->setQuery(QStringLiteral("zarfblat"));
    QTest::qWait(200);
    QCOMPARE(m_search->noteCount(), 0);

    // And the second vault holds exactly its own note.
    QSignalSpy replies(&secondIndex, &CollectionSearchIndex::queryFinished);
    SearchQuery request;
    request.query = QStringLiteral("zarfblat");
    request.nowMs = QDateTime::currentMSecsSinceEpoch();
    secondIndex.submitQuery(1, request);
    QTRY_COMPARE(replies.count(), 1);
    const SearchResults results =
        replies.at(0).at(1).value<SearchResults>();
    QVERIFY(results.ok);
    QCOMPARE(results.noteCount, 1);

    // Closing the second must leave the first attached and working.
    secondIndex.closeIndex();
    QVERIFY(m_index->isUsable());
    m_search->setQuery(QStringLiteral("fox"));
    QTRY_COMPARE(m_search->noteCount(), 2);
}

void TestCollectionSearch::testEqualSizeSameMtimeRewriteIsReindexed()
{
    // The two bodies are the same length, and the rewrite restores the
    // original modification time, so neither of the metadata keys the index
    // used to rely on moves. Same-length tag renames and timestamp-preserving
    // tools produce exactly this.
    const QString before = QStringLiteral("orbital mechanics notes\n");
    const QString after = QStringLiteral("orbital zarfblats notes\n");
    QCOMPARE(before.toUtf8().size(), after.toUtf8().size());

    writeNote(QStringLiteral("Stale.md"), before);
    m_collection->refresh();
    QTRY_VERIFY(!m_search->indexing());
    m_search->setQuery(QStringLiteral("mechanics"));
    QTRY_COMPARE(m_search->noteCount(), 1);

    const QString path = m_dir->filePath(QStringLiteral("Stale.md"));
    const QDateTime originalMtime = QFileInfo(path).lastModified();
    writeNote(QStringLiteral("Stale.md"), after);
    {
        QFile file(path);
        QVERIFY(file.open(QIODevice::ReadWrite));
        QVERIFY(file.setFileTime(originalMtime,
                                 QFileDevice::FileModificationTime));
    }
    QCOMPARE(QFileInfo(path).size(), qint64(before.toUtf8().size()));
    QCOMPARE(QFileInfo(path).lastModified(), originalMtime);

    // A plain rescan must notice. Nothing here tells the index which note
    // changed; reconcile has to work it out from the files.
    m_collection->refresh();
    QTRY_VERIFY(!m_search->indexing());

    m_search->setQuery(QStringLiteral("zarfblats"));
    QTRY_COMPARE(m_search->noteCount(), 1);
    m_search->setQuery(QStringLiteral("mechanics"));
    QTest::qWait(200);
    QCOMPARE(m_search->noteCount(), 0);
}

// Deciding freshness on the file's content means reading the file, and a
// reconcile runs over the whole vault at every warm start. The stat-only tier
// exists so that reading is confined to notes that a cheap and trustworthy
// test says may have changed; these two cases fix that it skips what it should
// and reads what it must.
void TestCollectionSearch::testUnchangedNotesAreNotReadOnReconcile()
{
    const QList<ReconcileEntry> listing = listingFor(m_dir->path());
    // Two things have to happen before a note is on the stat-only path. Its
    // change token has to be recorded, which takes one pass that reads it — a
    // note the application saved through the live feed has none, because that
    // path is handed text rather than a path. And the file has to have been
    // quiet for longer than the settling window, or the token is deliberately
    // not recorded at all. The fixture wrote these notes a moment ago, so the
    // wait is what makes the pass after it record anything.
    QTest::qWait(int(CollectionSearchIndex::changeTokenSettleMs()) + 150);
    m_index->reconcile(listing);
    QTRY_VERIFY(!m_index->isIndexing());

    SearchIndexOps::reset();
    m_index->reconcile(listing);
    QTRY_VERIFY(!m_index->isIndexing());
    if (CollectionSearchIndex::changeTokenIsTrustworthy()) {
        QCOMPARE(SearchIndexOps::fileReads(), quint64(0));
        QCOMPARE(SearchIndexOps::fingerprints(), quint64(0));
    } else {
        // No change token on this platform: correctness is unchanged and every
        // note is read, which is what the fingerprint costs where nothing
        // cheaper can be trusted.
        QCOMPARE(SearchIndexOps::fileReads(), quint64(listing.size()));
    }

    // The hole this must not reopen. Same length, same modification time,
    // different bytes: the pair the index used to trust says nothing moved.
    const QString before = QStringLiteral("orbital mechanics notes\n");
    const QString after = QStringLiteral("orbital zarfblats notes\n");
    QCOMPARE(before.toUtf8().size(), after.toUtf8().size());
    writeNote(QStringLiteral("Stale.md"), before);
    QTest::qWait(int(CollectionSearchIndex::changeTokenSettleMs()) + 150);
    m_index->reconcile(listingFor(m_dir->path()));
    QTRY_VERIFY(!m_index->isIndexing());

    // The note has to be genuinely on the fast path before the rewrite, or
    // what follows proves nothing: a note with no recorded token is read
    // whatever happens to it, and the test would pass without the fast path
    // ever having had the chance to skip a changed file.
    SearchIndexOps::reset();
    m_index->reconcile(listingFor(m_dir->path()));
    QTRY_VERIFY(!m_index->isIndexing());
    if (CollectionSearchIndex::changeTokenIsTrustworthy()) {
        QVERIFY2(SearchIndexOps::fileReads() == quint64(0),
                 "the note under test never reached the stat-only path, so "
                 "the rewrite below would not exercise it");
    }

    const QString path = m_dir->filePath(QStringLiteral("Stale.md"));
    const QDateTime originalMtime = QFileInfo(path).lastModified();
    writeNote(QStringLiteral("Stale.md"), after);
    {
        QFile file(path);
        QVERIFY(file.open(QIODevice::ReadWrite));
        QVERIFY(file.setFileTime(originalMtime,
                                 QFileDevice::FileModificationTime));
    }
    QCOMPARE(QFileInfo(path).size(), qint64(before.toUtf8().size()));
    QCOMPARE(QFileInfo(path).lastModified(), originalMtime);

    SearchIndexOps::reset();
    m_index->reconcile(listingFor(m_dir->path()));
    QTRY_VERIFY(!m_index->isIndexing());
    QVERIFY2(SearchIndexOps::fileReads() >= quint64(1),
             "the rewritten note was skipped on its metadata, which is the "
             "defect the fingerprint was introduced to close");
    m_search->setQuery(QStringLiteral("zarfblats"));
    QTRY_COMPARE(m_search->noteCount(), 1);
    m_search->setQuery(QStringLiteral("mechanics"));
    QTest::qWait(200);
    QCOMPARE(m_search->noteCount(), 0);

    // And the notes that did not change were still left unread.
    if (CollectionSearchIndex::changeTokenIsTrustworthy())
        QCOMPARE(SearchIndexOps::fileReads(), quint64(1));
}

// The regression that shipped, and what the settling window is for.
//
// A change token is a timestamp truncated to some granularity, and the first
// version of this recorded one the instant it read it. Two writes inside one
// granule then carry the same token, so an equal-size rewrite that restored the
// modification time looked identical to the file that had been indexed and its
// new text never reached the index. It was not a narrow race: measured on ext4
// under Linux 6.18, consecutive writes are separated by 40 to 130 microseconds,
// which Qt reports to the millisecond as no separation at all, and a tight loop
// of this sequence left stale text indexed in 283 rounds out of 300.
//
// The rule that closes it is about what may be *recorded*, so that is what this
// checks, and it holds whatever the machine is doing: a note that was written a
// moment ago must leave the index with no change token at all, because any
// token read that soon is one a second write can still reproduce. The code this
// replaces stored the file's change time here unconditionally, so the first
// assertion below fails against it on every run rather than one in six.
void TestCollectionSearch::testRewriteImmediatelyAfterIndexingIsStillSeen()
{
    const QString dbPath =
        CollectionSearchIndex::databasePathForRoot(m_dir->path());
    const QString path = m_dir->filePath(QStringLiteral("Churn.md"));
    const QString before = QStringLiteral("zzalpha orbital notes\n");
    const QString after = QStringLiteral("zzbetaa orbital notes\n");
    QCOMPARE(before.toUtf8().size(), after.toUtf8().size());

    writeNote(QStringLiteral("Churn.md"), before);
    m_index->reconcile(listingFor(m_dir->path()));
    QTRY_VERIFY(!m_index->isIndexing());
    QVERIFY(storedChangeToken(dbPath, QStringLiteral("Churn.md")) >= 0);
    QCOMPARE(storedChangeToken(dbPath, QStringLiteral("Churn.md")), qint64(0));

    // With no token recorded there is nothing for the stat-only path to match,
    // so the rewrite is seen however close behind the first write it lands.
    const QDateTime originalMtime = QFileInfo(path).lastModified();
    writeNote(QStringLiteral("Churn.md"), after);
    {
        QFile file(path);
        QVERIFY(file.open(QIODevice::ReadWrite));
        QVERIFY(file.setFileTime(originalMtime,
                                 QFileDevice::FileModificationTime));
    }
    QCOMPARE(QFileInfo(path).size(), qint64(before.toUtf8().size()));
    QCOMPARE(QFileInfo(path).lastModified(), originalMtime);

    SearchIndexOps::reset();
    m_index->reconcile(listingFor(m_dir->path()));
    QTRY_VERIFY(!m_index->isIndexing());
    QVERIFY2(SearchIndexOps::fileReads() >= quint64(1),
             "a note rewritten immediately after it was indexed was skipped on "
             "a change token that had not had time to become one");
    m_search->setQuery(QStringLiteral("zzbetaa"));
    QTRY_COMPARE(m_search->noteCount(), 1);
}

// The fast path has to survive a filesystem whose change time is not a change
// time. FAT and exFAT report the file's creation time there, so it does not
// move when the file is written, and a token that does not move is one an
// equal-size rewrite can hide behind forever. Nothing here needs to know which
// filesystem it is on: a token is recorded only when it is strictly greater
// than the one already stored, so a change time that stands still is never
// recorded and those notes stay on the fingerprint path.
//
// A stored token far in the future is the same situation from the index's
// side, and it is the one that can be staged without a FAT volume.
void TestCollectionSearch::testChangeTokenThatDoesNotMoveIsNotRecorded()
{
    const QString dbPath =
        CollectionSearchIndex::databasePathForRoot(m_dir->path());
    writeNote(QStringLiteral("Frozen.md"),
              QStringLiteral("earlier body of the zzfrozen note\n"));
    QTest::qWait(int(CollectionSearchIndex::changeTokenSettleMs()) + 150);
    m_index->reconcile(listingFor(m_dir->path()));
    QTRY_VERIFY(!m_index->isIndexing());

    // Every token this note can ever report is now smaller than what the index
    // holds, which is exactly what a change time that never advances looks
    // like from here.
    QVERIFY(runRawSqlOn(dbPath,
                        QStringLiteral("UPDATE search_notes SET "
                                       "change_token=4102444800000 "
                                       "WHERE rel_path='Frozen.md'")));

    writeNote(QStringLiteral("Frozen.md"),
              QStringLiteral("later body of the zzthawed note\n"));
    QTest::qWait(int(CollectionSearchIndex::changeTokenSettleMs()) + 150);
    m_index->reconcile(listingFor(m_dir->path()));
    QTRY_VERIFY(!m_index->isIndexing());

    // The new text is indexed, and the index refused to write down a token it
    // could not show had moved.
    m_search->setQuery(QStringLiteral("zzthawed"));
    QTRY_COMPARE(m_search->noteCount(), 1);
    QCOMPARE(storedChangeToken(dbPath, QStringLiteral("Frozen.md")),
             qint64(0));

    // So the note stays on the fingerprint path: the next pass reads it rather
    // than trusting a stamp that proved nothing.
    SearchIndexOps::reset();
    m_index->reconcile(listingFor(m_dir->path()));
    QTRY_VERIFY(!m_index->isIndexing());
    QVERIFY(SearchIndexOps::fileReads() >= quint64(1));
}

void TestCollectionSearch::testWarmReconcileCostWithAndWithoutTheStamp()
{
    // A vault large enough for the per-note cost to be what the measurement
    // sees. Each note is roughly a kilobyte of ordinary prose.
    static const int kNotes = 2000;
    for (int i = 0; i < kNotes; ++i) {
        QString body;
        for (int line = 0; line < 8; ++line) {
            body += QStringLiteral(
                        "Paragraph %1 of note %2 with enough prose in it to "
                        "cost something to read and hash\n\n")
                        .arg(line)
                        .arg(i);
        }
        writeNote(QStringLiteral("Bench/Note %1.md").arg(i), body);
    }
    const QList<ReconcileEntry> listing = listingFor(m_dir->path());
    QCOMPARE(listing.size(), kNotes + 4); // the fixture's four notes as well

    // Cold build, then one settling pass once the files are old enough for
    // their change tokens to be recorded, so what follows measures a warm
    // start over a vault the index already holds.
    m_index->reconcile(listing);
    QTRY_VERIFY(!m_index->isIndexing());
    QTest::qWait(int(CollectionSearchIndex::changeTokenSettleMs()) + 150);
    m_index->reconcile(listing);
    QTRY_VERIFY(!m_index->isIndexing());

    SearchIndexOps::reset();
    const qint64 withStampUs = timedReconcileUs(listing);
    const quint64 readsWithStamp = SearchIndexOps::fileReads();
    if (CollectionSearchIndex::changeTokenIsTrustworthy())
        QCOMPARE(readsWithStamp, quint64(0));

    // The same pass with the stored change tokens erased, which is how every
    // platform behaved before there was one and how Windows still behaves:
    // every note read, every note hashed, nothing reparsed. It also does one
    // UPDATE per note to record the token it just learned, which the earlier
    // code did not, so this figure is a slight overestimate of that code.
    QVERIFY(runRawSqlOn(CollectionSearchIndex::databasePathForRoot(
                            m_dir->path()),
                        QStringLiteral(
                            "UPDATE search_notes SET change_token=0")));
    SearchIndexOps::reset();
    const qint64 withoutStampUs = timedReconcileUs(listing);
    const quint64 readsWithoutStamp = SearchIndexOps::fileReads();
    QCOMPARE(readsWithoutStamp, quint64(listing.size()));
    QCOMPARE(SearchIndexOps::fingerprints(), quint64(listing.size()));

    // The read and the hash on their own, over the same files, so the saving
    // can be read without the database work either pass also does.
    QElapsedTimer readTimer;
    readTimer.start();
    for (const ReconcileEntry &entry : listing) {
        const CollectionSearchIndex::NoteSnapshot snapshot =
            CollectionSearchIndex::readNoteSnapshot(entry.absPath);
        QVERIFY(snapshot.ok);
        SearchIndexDb::contentFingerprint(snapshot.text);
    }
    const qint64 readHashUs = readTimer.nsecsElapsed() / 1000;

    qInfo("RECONCILE %lld notes warm: %.0f ms reading %llu files, %.0f ms "
          "reading %llu files; the reads and hashes alone are %.0f ms",
          qint64(listing.size()), withStampUs / 1000.0, readsWithStamp,
          withoutStampUs / 1000.0, readsWithoutStamp, readHashUs / 1000.0);
}

void TestCollectionSearch::testFailedWriteMarksTheIndexDegraded()
{
    QVERIFY(!m_index->isDegraded());
    const QString dbPath =
        CollectionSearchIndex::databasePathForRoot(m_dir->path());
    QVERIFY(QFileInfo::exists(dbPath));

    QSignalSpy degraded(m_index, &CollectionSearchIndex::degradedChanged);
    {
        // Nothing may grow: the write has nowhere to go. The index used to
        // report this as an ordinary successful update, leaving the search
        // results confidently wrong.
        FaultInjection::FileSizeLimit capped(
            qMax(QFileInfo(dbPath).size(),
                 QFileInfo(dbPath + QStringLiteral("-wal")).size()));
        if (!capped.supported())
            QSKIP(qPrintable(capped.skipReason()));
        m_index->replaceFromText(QStringLiteral("Blocked.md"),
                                 QStringLiteral("content that cannot land\n"),
                                 25, 0);
        QTRY_COMPARE(degraded.count(), 1);
        QVERIFY(m_index->isDegraded());
    }

    // Rebuilding is the way back: it discards the index that could not be
    // trusted and leaves an empty one for the caller to refill.
    QVERIFY(m_index->rebuildIndex());
    QVERIFY(!m_index->isDegraded());
    QVERIFY(m_index->isUsable());
    m_index->reconcile(listingFor(m_dir->path()));
    QTRY_VERIFY(!m_index->isIndexing());
    m_search->setQuery(QStringLiteral("fox"));
    QTRY_COMPARE(m_search->noteCount(), 2);
}

void TestCollectionSearch::testIndexingIsTrueTheMomentReconcileIsQueued()
{
    QVERIFY(!m_index->isIndexing());
    m_index->reconcile(listingFor(m_dir->path()));
    // Synchronously, before the event loop has run at all. Waiting for the
    // worker to announce itself left a window in which a caller — or a test —
    // could see an idle index that had a full vault queued behind it.
    QVERIFY2(m_index->isIndexing(),
             "the index reported itself idle with a reconcile already queued");
    QTRY_VERIFY(!m_index->isIndexing());
}

void TestCollectionSearch::testQueuedReconcilesDoNotFlickerTheIndexingFlag()
{
    QSignalSpy changes(m_index, &CollectionSearchIndex::indexingChanged);
    const QList<ReconcileEntry> listing = listingFor(m_dir->path());
    m_index->reconcile(listing);
    m_index->reconcile(listing);
    m_index->reconcile(listing);
    QCOMPARE(changes.count(), 1); // false -> true, once
    QVERIFY(m_index->isIndexing());

    QTRY_VERIFY(!m_index->isIndexing());
    // Exactly one transition each way for the whole batch: three jobs used to
    // produce three start/finish pairs, and the index looked idle between
    // them, which is a moment where a query can be published as a complete
    // answer over a half-built index.
    QCOMPARE(changes.count(), 2);
}

// A reply carrying ok == false means "the index could not tell you", which is
// a different answer from "no matches". The facade applied it like any other
// reply, so a database error silently emptied the result list and the reader
// was told, with no caveat, that their vault contains nothing matching.
void TestCollectionSearch::testFailedQueryKeepsThePreviousResults()
{
    // Learn the generation of a real reply, which is still the generation the
    // facade is showing once the query has settled.
    quint64 lastGeneration = 0;
    connect(m_index, &CollectionSearchIndex::queryFinished, this,
            [&lastGeneration](quint64 generation, SearchResults) {
                lastGeneration = generation;
            });

    m_search->setQuery(QStringLiteral("fox"));
    QTRY_COMPARE(m_search->noteCount(), 2);
    QVERIFY(lastGeneration != 0);
    QVERIFY(!m_search->lastQueryFailed());
    const int revisionBefore = m_search->revision();

    // The engine fails on the next attempt at the same question.
    QSignalSpy failedSpy(m_search, &CollectionSearch::lastQueryFailedChanged);
    SearchResults failure;
    failure.ok = false;
    emit m_index->queryFinished(lastGeneration, failure);

    QCOMPARE(m_search->noteCount(), 2);
    QCOMPARE(m_search->revision(), revisionBefore);
    QVERIFY2(m_search->lastQueryFailed(),
             "a query the engine could not run must be reported, not shown "
             "as an empty result set");
    QCOMPARE(failedSpy.count(), 1);

    // A later successful reply clears the flag and publishes normally.
    m_search->setQuery(QStringLiteral("stock"));
    QTRY_COMPARE(m_search->noteCount(), 1);
    QVERIFY(!m_search->lastQueryFailed());
}

void TestCollectionSearch::testDegradedStateIsVisibleToTheView()
{
    QVERIFY(!m_search->degraded());
    QSignalSpy degradedSpy(m_search, &CollectionSearch::degradedChanged);

    const QString dbPath =
        CollectionSearchIndex::databasePathForRoot(m_dir->path());
    {
        FaultInjection::FileSizeLimit capped(
            qMax(QFileInfo(dbPath).size(),
                 QFileInfo(dbPath + QStringLiteral("-wal")).size()));
        if (!capped.supported())
            QSKIP(qPrintable(capped.skipReason()));
        m_index->replaceFromText(QStringLiteral("Blocked.md"),
                                 QStringLiteral("content that cannot land\n"),
                                 25, 0);
        QTRY_VERIFY(m_search->degraded());
    }
    QVERIFY(degradedSpy.count() >= 1);
}

void TestCollectionSearch::testRebuildIndexRefillsFromDisk()
{
    const QString dbPath =
        CollectionSearchIndex::databasePathForRoot(m_dir->path());
    {
        FaultInjection::FileSizeLimit capped(
            qMax(QFileInfo(dbPath).size(),
                 QFileInfo(dbPath + QStringLiteral("-wal")).size()));
        if (!capped.supported())
            QSKIP(qPrintable(capped.skipReason()));
        m_index->replaceFromText(QStringLiteral("Blocked.md"),
                                 QStringLiteral("content that cannot land\n"),
                                 25, 0);
        QTRY_VERIFY(m_search->degraded());
    }

    // The recovery action the view offers: the empty index rebuildIndex()
    // leaves behind is refilled here, not left for the next incidental scan.
    QVERIFY(m_search->rebuildIndex());
    QVERIFY(!m_search->degraded());
    QTRY_VERIFY(!m_search->indexing());

    m_search->setQuery(QStringLiteral("fox"));
    QTRY_COMPARE(m_search->noteCount(), 2);
}

// ARCH-4. The shipped composition always attaches the search index, and
// opening a vault's index is two BlockingQueuedConnection calls onto its
// database worker threads. Switching straight from one vault to another
// reached those calls with the vault being left still reconciling and still
// answering queries, so the GUI thread waited on that worker queue.
//
// The fix is that the composition gives up the old vault's index first, with
// requestClose(), which returns immediately. This composes the real
// AppContext, because the previous responsiveness test used a bare
// NoteCollection and so never touched this path at all.
void TestCollectionSearch::testVaultSwitchReleasesTheOldIndexWithoutBlocking()
{
    QTemporaryDir first;
    QTemporaryDir second;
    QVERIFY(first.isValid() && second.isValid());

    auto seed = [](const QString &root, int count, const QString &word) {
        for (int i = 0; i < count; ++i) {
            const QString path =
                QDir(root).filePath(QStringLiteral("Note%1.md").arg(i));
            QFile file(path);
            QVERIFY(file.open(QIODevice::WriteOnly));
            file.write(QStringLiteral("# Note %1\n\n%2 body %1\n")
                           .arg(i).arg(word).toUtf8());
        }
    };
    // Enough notes in the vault being left that its cold reconcile is real
    // work rather than something that finishes between two statements.
    seed(first.path(), 400, QStringLiteral("alpha"));
    seed(second.path(), 3, QStringLiteral("omega"));

    AppContext::Options options;
    options.showSystemTray = false;
    options.configureLoggingFromSettings = false;
    AppContext context(options);
    QTemporaryDir config;
    context.openSettings(config.filePath(QStringLiteral("settings.json")));

    NoteCollection *collection = context.noteCollection();
    CollectionSearchIndex *index = context.searchIndex();
    CollectionSearch *search = context.collectionSearch();

    QVERIFY(collection->openRootAsync(first.path()));
    // Wait until the first vault's reconcile is actually queued and running.
    QTRY_VERIFY_WITH_TIMEOUT(index->isIndexing(), 15000);

    // And a query is in flight against it at the same time.
    search->setQuery(QStringLiteral("alpha"));
    search->submitNow();

    QSignalSpy usableSpy(index, &CollectionSearchIndex::usableChanged);

    QElapsedTimer timer;
    timer.start();
    QVERIFY(context.openVaultRoot(second.path()));
    const qint64 elapsedMs = timer.elapsed();

    // The observable difference: the vault being left had its index RELEASED
    // (usable -> false) before the next one's database was opened over the
    // top of it (false -> true). Opening straight through leaves usable true
    // throughout and emits nothing.
    QVERIFY2(usableSpy.count() >= 2,
             qPrintable(QStringLiteral("the old vault's index was not "
                                       "released before the new one opened "
                                       "(usableChanged fired %1 times)")
                            .arg(usableSpy.count())));
    QVERIFY2(elapsedMs < 2000,
             qPrintable(QStringLiteral("switching vaults blocked the calling "
                                       "thread for %1 ms").arg(elapsedMs)));

    QCOMPARE(collection->rootPath(), QDir(second.path()).absolutePath());

    // The new vault is genuinely searchable afterwards, and the old one's
    // content is gone from the results rather than lingering.
    QTRY_VERIFY_WITH_TIMEOUT(!index->isIndexing(), 20000);
    search->setQuery(QStringLiteral("omega"));
    QTRY_COMPARE_WITH_TIMEOUT(search->noteCount(), 3, 20000);
    search->setQuery(QStringLiteral("alpha"));
    QTRY_COMPARE_WITH_TIMEOUT(search->noteCount(), 0, 20000);
}

QTEST_MAIN(TestCollectionSearch)
#include "test_collectionsearch.moc"
