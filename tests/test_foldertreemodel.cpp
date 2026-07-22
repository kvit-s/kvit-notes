// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
#include <QtTest/QtTest>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSignalSpy>
#include <QTemporaryDir>

#include "foldertreemodel.h"
#include "notecollection.h"

class TestFolderTreeModel : public QObject
{
    Q_OBJECT

private slots:
    void recursiveCountsUpdateIncrementally();
    void visibleShapeChangesUseIncrementalSignals();

private:
    static void writeNote(const QString &path, const QString &content);
    static int noteCountAt(FolderTreeModel *model, const QString &relPath);
};

void TestFolderTreeModel::writeNote(const QString &path, const QString &content)
{
    QFileInfo info(path);
    QVERIFY(QDir().mkpath(info.absolutePath()));
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write(content.toUtf8());
}

int TestFolderTreeModel::noteCountAt(FolderTreeModel *model,
                                     const QString &relPath)
{
    const int row = model->rowOf(relPath);
    if (row < 0)
        return -1;
    return model->data(model->index(row, 0),
                       FolderTreeModel::NoteCountRole).toInt();
}

void TestFolderTreeModel::recursiveCountsUpdateIncrementally()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    writeNote(dir.filePath(QStringLiteral("A/One.md")), "one\n");
    writeNote(dir.filePath(QStringLiteral("A/Two.md")), "two\n");
    writeNote(dir.filePath(QStringLiteral("A/B/Deep.md")), "deep\n");
    writeNote(dir.filePath(QStringLiteral("C/Other.md")), "other\n");

    NoteCollection collection;
    QVERIFY(collection.openRoot(dir.path()));

    FolderTreeModel model;
    model.setCollection(&collection);

    QCOMPARE(model.rowCount(), 3);
    QCOMPARE(noteCountAt(&model, QStringLiteral("A")), 3);
    QCOMPARE(noteCountAt(&model, QStringLiteral("A/B")), 1);
    QCOMPARE(noteCountAt(&model, QStringLiteral("C")), 1);

    QSignalSpy resetSpy(&model, &QAbstractItemModel::modelReset);
    QSignalSpy dataSpy(&model, &QAbstractItemModel::dataChanged);

    QCOMPARE(collection.createNote(QStringLiteral("A/B"),
                                   QStringLiteral("Fresh")),
             QStringLiteral("A/B/Fresh.md"));
    QTRY_VERIFY(dataSpy.count() > 0);
    QCOMPARE(resetSpy.count(), 0);
    QCOMPARE(noteCountAt(&model, QStringLiteral("A")), 4);
    QCOMPARE(noteCountAt(&model, QStringLiteral("A/B")), 2);

    QVERIFY(collection.moveNote(QStringLiteral("A/One.md"),
                                QStringLiteral("C")));
    QTRY_COMPARE(noteCountAt(&model, QStringLiteral("A")), 3);
    QCOMPARE(noteCountAt(&model, QStringLiteral("A/B")), 2);
    QCOMPARE(noteCountAt(&model, QStringLiteral("C")), 2);

    QVERIFY(collection.deleteNote(QStringLiteral("A/B/Fresh.md")));
    QTRY_COMPARE(noteCountAt(&model, QStringLiteral("A")), 2);
    QCOMPARE(noteCountAt(&model, QStringLiteral("A/B")), 1);
    QCOMPARE(noteCountAt(&model, QStringLiteral("C")), 2);
}

void TestFolderTreeModel::visibleShapeChangesUseIncrementalSignals()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    writeNote(dir.filePath(QStringLiteral("A/One.md")), "one\n");
    writeNote(dir.filePath(QStringLiteral("A/B/Deep.md")), "deep\n");
    writeNote(dir.filePath(QStringLiteral("C/Other.md")), "other\n");

    NoteCollection collection;
    QVERIFY(collection.openRoot(dir.path()));

    FolderTreeModel model;
    model.setCollection(&collection);
    QCOMPARE(model.rowCount(), 3);

    QSignalSpy resetSpy(&model, &QAbstractItemModel::modelReset);
    QSignalSpy insertSpy(&model, &QAbstractItemModel::rowsInserted);
    QSignalSpy removeSpy(&model, &QAbstractItemModel::rowsRemoved);

    QCOMPARE(collection.createFolder(QString(), QStringLiteral("B")),
             QStringLiteral("B"));
    QTRY_VERIFY(insertSpy.count() > 0);
    QCOMPARE(resetSpy.count(), 0);
    QVERIFY(model.rowOf(QStringLiteral("B")) >= 0);

    insertSpy.clear();
    removeSpy.clear();
    collection.setFolderExpanded(QStringLiteral("A"), false);
    QTRY_COMPARE(model.rowOf(QStringLiteral("A/B")), -1);
    QTRY_VERIFY(removeSpy.count() > 0);
    QCOMPARE(resetSpy.count(), 0);

    insertSpy.clear();
    removeSpy.clear();
    collection.setFolderExpanded(QStringLiteral("A"), true);
    QTRY_VERIFY(model.rowOf(QStringLiteral("A/B")) >= 0);
    QTRY_VERIFY(insertSpy.count() > 0);
    QCOMPARE(resetSpy.count(), 0);

    insertSpy.clear();
    removeSpy.clear();
    QVERIFY(collection.deleteFolder(QStringLiteral("B")));
    QTRY_COMPARE(model.rowOf(QStringLiteral("B")), -1);
    QTRY_VERIFY(removeSpy.count() > 0);
    QCOMPARE(resetSpy.count(), 0);
}

QTEST_MAIN(TestFolderTreeModel)
#include "test_foldertreemodel.moc"
