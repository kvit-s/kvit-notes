// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
#include <QtTest/QtTest>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QFile>
#include <QDir>

#include "notecollection.h"
#include "collectionsearch.h"
#include "collectionsearchindex.h"

// Facade suite for global search (search.md). The query engine and its
// semantics are covered exhaustively by test_searchindexdb; this suite checks
// the QML-facing CollectionSearch: it wires the collection and the disk-backed
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

private:
    void writeNote(const QString &relPath, const QString &content);
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
    QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Text));
    file.write(content.toUtf8());
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
    // The QML-facing shape is preserved (search.md §12 Phase 2).
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
    // substrings (search.md §4.2).
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
    // reads and parses the note file directly (search.md §9).
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

QTEST_MAIN(TestCollectionSearch)
#include "test_collectionsearch.moc"
