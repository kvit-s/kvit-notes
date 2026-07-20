// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
#include <QtTest/QtTest>

#include "timingbudget.h"

#include "collectionsearchindex.h"
#include "searchindexdb.h"

#include <QDate>
#include <QDateTime>

// Unit and differential-oracle suite for the SQLite FTS5 search engine. The
// engine is thread-affine, so every test drives one SearchIndexDb on the test
// thread synchronously. The differential oracle is the primary guard against
// tokenizer false negatives: it full-scans every indexed block with the
// reference matching semantics and asserts the FTS candidate plus refinement
// pipeline reproduces the same groups, offsets, and counts.
class TestSearchIndexDb : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void init();
    void cleanup();

    void testCapabilityProbe();
    void testUnicodeScalarRouting();
    void testFtsPhraseEscaping();
    void testWholeWordBoundaries();      // §4.2 table
    void testLongSubstring();            // §4.3 table
    void testPunctuationShortQueryInert();
    void testCodeBlockVerbatimAndPunctuation();
    void testTitleMatchesDoNotCountBody();
    void testIdenticalBlocksDistinctLocations();
    void testRowCapAndMoreMatches();
    void testFolderScopeEscapesWildcards();
    void testTagFilterComposes();
    void testDatePresetAndCustomRange();
    void testCaseAndDiacritics();
    void testReplaceAndRemoveKeepFtsConsistent();
    void testIndexRevisionIncrements();
    void testCorruptDatabaseRebuilds();
    void testDifferentialOracle();
    void testQueryPerformanceGate();

private:
    struct Note {
        QString relPath;
        QString markdown;
        qint64 modifiedMs;
    };

    void loadNote(const QString &relPath, const QString &markdown,
                  qint64 modifiedMs = 0);
    SearchResults run(const QString &query, const QString &folder = QString(),
                      const QString &tag = QString(),
                      const QString &datePreset = QStringLiteral("any"),
                      QDate from = QDate(), QDate to = QDate(),
                      qint64 nowMs = 0);

    // The reference matcher: a full scan of every stored block with section-4
    // semantics, independent of FTS.
    SearchResults oracle(const QString &query, qint64 nowMs = 0) const;

    SearchIndexDb *m_db = nullptr;
    QList<IndexedNote> m_notes; // mirror of what was loaded, for the oracle
};

void TestSearchIndexDb::initTestCase()
{
    QVERIFY2(SearchIndexDb::probeCapability(),
             "packaged SQLite must provide FTS5 with the trigram tokenizer");
}

void TestSearchIndexDb::init()
{
    m_db = new SearchIndexDb();
    QVERIFY(m_db->open(QStringLiteral(":memory:"),
                       QStringLiteral("kvit_search_test")));
    QVERIFY(m_db->isUsable());
    m_notes.clear();
}

void TestSearchIndexDb::cleanup()
{
    delete m_db;
    m_db = nullptr;
    m_notes.clear();
}

void TestSearchIndexDb::loadNote(const QString &relPath, const QString &markdown,
                                 qint64 modifiedMs)
{
    const IndexedNote note = CollectionSearchIndex::parseNote(
        relPath, markdown, markdown.toUtf8().size(), modifiedMs);
    QVERIFY(m_db->replaceNote(note));
    // Keep a parallel copy for the oracle (replacing any prior version).
    for (int i = 0; i < m_notes.size(); ++i) {
        if (m_notes.at(i).relPath == relPath) {
            m_notes[i] = note;
            return;
        }
    }
    m_notes.append(note);
}

SearchResults TestSearchIndexDb::run(const QString &query, const QString &folder,
                                     const QString &tag,
                                     const QString &datePreset, QDate from,
                                     QDate to, qint64 nowMs)
{
    SearchQuery q;
    q.query = query;
    q.folderScope = folder;
    q.tagFilter = tag;
    q.datePreset = datePreset;
    q.customFrom = from;
    q.customTo = to;
    q.nowMs = nowMs ? nowMs : QDateTime::currentMSecsSinceEpoch();
    return m_db->query(q);
}

SearchResults TestSearchIndexDb::oracle(const QString &query, qint64 nowMs) const
{
    Q_UNUSED(nowMs);
    SearchResults results;
    const QString effective = query.trimmed();
    if (effective.isEmpty())
        return results;
    const int scalars = SearchMatching::unicodeScalarCount(effective);
    const bool wholeWord = scalars <= 2;
    if (wholeWord && !SearchMatching::hasWordChar(SearchMatching::fold(effective)))
        return results;

    QList<IndexedNote> sorted = m_notes;
    std::sort(sorted.begin(), sorted.end(),
              [](const IndexedNote &a, const IndexedNote &b) {
                  return a.relPath < b.relPath;
              });
    static const int cap = 10;
    for (const IndexedNote &note : sorted) {
        SearchGroup group;
        group.relPath = note.relPath;
        group.title = note.title;
        group.titleMatched = !SearchMatching::verifyOccurrences(
                                  note.title, effective, wholeWord)
                                  .isEmpty();
        for (const IndexedBlock &block : note.blocks) {
            const QList<int> offsets = SearchMatching::verifyOccurrences(
                block.displayText, effective, wholeWord);
            for (const int at : offsets) {
                ++group.matchCount;
                if (group.matches.size() < cap) {
                    SearchMatch m;
                    m.blockIndex = block.blockIndex;
                    m.start = at;
                    m.length = effective.size();
                    group.matches.append(m);
                }
            }
        }
        if (group.titleMatched || group.matchCount > 0) {
            group.moreMatches = group.matchCount - group.matches.size();
            results.matchCount += group.matchCount;
            results.groups.append(group);
        }
    }
    results.noteCount = results.groups.size();
    return results;
}

void TestSearchIndexDb::testCapabilityProbe()
{
    QVERIFY(SearchIndexDb::probeCapability());
}

void TestSearchIndexDb::testUnicodeScalarRouting()
{
    QCOMPARE(SearchMatching::unicodeScalarCount(QStringLiteral("AI")), 2);
    QCOMPARE(SearchMatching::unicodeScalarCount(QStringLiteral("row")), 3);
    // A supplementary-plane character (😀, U+1F600) is one scalar, not two
    // UTF-16 code units.
    const char32_t emojiScalar[] = {0x1F600};
    const QString emoji = QString::fromUcs4(emojiScalar, 1);
    QCOMPARE(emoji.size(), 2);                               // two code units
    QCOMPARE(SearchMatching::unicodeScalarCount(emoji), 1);  // one scalar
    // Decomposed "é" (e + combining acute) is one scalar after NFC.
    const QString decomposed = QStringLiteral("é");
    QCOMPARE(SearchMatching::unicodeScalarCount(decomposed), 1);
}

void TestSearchIndexDb::testFtsPhraseEscaping()
{
    QCOMPARE(SearchMatching::ftsPhrase(QStringLiteral("brown fox")),
             QStringLiteral("\"brown fox\""));
    // A double quote in the query is doubled, never treated as syntax.
    QCOMPARE(SearchMatching::ftsPhrase(QStringLiteral("say \"hi\"")),
             QStringLiteral("\"say \"\"hi\"\"\""));
}

void TestSearchIndexDb::testWholeWordBoundaries()
{
    loadNote(QStringLiteral("AI research.md"), QStringLiteral("AI research\n"));
    loadNote(QStringLiteral("AI driven.md"), QStringLiteral("AI-driven work\n"));
    loadNote(QStringLiteral("Chair.md"), QStringLiteral("a comfy chair\n"));
    loadNote(QStringLiteral("Learn.md"), QStringLiteral("Learn Go today\n"));
    loadNote(QStringLiteral("Going.md"), QStringLiteral("going home now\n"));
    loadNote(QStringLiteral("Stats.md"), QStringLiteral("analysis in R\n"));
    loadNote(QStringLiteral("R2.md"), QStringLiteral("uses R2 storage\n"));
    loadNote(QStringLiteral("Para.md"), QStringLiteral("one paragraph here\n"));

    // "AI": matches "AI research" and "AI-driven" (and their titles), not
    // "chair".
    SearchResults ai = run(QStringLiteral("AI"));
    QVERIFY(ai.matchCount >= 2);
    for (const SearchGroup &g : ai.groups)
        QVERIFY(g.relPath != QStringLiteral("Chair.md"));

    // "Go": "Learn Go" yes, "going" no.
    SearchResults go = run(QStringLiteral("Go"));
    bool hasLearn = false, hasGoing = false;
    for (const SearchGroup &g : go.groups) {
        if (g.relPath == QStringLiteral("Learn.md") && g.matchCount > 0)
            hasLearn = true;
        if (g.relPath == QStringLiteral("Going.md") && g.matchCount > 0)
            hasGoing = true;
    }
    QVERIFY(hasLearn);
    QVERIFY(!hasGoing);

    // "R": standalone R yes, "R2" no, "paragraph" no.
    SearchResults r = run(QStringLiteral("R"));
    bool hasStats = false;
    for (const SearchGroup &g : r.groups) {
        QVERIFY(g.relPath != QStringLiteral("Para.md"));
        if (g.relPath == QStringLiteral("Stats.md"))
            hasStats = g.matchCount > 0;
        if (g.relPath == QStringLiteral("R2.md"))
            QCOMPARE(g.matchCount, 0); // title "R2" also whole-word-fails on R
    }
    QVERIFY(hasStats);
}

void TestSearchIndexDb::testLongSubstring()
{
    loadNote(QStringLiteral("Brown.md"), QStringLiteral("The brown fox jumps\n"));
    loadNote(QStringLiteral("Cat.md"), QStringLiteral("concatenate values\n"));

    // "row" is a substring of "brown".
    SearchResults row = run(QStringLiteral("row"));
    bool inBrown = false;
    for (const SearchGroup &g : row.groups)
        if (g.relPath == QStringLiteral("Brown.md"))
            inBrown = g.matchCount > 0;
    QVERIFY(inBrown);

    // "cat" is a substring of "concatenate".
    SearchResults cat = run(QStringLiteral("cat"));
    bool inCat = false;
    for (const SearchGroup &g : cat.groups)
        if (g.relPath == QStringLiteral("Cat.md"))
            inCat = g.matchCount > 0;
    QVERIFY(inCat);

    // "brown fox" matches the exact sequence in one block.
    SearchResults phrase = run(QStringLiteral("brown fox"));
    QCOMPARE(phrase.noteCount, 1);
    QCOMPARE(phrase.groups.at(0).matchCount, 1);
}

void TestSearchIndexDb::testPunctuationShortQueryInert()
{
    loadNote(QStringLiteral("Code.md"),
             QStringLiteral("```\nx :: y\n```\n"));
    // "::" is a punctuation-only short query — no global body results in V1.
    SearchResults r = run(QStringLiteral("::"));
    QCOMPARE(r.matchCount, 0);
    QCOMPARE(r.noteCount, 0);
}

void TestSearchIndexDb::testCodeBlockVerbatimAndPunctuation()
{
    loadNote(QStringLiteral("Ops.md"),
             QStringLiteral("Use the arrow\n\n```\nSELECT a ->> b FROM t\n```\n"));
    // "->>" (3 chars) is a long literal substring, found verbatim in code.
    SearchResults r = run(QStringLiteral("->>"));
    QCOMPARE(r.noteCount, 1);
    QCOMPARE(r.groups.at(0).matches.at(0).blockIndex, 1);
    // Code is verbatim: markers are not stripped.
    loadNote(QStringLiteral("Fmt.md"),
             QStringLiteral("**bold** text\n\n```\n**bold** literal\n```\n"));
    SearchResults stars = run(QStringLiteral("**bold**"));
    // Only the code block carries the literal asterisks.
    QCOMPARE(stars.noteCount, 1);
    QCOMPARE(stars.groups.at(0).relPath, QStringLiteral("Fmt.md"));
    QCOMPARE(stars.groups.at(0).matchCount, 1);
    QCOMPARE(stars.groups.at(0).matches.at(0).blockIndex, 1);
}

void TestSearchIndexDb::testTitleMatchesDoNotCountBody()
{
    loadNote(QStringLiteral("Bread recipe.md"),
             QStringLiteral("Knead the dough well\n"));
    SearchResults r = run(QStringLiteral("bread"));
    QCOMPARE(r.noteCount, 1);
    QCOMPARE(r.groups.at(0).titleMatched, true);
    QCOMPARE(r.groups.at(0).matchCount, 0);
    QCOMPARE(r.matchCount, 0);
}

void TestSearchIndexDb::testIdenticalBlocksDistinctLocations()
{
    loadNote(QStringLiteral("Twins.md"),
             QStringLiteral("needle here\n\nfiller\n\nneedle here\n"));
    SearchResults r = run(QStringLiteral("needle"));
    QCOMPARE(r.noteCount, 1);
    QCOMPARE(r.groups.at(0).matchCount, 2);
    QCOMPARE(r.groups.at(0).matches.at(0).blockIndex, 0);
    QCOMPARE(r.groups.at(0).matches.at(1).blockIndex, 2);
}

void TestSearchIndexDb::testRowCapAndMoreMatches()
{
    QString body;
    for (int i = 0; i < 15; ++i)
        body += QStringLiteral("line %1 has a needle in it\n\n").arg(i);
    loadNote(QStringLiteral("Haystack.md"), body);
    SearchResults r = run(QStringLiteral("needle"));
    QCOMPARE(r.groups.at(0).matchCount, 15);
    QCOMPARE(r.groups.at(0).matches.size(), 10);
    QCOMPARE(r.groups.at(0).moreMatches, 5);
}

void TestSearchIndexDb::testFolderScopeEscapesWildcards()
{
    loadNote(QStringLiteral("a_b/note.md"), QStringLiteral("target text\n"));
    loadNote(QStringLiteral("axb/note.md"), QStringLiteral("target text\n"));
    loadNote(QStringLiteral("a_b/sub/deep.md"), QStringLiteral("target text\n"));

    SearchResults all = run(QStringLiteral("target"));
    QCOMPARE(all.noteCount, 3);

    // Scoping to "a_b" is recursive but must not leak into sibling "axb": the
    // underscore is a LIKE wildcard unless escaped.
    SearchResults scoped = run(QStringLiteral("target"), QStringLiteral("a_b"));
    QCOMPARE(scoped.noteCount, 2);
    for (const SearchGroup &g : scoped.groups)
        QVERIFY(g.relPath.startsWith(QStringLiteral("a_b/")));
}

void TestSearchIndexDb::testTagFilterComposes()
{
    loadNote(QStringLiteral("Bread.md"),
             QStringLiteral("---\ntags: [cooking]\n---\nknead the o dough\n"));
    loadNote(QStringLiteral("Notes.md"), QStringLiteral("other o content\n"));
    SearchResults r = run(QStringLiteral("o"), QString(),
                          QStringLiteral("cooking"));
    QCOMPARE(r.noteCount, 1);
    QCOMPARE(r.groups.at(0).relPath, QStringLiteral("Bread.md"));
}

void TestSearchIndexDb::testDatePresetAndCustomRange()
{
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    const qint64 day = qint64(86400000);
    loadNote(QStringLiteral("Fresh.md"), QStringLiteral("interesting fresh\n"),
             now - 2 * day);
    loadNote(QStringLiteral("Old.md"), QStringLiteral("interesting old\n"),
             now - 90 * day);

    QCOMPARE(run(QStringLiteral("interesting"), QString(), QString(),
                 QStringLiteral("any"), QDate(), QDate(), now).noteCount, 2);
    QCOMPARE(run(QStringLiteral("interesting"), QString(), QString(),
                 QStringLiteral("month"), QDate(), QDate(), now).noteCount, 1);
    QCOMPARE(run(QStringLiteral("interesting"), QString(), QString(),
                 QStringLiteral("year"), QDate(), QDate(), now).noteCount, 2);

    // Custom range around the old note's day includes only it.
    const QDate oldDay =
        QDateTime::fromMSecsSinceEpoch(now - 90 * day).date();
    SearchResults custom = run(QStringLiteral("interesting"), QString(),
                               QString(), QStringLiteral("custom"),
                               oldDay.addDays(-1), oldDay.addDays(1), now);
    QCOMPARE(custom.noteCount, 1);
    QCOMPARE(custom.groups.at(0).relPath, QStringLiteral("Old.md"));
}

void TestSearchIndexDb::testCaseAndDiacritics()
{
    // Title deliberately carries no accent so these assertions isolate body
    // matching from title matching.
    loadNote(QStringLiteral("Beverages.md"), QStringLiteral("Café society\n"));
    // Case-insensitive.
    QCOMPARE(run(QStringLiteral("CAFÉ")).noteCount, 1);
    QCOMPARE(run(QStringLiteral("café")).noteCount, 1);
    // Diacritics are NOT deliberately removed: "cafe" does not match "café".
    QCOMPARE(run(QStringLiteral("cafe")).noteCount, 0);
}

void TestSearchIndexDb::testReplaceAndRemoveKeepFtsConsistent()
{
    loadNote(QStringLiteral("Note.md"), QStringLiteral("alpha beta gamma\n"));
    QCOMPARE(run(QStringLiteral("beta")).noteCount, 1);

    // Replace: old postings must be withdrawn or they linger as false hits.
    loadNote(QStringLiteral("Note.md"), QStringLiteral("delta epsilon\n"));
    QCOMPARE(run(QStringLiteral("beta")).noteCount, 0);
    QCOMPARE(run(QStringLiteral("epsilon")).noteCount, 1);
    QVERIFY(m_db->integrityOk());

    // Remove: gone from both indexes.
    QVERIFY(m_db->removeNote(QStringLiteral("Note.md")));
    QCOMPARE(run(QStringLiteral("epsilon")).noteCount, 0);
    QVERIFY(m_db->integrityOk());
}

void TestSearchIndexDb::testIndexRevisionIncrements()
{
    qint64 rev1 = 0;
    IndexedNote note = CollectionSearchIndex::parseNote(
        QStringLiteral("Rev.md"), QStringLiteral("first\n"), 6, 0);
    QVERIFY(m_db->replaceNote(note, &rev1));
    QCOMPARE(rev1, qint64(1));

    qint64 rev2 = 0;
    note = CollectionSearchIndex::parseNote(QStringLiteral("Rev.md"),
                                            QStringLiteral("second\n"), 7, 0);
    QVERIFY(m_db->replaceNote(note, &rev2));
    QCOMPARE(rev2, qint64(2));
    QCOMPARE(m_db->revisionOf(QStringLiteral("Rev.md")), qint64(2));
}

void TestSearchIndexDb::testCorruptDatabaseRebuilds()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("search.sqlite"));
    // Write garbage where a database should be.
    {
        QFile f(path);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write(QByteArray(4096, 'x'));
    }
    SearchIndexDb db;
    // open() detects the corruption, removes the file, and rebuilds empty.
    QVERIFY(db.open(path, QStringLiteral("kvit_search_corrupt")));
    QVERIFY(db.isUsable());
    QCOMPARE(db.noteRowCount(), 0);
    db.close();
}

void TestSearchIndexDb::testDifferentialOracle()
{
    // A corpus spanning ASCII, composed/decomposed Unicode, non-Latin scripts,
    // emoji, punctuation, code, and nested Markdown spans.
    loadNote(QStringLiteral("ascii.md"),
             QStringLiteral("The quick brown fox jumps over the lazy dog\n\n"
                            "AI and Go and R and R2 in one line\n"));
    loadNote(QStringLiteral("folder/nested.md"),
             QStringLiteral("A **bold brown** and _italic fox_ span\n\n"
                            "concatenate cats and category\n"));
    loadNote(QStringLiteral("unicode.md"),
             QStringLiteral("Café near the naïve résumé\n\n"
                            "Straße und Grüße\n"));
    loadNote(QStringLiteral("cyrillic.md"),
             QStringLiteral("Привет мир и Москва\n"));
    loadNote(QStringLiteral("emoji.md"),
             QStringLiteral("smile 😀 and grin 😀 twice\n"));
    loadNote(QStringLiteral("code.md"),
             QStringLiteral("prose about arrows\n\n"
                            "```\nmap ->> reduce ->> collect\n::marker::\n```\n"));
    loadNote(QStringLiteral("punct.md"),
             QStringLiteral("dot.dot and (a.) plus a_b_c tokens\n"));
    loadNote(QStringLiteral("dup.md"),
             QStringLiteral("repeat word\n\nother\n\nrepeat word\n"));

    const QStringList queries = {
        QStringLiteral("AI"),     QStringLiteral("Go"),
        QStringLiteral("R"),      QStringLiteral("R2"),
        QStringLiteral("fox"),    QStringLiteral("row"),
        QStringLiteral("cat"),    QStringLiteral("brown fox"),
        QStringLiteral("bold brown"), QStringLiteral("café"),
        QStringLiteral("CAFÉ"),   QStringLiteral("cafe"),
        QStringLiteral("straße"), QStringLiteral("grüße"),
        QStringLiteral("Привет"), QStringLiteral("мир"),
        QStringLiteral("Москва"), QStringLiteral("😀"),
        QStringLiteral("->>"),    QStringLiteral("::marker::"),
        QStringLiteral("a."),     QStringLiteral("a_b_c"),
        QStringLiteral("dot.dot"),QStringLiteral("repeat word"),
        QStringLiteral("the"),    QStringLiteral("category"),
        QStringLiteral("concatenate"), QStringLiteral("zzz"),
    };

    for (const QString &query : queries) {
        const SearchResults got = run(query);
        const SearchResults want = oracle(query);
        QCOMPARE(got.noteCount, want.noteCount);
        QCOMPARE(got.matchCount, want.matchCount);
        QCOMPARE(got.groups.size(), want.groups.size());
        for (int i = 0; i < want.groups.size(); ++i) {
            const SearchGroup &a = got.groups.at(i);
            const SearchGroup &b = want.groups.at(i);
            const QString ctx =
                QStringLiteral("query=%1 group=%2").arg(query, b.relPath);
            QVERIFY2(a.relPath == b.relPath, qPrintable(ctx));
            QVERIFY2(a.titleMatched == b.titleMatched, qPrintable(ctx));
            QVERIFY2(a.matchCount == b.matchCount, qPrintable(ctx));
            QVERIFY2(a.moreMatches == b.moreMatches, qPrintable(ctx));
            QVERIFY2(a.matches.size() == b.matches.size(), qPrintable(ctx));
            for (int j = 0; j < b.matches.size(); ++j) {
                QVERIFY2(a.matches.at(j).blockIndex == b.matches.at(j).blockIndex,
                         qPrintable(ctx));
                QVERIFY2(a.matches.at(j).start == b.matches.at(j).start,
                         qPrintable(ctx));
            }
        }
    }
}

void TestSearchIndexDb::testQueryPerformanceGate()
{
    // A 500-note proxy for the large-vault performance gate. The engine query
    // is measured directly, synchronously, with no thread hop or debounce.
    for (int i = 0; i < 500; ++i) {
        QString body;
        for (int line = 0; line < 8 + (i % 12); ++line) {
            body += QStringLiteral(
                        "Paragraph %1 of note %2 with bold words and the "
                        "occasional needle%3 to find\n\n")
                        .arg(line)
                        .arg(i)
                        .arg(i % 5 == 0 ? QStringLiteral(" needle") : QString());
        }
        loadNote(QStringLiteral("Bench %1.md").arg(i), body);
    }

    run(QStringLiteral("warmup")); // touch caches
    struct Case { const char *label; QString query; };
    const QList<Case> cases = {
        {"rare", QStringLiteral("needle")},
        {"common", QStringLiteral("paragraph")},
        {"short-word", QStringLiteral("to")},
    };
    qint64 worst = 0;
    for (const Case &c : cases) {
        QElapsedTimer timer;
        timer.start();
        const SearchResults r = run(c.query);
        const qint64 elapsed = timer.elapsed();
        qInfo("SEARCH %-10s: %lld ms (%d matches, %d notes)", c.label, elapsed,
              r.matchCount, r.noteCount);
        worst = qMax(worst, elapsed);
    }
    if (!kvitTimingBudgetsEnforced())
        QSKIP(KVIT_TIMING_BUDGET_SKIP_REASON);
    QVERIFY2(worst < 50,
             qPrintable(QStringLiteral("500-note query must stay under 50 ms "
                                       "(worst measured %1 ms)")
                            .arg(worst)));
}

QTEST_MAIN(TestSearchIndexDb)
#include "test_searchindexdb.moc"
