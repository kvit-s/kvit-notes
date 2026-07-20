// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
#include <QtTest>

#include "todometa.h"

// Corpus for the todo metadata tail (phase10-plan.md decision 10): the
// Obsidian Tasks 📅/priority tokens split off the editable text, round-trip,
// and stay out of displayText (which counts and search run over).
class TestTodoMeta : public QObject
{
    Q_OBJECT

    static QString cal() { return QString::fromUtf8("\xF0\x9F\x93\x85"); }   // 📅
    static QString high() { return QString::fromUtf8("\xE2\x8F\xAB"); }      // ⏫
    static QString med() { return QString::fromUtf8("\xF0\x9F\x94\xBC"); }   // 🔼
    static QString low() { return QString::fromUtf8("\xF0\x9F\x94\xBD"); }   // 🔽

private slots:
    void noMetadata()
    {
        const auto m = TodoMeta::parse("buy milk");
        QCOMPARE(m.text, QString("buy milk"));
        QCOMPARE(m.due, QString());
        QCOMPARE(m.priority, int(TodoMeta::None));
        QCOMPARE(m.tail, QString());
    }

    void dueDate()
    {
        const auto m = TodoMeta::parse("buy milk " + cal() + " 2026-07-15");
        QCOMPARE(m.text, QString("buy milk"));
        QCOMPARE(m.due, QString("2026-07-15"));
        QCOMPARE(m.priority, int(TodoMeta::None));
    }

    void priorityAndDate()
    {
        const auto m = TodoMeta::parse("ship it " + high() + " " + cal() + " 2026-01-02");
        QCOMPARE(m.text, QString("ship it"));
        QCOMPARE(m.due, QString("2026-01-02"));
        QCOMPARE(m.priority, int(TodoMeta::High));
    }

    void priorityOnly()
    {
        QCOMPARE(TodoMeta::parse("later " + low()).priority, int(TodoMeta::Low));
        QCOMPARE(TodoMeta::parse("soon " + med()).priority, int(TodoMeta::Medium));
    }

    void buildRoundTrips_data()
    {
        QTest::addColumn<QString>("text");
        QTest::addColumn<QString>("due");
        QTest::addColumn<int>("priority");
        QTest::newRow("plain") << "task" << "" << int(TodoMeta::None);
        QTest::newRow("due") << "task" << "2026-07-15" << int(TodoMeta::None);
        QTest::newRow("prio") << "task" << "" << int(TodoMeta::High);
        QTest::newRow("both") << "task" << "2026-07-15" << int(TodoMeta::Medium);
    }
    void buildRoundTrips()
    {
        QFETCH(QString, text);
        QFETCH(QString, due);
        QFETCH(int, priority);
        const QString built = TodoMeta::build(text, due, priority);
        const auto m = TodoMeta::parse(built);
        QCOMPARE(m.text, text);
        QCOMPARE(m.due, due);
        QCOMPARE(m.priority, priority);
    }

    void displayTextExcludesTail()
    {
        // The metadata emoji never reach counts/search.
        const QString content = "review PR " + high() + " " + cal() + " 2026-07-15";
        QCOMPARE(TodoMeta::displayText(content), QString("review PR"));
    }

    void formattedTextSurvives()
    {
        const auto m = TodoMeta::parse("**bold** task " + cal() + " 2026-07-15");
        QCOMPARE(m.text, QString("**bold** task"));
    }
};

QTEST_MAIN(TestTodoMeta)
#include "test_todometa.moc"
