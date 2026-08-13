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
#include "documentcompare.h"
#include "documentdecorations.h"
#include "documentexporter.h"
#include "documentheights.h"
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
#include "filesystemtreemodel.h"
#include "foldertreemodel.h"
#include "globalhotkey.h"
#include "imageassets.h"
#include "ignorerules.h"
#include "kanbandata.h"
#include "markdownformatter.h"
#include "mathcommandmodel.h"
#include "mathrenderer.h"
#include "menuaccesskeys.h"
#include "navigationhistory.h"
#include "notecollection.h"
#include "notelistmodel.h"
#include "notetemplates.h"
#include "processservices.h"
#include "qmlservices.h"
#include "querytools.h"
#include "quickswitchermodel.h"
#include "remotemediacache.h"
#include "settingsstore.h"
#include "shortcutcatalog.h"
#include "startupcontroller.h"
#include "systemtray.h"
#include "tabledata.h"
#include "textfileviewmodel.h"
#include "theme.h"
#include "todometa.h"
#include "typography.h"
#include "undostack.h"
#include "updatechecker.h"
#include "urllauncher.h"

class QQmlContext;
class QQmlEngine;
class WindowRouter;

// The application's composition root: every long-lived object the editor runs
// on, constructed and wired together, and published to QML as context
// properties.
//
// This used to be the body of main(). It lives in the core library so that a
// binary other than the stock editor — a build that links the core plus the
// agent module and supplies its own main() — composes the same editor
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
    // cannot run the same way in a headless harness. Both fields configure
    // process-global services, so the struct now lives on ProcessServices; the
    // alias keeps the AppContext::Options spelling the tests already use.
    using Options = ProcessServices::Options;

    // Owning constructors: the context builds its own ProcessServices from the
    // given options and holds the only reference to it. This is the shape a
    // single-composition test uses — one window, one set of globals — and
    // matches how AppContext behaved before the process/per-vault split.
    explicit AppContext(QObject *parent = nullptr);
    explicit AppContext(const Options &options, QObject *parent = nullptr);
    // Borrowing constructor: the context shares the caller's ProcessServices,
    // which must outlive it. This is the shape the application and its multiple
    // windows use — every window's per-vault services are its own, while the
    // globals are the one shared set.
    explicit AppContext(ProcessServices &globals, QObject *parent = nullptr);
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

    // Installs the process window registry this window's open actions route
    // through. When unset (single-composition tests), AppActions open requests
    // fall back to switching this context in place. Set by VaultWindow.
    void setWindowRouter(WindowRouter *router) { m_router = router; }

    // Accessors for the launcher's startup instrumentation and for a superset
    // build that wires its own objects against the core's.
    BlockModel *blockModel() { return &m_blockModel; }
    UndoStack *undoStack() { return &m_undoStack; }
    DocumentManager *documentManager() { return &m_documentManager; }
    NoteCollection *noteCollection() { return &m_noteCollection; }
    StartupController *startupController() { return &m_startupController; }
    AppActions *appActions() { return &m_appActions; }
    CollectionSearch *collectionSearch() { return &m_collectionSearch; }
    // The document-view decoration seam a linked module registers against.
    DocumentDecorations *documentDecorations() { return &m_documentDecorations; }
    // The heights the block list measured, which the editor's scrollbar is
    // drawn from.
    DocumentHeights *documentHeights() { return &m_documentHeights; }
    CollectionSearchIndex *searchIndex() { return &m_searchIndex; }
    // Reached by the Qt Quick test harness, which points it at nothing so a
    // click on a link during the suite cannot open a browser on the desk of
    // whoever is running it.
    UrlLauncher *urlLauncher() { return &m_urlLauncher; }
    // The process-global services, forwarded from the shared ProcessServices
    // this context is wired against. Same surface as before the split, so the
    // launcher and any downstream main() compile unchanged.
    ProcessServices *processServices() { return &m_globals; }
    SettingsStore *settings() { return m_globals.settings(); }
    SystemTray *systemTray() { return m_globals.systemTray(); }
    Theme *theme() { return m_globals.theme(); }
    Typography *typography() { return m_globals.typography(); }
    UpdateChecker *updateChecker() { return m_globals.updateChecker(); }
    // The two extension seams, owned here rather than process-global. The
    // launcher installs modules into the registry and asks them to claim
    // their fence kinds before the shell loads.
    ExtensionRegistry *extensions() { return m_globals.extensions(); }
    BlockKindRegistry *blockKinds() { return m_globals.blockKinds(); }
    // What the QML singletons must resolve to. Exposed so a test can compare
    // each singleton against the object registered for its type, and so catch
    // the engine default-constructing one of its own — which looks identical
    // from QML and is wired to nothing. See
    // everySingletonResolvesWithinItsOwnComposition in tests/test_shell.cpp.
    const KvitQml::ServiceTable *services() const { return &m_services; }
    // The one transport and the one policy. The launcher hands the fetcher
    // to the update checker; nothing else in the tree opens a connection.
    EgressFetcher *egressFetcher() { return m_globals.egressFetcher(); }
    EgressPolicy *egressPolicy() { return m_globals.egressPolicy(); }
    RemoteMediaCache *remoteMediaCache() { return m_globals.remoteMediaCache(); }
    FileWatcher *fileWatcher() { return &m_fileWatcher; }
    FileSystemTreeModel *fileSystemTreeModel() { return &m_fileSystemTreeModel; }
    TextFileViewModel *textFileViewModel() { return &m_textFileViewModel; }
    IgnoreRules *ignoreRules() { return &m_ignoreRules; }
    NoteListModel *noteListModel() { return &m_noteListModel; }

private:
    void wire();

    QStringList m_installedProperties;
    // The process window registry this window's open actions route through,
    // or null in a composition with no registry (tests).
    WindowRouter *m_router = nullptr;
    // What the QML singletons resolve against. Declared before the services
    // it points at so it is destroyed after them, and so an engine outliving
    // this context cannot read a table of dangling pointers.
    KvitQml::ServiceTable m_services;

    // The process-global services this context is wired against. m_ownedGlobals
    // holds them only when an owning constructor built them; the borrowing
    // constructor leaves it null and binds m_globals to the caller's instance.
    // Declared here — before every per-vault member that points into a global —
    // so the globals outlive them (destruction runs in reverse). m_globals is a
    // reference and so must follow m_ownedGlobals, which is what initializes it.
    std::unique_ptr<ProcessServices> m_ownedGlobals;
    ProcessServices &m_globals;

    // Declaration order = construction order; destruction runs in reverse.
    // The block model resolves delegate kinds against the block-kind registry,
    // which is process-global now and lives in ProcessServices.
    AppActions m_appActions;
    UndoStack m_undoStack;
    BlockModel m_blockModel;
    DocumentManager m_documentManager;
    MarkdownFormatter m_markdownFormatter;
    ClipboardHelper m_clipboardHelper;
    BlockMenuModel m_blockMenuModel;
    MathCommandModel m_mathCommandModel;
    DocumentSelection m_documentSelection;
    DocumentSearch m_documentSearch;
    DocumentOutline m_documentOutline;
    DocumentStats m_documentStats;
    DocumentHeights m_documentHeights;
    DocumentExporter m_documentExporter;
    DocumentSerializer m_documentSerializer;
    DocumentCompare m_documentCompare;
    // One policy object per root, shared by the collection scan, watcher and
    // filesystem tree. It precedes those borrowers in declaration order.
    IgnoreRules m_ignoreRules;
    NoteCollection m_noteCollection;
    CollectionSearchIndex m_searchIndex;
    FileSystemTreeModel m_fileSystemTreeModel;
    TextFileViewModel m_textFileViewModel;
    FolderTreeModel m_folderTreeModel;
    NoteListModel m_noteListModel;
    CollectionSearch m_collectionSearch;
    NoteTemplates m_noteTemplates;
    DocumentImporter m_documentImporter;
    // A test-supplied embed transport, when one has been installed. Declared
    // before the EmbedMetadata that borrows it. When unset, EmbedMetadata
    // borrows the process-global fetcher that lives in ProcessServices.
    std::unique_ptr<EmbedFetcher> m_embedFetcherOverride;
    EmbedMetadata m_embedMetadata;
    StartupController m_startupController;
    ImageAssets m_imageAssets;
    AssetStore m_assetStore;
    BlockAttributes m_blockAttributes;
    ShortcutCatalog m_shortcutCatalog;
    MenuAccessKeys m_menuAccessKeys;
    AccessibilityAnnouncer m_a11y;
    FileWatcher m_fileWatcher;
    TableTools m_tableTools;
    TodoMetaTools m_todoMeta;
    KanbanTools m_kanbanTools;
    MathTools m_mathTools;
    NavigationHistory m_navigationHistory;
    QuickSwitcherModel m_quickSwitcherModel;
    QueryTools m_queryTools;
    // Where a linked module may draw inside the document view. Per window,
    // like the document it decorates.
    DocumentDecorations m_documentDecorations;
    // Per window rather than per process: it answers a click with a signal,
    // and a shared one would announce a failed link in every open window.
    UrlLauncher m_urlLauncher;
};

#endif // APPCONTEXT_H
