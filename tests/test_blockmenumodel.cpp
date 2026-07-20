// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
#include <QtTest/QtTest>
#include "blockmenumodel.h"
#include "block.h"

// The slash-command catalog and fuzzy filter (phase5-plan.md step 2).
// itemsFor() returns display rows: header maps { kind: "header", text }
// and entry maps { kind: "entry", name, description, icon, type }.
class TestBlockMenuModel : public QObject
{
    Q_OBJECT

private slots:
    void init();
    void cleanup();

    void testCatalogComplete();
    void testEmptyQueryGroupedWithHeaders();
    void testWhitespaceQueryIsEmpty();
    void testFuzzyMatching_data();
    void testFuzzyMatching();
    void testRankingTiers();
    void testCaseInsensitive();
    void testNoMatchYieldsEmpty();
    void testHeadersDroppedWhileFiltering();
    void testRecentlyUsedLeadsEmptyQuery();
    void testRecentlyUsedReordersAndCaps();
    void testRecentlyUsedAbsentWhileFiltering();
    void testRecentTypesRoundTrip();
    void testSetRecentTypesSanitizes();
    void testRecentChangedSignalDiscipline();
    void testCodeLanguageAliases();

private:
    // All entry rows of a result, headers skipped.
    static QList<QVariantMap> entries(const QVariantList &rows)
    {
        QList<QVariantMap> result;
        for (const QVariant &row : rows) {
            QVariantMap map = row.toMap();
            if (map.value("kind").toString() == QLatin1String("entry"))
                result.append(map);
        }
        return result;
    }

    static QStringList headerTexts(const QVariantList &rows)
    {
        QStringList result;
        for (const QVariant &row : rows) {
            QVariantMap map = row.toMap();
            if (map.value("kind").toString() == QLatin1String("header"))
                result.append(map.value("text").toString());
        }
        return result;
    }

    static QStringList entryNames(const QVariantList &rows)
    {
        QStringList result;
        for (const QVariantMap &entry : entries(rows))
            result.append(entry.value("name").toString());
        return result;
    }

    BlockMenuModel *m_menu = nullptr;
};

void TestBlockMenuModel::init()
{
    m_menu = new BlockMenuModel();
}

void TestBlockMenuModel::cleanup()
{
    delete m_menu;
    m_menu = nullptr;
}

void TestBlockMenuModel::testCatalogComplete()
{
    // Every implemented block type appears exactly once with non-empty
    // display fields (features.md §4.2 for the wave-1 set; wave-2 types
    // join the catalog with their block types in Phase 10).
    const QList<int> implemented = {
        Block::Paragraph, Block::Heading1, Block::Heading2, Block::Heading3,
        Block::Heading4, Block::BulletList, Block::NumberedList, Block::Todo,
        Block::Quote, Block::CodeBlock, Block::Divider,
        Block::Image,      // Phase 10 Media group
        Block::Callout,    // Callout + Toggle both insert a Callout block
        Block::Table,
        Block::MathBlock,  // Phase 10 math (decision 12)
        Block::Media,      // Phase 10 local media (decision 13)
    };

    const auto rows = entries(m_menu->itemsFor(QString()));

    QSet<int> seen;
    for (const QVariantMap &entry : rows) {
        const int type = entry.value("type").toInt();
        QVERIFY2(implemented.contains(type),
                 qPrintable(QStringLiteral("unexpected type %1").arg(type)));
        seen.insert(type);
        QVERIFY(!entry.value("name").toString().isEmpty());
        QVERIFY(!entry.value("description").toString().isEmpty());
        QVERIFY(!entry.value("icon").toString().isEmpty());
    }
    // Every implemented type appears; some types carry extra entries —
    // Callout (Callout + Toggle), CodeBlock (Code Block + Task Board + Table
    // of Contents + Mermaid Diagram + Collection Query), Image (Image + Web
    // Embed), and Paragraph (Text + Drop Cap, §1.2.16 being an attribute on a
    // paragraph rather than a stored type) — so the row count is the type
    // count plus those seven extras.
    // Character diagrams have no menu entry: a `diagram` fence renders as an
    // ordinary code block, the tag only marks it for ingest straightening
    // (§7.5).
    QCOMPARE(seen.size(), implemented.size());
    QCOMPARE(rows.size(), implemented.size() + 7);
    // The Toggle entry seeds the toggle marker via `language`; Task Board,
    // Table of Contents, and Web Embed seed their fence/embed marker the same
    // way.
    QStringList names;
    for (const QVariantMap &e : rows)
        names.append(e.value("name").toString());
    QVERIFY(names.contains("Callout"));
    QVERIFY(names.contains("Toggle"));
    QVERIFY(names.contains("Task Board"));
    QVERIFY(names.contains("Table of Contents"));
    QVERIFY(names.contains("Web Embed"));
    QVERIFY(names.contains("Drop Cap"));
    QVERIFY(!names.contains("Text Diagram"));
    QVERIFY(names.contains("Mermaid Diagram"));
    QVERIFY(names.contains("Collection Query"));
}

void TestBlockMenuModel::testEmptyQueryGroupedWithHeaders()
{
    const QVariantList rows = m_menu->itemsFor(QString());

    // Canonical group order (phase5-plan.md decision 7; Media added in
    // Phase 10, §4.3); no recency yet
    QCOMPARE(headerTexts(rows),
             QStringList({ "Basic", "Lists", "Advanced", "Media" }));

    // The first row is a header, and every entry sits under its group
    QCOMPARE(rows.first().toMap().value("kind").toString(), QString("header"));

    // Basic holds Text and the four heading levels, in order
    const QStringList names = entryNames(rows);
    QCOMPARE(names.mid(0, 5),
             QStringList({ "Text", "Heading 1", "Heading 2", "Heading 3",
                           "Heading 4" }));
}

void TestBlockMenuModel::testWhitespaceQueryIsEmpty()
{
    QCOMPARE(m_menu->itemsFor("  "), m_menu->itemsFor(QString()));
}

void TestBlockMenuModel::testFuzzyMatching_data()
{
    QTest::addColumn<QString>("query");
    QTest::addColumn<QString>("expectedFirst");

    // The spec's named example (features.md §4.3)
    QTest::newRow("h1 finds Heading 1") << "h1" << "Heading 1";
    QTest::newRow("h2") << "h2" << "Heading 2";
    QTest::newRow("h4") << "h4" << "Heading 4";
    // Alias table
    QTest::newRow("todo") << "todo" << "To-do";
    QTest::newRow("task") << "task" << "To-do";
    QTest::newRow("checkbox") << "checkbox" << "To-do";
    QTest::newRow("ol") << "ol" << "Numbered List";
    QTest::newRow("ul") << "ul" << "Bulleted List";
    QTest::newRow("hr") << "hr" << "Divider";
    QTest::newRow("code") << "code" << "Code Block";
    QTest::newRow("quote") << "quote" << "Quote";
    QTest::newRow("blockquote") << "blockquote" << "Quote";
    QTest::newRow("paragraph") << "paragraph" << "Text";
    QTest::newRow("markdown > prefix") << ">" << "Quote";
    QTest::newRow("markdown --- prefix") << "---" << "Divider";
    QTest::newRow("markdown ``` prefix") << "```" << "Code Block";
    // Name prefixes
    QTest::newRow("name prefix div") << "div" << "Divider";
    QTest::newRow("name prefix bull") << "bull" << "Bulleted List";
    QTest::newRow("second word prefix num") << "num" << "Numbered List";
    // Bare subsequence ("cdb" is not a prefix of anything)
    QTest::newRow("subsequence cdb") << "cdb" << "Code Block";
}

void TestBlockMenuModel::testFuzzyMatching()
{
    QFETCH(QString, query);
    QFETCH(QString, expectedFirst);

    const QStringList names = entryNames(m_menu->itemsFor(query));
    QVERIFY2(!names.isEmpty(),
             qPrintable(QStringLiteral("'%1' matched nothing").arg(query)));
    QCOMPARE(names.first(), expectedFirst);
}

void TestBlockMenuModel::testRankingTiers()
{
    // "he": whole-string prefix of "Heading N" — all four headings lead,
    // in catalog order; any bare-subsequence stragglers rank after.
    QStringList names = entryNames(m_menu->itemsFor("he"));
    QVERIFY(names.size() >= 4);
    QCOMPARE(names.mid(0, 4),
             QStringList({ "Heading 1", "Heading 2", "Heading 3",
                           "Heading 4" }));

    // "list": word prefix in "Bulleted List" / "Numbered List" (and the
    // bullet alias "list" as whole-string prefix). To-do never matches.
    names = entryNames(m_menu->itemsFor("list"));
    QVERIFY(names.contains("Bulleted List"));
    QVERIFY(names.contains("Numbered List"));
    QVERIFY(!names.contains("To-do"));

    // Prefix beats subsequence: for "te", "Text" (prefix) must precede
    // any entry matching only as a subsequence.
    names = entryNames(m_menu->itemsFor("te"));
    QCOMPARE(names.first(), QString("Text"));
}

void TestBlockMenuModel::testCaseInsensitive()
{
    QCOMPARE(entryNames(m_menu->itemsFor("H1")).first(), QString("Heading 1"));
    QCOMPARE(entryNames(m_menu->itemsFor("TODO")).first(), QString("To-do"));
    QCOMPARE(entryNames(m_menu->itemsFor("Code")).first(), QString("Code Block"));
}

void TestBlockMenuModel::testNoMatchYieldsEmpty()
{
    QVERIFY(m_menu->itemsFor("zzz").isEmpty());
    QVERIFY(m_menu->itemsFor("xylophone").isEmpty());
}

void TestBlockMenuModel::testHeadersDroppedWhileFiltering()
{
    const QVariantList rows = m_menu->itemsFor("h");
    QVERIFY(!rows.isEmpty());
    QVERIFY(headerTexts(rows).isEmpty());
}

void TestBlockMenuModel::testRecentlyUsedLeadsEmptyQuery()
{
    m_menu->noteUsed(Block::Todo);
    m_menu->noteUsed(Block::CodeBlock);

    const QVariantList rows = m_menu->itemsFor(QString());
    QCOMPARE(headerTexts(rows).first(), QString("Recently used"));

    // Most recent first; the catalog groups follow in full
    const QStringList names = entryNames(rows);
    QCOMPARE(names.mid(0, 2), QStringList({ "Code Block", "To-do" }));
    QVERIFY(names.count("To-do") == 2);  // recency row plus its group row
}

void TestBlockMenuModel::testRecentlyUsedReordersAndCaps()
{
    m_menu->noteUsed(Block::Todo);
    m_menu->noteUsed(Block::Quote);
    m_menu->noteUsed(Block::Divider);
    m_menu->noteUsed(Block::CodeBlock);   // pushes Todo out (cap 3)
    m_menu->noteUsed(Block::Quote);       // re-orders, no duplicate

    const QStringList names = entryNames(m_menu->itemsFor(QString()));
    QCOMPARE(names.mid(0, 3),
             QStringList({ "Quote", "Code Block", "Divider" }));
    // The row after the recency group is the first catalog entry
    QCOMPARE(names.at(3), QString("Text"));
}

void TestBlockMenuModel::testRecentlyUsedAbsentWhileFiltering()
{
    m_menu->noteUsed(Block::Divider);

    // Filtering ranks purely; no recency group, no duplicated entries
    const QVariantList rows = m_menu->itemsFor("h1");
    QVERIFY(headerTexts(rows).isEmpty());
    QCOMPARE(entryNames(rows).count("Divider"), 0);

    const QVariantList divRows = m_menu->itemsFor("divider");
    QCOMPARE(entryNames(divRows).count("Divider"), 1);
}

void TestBlockMenuModel::testRecentTypesRoundTrip()
{
    m_menu->noteUsed(Block::Todo);
    m_menu->noteUsed(Block::Quote);

    const QVariantList recent = m_menu->recentTypes();
    QCOMPARE(recent.size(), 2);
    QCOMPARE(recent.at(0).toInt(), int(Block::Quote));
    QCOMPARE(recent.at(1).toInt(), int(Block::Todo));

    // A fresh model (a restart) fed the persisted list reproduces the
    // recently-used group exactly.
    BlockMenuModel fresh;
    fresh.setRecentTypes(recent);
    const QStringList names = entryNames(fresh.itemsFor(QString()));
    QCOMPARE(names.at(0), QString("Quote"));
    QCOMPARE(names.at(1), QString("To-do"));
    QCOMPARE(names.at(2), QString("Text"));  // catalog resumes after
}

void TestBlockMenuModel::testSetRecentTypesSanitizes()
{
    // A stale or hand-edited settings value: non-numbers, a type the
    // catalog does not hold, duplicates, and more than MaxRecent
    // survivors. JSON numbers arrive as doubles, so include one.
    m_menu->setRecentTypes(QVariantList{
        QStringLiteral("junk"), 999, double(Block::Quote), Block::Quote,
        Block::Todo, Block::Divider, Block::CodeBlock });

    const QVariantList recent = m_menu->recentTypes();
    QCOMPARE(recent.size(), BlockMenuModel::MaxRecent);
    QCOMPARE(recent.at(0).toInt(), int(Block::Quote));
    QCOMPARE(recent.at(1).toInt(), int(Block::Todo));
    QCOMPARE(recent.at(2).toInt(), int(Block::Divider));
}

void TestBlockMenuModel::testRecentChangedSignalDiscipline()
{
    QSignalSpy spy(m_menu, &BlockMenuModel::recentChanged);

    // Loading persisted state is silent (it must not re-save itself)...
    m_menu->setRecentTypes(QVariantList{ Block::Quote });
    QCOMPARE(spy.count(), 0);

    // ...while a user choice notifies.
    m_menu->noteUsed(Block::Todo);
    QCOMPARE(spy.count(), 1);
}

void TestBlockMenuModel::testCodeLanguageAliases()
{
    // "/code <language>" (decision 14): the remainder after "code " matches
    // language names and aliases, and every row is a CodeBlock carrying the
    // resolved language id.
    auto rows = entries(m_menu->itemsFor(QStringLiteral("code py")));
    QVERIFY(!rows.isEmpty());
    QCOMPARE(rows.first().value("type").toInt(), int(Block::CodeBlock));
    QCOMPARE(rows.first().value("language").toString(), QStringLiteral("python"));
    QVERIFY(rows.first().value("name").toString().contains("Python"));

    // A full name works too; the aliased exact match leads.
    rows = entries(m_menu->itemsFor(QStringLiteral("code javascript")));
    QCOMPARE(rows.first().value("language").toString(), QStringLiteral("javascript"));

    // "c++" resolves through the alias map.
    rows = entries(m_menu->itemsFor(QStringLiteral("code c++")));
    QVERIFY(!rows.isEmpty());
    QCOMPARE(rows.first().value("language").toString(), QStringLiteral("cpp"));

    // Bare "code" (no remainder) stays the ordinary Code Block entry — no
    // language field — so the plain path is untouched.
    rows = entries(m_menu->itemsFor(QStringLiteral("code")));
    QVERIFY(!rows.isEmpty());
    bool anyLang = false;
    for (const QVariantMap &r : rows)
        if (r.contains("language"))
            anyLang = true;
    QVERIFY(!anyLang);

    // A remainder matching nothing falls through rather than emitting bogus
    // language rows.
    rows = entries(m_menu->itemsFor(QStringLiteral("code zzzz")));
    for (const QVariantMap &r : rows)
        QVERIFY(!r.contains("language"));
}

QTEST_MAIN(TestBlockMenuModel)
#include "test_blockmenumodel.moc"
