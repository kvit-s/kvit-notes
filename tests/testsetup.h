// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
#ifndef TESTSETUP_H
#define TESTSETUP_H

#include <QQmlEngine>
#include <QQmlContext>
#include <QQuickStyle>
#include <QDir>
#include <QFile>

#include "blockkindregistry.h"
#include "blockmodel.h"
#include "extensionregistry.h"
#include "markdownformatter.h"
#include "undostack.h"
#include "documentmanager.h"
#include "clipboardhelper.h"
#include "blockeditorengine.h"
#include "blockmenumodel.h"
#include "mathcommandmodel.h"
#include "codelanguages.h"
#include "imageassets.h"
#include "blockattributes.h"
#include "shortcutcatalog.h"
#include "accessibilityannouncer.h"
#include "globalhotkey.h"
#include "systemtray.h"
#include "filewatcher.h"
#include "tabledata.h"
#include "todometa.h"
#include "kanbandata.h"
#include "mathrenderer.h"
#include "documentselection.h"
#include "documentsearch.h"
#include "documentoutline.h"
#include "documentstats.h"
#include "documentexporter.h"
#include "documentserializer.h"
#include "notecollection.h"
#include "navigationhistory.h"
#include "quickswitchermodel.h"
#include "querytools.h"
#include "foldertreemodel.h"
#include "notelistmodel.h"
#include "collectionsearch.h"
#include "notetemplates.h"
#include "documentimporter.h"
#include "embedmetadata.h"
#include "settingsstore.h"
#include "perflog.h"
#include "updatechecker.h"
#include "diagrams/diagramcanvas.h"

// A hermetic embed fetcher for the Qt Quick tests: returns canned OpenGraph
// HTML synchronously, so embed cards render without touching the network.
class FakeEmbedFetcher : public EmbedFetcher
{
public:
    void fetch(const QString &url,
               std::function<void(bool, const QString &)> done) override
    {
        const QString html =
            "<html><head>"
            "<meta property=\"og:title\" content=\"Example Page Title\">"
            "<meta property=\"og:description\" content=\"A short description of "
            "the linked page for the preview card.\">"
            "<meta property=\"og:image\" content=\"https://example.com/thumb.png\">"
            "</head><body>x</body></html>";
        done(true, html);
    }
};
#include "theme.h"
#include "typography.h"

#include <QTemporaryDir>
#include <QImage>
#include <QPainter>

// A file-writing seam for the Qt Quick tests: lets a test act as "another
// program" editing a note on disk, so the FileWatcher → refreshPaths live
// paths (backlinks panel, query block) can be exercised end to end.
class TestFileHelper : public QObject
{
    Q_OBJECT
public:
    using QObject::QObject;
    Q_INVOKABLE bool writeFile(const QString &path, const QString &text)
    {
        QFile file(path);
        if (!file.open(QIODevice::WriteOnly | QIODevice::Text))
            return false;
        file.write(text.toUtf8());
        return true;
    }
    Q_INVOKABLE QString readFile(const QString &path)
    {
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
            return QString();
        return QString::fromUtf8(file.readAll());
    }
};

// Shared qmlEngineAvailable setup for the Qt Quick Test binaries
// (test_integration and test_visual). Mirrors the context properties
// installed by the real application's main.cpp, and adds the
// screenshotDir property used by the saveScreenshot test helper.
class Setup : public QObject
{
    Q_OBJECT

public:
    Setup() {}

public slots:
    void qmlEngineAvailable(QQmlEngine *engine)
    {
        QQuickStyle::setStyle("Fusion");

        qmlRegisterType<BlockEditorEngine>("Kvit", 1, 0, "BlockEditorEngine");
        qmlRegisterType<SettingsStore>("Kvit", 1, 0, "SettingsStore");
        qmlRegisterType<DiagramCanvas>("Kvit", 1, 0, "DiagramCanvas");
        qmlRegisterUncreatableType<Theme>("Kvit", 1, 0, "Theme",
                                          QStringLiteral("Use the theme context property"));
        qmlRegisterUncreatableType<Block>("Kvit", 1, 0, "Block",
                                          QStringLiteral("Block is model data; the enum is what QML needs"));

        // Create the undo stack
        UndoStack *undoStack = new UndoStack(engine);

        // Create the block model and connect it to the undo stack
        BlockModel *model = new BlockModel(engine);
        model->setUndoStack(undoStack);
        model->initializeWithSampleData();

        // Create the document manager
        DocumentManager *documentManager = new DocumentManager(engine);
        documentManager->setBlockModel(model);
        documentManager->setUndoStack(undoStack);

        // Clear the undo stack after loading sample data (so sample data isn't undoable)
        undoStack->clear();
        undoStack->setClean();

        engine->rootContext()->setContextProperty("blockModel", model);
        engine->rootContext()->setContextProperty("undoStack", undoStack);
        engine->rootContext()->setContextProperty("documentManager", documentManager);

        MarkdownFormatter *formatter = new MarkdownFormatter(engine);
        engine->rootContext()->setContextProperty("markdownFormatter", formatter);

        ClipboardHelper *clipboardHelper = new ClipboardHelper(engine);
        engine->rootContext()->setContextProperty("clipboard", clipboardHelper);

        BlockMenuModel *blockMenuModel = new BlockMenuModel(engine);
        engine->rootContext()->setContextProperty("blockMenuModel", blockMenuModel);
        MathCommandModel *mathCommandModel = new MathCommandModel(engine);
        engine->rootContext()->setContextProperty("mathCommandModel", mathCommandModel);

        DocumentSelection *documentSelection = new DocumentSelection(engine);
        documentSelection->setModel(model);
        engine->rootContext()->setContextProperty("documentSelection", documentSelection);

        DocumentSearch *documentSearch = new DocumentSearch(engine);
        documentSearch->setModel(model);
        engine->rootContext()->setContextProperty("documentSearch", documentSearch);

        DocumentOutline *documentOutline = new DocumentOutline(engine);
        documentOutline->setModel(model);
        engine->rootContext()->setContextProperty("documentOutline", documentOutline);

        DocumentStats *documentStats = new DocumentStats(engine);
        documentStats->setModel(model);
        engine->rootContext()->setContextProperty("documentStats", documentStats);

        // Theme is wired below (created later); the exporter renders with its
        // built-in default palette when none is set, which the assertions use.
        DocumentExporter *documentExporter = new DocumentExporter(engine);
        engine->rootContext()->setContextProperty("documentExporter", documentExporter);

        DocumentSerializer *documentSerializer = new DocumentSerializer(engine);
        engine->rootContext()->setContextProperty("documentSerializer", documentSerializer);

        // The collection starts UNOPENED: the shell renders single-file
        // geometry, so every pre-Phase-8 test runs unchanged. Collection
        // tests open a fresh subdirectory of testCollectionDir in init()
        // and close it in cleanup().
        NoteCollection *noteCollection = new NoteCollection(engine);
        engine->rootContext()->setContextProperty("noteCollection", noteCollection);
        FolderTreeModel *folderTreeModel = new FolderTreeModel(engine);
        folderTreeModel->setCollection(noteCollection);
        engine->rootContext()->setContextProperty("folderTreeModel", folderTreeModel);
        NoteListModel *noteListModel = new NoteListModel(engine);
        noteListModel->setCollection(noteCollection);
        engine->rootContext()->setContextProperty("noteListModel", noteListModel);
        CollectionSearch *collectionSearch = new CollectionSearch(engine);
        collectionSearch->setCollection(noteCollection);
        engine->rootContext()->setContextProperty("collectionSearch", collectionSearch);

        NoteTemplates *noteTemplates = new NoteTemplates(engine);
        noteTemplates->setCollection(noteCollection);
        engine->rootContext()->setContextProperty("noteTemplates", noteTemplates);

        // Wiki-link navigation, mirroring main.cpp's wiring.
        NavigationHistory *navigationHistory = new NavigationHistory(engine);
        QObject::connect(noteCollection, &NoteCollection::noteMoved,
                         navigationHistory, &NavigationHistory::renamePath);
        QObject::connect(noteCollection, &NoteCollection::noteRemoved,
                         navigationHistory, &NavigationHistory::dropPath);
        QObject::connect(noteCollection, &NoteCollection::rootChanged,
                         navigationHistory, &NavigationHistory::clear);
        engine->rootContext()->setContextProperty("navigationHistory",
                                                  navigationHistory);

        // Mirror main.cpp's updateChecker property. No fetcher is installed,
        // so it can never fetch; the status-bar notice binding just resolves.
        UpdateChecker *updateChecker = new UpdateChecker(engine);
        engine->rootContext()->setContextProperty("updateChecker", updateChecker);
        QuickSwitcherModel *quickSwitcherModel = new QuickSwitcherModel(engine);
        quickSwitcherModel->setCollection(noteCollection);
        engine->rootContext()->setContextProperty("quickSwitcherModel",
                                                  quickSwitcherModel);
        QueryTools *queryTools = new QueryTools(engine);
        queryTools->setCollection(noteCollection);
        engine->rootContext()->setContextProperty("queryTools", queryTools);

        DocumentImporter *documentImporter = new DocumentImporter(engine);
        documentImporter->setCollection(noteCollection);
        engine->rootContext()->setContextProperty("documentImporter", documentImporter);

        EmbedMetadata *embedMetadata = new EmbedMetadata(engine);
        embedMetadata->setFetcher(new FakeEmbedFetcher);
        embedMetadata->setCollection(noteCollection);
        engine->rootContext()->setContextProperty("embedMetadata", embedMetadata);

        if (!m_collectionDir.isValid())
            qWarning("testsetup: temporary collection dir is invalid");
        engine->rootContext()->setContextProperty("testCollectionDir",
                                                  m_collectionDir.path());

        // A sample image on disk for the image-block storyboard/integration.
        const QString samplePath = m_collectionDir.filePath("sample.png");
        {
            QImage sample(240, 150, QImage::Format_ARGB32);
            sample.fill(QColor("#4a90d9"));
            QPainter p(&sample);
            p.setBrush(QColor("#ffd166"));
            p.setPen(Qt::NoPen);
            p.drawEllipse(60, 30, 120, 90);
            p.end();
            sample.save(samplePath, "PNG");
        }
        engine->rootContext()->setContextProperty("sampleImagePath", samplePath);

        // Sample audio/video on disk for the media-block storyboard/integration
        // (phase10 step 10). Copied from the committed fixtures into the
        // collection so they resolve like any note asset.
#ifdef KVIT_TEST_FIXTURES
        {
            const QString fx = QStringLiteral(KVIT_TEST_FIXTURES);
            const QString audio = m_collectionDir.filePath("sample.wav");
            const QString video = m_collectionDir.filePath("sample.mp4");
            QFile::remove(audio);
            QFile::copy(fx + QStringLiteral("/sample.wav"), audio);
            QFile::remove(video);
            QFile::copy(fx + QStringLiteral("/sample.mp4"), video);
            engine->rootContext()->setContextProperty("sampleAudioPath", audio);
            engine->rootContext()->setContextProperty("sampleVideoPath", video);

            // A sample image for the image-effects storyboard (phase12 §1.2.8):
            // copied into the collection so a relative ![](sample.png) resolves.
            const QString image = m_collectionDir.filePath("sample.png");
            QFile::remove(image);
            QFile::copy(fx + QStringLiteral("/sample.png"), image);
            engine->rootContext()->setContextProperty("sampleImagePath", image);
        }
#endif

        // Per-user settings on a temp path, mirroring main.cpp.
        SettingsStore *settingsStore = new SettingsStore(engine);
        settingsStore->open(m_collectionDir.filePath(
            QStringLiteral("app-settings.json")));
        engine->rootContext()->setContextProperty("appSettings", settingsStore);
        engine->rootContext()->setContextProperty("perfLog", &PerfLog::instance());

        Theme *theme = new Theme(engine);
        theme->setSettings(settingsStore);
        engine->rootContext()->setContextProperty("theme", theme);

        Typography *typography = new Typography(engine);
        typography->setSettings(settingsStore);
        engine->rootContext()->setContextProperty("typography", typography);

        engine->rootContext()->setContextProperty(
            "codeLanguageList",
            QVariant::fromValue(CodeLanguages::supportedLanguages()));

        ImageAssets *imageAssets = new ImageAssets(engine);
        engine->rootContext()->setContextProperty("imageAssets", imageAssets);

        BlockAttributes *blockAttributes = new BlockAttributes(engine);
        engine->rootContext()->setContextProperty("blockAttributes", blockAttributes);

        ShortcutCatalog *shortcutCatalog = new ShortcutCatalog(engine);
        engine->rootContext()->setContextProperty("shortcutCatalog", shortcutCatalog);

        AccessibilityAnnouncer *a11y = new AccessibilityAnnouncer(engine);
        engine->rootContext()->setContextProperty("a11y", a11y);

        GlobalHotkey *globalHotkey = new GlobalHotkey(engine);
        engine->rootContext()->setContextProperty("globalHotkey", globalHotkey);
        SystemTray *systemTray = new SystemTray(engine);
        engine->rootContext()->setContextProperty("systemTray", systemTray);
        FileWatcher *fileWatcher = new FileWatcher(engine);
        engine->rootContext()->setContextProperty("fileWatcher", fileWatcher);
        // Mirror main.cpp: external changes refresh the affected notes, so
        // revision-bound views (backlinks, query block) update live in the
        // tests exactly as in the app.
        QObject::connect(fileWatcher, &FileWatcher::externalChangePaths,
                         noteCollection, &NoteCollection::refreshPaths);
        engine->rootContext()->setContextProperty(
            "testFiles", new TestFileHelper(engine));

        TableTools *tableTools = new TableTools(engine);
        engine->rootContext()->setContextProperty("tableTools", tableTools);

        TodoMetaTools *todoMeta = new TodoMetaTools(engine);
        engine->rootContext()->setContextProperty("todoMeta", todoMeta);

        KanbanTools *kanbanTools = new KanbanTools(engine);
        engine->rootContext()->setContextProperty("kanbanTools", kanbanTools);
        engine->addImageProvider(QStringLiteral("math"), new MathImageProvider);
        MathTools *mathTools = new MathTools(engine);
        engine->rootContext()->setContextProperty("mathRenderer", mathTools);

        // The two extension seams, mirroring main.cpp. The tests
        // install no module, so these are the same inert registries the open
        // app runs with: the shell's slot Loaders stay empty and the delegate
        // chooser sees only the built-in fence kinds.
        engine->rootContext()->setContextProperty(
            "blockKinds", &BlockKindRegistry::instance());
        engine->rootContext()->setContextProperty(
            "extensions", &ExtensionRegistry::instance());

        // Screenshot directory for the saveScreenshot helper.
        // build.sh wipes and exports KVIT_SHOT_DIR; standalone runs fall
        // back to <cwd>/screenshots.
        QString shotDir = qEnvironmentVariable("KVIT_SHOT_DIR");
        if (shotDir.isEmpty()) {
            shotDir = QDir::currentPath() + "/screenshots";
        }
        QDir().mkpath(shotDir);
        engine->rootContext()->setContextProperty("screenshotDir", shotDir);
    }

private:
    QTemporaryDir m_collectionDir;
};

#endif // TESTSETUP_H
