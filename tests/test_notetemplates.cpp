// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
#include <QtTest/QtTest>
#include "notetemplates.h"
#include "notecollection.h"

#include <QTemporaryDir>
#include <QDir>
#include <QFile>

// Note templates: the variable expander (injected clock),
// CRUD over .kvit/templates/ markdown files, built-in seeding, and the
// front-matter carry-through on instantiation.
class TestNoteTemplates : public QObject
{
    Q_OBJECT

private slots:
    void init();
    void cleanup();

    void testExpand_data();
    void testExpand();
    void testExpandUnknownTokenLeftVerbatim();

    void testWriteReadDeleteRoundTrip();
    void testTemplateNamesSorted();
    void testSeedBuiltinsOnce();
    void testSeedDoesNotOverwrite();

    void testInstantiateExpandsAndSplitsFrontMatter();
    void testInstantiateUnknownReturnsEmpty();

    void testDistinctNamesDoNotShareOneFile();
    void testDeleteDoesNotRemoveAliasedTemplate();

private:
    QTemporaryDir *m_dir = nullptr;
    NoteCollection *m_collection = nullptr;
    NoteTemplates *m_templates = nullptr;
    QDateTime m_now;
};

void TestNoteTemplates::init()
{
    m_dir = new QTemporaryDir();
    QVERIFY(m_dir->isValid());
    m_collection = new NoteCollection(this);
    QVERIFY(m_collection->openRoot(m_dir->path()));
    m_templates = new NoteTemplates(this);
    m_templates->setCollection(m_collection);
    // A fixed clock so expansion is deterministic.
    m_now = QDateTime(QDate(2026, 7, 8), QTime(9, 30, 0));
    m_templates->setClockForTesting([this]() { return m_now; });
}

void TestNoteTemplates::cleanup()
{
    delete m_templates; m_templates = nullptr;
    delete m_collection; m_collection = nullptr;
    delete m_dir; m_dir = nullptr;
}

void TestNoteTemplates::testExpand_data()
{
    QTest::addColumn<QString>("input");
    QTest::addColumn<QString>("title");
    QTest::addColumn<QString>("expected");
    QTest::newRow("date") << "on {{date}}" << "T" << "on 2026-07-08";
    QTest::newRow("time") << "at {{time}}" << "T" << "at 09:30";
    QTest::newRow("title") << "# {{title}}" << "My Note" << "# My Note";
    QTest::newRow("date-format")
        << "{{date:yyyy}}" << "T" << "2026";
    QTest::newRow("whitespace-in-braces")
        << "{{ title }}" << "Hi" << "Hi";
    QTest::newRow("multiple")
        << "{{title}} — {{date}} {{time}}" << "X"
        << "X — 2026-07-08 09:30";
}

void TestNoteTemplates::testExpand()
{
    QFETCH(QString, input);
    QFETCH(QString, title);
    QFETCH(QString, expected);
    const QDateTime now(QDate(2026, 7, 8), QTime(9, 30, 0));
    QCOMPARE(NoteTemplates::expand(input, title, now), expected);
}

void TestNoteTemplates::testExpandUnknownTokenLeftVerbatim()
{
    const QDateTime now(QDate(2026, 7, 8), QTime(9, 30, 0));
    QCOMPARE(NoteTemplates::expand("{{unknown}} {{title}}", "T", now),
             QString("{{unknown}} T"));
}

void TestNoteTemplates::testWriteReadDeleteRoundTrip()
{
    QVERIFY(m_templates->writeTemplate("My Template", "# {{title}}\n\nbody"));
    QCOMPARE(m_templates->readTemplate("My Template"), QString("# {{title}}\n\nbody"));
    // The file exists on disk under .kvit/templates.
    QVERIFY(QFile::exists(QDir(m_templates->templatesDir())
                              .filePath("My Template.md")));
    QVERIFY(m_templates->deleteTemplate("My Template"));
    QVERIFY(m_templates->readTemplate("My Template").isEmpty());
    QVERIFY(!m_templates->deleteTemplate("My Template")); // already gone
}

void TestNoteTemplates::testTemplateNamesSorted()
{
    m_templates->writeTemplate("Zebra", "z");
    m_templates->writeTemplate("apple", "a");
    m_templates->writeTemplate("Mango", "m");
    QCOMPARE(m_templates->templateNames(),
             (QStringList{"apple", "Mango", "Zebra"}));
}

void TestNoteTemplates::testSeedBuiltinsOnce()
{
    QVERIFY(m_templates->templateNames().isEmpty());
    m_templates->seedBuiltinsIfEmpty();
    const QStringList names = m_templates->templateNames();
    QCOMPARE(names.size(), 3);
    QVERIFY(names.contains("Meeting Notes"));
    QVERIFY(names.contains("Project Plan"));
    QVERIFY(names.contains("Daily Journal"));
}

void TestNoteTemplates::testSeedDoesNotOverwrite()
{
    m_templates->writeTemplate("Meeting Notes", "my custom version");
    m_templates->seedBuiltinsIfEmpty(); // the dir is non-empty → no seeding
    QCOMPARE(m_templates->readTemplate("Meeting Notes"),
             QString("my custom version"));
    QCOMPARE(m_templates->templateNames().size(), 1);
}

void TestNoteTemplates::testInstantiateExpandsAndSplitsFrontMatter()
{
    m_templates->writeTemplate("Journal",
        "---\ntags: [journal, daily]\nfavorite: true\n---\n"
        "# {{date}}\n\nWrote on {{date}}.");
    const QVariantMap r = m_templates->instantiate("Journal", "Ignored");
    // Front-matter is parsed out; the body has variables expanded.
    QCOMPARE(r.value("body").toString(),
             QString("# 2026-07-08\n\nWrote on 2026-07-08."));
    QCOMPARE(r.value("tags").toStringList(),
             (QStringList{"journal", "daily"}));
    QCOMPARE(r.value("favorite").toBool(), true);
}

void TestNoteTemplates::testInstantiateUnknownReturnsEmpty()
{
    QVERIFY(m_templates->instantiate("Nope", "T").isEmpty());
}

// Sanitization strips path separators, so two names the user sees as
// distinct can map onto the same file. Writing "A/B" must not silently
// overwrite the template stored as "AB".
void TestNoteTemplates::testDistinctNamesDoNotShareOneFile()
{
    QVERIFY(m_templates->writeTemplate("AB", "first"));
    QCOMPARE(m_templates->readTemplate("AB"), QString("first"));

    // Either the awkward name is rejected outright or it gets a file of its
    // own; what must never happen is it landing on top of "AB".
    m_templates->writeTemplate("A/B", "second");
    QCOMPARE(m_templates->readTemplate("AB"), QString("first"));
}

void TestNoteTemplates::testDeleteDoesNotRemoveAliasedTemplate()
{
    QVERIFY(m_templates->writeTemplate("Notes", "kept"));
    QVERIFY(!m_templates->templateNames().isEmpty());

    // "Note:s" sanitizes to "Notes"; deleting it must not delete "Notes".
    m_templates->deleteTemplate("Note:s");
    QCOMPARE(m_templates->readTemplate("Notes"), QString("kept"));
}

QTEST_MAIN(TestNoteTemplates)
#include "test_notetemplates.moc"
