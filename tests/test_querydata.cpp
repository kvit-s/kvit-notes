// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
#include <QtTest/QtTest>

#include "timingbudget.h"
#include <QTemporaryDir>
#include <QFile>
#include <QDir>
#include <QElapsedTimer>

#include "notecollection.h"
#include "querydata.h"
#include "querytools.h"

#include <algorithm>

// Unit suite for the collection query block's pure core: the line-oriented
// spec grammar with its explicit error surface, and evaluation — filter,
// type-inferred sort, grouping — against a fixture collection on a temp
// directory.
class TestQueryData : public QObject
{
    Q_OBJECT

private slots:
    void init();
    void cleanup();

    // Grammar
    void testParseFullSpec();
    void testParseDefaults();
    void testParseErrors_data();
    void testParseErrors();
    void testGroupByImpliesBoard();

    // Evaluation
    void testOperators_data();
    void testOperators();
    void testFolderScoping();
    void testSortTypedAndStable();
    void testLimit();
    void testBoardGroups();
    void testPseudoFields();
    void testQueryToolsCache();
    void testQueryToolsRunsOffTheCallingThread();
    void testEvaluate1000NoteBudget();

private:
    void writeNote(const QString &relPath, const QString &content)
    {
        QFileInfo info(m_dir->filePath(relPath));
        QVERIFY(QDir().mkpath(info.absolutePath()));
        QFile file(m_dir->filePath(relPath));
        QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Text));
        file.write(content.toUtf8());
    }

    // Root fixtures: four project notes with mixed front-matter plus one
    // note without any.
    void makeFixture()
    {
        writeNote("projects/Alpha.md",
                  "---\nstatus: active\npriority: 2\ndue: 2026-08-01\n"
                  "tags: [work]\n---\nAlpha body\n");
        writeNote("projects/Beta.md",
                  "---\nstatus: active\npriority: 10\ndue: 2026-07-15\n---\n"
                  "Beta body\n");
        writeNote("projects/Gamma.md",
                  "---\nstatus: done\npriority: 2\n---\nGamma body\n");
        writeNote("notes/Delta.md",
                  "---\nstatus: active\n---\nDelta body\n");
        writeNote("Plain.md", "No front-matter here\n");
        QVERIFY(m_collection->openRoot(m_dir->path()));
    }

    QStringList titlesOf(const QueryData::Result &result) const
    {
        QStringList titles;
        for (const QueryData::Row &row : result.rows)
            titles.append(row.cells.value(0));
        return titles;
    }

    QueryData::Result run(const QString &body)
    {
        const QueryData::ParseResult parsed = QueryData::parse(body);
        if (!parsed.ok)
            qWarning("parse error: %s", qPrintable(parsed.error));
        Q_ASSERT(parsed.ok);
        return QueryData::evaluate(parsed.spec, *m_collection);
    }

    QTemporaryDir *m_dir = nullptr;
    NoteCollection *m_collection = nullptr;
};

void TestQueryData::init()
{
    m_dir = new QTemporaryDir();
    QVERIFY(m_dir->isValid());
    m_collection = new NoteCollection();
}

void TestQueryData::cleanup()
{
    delete m_collection;
    delete m_dir;
    m_collection = nullptr;
    m_dir = nullptr;
}

void TestQueryData::testParseFullSpec()
{
    const auto result = QueryData::parse(
        "from: projects/\n"
        "where: status = active, priority > 1\n"
        "where: due exists\n"
        "view: board\n"
        "columns: title, status, due\n"
        "group-by: status\n"
        "sort: due asc, priority desc\n"
        "limit: 20\n"
        "# a comment line is structure, not spec\n"
        "\n");
    QVERIFY2(result.ok, qPrintable(result.error));
    QCOMPARE(result.spec.from, QString("projects"));
    QCOMPARE(result.spec.where.size(), 3);
    QCOMPARE(result.spec.where.at(0).field, QString("status"));
    QVERIFY(result.spec.where.at(0).op == QueryData::Op::Eq);
    QCOMPARE(result.spec.where.at(0).value, QString("active"));
    QVERIFY(result.spec.where.at(1).op == QueryData::Op::Gt);
    QVERIFY(result.spec.where.at(2).op == QueryData::Op::Exists);
    QVERIFY(result.spec.view == QueryData::View::Board);
    QCOMPARE(result.spec.columns,
             (QStringList() << "title" << "status" << "due"));
    QCOMPARE(result.spec.groupBy, QString("status"));
    QCOMPARE(result.spec.sort.size(), 2);
    QVERIFY(result.spec.sort.at(0).ascending);
    QVERIFY(!result.spec.sort.at(1).ascending);
    QCOMPARE(result.spec.limit, 20);
}

void TestQueryData::testParseDefaults()
{
    const auto result = QueryData::parse("");
    QVERIFY(result.ok);
    QVERIFY(result.spec.view == QueryData::View::Table);
    QVERIFY(result.spec.where.isEmpty());
    QCOMPARE(result.spec.limit, 0);
}

void TestQueryData::testParseErrors_data()
{
    QTest::addColumn<QString>("body");
    QTest::newRow("unknown-key") << "sortby: x";
    QTest::newRow("not-a-mapping") << "just words";
    QTest::newRow("bad-view") << "view: cards";
    QTest::newRow("bad-condition") << "where: status active";
    QTest::newRow("missing-value") << "where: status =";
    QTest::newRow("empty-where") << "where: ,";
    QTest::newRow("bad-sort-dir") << "sort: due sideways";
    QTest::newRow("bad-limit") << "limit: many";
    QTest::newRow("zero-limit") << "limit: 0";
    QTest::newRow("board-without-group") << "view: board";
    QTest::newRow("group-on-table") << "view: table\ngroup-by: status";
    QTest::newRow("group-two-words") << "group-by: two words";
}

void TestQueryData::testParseErrors()
{
    QFETCH(QString, body);
    const auto result = QueryData::parse(body);
    QVERIFY(!result.ok);
    QVERIFY(!result.error.isEmpty());
}

void TestQueryData::testGroupByImpliesBoard()
{
    const auto result = QueryData::parse("group-by: status");
    QVERIFY(result.ok);
    QVERIFY(result.spec.view == QueryData::View::Board);
}

void TestQueryData::testOperators_data()
{
    QTest::addColumn<QString>("where");
    QTest::addColumn<QStringList>("expected");

    QTest::newRow("eq") << "where: status = active"
        << (QStringList() << "Alpha" << "Beta" << "Delta");
    QTest::newRow("ne") << "from: projects\nwhere: status != active"
        << (QStringList() << "Gamma");
    QTest::newRow("gt-numeric") << "where: priority > 2"
        << (QStringList() << "Beta"); // 10 > 2 numerically, not "10" < "2"
    QTest::newRow("ge") << "where: priority >= 2"
        << (QStringList() << "Alpha" << "Beta" << "Gamma");
    QTest::newRow("lt-date") << "where: due < 2026-07-20"
        << (QStringList() << "Beta");
    QTest::newRow("le-date") << "where: due <= 2026-08-01"
        << (QStringList() << "Alpha" << "Beta");
    QTest::newRow("contains") << "where: title contains lph"
        << (QStringList() << "Alpha");
    QTest::newRow("has") << "where: tags has work"
        << (QStringList() << "Alpha");
    QTest::newRow("exists") << "where: due exists"
        << (QStringList() << "Alpha" << "Beta");
    QTest::newRow("and") << "where: status = active\nwhere: priority = 2"
        << (QStringList() << "Alpha");
    QTest::newRow("case-insensitive-eq") << "where: status = ACTIVE"
        << (QStringList() << "Alpha" << "Beta" << "Delta");
}

void TestQueryData::testOperators()
{
    QFETCH(QString, where);
    QFETCH(QStringList, expected);
    makeFixture();
    const auto result = run(where + "\ncolumns: title\nsort: title asc");
    QCOMPARE(titlesOf(result), expected);
}

void TestQueryData::testFolderScoping()
{
    makeFixture();
    auto result = run("from: projects\ncolumns: title\nsort: title asc");
    QCOMPARE(titlesOf(result),
             (QStringList() << "Alpha" << "Beta" << "Gamma"));

    // Nested folders are in scope; unrelated prefixes are not.
    writeNote("projects/sub/Nested.md", "---\nstatus: active\n---\nx\n");
    writeNote("projectsish/Other.md", "---\nstatus: active\n---\nx\n");
    m_collection->refresh();
    result = run("from: projects\ncolumns: title\nsort: title asc");
    QCOMPARE(titlesOf(result),
             (QStringList() << "Alpha" << "Beta" << "Gamma" << "Nested"));
}

void TestQueryData::testSortTypedAndStable()
{
    makeFixture();

    // Numeric: 2 < 10 (string order would be "10" < "2").
    auto result = run("from: projects\ncolumns: title\n"
                      "sort: priority desc, title asc");
    QCOMPARE(titlesOf(result),
             (QStringList() << "Beta" << "Alpha" << "Gamma"));

    // Dates sort as dates; notes without the field group deterministically.
    result = run("from: projects\nwhere: due exists\ncolumns: title\n"
                 "sort: due asc");
    QCOMPARE(titlesOf(result), (QStringList() << "Beta" << "Alpha"));
}

void TestQueryData::testLimit()
{
    makeFixture();
    const auto result =
        run("columns: title\nsort: title asc\nlimit: 2");
    QCOMPARE(titlesOf(result), (QStringList() << "Alpha" << "Beta"));
}

void TestQueryData::testBoardGroups()
{
    makeFixture();
    const auto result = run("from: projects\ngroup-by: status\n"
                            "columns: title\nsort: title asc");
    QCOMPARE(result.groups.size(), 2);
    QCOMPARE(result.groups.at(0).name, QString("active"));
    QCOMPARE(result.groups.at(0).rows.size(), 2);
    QCOMPARE(result.groups.at(1).name, QString("done"));
    QCOMPARE(result.groups.at(1).rows.size(), 1);

    // Notes without the group key land in the trailing "(none)" group.
    const auto all = run("group-by: status\ncolumns: title\nsort: title asc");
    QCOMPARE(all.groups.last().name, QString("(none)"));
    QCOMPARE(all.groups.last().rows.size(), 1); // Plain.md
}

void TestQueryData::testPseudoFields()
{
    makeFixture();
    // Default columns and the pseudo-fields resolve from the entry.
    const auto result = run("where: path contains projects/A\n"
                            "columns: title, folder, words, tags, path");
    QCOMPARE(result.rows.size(), 1);
    const QueryData::Row &row = result.rows.first();
    QCOMPARE(row.cells.at(0), QString("Alpha"));
    QCOMPARE(row.cells.at(1), QString("projects"));
    QCOMPARE(row.cells.at(2), QString("2")); // "Alpha body"
    QCOMPARE(row.cells.at(3), QString("work"));
    QCOMPARE(row.cells.at(4), QString("projects/Alpha.md"));

    // Defaulted columns.
    const auto defaulted = run("where: title = Alpha");
    QCOMPARE(defaulted.columns,
             (QStringList() << "title" << "modified"));
}

void TestQueryData::testQueryToolsCache()
{
    makeFixture();
    QueryTools tools;
    tools.setCollection(m_collection);
    tools.clearCache();

    const QString spec = QStringLiteral(
        "where: status = active\ncolumns: title\nsort: title asc");
    QVERIFY(tools.run(spec).value("ok").toBool());
    QVERIFY(tools.run(spec).value("ok").toBool());
    QCOMPARE(tools.evaluationCount(), 1); // identical blocks share the entry

    QVERIFY(!tools.run("unknown: key").value("ok").toBool());
    QVERIFY(!tools.run("unknown: key").value("ok").toBool());
    QCOMPARE(tools.evaluationCount(), 2); // parse errors are cached too

    writeNote("projects/New.md",
              "---\nstatus: active\n---\nnew\n");
    m_collection->refreshPaths({m_dir->filePath("projects/New.md")});
    QCOMPARE(tools.run(spec).value("rows").toList().size(), 4);
    QCOMPARE(tools.evaluationCount(), 3); // revision is part of the key

    QTemporaryDir other;
    QVERIFY(other.isValid());
    QFile note(other.filePath("Only.md"));
    QVERIFY(note.open(QIODevice::WriteOnly | QIODevice::Text));
    note.write("---\nstatus: active\n---\nonly\n");
    note.close();
    QVERIFY(m_collection->openRoot(other.path()));
    QCOMPARE(tools.cacheSize(), 0); // rootChanged advances collection generation
    QCOMPARE(tools.run(spec).value("rows").toList().size(), 1);
    QCOMPARE(tools.evaluationCount(), 4);
}

// A query scans every note, sorts the matches and builds a QVariant tree, so
// it runs on a pool thread and answers with a signal. Three properties matter
// beyond "the right rows come back": the call returns before the work is
// done, several blocks asking the same question at the same revision cost one
// scan rather than one each, and a result whose revision has been superseded
// is dropped instead of being shown.
void TestQueryData::testQueryToolsRunsOffTheCallingThread()
{
    makeFixture();
    QueryTools tools;
    tools.setCollection(m_collection);
    tools.clearCache();

    const QString spec = QStringLiteral(
        "where: status = active\ncolumns: title\nsort: title asc");

    QList<QPair<QString, QVariantMap>> delivered;
    connect(&tools, &QueryTools::resultReady, &tools,
            [&delivered](const QString &token, const QVariantMap &result) {
                delivered.append({token, result});
            });

    // Three blocks, one question, one revision. The answer arrives for all
    // three and the vault is scanned once.
    tools.requestRun(QStringLiteral("a"), spec);
    tools.requestRun(QStringLiteral("b"), spec);
    tools.requestRun(QStringLiteral("c"), spec);
    QVERIFY2(delivered.isEmpty(),
             "requestRun answered before returning, so it evaluated on the "
             "calling thread");

    QTRY_COMPARE_WITH_TIMEOUT(delivered.size(), 3, 5000);
    QCOMPARE(tools.evaluationCount(), 1);
    QStringList tokens;
    for (const auto &entry : delivered) {
        tokens << entry.first;
        QVERIFY(entry.second.value("ok").toBool());
        QCOMPARE(entry.second.value("rows").toList().size(), 3);
    }
    tokens.sort();
    QCOMPARE(tokens, QStringList({"a", "b", "c"}));

    // Cached for that revision, so a later request is answered at once.
    delivered.clear();
    tools.requestRun(QStringLiteral("d"), spec);
    QCOMPARE(delivered.size(), 1);
    QCOMPARE(tools.evaluationCount(), 1);

    // A parse error needs no scan and no thread.
    delivered.clear();
    tools.requestRun(QStringLiteral("e"), QStringLiteral("unknown: key"));
    QCOMPARE(delivered.size(), 1);
    QVERIFY(!delivered.first().second.value("ok").toBool());

    // A result whose revision has been superseded is not delivered. The
    // request below is issued, then the collection changes underneath it;
    // whatever the worker produced describes a state nothing is showing.
    delivered.clear();
    writeNote("projects/New.md", "---\nstatus: active\n---\nnew\n");
    m_collection->refreshPaths({m_dir->filePath("projects/New.md")});
    tools.requestRun(QStringLiteral("f"), spec);
    writeNote("projects/Newer.md", "---\nstatus: active\n---\nnewer\n");
    m_collection->refreshPaths({m_dir->filePath("projects/Newer.md")});
    QTest::qWait(200);
    for (const auto &entry : delivered) {
        QCOMPARE(entry.second.value("rows").toList().size(), 5);
    }

    // And asking again against the current revision does answer, with the
    // row the superseded run would have missed.
    delivered.clear();
    tools.requestRun(QStringLiteral("g"), spec);
    QTRY_VERIFY_WITH_TIMEOUT(!delivered.isEmpty(), 5000);
    QCOMPARE(delivered.first().second.value("rows").toList().size(), 5);
}

void TestQueryData::testEvaluate1000NoteBudget()
{
#ifndef NDEBUG
    QSKIP("The 25 ms query gate is enforced in Release builds");
#endif
    for (int i = 0; i < 1000; ++i) {
        writeNote(QStringLiteral("notes/%1.md").arg(i, 4, 10, QLatin1Char('0')),
                  QStringLiteral("---\nstatus: %1\npriority: %2\n"
                                 "due: 2026-%3-%4\n---\nbody words here\n")
                      .arg(i % 3 == 0 ? QStringLiteral("active")
                                      : QStringLiteral("done"))
                      .arg(i % 17)
                      .arg((i % 12) + 1, 2, 10, QLatin1Char('0'))
                      .arg((i % 28) + 1, 2, 10, QLatin1Char('0')));
    }
    QVERIFY(m_collection->openRoot(m_dir->path())); // indexing is not timed
    const QueryData::ParseResult parsed = QueryData::parse(
        "from: notes\nwhere: status = active\nwhere: priority >= 3\n"
        "columns: title, status, priority, due, folder\n"
        "sort: due asc, priority desc, title asc\nlimit: 250");
    QVERIFY2(parsed.ok, qPrintable(parsed.error));

    QueryData::evaluate(parsed.spec, *m_collection); // warm caches/allocator
    QList<double> cpuSamples;
    double worstWall = 0.0;
    double worstContention = 1.0;
    double worstCpu = 0.0;
    for (int i = 0; i < 9; ++i) {
        KvitOpTimer timer;
        const QueryData::Result result =
            QueryData::evaluate(parsed.spec, *m_collection);
        const double cpu = timer.cpuMs();
        QVERIFY(!result.rows.isEmpty());
        cpuSamples.append(cpu);
        if (cpu >= worstCpu) {
            worstCpu = cpu;
            worstWall = timer.wallMs();
            worstContention = timer.contention();
        }
    }
    std::sort(cpuSamples.begin(), cpuSamples.end());
    const double median = cpuSamples.at(cpuSamples.size() / 2);
    const double maximum = cpuSamples.last();

    // Budgeted in CPU time. evaluate() is synchronous and CPU-bound, so its
    // CPU cost is the work the query does; measured here, a median of about
    // 10 ms on an idle machine.
    //
    // The wall-clock form skipped itself whenever CI was set, so it never ran
    // on a hosted runner at all, and enforced on developer machines where it
    // flapped - failing two runs in three at load average 36. It also carried
    // a 1.5x Windows allowance its own comment described as "pending
    // calibration against the CI runners", which is exactly the per-platform
    // ladder tracking runner capacity that this file's header warns about.
    // MSVC codegen really is 6-8% slower on this path, so a small platform
    // allowance stays; it no longer has to absorb machine noise as well, and
    // it should be re-measured rather than inherited if it starts to matter.
#ifdef Q_OS_WIN
    const double medianBudgetMs = 22.0;
    const double ceilingMs = 60.0;
#else
    const double medianBudgetMs = 20.0;
    const double ceilingMs = 55.0;
#endif
    qInfo("QUERY 1000: median %.3f ms cpu, max %.3f ms cpu", median, maximum);
    KVIT_ASSERT_CPU_BUDGET_VALUES("query 1000-note evaluate", median, worstWall,
                                  worstContention, medianBudgetMs, ceilingMs);
}

QTEST_MAIN(TestQueryData)
#include "test_querydata.moc"
