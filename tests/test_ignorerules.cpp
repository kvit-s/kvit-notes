// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
#include <QtTest>

#include <QDir>
#include <QFile>
#include <QSignalSpy>
#include <QTemporaryDir>

#include "filewatcher.h"
#include "ignorerules.h"
#include "notecollection.h"
#include "settingsstore.h"

namespace {

void writeFile(const QString &path, const QByteArray &contents)
{
    QVERIFY2(QDir().mkpath(QFileInfo(path).absolutePath()),
             qPrintable(QFileInfo(path).absolutePath()));
    QFile file(path);
    QVERIFY2(file.open(QIODevice::WriteOnly | QIODevice::Truncate),
             qPrintable(file.errorString()));
    QCOMPARE(file.write(contents), contents.size());
}

void makeDir(const QString &path)
{
    QVERIFY2(QDir().mkpath(path), qPrintable(path));
}

} // namespace

class IgnoreRulesTests : public QObject
{
    Q_OBJECT

private slots:
    void gitPatternsAndNestedNegation();
    void infoExcludeAndSettingsAreRootSpecific();
    void collectionExcludesRulesFromEveryDerivedIndex();
    void changingSettingsRescansAnOpenCollection();
    void watcherNeverEntersIgnoredDirectories();
    void emptyPolicyPreservesTheExistingWalk();
};

void IgnoreRulesTests::gitPatternsAndNestedNegation()
{
    QTemporaryDir root;
    QVERIFY(root.isValid());
    writeFile(root.filePath(QStringLiteral(".gitignore")),
              "build/\n"
              "*.tmp\n"
              "!important.tmp\n"
              "/docs/generated/*.md\n"
              "logs/**/debug.log\n");
    writeFile(root.filePath(QStringLiteral("src/.gitignore")),
              "*.md\n!keep.md\n");

    IgnoreRules rules;
    rules.setRootPath(root.path(), false);
    const IgnoreRules::Snapshot atRoot = rules.snapshot();

    QVERIFY(atRoot.isExcluded(QStringLiteral("build"), true));
    QVERIFY(atRoot.isExcluded(QStringLiteral("build/generated/note.md"), false));
    QVERIFY(atRoot.isExcluded(QStringLiteral("scratch.tmp"), false));
    QVERIFY(!atRoot.isExcluded(QStringLiteral("important.tmp"), false));
    QVERIFY(atRoot.isExcluded(QStringLiteral("docs/generated/api.md"), false));
    QVERIFY(!atRoot.isExcluded(
        QStringLiteral("src/docs/generated/api.md"), false));
    QVERIFY(atRoot.isExcluded(QStringLiteral("logs/a/b/debug.log"), false));

    const IgnoreRules::Snapshot inSource =
        atRoot.withDirectory(QStringLiteral("src"));
    QVERIFY(inSource.isExcluded(QStringLiteral("src/readme.md"), false));
    QVERIFY(!inSource.isExcluded(QStringLiteral("src/keep.md"), false));
    QVERIFY(!inSource.isExcluded(QStringLiteral("other/readme.md"), false));
}

void IgnoreRulesTests::infoExcludeAndSettingsAreRootSpecific()
{
    QTemporaryDir root;
    QTemporaryDir other;
    QTemporaryDir config;
    QVERIFY(root.isValid());
    QVERIFY(other.isValid());
    QVERIFY(config.isValid());
    writeFile(root.filePath(QStringLiteral(".git/info/exclude")),
              "vendor/\n");

    SettingsStore settings;
    QVERIFY(settings.open(config.filePath(QStringLiteral("settings.json"))));

    IgnoreRules rules;
    rules.setSettings(&settings);
    rules.setRootPath(root.path(), false);
    rules.setAdditionalPatterns(
        QStringList{QStringLiteral("node_modules/"),
                    QStringLiteral("dist/**")});

    const IgnoreRules::Snapshot snapshot = rules.snapshot();
    QVERIFY(snapshot.isExcluded(QStringLiteral("vendor/package/a.md"), false));
    QVERIFY(snapshot.isExcluded(
        QStringLiteral("node_modules/pkg/README.md"), false));
    QVERIFY(snapshot.isExcluded(QStringLiteral("dist/app/main.js"), false));

    IgnoreRules reloaded;
    reloaded.setSettings(&settings);
    reloaded.setRootPath(root.path(), false);
    QCOMPARE(reloaded.additionalPatterns(), rules.additionalPatterns());

    reloaded.setRootPath(other.path(), false);
    QVERIFY(reloaded.additionalPatterns().isEmpty());
    QVERIFY(!reloaded.snapshot().isExcluded(
        QStringLiteral("node_modules/pkg/README.md"), false));
}

void IgnoreRulesTests::collectionExcludesRulesFromEveryDerivedIndex()
{
    QTemporaryDir root;
    QVERIFY(root.isValid());
    writeFile(root.filePath(QStringLiteral(".gitignore")),
              "node_modules/\nbuild/\n");
    writeFile(root.filePath(QStringLiteral("Keep.md")),
              "# Keep\n\n[[README]]\n");
    writeFile(root.filePath(QStringLiteral("node_modules/pkg/README.md")),
              "# Dependency documentation\nneedle from a package\n");
    writeFile(root.filePath(QStringLiteral("build/Generated.md")),
              "# Generated\n");
    writeFile(root.filePath(QStringLiteral("private/Secret.md")),
              "# Secret\n");

    IgnoreRules rules;
    rules.setRootPath(root.path(), false);
    rules.setAdditionalPatterns(QStringList{QStringLiteral("private/")});
    NoteCollection collection;
    collection.setIgnoreRules(&rules);
    QVERIFY(collection.openRoot(root.path()));

    QCOMPARE(collection.noteRelPaths(), QStringList{QStringLiteral("Keep.md")});
    QCOMPARE(collection.noteCount(), 1);
    QVERIFY(collection.resolveWikiTarget(QStringLiteral("README")).isEmpty());
    QVERIFY(collection.noteInfo(QStringLiteral("build/Generated.md")).isEmpty());
    QVERIFY(!collection.folderRelPaths().contains(QStringLiteral("node_modules")));
}

void IgnoreRulesTests::changingSettingsRescansAnOpenCollection()
{
    QTemporaryDir root;
    QVERIFY(root.isValid());
    writeFile(root.filePath(QStringLiteral("Keep.md")), "# Keep\n");
    writeFile(root.filePath(QStringLiteral("drafts/Draft.md")), "# Draft\n");

    IgnoreRules rules;
    rules.setRootPath(root.path(), false);
    NoteCollection collection;
    collection.setIgnoreRules(&rules);
    QVERIFY(collection.openRoot(root.path()));
    QCOMPARE(collection.noteCount(), 2);

    QSignalSpy finished(&collection, &NoteCollection::scanFinished);
    rules.setAdditionalPatterns(QStringList{QStringLiteral("drafts/")});
    if (collection.scanInProgress())
        QVERIFY(finished.wait(5000));
    QTRY_VERIFY_WITH_TIMEOUT(!collection.scanInProgress(), 5000);
    QCOMPARE(collection.noteRelPaths(), QStringList{QStringLiteral("Keep.md")});
}

void IgnoreRulesTests::watcherNeverEntersIgnoredDirectories()
{
    QTemporaryDir root;
    QVERIFY(root.isValid());
    writeFile(root.filePath(QStringLiteral(".gitignore")), "build/\n");
    makeDir(root.filePath(QStringLiteral("visible")));
    for (int i = 0; i < 600; ++i) {
        makeDir(root.filePath(
            QStringLiteral("build/generated-%1/nested").arg(i)));
    }

    IgnoreRules rules;
    rules.setRootPath(root.path(), false);
    FileWatcher watcher;
    watcher.setIgnoreRules(&rules);
    watcher.watchRoot(root.path());
    QTRY_VERIFY_WITH_TIMEOUT(!watcher.discoveryPending(), 5000);

    QCOMPARE(watcher.watchedDirectoryCountForTests(), 2); // root + visible
    QVERIFY(!watcher.watchDegraded());
    QVERIFY(watcher.watchedFilesForTests().contains(
        root.filePath(QStringLiteral(".gitignore"))));

    // Drain before measuring. A registration placed over a tree that was
    // created moments ago can be handed an event for that creation, which
    // macOS delivers after the watch is in place, and the debounced handler
    // emits externalChange whether or not any path survived to it. The count
    // below then reports an event from before the case began: it failed on
    // macOS with one change and none of it fed here, since an ignored path is
    // dropped in feedChange before it can reach the debounce at all.
    watcher.setDebounceMs(0);
    QTest::qWait(50);

    QSignalSpy external(&watcher, &FileWatcher::externalChange);
    watcher.feedChange(root.filePath(QStringLiteral("build")), false);
    QTest::qWait(20);
    QCOMPARE(external.count(), 0);

    rules.setAdditionalPatterns(QStringList{QStringLiteral("visible/")});
    QTRY_VERIFY_WITH_TIMEOUT(!watcher.discoveryPending(), 5000);
    QCOMPARE(watcher.watchedDirectoryCountForTests(), 1);
}

void IgnoreRulesTests::emptyPolicyPreservesTheExistingWalk()
{
    QTemporaryDir root;
    QVERIFY(root.isValid());
    writeFile(root.filePath(QStringLiteral("One.md")), "# One\n");
    writeFile(root.filePath(QStringLiteral("folder/Two.md")), "# Two\n");

    IgnoreRules rules;
    rules.setRootPath(root.path(), false);
    NoteCollection collection;
    collection.setIgnoreRules(&rules);
    QVERIFY(collection.openRoot(root.path()));

    QCOMPARE(collection.noteRelPaths(),
             (QStringList{QStringLiteral("One.md"),
                          QStringLiteral("folder/Two.md")}));
    QCOMPARE(collection.folderRelPaths(),
             QStringList{QStringLiteral("folder")});

    FileWatcher watcher;
    watcher.setIgnoreRules(&rules);
    watcher.watchRoot(root.path());
    QTRY_VERIFY_WITH_TIMEOUT(!watcher.discoveryPending(), 5000);
    // .kvit is still application-owned and skipped; everything else is the
    // same root + folder watch the pre-ignore walk registered.
    QCOMPARE(watcher.watchedDirectoryCountForTests(), 2);
}

QTEST_GUILESS_MAIN(IgnoreRulesTests)
#include "test_ignorerules.moc"
