// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
//
// Single-file mode: `kvit-notes <file.md>` opens a lone
// file with NO vault - the collection stays closed, the full block editor
// works on the file's typed blocks, and the quiet upgrade path (open the
// containing folder as a vault) keeps the file open. These tests drive the
// same composed AppContext the launcher runs.
#include <QTemporaryDir>
#include <QtTest>

#include "appcontext.h"
#include "assetstore.h"

#include <QImage>

namespace {

const char *kSample =
    "# Standalone note\n"
    "\n"
    "Prose with $x^2$ inline math.\n"
    "\n"
    "$$\\frac{a}{b}$$\n"
    "\n"
    "| a | b |\n"
    "| - | - |\n"
    "| 1 | 2 |\n"
    "\n"
    "```mermaid\n"
    "flowchart TD\n"
    "  A --> B\n"
    "```\n";

bool hasBlockOfType(BlockModel *model, Block::BlockType type)
{
    for (int i = 0; i < model->count(); ++i) {
        if (model->blockAt(i) && model->blockAt(i)->blockType() == type)
            return true;
    }
    return false;
}

} // namespace

class TestSingleFileMode : public QObject
{
    Q_OBJECT

private slots:
    void fileArgumentOpensStandalone()
    {
        QTemporaryDir dir;
        const QString file = dir.filePath("note.md");
        {
            QFile f(file);
            QVERIFY(f.open(QIODevice::WriteOnly));
            f.write(kSample);
        }

        AppContext ctx;
        ctx.openSettings(dir.filePath("settings.json"));
        ctx.applyStartupArguments({QStringLiteral("kvit-notes"), file});

        // The file is open; no vault exists or gets created around it.
        QCOMPARE(ctx.documentManager()->currentFilePath(), file);
        QVERIFY(!ctx.noteCollection()->isOpen());

        // Startup with no root finishes immediately - the instant-start
        // property single-file mode is specified around.
        ctx.startupController()->start();
        QTRY_VERIFY(ctx.startupController()->finished());

        // The demo-feature block types all parse as their real typed blocks
        // in-file: math, table, and a diagram-bearing code fence.
        BlockModel *model = ctx.blockModel();
        QVERIFY(model->count() >= 5);
        QVERIFY(hasBlockOfType(model, Block::MathBlock));
        QVERIFY(hasBlockOfType(model, Block::Table));
        QVERIFY(hasBlockOfType(model, Block::CodeBlock));
    }

    void vaultUpgradeKeepsFileOpen()
    {
        QTemporaryDir dir;
        const QString file = dir.filePath("note.md");
        {
            QFile f(file);
            QVERIFY(f.open(QIODevice::WriteOnly));
            f.write(kSample);
        }
        // A sibling so the new vault has something else to index.
        {
            QFile f(dir.filePath("other.md"));
            QVERIFY(f.open(QIODevice::WriteOnly));
            f.write("# Other\n");
        }

        AppContext ctx;
        ctx.openSettings(dir.filePath("settings.json"));
        ctx.applyStartupArguments({QStringLiteral("kvit-notes"), file});
        QVERIFY(!ctx.noteCollection()->isOpen());

        // What the createVaultDialog confirm button runs: open the file's
        // folder as the collection root.
        QVERIFY(ctx.noteCollection()->openRoot(QFileInfo(file).absolutePath()));

        QVERIFY(ctx.noteCollection()->isOpen());
        QCOMPARE(QDir(ctx.noteCollection()->rootPath()).canonicalPath(),
                 QDir(dir.path()).canonicalPath());
        // The open document survives the upgrade and is now a vault note.
        QCOMPARE(ctx.documentManager()->currentFilePath(), file);
        QCOMPARE(ctx.noteCollection()->relativePath(file),
                 QStringLiteral("note.md"));
        QTRY_VERIFY(ctx.noteCollection()->noteCount() >= 2);
    }

    void directoryArgumentStillOpensVaultRoot()
    {
        // The counterpart path must stay intact: a directory argument routes
        // to the startup controller as the collection root.
        QTemporaryDir dir;
        {
            QFile f(dir.filePath("a.md"));
            QVERIFY(f.open(QIODevice::WriteOnly));
            f.write("# A\n");
        }

        AppContext ctx;
        ctx.openSettings(dir.filePath("settings.json"));
        ctx.applyStartupArguments({QStringLiteral("kvit-notes"), dir.path()});
        ctx.startupController()->start();
        QTRY_VERIFY(ctx.startupController()->finished());
        QVERIFY(ctx.noteCollection()->isOpen());
        QCOMPARE(QDir(ctx.noteCollection()->rootPath()).canonicalPath(),
                 QDir(dir.path()).canonicalPath());
    }

    // In single-file mode the assets directory sits beside the note rather
    // than under a vault root, and it is the same repository-owned directory:
    // ingesting an image creates files there under names this application
    // chooses. A link standing in its place would put every pasted image into
    // whatever directory the link named.
    void assetsBesideTheFileMustNotBeALink()
    {
#ifdef Q_OS_WIN
        QSKIP("real symbolic links require elevation on Windows; the junction "
              "case is covered by NoteCollectionTests");
#else
        QTemporaryDir dir;
        QTemporaryDir outside;
        QVERIFY(QDir().mkpath(outside.filePath("elsewhere")));
        {
            QFile f(dir.filePath("note.md"));
            QVERIFY(f.open(QIODevice::WriteOnly));
            f.write(kSample);
        }
        QVERIFY(QFile::link(outside.filePath("elsewhere"),
                            dir.filePath("assets")));

        AssetStore store;
        QImage image(4, 4, QImage::Format_ARGB32);
        image.fill(Qt::magenta);
        // No vault is open, so the note's own directory is the base.
        QVERIFY2(store.ingestImage(image, QStringLiteral("note"), QString(),
                                   dir.path()).isEmpty(),
                 "an image was stored through a linked assets directory");
        QCOMPARE(QDir(outside.filePath("elsewhere"))
                     .entryList(QDir::Files | QDir::NoDotAndDotDot).size(), 0);
#endif
    }
};

QTEST_MAIN(TestSingleFileMode)
#include "test_singlefilemode.moc"
