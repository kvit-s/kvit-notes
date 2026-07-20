// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
#include "appcontext.h"

#include <QDir>
#include <QFileInfo>
#include <QQmlContext>
#include <QQmlEngine>
#include <QStandardPaths>
#include <QTimer>
#include <QUrl>

#include "blockkindregistry.h"
#include "codelanguages.h"
#include "diagrams/diagramcanvas.h"
#include "extensionregistry.h"
#include "perflog.h"
#include "remoteimageprovider.h"

AppContext::AppContext(QObject *parent)
    : AppContext(Options{}, parent)
{
}

AppContext::AppContext(const Options &options, QObject *parent)
    : QObject(parent)
    , m_options(options)
    , m_egressFetcher(std::make_unique<EgressFetcher>())
{
    wire();
}

void AppContext::setEmbedFetcher(std::unique_ptr<EmbedFetcher> fetcher)
{
    if (!fetcher)
        return;
    // EmbedMetadata borrows its fetcher, so hand it the new one before the
    // previous override is destroyed at the end of this scope. The default
    // it is replacing is the EgressFetcher, which owns the only
    // QNetworkAccessManager in the tree — so a harness that does not call
    // this reaches the network for real.
    //
    // Only the wire is swapped. EgressPolicy still sits in front of it, and
    // m_egressFetcher stays wired to the remote image provider, so consent
    // and address validation behave under test exactly as they ship.
    std::unique_ptr<EmbedFetcher> previous = std::move(m_embedFetcherOverride);
    m_embedFetcherOverride = std::move(fetcher);
    m_embedMetadata.setFetcher(m_embedFetcherOverride.get());
}

AppContext::~AppContext() = default;

void AppContext::registerQmlTypes()
{
    qmlRegisterType<BlockEditorEngine>("Kvit", 1, 0, "BlockEditorEngine");
    // Creatable so tests can open a second store on a path; the app
    // itself uses the appSettings context property.
    qmlRegisterType<SettingsStore>("Kvit", 1, 0, "SettingsStore");
    qmlRegisterUncreatableType<Theme>("Kvit", 1, 0, "Theme",
                                      QStringLiteral("Use the theme context property"));
    qmlRegisterUncreatableType<Block>("Kvit", 1, 0, "Block",
                                      QStringLiteral("Block is model data; the enum is what QML needs"));
    // The built-in fence kinds, so main.qml's DelegateChooser names them
    // (`BlockKinds.Kanban`) instead of repeating their numbers. One
    // definition, in blockkindregistry.h.
    qmlRegisterUncreatableMetaObject(
        BlockKinds::staticMetaObject, "Kvit", 1, 0, "BlockKinds",
        QStringLiteral("BlockKinds is an enum namespace"));
    // The native Mermaid diagram painter, used by DiagramBlock.qml. Parses
    // and lays out off the UI thread.
    qmlRegisterType<DiagramCanvas>("Kvit", 1, 0, "DiagramCanvas");
}

void AppContext::wire()
{
    m_blockModel.setUndoStack(&m_undoStack);
    // The model resolves fence kinds against this context's registry, so a
    // module's kinds are visible to it and a second AppContext in one process
    // keeps its own.
    m_blockModel.setBlockKindRegistry(&m_blockKinds);

    m_documentManager.setBlockModel(&m_blockModel);
    m_documentManager.setUndoStack(&m_undoStack);


    m_documentSelection.setModel(&m_blockModel);
    m_documentSearch.setModel(&m_blockModel);
    // The document outline: a heading-tree projection feeding the outline
    // panel, the TOC block, and internal links.
    m_documentOutline.setModel(&m_blockModel);
    // Document statistics (features.md §19.1).
    m_documentStats.setModel(&m_blockModel);
    // Export (features.md §12.5).
    m_documentExporter.setTheme(&m_theme);

    // Disk-backed global search: one SQLite FTS5 index the collection feeds
    // and the search facade queries, off the GUI thread.
    // A capability probe guards packaging: a release build without FTS5 and the
    // trigram tokenizer cannot serve global search.
    if (!CollectionSearchIndex::capabilityAvailable()) {
        qWarning("Global search disabled: the SQLite driver lacks FTS5 with the "
                 "trigram tokenizer. Packaged builds must ship it.");
    }
    m_noteCollection.setSearchIndex(&m_searchIndex);
    m_folderTreeModel.setCollection(&m_noteCollection);
    m_noteListModel.setCollection(&m_noteCollection);
    m_collectionSearch.setSearchIndex(&m_searchIndex);
    m_collectionSearch.setCollection(&m_noteCollection);

    // Note templates (features.md §18).
    m_noteTemplates.setCollection(&m_noteCollection);
    // Import into the collection (features.md §12.6).
    m_documentImporter.setCollection(&m_noteCollection);
    // Every outbound request in the app runs over one fetcher, which asks one
    // policy. Embed previews, the images those previews name, remote images
    // and media in a note, and the update check all pass through here.
    m_egressFetcher->setPolicy(&m_egressPolicy);
    // Embed preview cards (features.md §1.2.14). The card is inert until the
    // reader approves the origin, so a note cannot fetch by being opened.
    m_embedMetadata.setFetcher(m_egressFetcher.get());
    m_embedMetadata.setPolicy(&m_egressPolicy);
    m_embedMetadata.setCollection(&m_noteCollection);
    // The update check shares this transport, but the launcher hands it over
    // (KvitApplication::start), so composing an AppContext in a test still
    // yields an update checker with no fetcher and no way to reach the wire.

    m_startupController.setCollection(&m_noteCollection);
    m_startupController.setDocumentManager(&m_documentManager);
    m_startupController.setBlockModel(&m_blockModel);
    m_startupController.setUndoStack(&m_undoStack);

    // System integration seams. The tray shows only where a status-notifier
    // host exists; both route their actions through their signals so the
    // in-app path (quick capture, tray menu) works regardless.
    if (m_options.showSystemTray)
        m_systemTray.show();
    // No system-wide grab is registered on ANY platform: GlobalHotkey is a
    // seam with no backend behind it (X11 XGrabKey, the GlobalShortcuts
    // portal, RegisterHotKey, and the macOS equivalent are all unwritten), so
    // this is false everywhere rather than a WSLg-specific limitation as the
    // previous comment implied. The configured chord still works while the
    // window has focus, through the Shortcut in main.qml that reads the same
    // setting. features.md §15.1 describes the system-wide behavior as
    // intended, not as shipped.
    m_globalHotkey.setSupported(false);

    // External file watching (features.md §12.1). Debounced outside
    // changes refresh the affected note paths when possible; directory-level
    // changes still fall back to a full collection refresh. The own-write guard
    // (hooked to the save path) keeps the app's own writes from self-triggering.
    // The open note is watched closely for the conflict case, which main.qml
    // turns into a keep-mine/load-theirs banner.
    connect(&m_fileWatcher, &FileWatcher::externalChangePaths,
            &m_noteCollection, &NoteCollection::refreshPaths);
    connect(&m_documentManager, &DocumentManager::aboutToSave,
            &m_fileWatcher, [this](const QString &path) {
                m_fileWatcher.noteOwnWrite(path);
            });
    connect(&m_noteCollection, &NoteCollection::rootChanged,
            &m_fileWatcher, [this]() {
                m_fileWatcher.watchRoot(m_noteCollection.rootPath());
            });
    connect(&m_documentManager, &DocumentManager::currentFilePathChanged,
            &m_fileWatcher, [this]() {
                m_fileWatcher.watchFile(m_documentManager.currentFilePath());
            });
    // A save replaces the file rather than editing it in place, so the kernel
    // watch is left pointing at the inode that was just discarded. FileWatcher
    // renews it when the guarded change event arrives, but that event is not
    // guaranteed — a same-path save on some platforms delivers nothing at all —
    // so confirm the registration here too. Both paths are idempotent.
    connect(&m_documentManager, &DocumentManager::saveSucceeded,
            &m_fileWatcher, [this](const QString &path) {
                if (path == m_documentManager.currentFilePath())
                    m_fileWatcher.rewatchCurrentFile();
            });

    // Wiki-link navigation: back/forward history and the quick switcher's
    // filter. History entries follow collection renames/deletions and clear
    // with the root.
    connect(&m_noteCollection, &NoteCollection::noteMoved,
            &m_navigationHistory, &NavigationHistory::renamePath);
    connect(&m_noteCollection, &NoteCollection::noteRemoved,
            &m_navigationHistory, &NavigationHistory::dropPath);
    connect(&m_noteCollection, &NoteCollection::rootChanged,
            &m_navigationHistory, &NavigationHistory::clear);
    m_quickSwitcherModel.setCollection(&m_noteCollection);

    // Collection query block: the QML seam over the pure QueryData
    // parse/evaluate module.
    m_queryTools.setCollection(&m_noteCollection);
}

void AppContext::openSettings(const QString &settingsPath)
{
    // Per-user settings. The store flushes any pending debounced write when
    // it is destroyed with this context.
    const QString path = settingsPath.isEmpty()
        ? QDir(QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation))
              .filePath(QStringLiteral("settings.json"))
        : settingsPath;
    m_settingsStore.open(path);

    // Theme and typography snapshot the store's values when attached, so
    // they attach here, after open() — attaching in wire() would read an
    // empty store and discard the persisted theme.id and type.* values.
    m_theme.setSettings(&m_settingsStore);
    // Typography settings (features.md §10.2).
    m_typography.setSettings(&m_settingsStore);

    PerfLog &perfLog = PerfLog::instance();
    if (m_options.configureLoggingFromSettings
        && !perfLog.hasEnvironmentOverride())
        perfLog.configureFromSetting(
            m_settingsStore.value(QStringLiteral("perf.logging"), QVariant()));
    if (m_options.configureLoggingFromSettings && perfLog.enabled()
        && !perfLog.hasLogFilePath()) {
        perfLog.setLogFilePath(
            QDir(QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation))
                .filePath(QStringLiteral("perf.log")));
    }

    // The quick-capture chord, both now and whenever it is edited. The
    // in-app Shortcut in main.qml reads the same key, so the two never
    // disagree about which chord the user chose.
    const auto applyQuickCaptureChord = [this]() {
        m_globalHotkey.registerShortcut(
            m_settingsStore.value(QStringLiteral("hotkey.quickCapture"),
                                  QStringLiteral("Ctrl+Alt+N")).toString());
    };
    applyQuickCaptureChord();
    connect(&m_settingsStore, &SettingsStore::valueChanged,
            &m_globalHotkey, [applyQuickCaptureChord](const QString &key) {
                if (key == QLatin1String("hotkey.quickCapture"))
                    applyQuickCaptureChord();
            });

    // The disclosed opt-out update check reads its enabled flag and
    // once-per-day stamp from the same store.
    m_updateChecker.setSettings(&m_settingsStore);

    // Remote-content consent: the master switch and the origins the reader
    // has approved. Attached here rather than in wire() for the same reason
    // Theme is — the policy reads its stored values on attach, and a store
    // that has not been opened yet would answer with defaults and drop every
    // approval the reader made in an earlier session.
    m_egressPolicy.setSettings(&m_settingsStore);

    // Close-to-tray is opt-in (tray.closeToTray, default off): closing the
    // last window quits unless the user chose to stay resident in the tray.
    m_systemTray.setSettings(&m_settingsStore);
}

void AppContext::applyStartupArguments(const QStringList &arguments)
{
    QString fileArg;
    QString rootArg;
    if (arguments.size() > 1) {
        QFileInfo argInfo(arguments.at(1));
        if (argInfo.exists() && argInfo.isFile())
            fileArg = argInfo.absoluteFilePath();
        else if (argInfo.exists() && argInfo.isDir())
            rootArg = argInfo.absoluteFilePath();
    }

    if (!fileArg.isEmpty()) {
        m_documentManager.open(QUrl::fromLocalFile(fileArg));
    } else {
        if (rootArg.isEmpty()) {
            rootArg = QDir(QStandardPaths::writableLocation(
                               QStandardPaths::DocumentsLocation))
                          .filePath(QStringLiteral("Kvit"));
        }
        m_startupController.setRootPath(rootArg);
    }

    if (m_noteCollection.isOpen())
        m_fileWatcher.watchRoot(m_noteCollection.rootPath());
}

void AppContext::installContextProperties(QQmlEngine *engine)
{
    if (!engine)
        return;
    QQmlContext *context = engine->rootContext();

    // Every property goes through one helper so the published set is
    // recorded as it is built, and two things read that one list. A test
    // compares it with the names the shell binds to, so neither side drifts
    // by hand; and ExtensionRegistry refuses a module namespace that collides
    // with a name already on it. `const auto &value` rather than QObject *
    // because codeLanguageList publishes a QVariant.
    m_installedProperties.clear();
    auto publish = [&](const char *name, const auto &value) {
        m_installedProperties << QString::fromLatin1(name);
        context->setContextProperty(name, value);
    };

    publish("blockModel", &m_blockModel);
    publish("markdownFormatter", &m_markdownFormatter);
    publish("undoStack", &m_undoStack);
    publish("documentManager", &m_documentManager);
    publish("clipboard", &m_clipboardHelper);
    publish("blockMenuModel", &m_blockMenuModel);
    publish("mathCommandModel", &m_mathCommandModel);
    publish("documentSelection", &m_documentSelection);
    publish("documentSearch", &m_documentSearch);
    publish("documentOutline", &m_documentOutline);
    publish("documentStats", &m_documentStats);
    publish("documentExporter", &m_documentExporter);
    publish("documentSerializer", &m_documentSerializer);
    publish("noteCollection", &m_noteCollection);
    publish("folderTreeModel", &m_folderTreeModel);
    publish("noteListModel", &m_noteListModel);
    publish("collectionSearch", &m_collectionSearch);
    publish("startupController", &m_startupController);
    publish("noteTemplates", &m_noteTemplates);
    publish("documentImporter", &m_documentImporter);
    publish("embedMetadata", &m_embedMetadata);
    // Delegates ask this before rendering anything remote; see EgressPolicy.
    publish("egressPolicy", &m_egressPolicy);
    publish("appSettings", &m_settingsStore);
    publish("perfLog", &PerfLog::instance());
    publish("theme", &m_theme);
    publish("typography", &m_typography);
    // The canonical code-highlight language ids: the single
    // source of truth for the language picker and the /code aliases, so the
    // UI list can never drift from what the highlighter recognizes.
    publish(
        "codeLanguageList",
        QVariant::fromValue(CodeLanguages::supportedLanguages()));
    publish("imageAssets", &m_imageAssets);
    // The per-block attribute reader/editor: delegates read typed
    // presentation values off a block's `attributes` payload, and the
    // attribute editors compute a new payload to hand to setBlockAttributes.
    publish("blockAttributes", &m_blockAttributes);
    // The shortcut catalog (features.md §13): the source the shortcut
    // reference renders and the test_shortcutmap audit checks.
    publish("shortcutCatalog", &m_shortcutCatalog);
    // The live-region announcer: dynamic changes speak
    // through this seam to assistive technology.
    publish("a11y", &m_a11y);
    publish("systemTray", &m_systemTray);
    publish("globalHotkey", &m_globalHotkey);
    publish("fileWatcher", &m_fileWatcher);
    publish("navigationHistory", &m_navigationHistory);
    publish("updateChecker", &m_updateChecker);
    publish("quickSwitcherModel", &m_quickSwitcherModel);
    publish("tableTools", &m_tableTools);
    publish("todoMeta", &m_todoMeta);
    publish("kanbanTools", &m_kanbanTools);
    publish("queryTools", &m_queryTools);
    // Math: the MicroTeX seam. The provider owns rendering under
    // image://math/...; mathRenderer is the parse-check + encoder the
    // delegates use. The engine takes ownership of the provider.
    engine->addImageProvider(QStringLiteral("math"), new MathImageProvider);
    // The only way a remote image reaches QML: image://remote/<url> fetches
    // through the egress fetcher, so consent, address validation, redirect
    // revalidation and the byte cap all apply. Binding a remote URL straight
    // to an Image's `source` would bypass every one of them.
    engine->addImageProvider(QStringLiteral("remote"),
                             new RemoteImageProvider(m_egressFetcher.get()));
    publish("mathRenderer", &m_mathTools);

    // The two extension seams: block-kind registration and QML slot
    // injection. Both are inert in the open build: no module is installed,
    // so `blockKinds` reports only the built-in fence kinds and every
    // `extensions` slot resolves to an empty source.
    publish("blockKinds", &m_blockKinds);
    publish("extensions", &m_extensions);
    // Modules publish last and under their own namespace, and every name the
    // core just took is refused to them.
    m_extensions.installContextProperties(context, m_installedProperties);
}
