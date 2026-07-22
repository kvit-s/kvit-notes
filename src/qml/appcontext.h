// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
#ifndef APPCONTEXT_H
#define APPCONTEXT_H

#include <QObject>
#include <QString>
#include <QStringList>

#include <memory>

#include "accessibilityannouncer.h"
#include "appactions.h"
#include "blockattributes.h"
#include "blockeditorengine.h"
#include "assetstore.h"
#include "blockmenumodel.h"
#include "blockkindregistry.h"
#include "blockmodel.h"
#include "clipboardhelper.h"
#include "collectionsearch.h"
#include "collectionsearchindex.h"
#include "documentexporter.h"
#include "documentimporter.h"
#include "documentmanager.h"
#include "documentoutline.h"
#include "documentsearch.h"
#include "documentselection.h"
#include "documentserializer.h"
#include "documentstats.h"
#include "egressfetcher.h"
#include "egresspolicy.h"
#include "embedmetadata.h"
#include "extensionregistry.h"
#include "filewatcher.h"
#include "foldertreemodel.h"
#include "globalhotkey.h"
#include "imageassets.h"
#include "kanbandata.h"
#include "markdownformatter.h"
#include "mathcommandmodel.h"
#include "mathrenderer.h"
#include "navigationhistory.h"
#include "notecollection.h"
#include "notelistmodel.h"
#include "notetemplates.h"
#include "qmlservices.h"
#include "querytools.h"
#include "quickswitchermodel.h"
#include "remotemediacache.h"
#include "settingsstore.h"
#include "shortcutcatalog.h"
#include "startupcontroller.h"
#include "systemtray.h"
#include "tabledata.h"
#include "theme.h"
#include "todometa.h"
#include "typography.h"
#include "undostack.h"
#include "updatechecker.h"

class QQmlContext;
class QQmlEngine;

// The application's composition root: every long-lived object the editor runs
// on, constructed and wired together, and published to QML as context
// properties.
//
// This used to be the body of main(). It lives in the core library so that a
// binary other than the stock editor — a build that links the core plus a
// premium module and supplies its own main() — composes the same editor
// without copying the wiring. The stock launcher is then only
// KvitApplication plus a nine-line main().
//
// Member order is load-bearing: members are destroyed in reverse declaration
// order, and objects that hold pointers to each other must outlive their
// holders, so the declaration order below mirrors the construction order the
// wiring needs.
class AppContext : public QObject
{
    Q_OBJECT

public:
    // The parts of the composition that reach outside the process, and so
    // cannot run the same way in a headless harness. Everything else — every
    // service, every connection, every context property — is identical in
    // production and under test, which is the point: a test that composes
    // this class is testing the graph the application actually runs on.
    //
    // Keep this struct small. Each field is a place where the two
    // compositions differ, and so a place a defect can hide from the suite.
    struct Options {
        // SystemTray::show() asks the desktop session for a status-notifier
        // item. Offscreen there is no session to ask.
        bool showSystemTray = true;
        // PerfLog writes to a file path taken from settings. A harness keeps
        // its own logging configuration.
        bool configureLoggingFromSettings = true;
    };

    explicit AppContext(QObject *parent = nullptr);
    explicit AppContext(const Options &options, QObject *parent = nullptr);
    ~AppContext() override;

    // Replace the transport embed cards fetch through, before any fetch is
    // issued; AppContext takes ownership. The default is the EgressFetcher,
    // which holds the only QNetworkAccessManager in the tree, so a test that
    // does not call this would reach the network — which is exactly why the
    // harness calls it. The egress policy in front of the transport is
    // unaffected: this swaps the wire, not the consent decision.
    void setEmbedFetcher(std::unique_ptr<EmbedFetcher> fetcher);

    // The context-property names installContextProperties() published, in
    // registration order. Exposed so a test can assert the published set
    // against the names the shell binds to, rather than discovering a rename
    // as an unresolved binding at runtime.
    QStringList installedContextPropertyNames() const
    {
        return m_installedProperties;
    }

    // Registers the QML types the shell instantiates (BlockEditorEngine,
    // SettingsStore, DiagramCanvas, and the enum-only types). Static because
    // it touches the process-wide QML type registry, not this instance.
    static void registerQmlTypes();

    // The Qt Quick Controls style the shell is written against. Every control
    // in qml/ styles its own background from the theme tokens, which a native
    // style refuses to let anything customise: under the macOS style Qt
    // rejected the backgrounds of the tag strip, the quick switcher and the
    // quick-capture window and drew its own. The app has always set this in
    // its launcher; it lives here so that anything loading the shell without
    // that launcher - the shell test does exactly that - cannot end up
    // exercising a configuration the app never runs in.
    static void applyQuickStyle();

    // Opens the per-user settings file, defaulting to settings.json under the
    // platform's application-config location, and applies the settings that
    // configure logging. A test or a second binary can pass its own path.
    void openSettings(const QString &settingsPath = QString());

    // Applies the startup mode implied by the command line: a FILE argument
    // opens that file with no collection, a DIRECTORY argument opens it as the
    // notes root, and no argument opens the default root, created and seeded
    // on first run. `arguments` is the whole argv-derived list, program name
    // included.
    void applyStartupArguments(const QStringList &arguments);

    // Publishes every core object on the QML root context, adds the math image
    // provider to the engine, and then lets each installed extension publish
    // its own objects through the context-property injection seam.
    void installContextProperties(QQmlEngine *engine);

    // Switch to another vault. Everything a root change needs that the
    // repository alone cannot do: the search index for the vault being left is
    // released first, without blocking, so opening the next vault's index does
    // not wait on the previous one's reconcile or queries. QML reaches this
    // through AppActions::requestOpenVault().
    bool openVaultRoot(const QString &path);

    // Accessors for the launcher's startup instrumentation and for a superset
    // build that wires premium objects against the core's.
    BlockModel *blockModel() { return &m_blockModel; }
    UndoStack *undoStack() { return &m_undoStack; }
    DocumentManager *documentManager() { return &m_documentManager; }
    NoteCollection *noteCollection() { return &m_noteCollection; }
    StartupController *startupController() { return &m_startupController; }
    AppActions *appActions() { return &m_appActions; }
    CollectionSearch *collectionSearch() { return &m_collectionSearch; }
    CollectionSearchIndex *searchIndex() { return &m_searchIndex; }
    SettingsStore *settings() { return &m_settingsStore; }
    SystemTray *systemTray() { return &m_systemTray; }
    Theme *theme() { return &m_theme; }
    Typography *typography() { return &m_typography; }
    UpdateChecker *updateChecker() { return &m_updateChecker; }
    // The two extension seams, owned here rather than process-global. The
    // launcher installs modules into the registry and asks them to claim
    // their fence kinds before the shell loads.
    ExtensionRegistry *extensions() { return &m_extensions; }
    BlockKindRegistry *blockKinds() { return &m_blockKinds; }
    // What the QML singletons must resolve to. Exposed so a test can compare
    // each singleton against the object registered for its type, and so catch
    // the engine default-constructing one of its own — which looks identical
    // from QML and is wired to nothing. See
    // everySingletonResolvesWithinItsOwnComposition in tests/test_shell.cpp.
    const KvitQml::ServiceTable *services() const { return &m_services; }
    // The one transport and the one policy. The launcher hands the fetcher
    // to the update checker; nothing else in the tree opens a connection.
    EgressFetcher *egressFetcher() { return m_egressFetcher.get(); }
    EgressPolicy *egressPolicy() { return &m_egressPolicy; }
    RemoteMediaCache *remoteMediaCache() { return &m_remoteMediaCache; }
    FileWatcher *fileWatcher() { return &m_fileWatcher; }

private:
    void wire();

    const Options m_options;
    QStringList m_installedProperties;
    // What the QML singletons resolve against. Declared before the services
    // it points at so it is destroyed after them, and so an engine outliving
    // this context cannot read a table of dangling pointers.
    KvitQml::ServiceTable m_services;

    // Declaration order = construction order; destruction runs in reverse.
    // The registries come first: the block model resolves delegate kinds
    // against one of them, and modules claim kinds before anything renders.
    AppActions m_appActions;
    BlockKindRegistry m_blockKinds;
    ExtensionRegistry m_extensions;
    UndoStack m_undoStack;
    BlockModel m_blockModel;
    DocumentManager m_documentManager;
    SettingsStore m_settingsStore;
    Theme m_theme;
    Typography m_typography;
    MarkdownFormatter m_markdownFormatter;
    ClipboardHelper m_clipboardHelper;
    BlockMenuModel m_blockMenuModel;
    MathCommandModel m_mathCommandModel;
    DocumentSelection m_documentSelection;
    DocumentSearch m_documentSearch;
    DocumentOutline m_documentOutline;
    DocumentStats m_documentStats;
    DocumentExporter m_documentExporter;
    DocumentSerializer m_documentSerializer;
    NoteCollection m_noteCollection;
    CollectionSearchIndex m_searchIndex;
    FolderTreeModel m_folderTreeModel;
    NoteListModel m_noteListModel;
    CollectionSearch m_collectionSearch;
    NoteTemplates m_noteTemplates;
    DocumentImporter m_documentImporter;
    // The network trust boundary, declared before everything that borrows it.
    // The policy outlives the fetcher, and both outlive the embed cache and
    // the image provider that hold non-owning pointers to them.
    EgressPolicy m_egressPolicy;
    std::unique_ptr<EgressFetcher> m_egressFetcher;
    // A test-supplied embed transport, when one has been installed. Declared
    // beside the fetcher it stands in for and before the cache that borrows
    // whichever of the two is in use.
    std::unique_ptr<EmbedFetcher> m_embedFetcherOverride;
    RemoteMediaCache m_remoteMediaCache;
    EmbedMetadata m_embedMetadata;
    StartupController m_startupController;
    ImageAssets m_imageAssets;
    AssetStore m_assetStore;
    BlockAttributes m_blockAttributes;
    ShortcutCatalog m_shortcutCatalog;
    AccessibilityAnnouncer m_a11y;
    SystemTray m_systemTray;
    GlobalHotkey m_globalHotkey;
    FileWatcher m_fileWatcher;
    TableTools m_tableTools;
    TodoMetaTools m_todoMeta;
    KanbanTools m_kanbanTools;
    MathTools m_mathTools;
    NavigationHistory m_navigationHistory;
    QuickSwitcherModel m_quickSwitcherModel;
    QueryTools m_queryTools;
    // Declared after the settings store it reads (destroyed before it). The
    // launcher injects the network fetcher; without one it never fetches.
    UpdateChecker m_updateChecker;
};

#endif // APPCONTEXT_H
