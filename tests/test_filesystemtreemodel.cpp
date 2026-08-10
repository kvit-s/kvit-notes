// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
#include <QtTest>

#include <QDir>
#include <QFile>
#include <QSignalSpy>
#include <QTemporaryDir>

#include "filesystemtreemodel.h"
#include "ignorerules.h"
#include "notecollection.h"

namespace {

void writeFile(const QString &path, const QByteArray &bytes = QByteArray())
{
    QVERIFY(QDir().mkpath(QFileInfo(path).absolutePath()));
    QFile file(path);
    QVERIFY2(file.open(QIODevice::WriteOnly), qPrintable(file.errorString()));
    QCOMPARE(file.write(bytes), bytes.size());
}

QVariantMap entry(const FileSystemTreeModel &model, const QString &path)
{
    const int row = model.rowOf(path);
    return row < 0 ? QVariantMap() : model.entryAt(row);
}

} // namespace

class FileSystemTreeModelTests : public QObject
{
    Q_OBJECT

private slots:
    void readsOnlyExpandedDirectories();
    void ignoredEntriesAreAbsent();
    void classifiesAndActivatesEachFileRoute();
    void sixThousandFilesStillReadOneDirectory();
};

void FileSystemTreeModelTests::readsOnlyExpandedDirectories()
{
    QTemporaryDir root;
    QVERIFY(root.isValid());
    writeFile(root.filePath(QStringLiteral("Top.txt")), "top\n");
    writeFile(root.filePath(QStringLiteral("src/main.cpp")), "int main() {}\n");
    writeFile(root.filePath(QStringLiteral("src/lib/deep.rs")), "fn deep() {}\n");

    IgnoreRules rules;
    NoteCollection collection;
    FileSystemTreeModel model;
    collection.setIgnoreRules(&rules);
    model.setIgnoreRules(&rules);
    model.setCollection(&collection);
    QVERIFY(collection.openRoot(root.path()));

    QCOMPARE(model.loadedDirectoryCountForTests(), 1);
    QCOMPARE(model.watchedDirectoryCountForTests(), 1);
    QVERIFY(model.rowOf(QStringLiteral("src")) >= 0);
    QVERIFY(model.rowOf(QStringLiteral("src/main.cpp")) < 0);

    model.toggleExpanded(model.rowOf(QStringLiteral("src")));
    QCOMPARE(model.loadedDirectoryCountForTests(), 2);
    QCOMPARE(model.watchedDirectoryCountForTests(), 2);
    QVERIFY(model.rowOf(QStringLiteral("src/main.cpp")) >= 0);
    QVERIFY(model.rowOf(QStringLiteral("src/lib/deep.rs")) < 0);

    model.toggleExpanded(model.rowOf(QStringLiteral("src/lib")));
    QCOMPARE(model.loadedDirectoryCountForTests(), 3);
    QCOMPARE(model.watchedDirectoryCountForTests(), 3);
    QVERIFY(model.rowOf(QStringLiteral("src/lib/deep.rs")) >= 0);

    model.toggleExpanded(model.rowOf(QStringLiteral("src")));
    QCOMPARE(model.loadedDirectoryCountForTests(), 1);
    QCOMPARE(model.watchedDirectoryCountForTests(), 1);
    QVERIFY(model.rowOf(QStringLiteral("src/main.cpp")) < 0);
}

void FileSystemTreeModelTests::ignoredEntriesAreAbsent()
{
    QTemporaryDir root;
    QVERIFY(root.isValid());
    writeFile(root.filePath(QStringLiteral(".gitignore")),
              "build/\nnode_modules/\n");
    writeFile(root.filePath(QStringLiteral("Keep.md")), "# Keep\n");
    writeFile(root.filePath(QStringLiteral("build/out.txt")), "generated\n");
    writeFile(root.filePath(QStringLiteral("node_modules/pkg/README.md")),
              "dependency\n");

    IgnoreRules rules;
    NoteCollection collection;
    FileSystemTreeModel model;
    collection.setIgnoreRules(&rules);
    model.setIgnoreRules(&rules);
    model.setCollection(&collection);
    QVERIFY(collection.openRoot(root.path()));

    QVERIFY(model.rowOf(QStringLiteral("Keep.md")) >= 0);
    QVERIFY(model.rowOf(QStringLiteral(".gitignore")) >= 0);
    QVERIFY(model.rowOf(QStringLiteral("build")) < 0);
    QVERIFY(model.rowOf(QStringLiteral("node_modules")) < 0);
}

void FileSystemTreeModelTests::classifiesAndActivatesEachFileRoute()
{
    QTemporaryDir root;
    QVERIFY(root.isValid());
    writeFile(root.filePath(QStringLiteral("note.md")), "# Note\n");
    writeFile(root.filePath(QStringLiteral("main.rs")), "fn main() {}\n");
    writeFile(root.filePath(QStringLiteral("picture.png")));
    writeFile(root.filePath(QStringLiteral("movie.mp4")));
    writeFile(root.filePath(QStringLiteral("manual.pdf")));

    IgnoreRules rules;
    NoteCollection collection;
    FileSystemTreeModel model;
    collection.setIgnoreRules(&rules);
    model.setIgnoreRules(&rules);
    model.setCollection(&collection);
    QVERIFY(collection.openRoot(root.path()));

    QCOMPARE(entry(model, QStringLiteral("note.md")).value("kind").toString(),
             QStringLiteral("markdown"));
    QCOMPARE(entry(model, QStringLiteral("main.rs")).value("kind").toString(),
             QStringLiteral("text"));
    QCOMPARE(entry(model, QStringLiteral("picture.png")).value("kind").toString(),
             QStringLiteral("image"));
    QCOMPARE(entry(model, QStringLiteral("movie.mp4")).value("kind").toString(),
             QStringLiteral("media"));
    QCOMPARE(entry(model, QStringLiteral("manual.pdf")).value("kind").toString(),
             QStringLiteral("external"));

    QSignalSpy activated(&model, &FileSystemTreeModel::fileActivated);
    model.activate(model.rowOf(QStringLiteral("main.rs")));
    QCOMPARE(activated.count(), 1);
    QCOMPARE(activated.first().at(0).toString(),
             root.filePath(QStringLiteral("main.rs")));
    QCOMPARE(activated.first().at(1).toString(), QStringLiteral("text"));
}

void FileSystemTreeModelTests::sixThousandFilesStillReadOneDirectory()
{
    QTemporaryDir root;
    QVERIFY(root.isValid());
    for (int i = 0; i < 6000; ++i) {
        writeFile(root.filePath(
            QStringLiteral("source-%1.txt").arg(i, 4, 10, QLatin1Char('0'))));
    }

    IgnoreRules rules;
    NoteCollection collection;
    FileSystemTreeModel model;
    collection.setIgnoreRules(&rules);
    model.setIgnoreRules(&rules);
    model.setCollection(&collection);
    QVERIFY(collection.openRoot(root.path()));

    QCOMPARE(model.rowCount(), 6000);
    QCOMPARE(model.loadedDirectoryCountForTests(), 1);
    QCOMPARE(model.watchedDirectoryCountForTests(), 1);
}

QTEST_GUILESS_MAIN(FileSystemTreeModelTests)
#include "test_filesystemtreemodel.moc"
