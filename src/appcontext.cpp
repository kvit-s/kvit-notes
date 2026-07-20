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

#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>

#include "blockkindregistry.h"
#include "codelanguages.h"
#include "diagrams/diagramcanvas.h"
#include "extensionregistry.h"
#include "perflog.h"

namespace {

// The real embed fetcher: pulls a page's HTML through a
// QNetworkAccessManager with a bounded timeout and safe redirects. Tests wire
// a fake instead, so the suite never touches the network.
class NetworkEmbedFetcher : public EmbedFetcher
{
public:
    void fetch(const QString &url,
               std::function<void(bool, const QString &)> done) override
    {
        QNetworkRequest req((QUrl(url)));
        req.setHeader(QNetworkRequest::UserAgentHeader,
                      QByteArrayLiteral("KvitEmbed/1.0"));
        req.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::NoLessSafeRedirectPolicy);
        QNetworkReply *reply = m_nam.get(req);
        QTimer *timer = new QTimer(reply);
        timer->setSingleShot(true);
        QObject::connect(timer, &QTimer::timeout, reply, &QNetworkReply::abort);
        timer->start(8000);
        QObject::connect(reply, &QNetworkReply::finished, reply, [reply, done]() {
            if (reply->error() != QNetworkReply::NoError) {
                done(false, QString());
            } else {
                done(true, QString::fromUtf8(reply->readAll().left(200000)));
            }
            reply->deleteLater();
        });
    }

private:
    QNetworkAccessManager m_nam;
};

} // namespace

AppContext::AppContext(QObject *parent)
    : AppContext(Options{}, parent)
{
}

AppContext::AppContext(const Options &options, QObject *parent)
    : QObject(parent)
    , m_options(options)
    , m_embedFetcher(std::make_unique<NetworkEmbedFetcher>())
{
    wire();
}

void AppContext::setEmbedFetcher(std::unique_ptr<EmbedFetcher> fetcher)
{
    if (!fetcher)
        return;
    // EmbedMetadata borrows the fetcher, so hand it the new one before the
    // old one is destroyed at the end of this scope.
    std::unique_ptr<EmbedFetcher> previous = std::move(m_embedFetcher);
    m_embedFetcher = std::move(fetcher);
    m_embedMetadata.setFetcher(m_embedFetcher.get());
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
    // The native Mermaid diagram painter, used by DiagramBlock.qml. Parses
    // and lays out off the UI thread.
    qmlRegisterType<DiagramCanvas>("Kvit", 1, 0, "DiagramCanvas");
}

void AppContext::wire()
{
    m_blockModel.setUndoStack(&m_undoStack);

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
    // Embed preview cards (features.md §1.2.14).
    m_embedMetadata.setFetcher(m_embedFetcher.get());
    m_embedMetadata.setCollection(&m_noteCollection);

    m_startupController.setCollection(&m_noteCollection);
    m_startupController.setDocumentManager(&m_documentManager);
    m_startupController.setBlockModel(&m_blockModel);
    m_startupController.setUndoStack(&m_undoStack);

    // System integration seams. The tray shows only where a status-notifier
    // host exists; the global hotkey backend is not registered under WSLg,
    // but both route their actions through their signals so the in-app path
    // (quick capture, tray menu) works regardless.
    if (m_options.showSystemTray)
        m_systemTray.show();
    m_globalHotkey.setSupported(false);   // no X11/portal backend on this platform

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

    m_globalHotkey.registerShortcut(
        m_settingsStore.value(QStringLiteral("hotkey.quickCapture"),
                              QStringLiteral("Ctrl+Alt+N")).toString());

    // The disclosed opt-out update check reads its enabled flag and
    // once-per-day stamp from the same store.
    m_updateChecker.setSettings(&m_settingsStore);

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
    // recorded as it is built. A test reads it back and compares it with the
    // names the shell binds to; nothing has to be kept in sync by hand.
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
    publish("mathRenderer", &m_mathTools);

    // The two extension seams: block-kind registration and QML slot
    // injection. Both are inert in the open build: no module is installed,
    // so `blockKinds` reports only the built-in fence kinds and every
    // `extensions` slot resolves to an empty source.
    publish("blockKinds", &BlockKindRegistry::instance());
    publish("extensions", &ExtensionRegistry::instance());
    ExtensionRegistry::instance().installContextProperties(context);
}
