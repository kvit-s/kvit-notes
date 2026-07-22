// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
#include <QtTest/QtTest>
#include <QTemporaryDir>
#include <QFile>
#include <QDir>

#include "notecollection.h"
#include "quickswitchermodel.h"

// Unit suite for the quick switcher's filter: ranking through the
// shared fuzzy matcher over title and relPath, the
// empty-query recency listing, and the row shape QML renders.
class TestQuickSwitcherModel : public QObject
{
    Q_OBJECT

private slots:
    void init();
    void cleanup();

    void testEmptyQueryListsAllByRecency();
    void testRankingTiers();
    void testNoCollection();

private:
    void writeNote(const QString &relPath, const QString &content)
    {
        QFileInfo info(m_dir->filePath(relPath));
        QVERIFY(QDir().mkpath(info.absolutePath()));
        QFile file(m_dir->filePath(relPath));
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write(content.toUtf8());
    }

    QTemporaryDir *m_dir = nullptr;
    NoteCollection *m_collection = nullptr;
    QuickSwitcherModel *m_model = nullptr;
};

void TestQuickSwitcherModel::init()
{
    m_dir = new QTemporaryDir();
    QVERIFY(m_dir->isValid());
    m_collection = new NoteCollection();
    m_model = new QuickSwitcherModel();
    m_model->setCollection(m_collection);
}

void TestQuickSwitcherModel::cleanup()
{
    delete m_model;
    delete m_collection;
    delete m_dir;
    m_model = nullptr;
    m_collection = nullptr;
    m_dir = nullptr;
}

void TestQuickSwitcherModel::testEmptyQueryListsAllByRecency()
{
    writeNote("Old.md", "old\n");
    writeNote("New.md", "new\n");
    QVERIFY(m_collection->openRoot(m_dir->path()));

    // Make one clearly newer through the save seam.
    writeNote("New.md", "newer content\n");
    m_collection->noteSaved(m_dir->filePath("New.md"));

    const QVariantList rows = m_model->itemsFor("");
    QCOMPARE(rows.size(), 2);
    QCOMPARE(rows.first().toMap().value("title").toString(), QString("New"));
    QVERIFY(rows.first().toMap().contains("relPath"));
    QVERIFY(rows.first().toMap().contains("folder"));
}

void TestQuickSwitcherModel::testRankingTiers()
{
    writeNote("Plan.md", "x\n");                 // prefix match for "pl"
    writeNote("Weekly planning.md", "x\n");      // word-prefix match
    writeNote("Pipeline.md", "x\n");             // subsequence ("p...l")
    writeNote("Other.md", "x\n");                // no match
    QVERIFY(m_collection->openRoot(m_dir->path()));

    const QVariantList rows = m_model->itemsFor("pl");
    QVERIFY(rows.size() >= 2);
    QCOMPARE(rows.at(0).toMap().value("title").toString(), QString("Plan"));
    QCOMPARE(rows.at(1).toMap().value("title").toString(),
             QString("Weekly planning"));
    for (const QVariant &row : rows)
        QVERIFY(row.toMap().value("title").toString() != QString("Other"));

    // relPath matches too: a folder name reaches notes inside it.
    writeNote("projects/Roadmap.md", "x\n");
    m_collection->refresh();
    const QVariantList byPath = m_model->itemsFor("projects/road");
    QCOMPARE(byPath.size(), 1);
    QCOMPARE(byPath.first().toMap().value("relPath").toString(),
             QString("projects/Roadmap.md"));
}

void TestQuickSwitcherModel::testNoCollection()
{
    QuickSwitcherModel bare;
    QVERIFY(bare.itemsFor("anything").isEmpty());
}

QTEST_MAIN(TestQuickSwitcherModel)
#include "test_quickswitchermodel.moc"
