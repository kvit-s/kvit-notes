// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
#include <QtTest/QtTest>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QFile>
#include <QDir>

#include "notecollection.h"
#include "notelistmodel.h"

// Unit suite for the note-list projection:
// scope → tag filter → sort, pinned floating to the top within every
// sort, manual order per folder, and rebuilds on the collection revision.
class TestNoteListModel : public QObject
{
    Q_OBJECT

private slots:
    void init();
    void cleanup();

    void testScopes();
    void testFavoritesScope();
    void testTagFilterComposesWithScope();
    void testSortByTitle();
    void testSortByCreatedAndModified();
    void testPinnedFloatInEverySort();
    void testManualSortInFolderScope();
    void testManualSortFallsBackToTitleElsewhere();
    void testRebuildsOnCollectionChange();
    void testMetadataChangeUpdatesRowsWithoutReset();
    void testShapeChangesUseIncrementalSignals();
    void testRowLookups();

private:
    void writeNote(const QString &relPath, const QString &content,
                   const QDateTime &mtime = QDateTime());
    QStringList rows() const;

    // The created time a note without a front-matter `created:` date
    // resolves to: the file's birth time where the filesystem reports one,
    // otherwise its modification time (fileCreatedTime() in
    // notecollection.cpp).
    QDateTime effectiveCreated(const QString &relPath) const
    {
        const QFileInfo info(m_dir->filePath(relPath));
        const QDateTime birth = info.birthTime();
        return birth.isValid() ? birth : info.lastModified();
    }

    // Whether undated notes sort as newer than every front-matter date in
    // this fixture, which decides which way the created ordering brackets.
    //
    // "Does the filesystem report a birth time" is the wrong question, and
    // asking it that way failed on macOS CI. APFS does report one, but
    // setting a modification time earlier than the birth time pulls the
    // birth time back to match, so this fixture's planted January mtimes
    // rewrite the creation dates and undated notes end up oldest. On Linux
    // the birth time stays at file creation, so they end up newest. Both
    // are the platform behaving as documented, so ask the question whose
    // answer actually determines the order.
    bool undatedNotesSortNewest() const
    {
        return effectiveCreated(QStringLiteral("Fruit/Elderberry.md"))
            > QDateTime(QDate(2026, 3, 1), QTime(0, 0));
    }

    QTemporaryDir *m_dir = nullptr;
    NoteCollection *m_collection = nullptr;
    NoteListModel *m_model = nullptr;
};

void TestNoteListModel::init()
{
    m_dir = new QTemporaryDir();
    QVERIFY(m_dir->isValid());

    // Distinct mtimes make the modified sort deterministic.
    writeNote("Banana.md", "banana body",
              QDateTime(QDate(2026, 1, 10), QTime(9, 0)));
    writeNote("apple.md", "---\ncreated: 2026-03-01\n---\napple body",
              QDateTime(QDate(2026, 1, 5), QTime(9, 0)));
    writeNote("Cherry.md", "---\npinned: true\nfavorite: true\n"
                           "created: 2026-02-01\n---\ncherry body",
              QDateTime(QDate(2026, 1, 20), QTime(9, 0)));
    QDir().mkpath(m_dir->filePath("Fruit"));
    writeNote("Fruit/Date.md", "---\ntags: [dry]\n---\ndate body",
              QDateTime(QDate(2026, 1, 15), QTime(9, 0)));
    writeNote("Fruit/Elderberry.md", "---\nfavorite: true\n---\nelder body",
              QDateTime(QDate(2026, 1, 1), QTime(9, 0)));

    m_collection = new NoteCollection();
    QVERIFY(m_collection->openRoot(m_dir->path()));

    m_model = new NoteListModel();
    m_model->setCollection(m_collection);
}

void TestNoteListModel::cleanup()
{
    delete m_model;
    delete m_collection;
    delete m_dir;
    m_model = nullptr;
    m_collection = nullptr;
    m_dir = nullptr;
}

void TestNoteListModel::writeNote(const QString &relPath, const QString &content,
                                  const QDateTime &mtime)
{
    const QString path = m_dir->filePath(relPath);
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Text));
    file.write((content + QStringLiteral("\n")).toUtf8());
    file.close();
    if (mtime.isValid()) {
        QFile touch(path);
        QVERIFY(touch.open(QIODevice::ReadWrite));
        touch.setFileTime(mtime, QFileDevice::FileModificationTime);
    }
}

QStringList TestNoteListModel::rows() const
{
    QStringList out;
    for (int i = 0; i < m_model->rowCount(); ++i)
        out.append(m_model->relPathAt(i));
    return out;
}

void TestNoteListModel::testScopes()
{
    QCOMPARE(m_model->rowCount(), 5); // "all" default

    m_model->setScope(QStringLiteral("folder"));
    m_model->setFolderPath(QStringLiteral("Fruit"));
    QCOMPARE(m_model->rowCount(), 2);
    QVERIFY(rows().contains(QStringLiteral("Fruit/Date.md")));

    m_model->setFolderPath(QString()); // the root folder
    QCOMPARE(m_model->rowCount(), 3);
}

void TestNoteListModel::testFavoritesScope()
{
    m_model->setScope(QStringLiteral("favorites"));
    QCOMPARE(rows().size(), 2);
    QVERIFY(rows().contains(QStringLiteral("Cherry.md")));
    QVERIFY(rows().contains(QStringLiteral("Fruit/Elderberry.md")));
}

void TestNoteListModel::testTagFilterComposesWithScope()
{
    m_model->setTagFilter(QStringLiteral("dry"));
    QCOMPARE(rows(), QStringList{QStringLiteral("Fruit/Date.md")});

    m_model->setScope(QStringLiteral("folder"));
    m_model->setFolderPath(QString());
    QCOMPARE(m_model->rowCount(), 0); // no "dry" notes at the root

    m_model->setTagFilter(QString());
    QCOMPARE(m_model->rowCount(), 3);
}

void TestNoteListModel::testSortByTitle()
{
    m_model->setSortMode(QStringLiteral("title"));
    m_model->setAscending(true);
    // Case-insensitive; Cherry is pinned and floats regardless.
    QCOMPARE(rows(),
             (QStringList() << "Cherry.md" << "apple.md" << "Banana.md"
                            << "Fruit/Date.md" << "Fruit/Elderberry.md"));

    m_model->setAscending(false);
    QCOMPARE(rows(),
             (QStringList() << "Cherry.md" << "Fruit/Elderberry.md"
                            << "Fruit/Date.md" << "Banana.md" << "apple.md"));
}

void TestNoteListModel::testSortByCreatedAndModified()
{
    // Modified, descending (the default): file mtimes.
    QCOMPARE(rows().at(0), QStringLiteral("Cherry.md"));   // pinned AND newest
    QCOMPARE(rows().at(1), QStringLiteral("Fruit/Date.md"));
    QCOMPARE(rows().at(4), QStringLiteral("Fruit/Elderberry.md"));

    // Created: front-matter dates beat file times where present, and notes
    // without one take the file's created time (undatedNotesSortNewest()
    // above explains why that differs by platform). Either way the dated
    // notes bracket the list, with Cherry always floating because it is
    // pinned; which note sits at the ends is what changes.
    m_model->setSortMode(QStringLiteral("created"));
    m_model->setAscending(true);
    QCOMPARE(rows().at(0), QStringLiteral("Cherry.md"));

    const QString oldestCreated = undatedNotesSortNewest()
        ? QStringLiteral("apple.md")               // 2026-03-01
        : QStringLiteral("Fruit/Elderberry.md");   // mtime 2026-01-01
    QCOMPARE(rows().at(1), oldestCreated);

    m_model->setAscending(false);
    QCOMPARE(rows().at(0), QStringLiteral("Cherry.md"));
    QCOMPARE(rows().last(), oldestCreated);
}

void TestNoteListModel::testPinnedFloatInEverySort()
{
    const QStringList modes = {QStringLiteral("modified"),
                               QStringLiteral("created"),
                               QStringLiteral("title")};
    for (const QString &mode : modes) {
        m_model->setSortMode(mode);
        for (bool ascending : {true, false}) {
            m_model->setAscending(ascending);
            QCOMPARE(rows().first(), QStringLiteral("Cherry.md"));
        }
    }
}

void TestNoteListModel::testManualSortInFolderScope()
{
    m_model->setScope(QStringLiteral("folder"));
    m_model->setFolderPath(QStringLiteral("Fruit"));
    m_model->setSortMode(QStringLiteral("manual"));
    m_model->setAscending(true);

    // Reorder through the collection; the projection follows.
    QVERIFY(m_collection->setManualPosition(
        QStringLiteral("Fruit/Elderberry.md"), 0));
    QTRY_COMPARE(rows(),
                 (QStringList() << "Fruit/Elderberry.md" << "Fruit/Date.md"));

    QVERIFY(m_collection->setManualPosition(
        QStringLiteral("Fruit/Elderberry.md"), 1));
    QTRY_COMPARE(rows(),
                 (QStringList() << "Fruit/Date.md" << "Fruit/Elderberry.md"));
}

void TestNoteListModel::testManualSortFallsBackToTitleElsewhere()
{
    m_model->setSortMode(QStringLiteral("manual"));
    m_model->setAscending(true);
    // "all" scope has no single manual order; degrade to title.
    QCOMPARE(rows(),
             (QStringList() << "Cherry.md" << "apple.md" << "Banana.md"
                            << "Fruit/Date.md" << "Fruit/Elderberry.md"));
}

void TestNoteListModel::testRebuildsOnCollectionChange()
{
    QCOMPARE(m_model->rowCount(), 5);
    QVERIFY(!m_collection->createNote(QString(), QStringLiteral("Fig")).isEmpty());
    QTRY_COMPARE(m_model->rowCount(), 6);
    QTRY_VERIFY(m_model->rowOf(QStringLiteral("Fig.md")) >= 0);

    QVERIFY(m_collection->deleteNote(QStringLiteral("Fig.md")));
    QTRY_COMPARE(m_model->rowCount(), 5);

    // Metadata changes reproject too: favoriting adds to that scope.
    m_model->setScope(QStringLiteral("favorites"));
    QCOMPARE(m_model->rowCount(), 2);
    QVERIFY(m_collection->setFavorite(QStringLiteral("Banana.md"), true));
    QTRY_COMPARE(m_model->rowCount(), 3);
}

void TestNoteListModel::testMetadataChangeUpdatesRowsWithoutReset()
{
    m_model->setSortMode(QStringLiteral("title"));
    m_model->setAscending(true);

    QSignalSpy resetSpy(m_model, &QAbstractItemModel::modelReset);
    QSignalSpy dataSpy(m_model, &QAbstractItemModel::dataChanged);

    QVERIFY(m_collection->setTags(QStringLiteral("Banana.md"),
                                  QStringList{QStringLiteral("fresh")}));

    QTRY_VERIFY(dataSpy.count() > 0);
    QCOMPARE(resetSpy.count(), 0);
    const int row = m_model->rowOf(QStringLiteral("Banana.md"));
    QVERIFY(row >= 0);
    const QModelIndex index = m_model->index(row, 0);
    QCOMPARE(m_model->data(index, NoteListModel::TagsRole).toStringList(),
             QStringList{QStringLiteral("fresh")});
}

void TestNoteListModel::testShapeChangesUseIncrementalSignals()
{
    m_model->setSortMode(QStringLiteral("title"));
    m_model->setAscending(true);

    QSignalSpy resetSpy(m_model, &QAbstractItemModel::modelReset);
    QSignalSpy insertSpy(m_model, &QAbstractItemModel::rowsInserted);
    QSignalSpy removeSpy(m_model, &QAbstractItemModel::rowsRemoved);
    QSignalSpy moveSpy(m_model, &QAbstractItemModel::rowsMoved);

    QCOMPARE(m_collection->createNote(QString(), QStringLiteral("Fig")),
             QStringLiteral("Fig.md"));
    QTRY_COMPARE(m_model->rowCount(), 6);
    QTRY_VERIFY(insertSpy.count() > 0);
    QCOMPARE(resetSpy.count(), 0);
    QVERIFY(m_model->rowOf(QStringLiteral("Fig.md")) >= 0);

    insertSpy.clear();
    removeSpy.clear();
    QVERIFY(m_collection->deleteNote(QStringLiteral("Fig.md")));
    QTRY_COMPARE(m_model->rowCount(), 5);
    QTRY_VERIFY(removeSpy.count() > 0);
    QCOMPARE(resetSpy.count(), 0);
    QCOMPARE(m_model->rowOf(QStringLiteral("Fig.md")), -1);

    m_model->setScope(QStringLiteral("folder"));
    m_model->setFolderPath(QStringLiteral("Fruit"));
    m_model->setSortMode(QStringLiteral("manual"));
    m_model->setAscending(true);
    // Notes with no recorded manual position order by created time, oldest
    // first (NoteCollection::manualOrder). Neither Fruit note carries a
    // front-matter date, so which one leads depends on the platform's
    // created times, as undatedNotesSortNewest() explains.
    const QStringList beforeMove = rows();
    QCOMPARE(beforeMove.size(), 2);
    QVERIFY(beforeMove.contains(QStringLiteral("Fruit/Date.md")));
    QVERIFY(beforeMove.contains(QStringLiteral("Fruit/Elderberry.md")));

    resetSpy.clear();
    insertSpy.clear();
    removeSpy.clear();
    moveSpy.clear();

    // Promote whichever note is currently second. Naming one outright made
    // this a no-op on platforms where it already led, and a reorder that
    // changes nothing emits no rowsMoved, which is what failed on macOS.
    const QString promoted = beforeMove.at(1);
    QVERIFY(m_collection->setManualPosition(promoted, 0));
    QTRY_COMPARE(rows(), (QStringList() << promoted << beforeMove.at(0)));
    QTRY_VERIFY(moveSpy.count() > 0);
    QCOMPARE(insertSpy.count(), 0);
    QCOMPARE(removeSpy.count(), 0);
    QCOMPARE(resetSpy.count(), 0);
}

void TestNoteListModel::testRowLookups()
{
    const int row = m_model->rowOf(QStringLiteral("Banana.md"));
    QVERIFY(row >= 0);
    QCOMPARE(m_model->relPathAt(row), QStringLiteral("Banana.md"));
    QCOMPARE(m_model->rowOf(QStringLiteral("missing.md")), -1);
    QCOMPARE(m_model->relPathAt(99), QString());

    const QModelIndex index = m_model->index(row, 0);
    QCOMPARE(m_model->data(index, NoteListModel::TitleRole).toString(),
             QStringLiteral("Banana"));
    QCOMPARE(m_model->data(index, NoteListModel::SnippetRole).toString(),
             QStringLiteral("banana body"));
    QCOMPARE(m_model->data(index, NoteListModel::WordCountRole).toInt(), 2);
    QCOMPARE(m_model->data(index, NoteListModel::PinnedRole).toBool(), false);
}

QTEST_MAIN(TestNoteListModel)
#include "test_notelistmodel.moc"
